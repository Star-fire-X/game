#!/usr/bin/env python3
"""Gateway-Logic E2E pressure script (io_threads tunable).

This script runs two explicit phases while continuously sampling gateway metrics:
1. Real business-path TCP load: login authentication -> business requests.
2. UDP injection phase: KCP upgrade + crafted KCP UDP traffic to exercise udp_send observability.

Outputs include disconnect peaks and udp_send error curves.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


PACKET_MAGIC_V2 = 0x4D495233  # "MIR3"
PACKET_VERSION_V2 = 0x01
PACKET_HEADER_SIZE = 16

MSG_ID_LOGIN_REQ = 1001
MSG_ID_LOGIN_RSP = 1002
MSG_ID_ROLE_LIST_REQ = 1020
MSG_ID_ROLE_LIST_RSP = 1021
MSG_ID_KCP_UPGRADE_REQ = 9100
MSG_ID_KCP_UPGRADE_RSP = 9101

ERR_OK = 0
ERR_ACCOUNT_NOT_FOUND = 100
ERR_PASSWORD_WRONG = 101
ERR_RATE_LIMITED = 2010
KCP_CMD_PUSH = 81

UNREGISTER_COUNTER_CANDIDATES = [
    "gateway_session_unregister_total",
    "gateway_session_unregister",
]
DISCONNECT_QUEUE_GAUGE_CANDIDATES = [
    "gateway_disconnect_queue_size",
]
CONNECTIONS_GAUGE_CANDIDATES = [
    "mir2_connections",
]
ERROR_COUNTER_CANDIDATES = [
    "mir2_errors_total",
    "mir2_errors",
]

YAML_KEY_RE = re.compile(r"^(\s*)([A-Za-z0-9_]+):(.*)$")
PROM_METRIC_RE = re.compile(
    r"^([A-Za-z_:][A-Za-z0-9_:]*)(\{[^}]*\})?\s+([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)$"
)
PROM_LABEL_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)="((?:[^"\\]|\\.)*)"')
SAFE_PREFIX_RE = re.compile(r"^[A-Za-z0-9_]+$")
YAML_INT_RE = re.compile(r"^[-+]?[0-9]+$")


@dataclass
class SampleRow:
    phase: str
    unix_ts: float
    elapsed_sec: float
    connections: float
    session_unregister_total: float
    session_unregister_delta: float
    disconnect_queue_size: float
    udp_send_errors_total: float
    udp_send_errors_delta: float


@dataclass
class LoadStats:
    connect_attempts: int = 0
    connect_success: int = 0
    send_errors: int = 0
    login_attempts: int = 0
    login_success: int = 0
    login_failed: int = 0
    login_rate_limited: int = 0
    login_account_not_found: int = 0
    login_password_wrong: int = 0
    login_code_ok_zero_account: int = 0
    login_code_unknown: int = 0
    login_other_code: int = 0
    login_timeout: int = 0
    login_decode_errors: int = 0
    business_requests: int = 0
    business_rsp_ok: int = 0
    business_rsp_non_ok: int = 0
    business_rsp_timeout: int = 0
    business_decode_errors: int = 0


@dataclass
class UdpInjectStats:
    upgrade_attempts: int = 0
    upgrade_success: int = 0
    upgrade_failed: int = 0
    upgrade_timeout: int = 0
    udp_packets_sent: int = 0
    udp_client_send_errors: int = 0
    udp_ack_packets_recv: int = 0


@dataclass
class SamplerState:
    prev_unregister_total: float = 0.0
    prev_udp_send_total: float = 0.0
    prev_disconnect_queue_size: float = 0.0
    prev_connections: float = 0.0
    first_sample: bool = True
    metrics_fetch_errors: int = 0
    consecutive_metrics_failures: int = 0
    sample_count: int = 0


@dataclass
class LoginResultDecoded:
    ok: bool
    code: int
    account_id: int
    session_token: str


@dataclass
class KcpUpgradeResponseDecoded:
    success: bool
    conv_id: int
    server_udp_port: int
    session_token_bytes: bytes
    error_code: int


class FlatBufferDecodeError(RuntimeError):
    pass


class PacketDecodeError(RuntimeError):
    pass


def utc_now_stamp() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")


def alloc_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def alloc_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def alloc_distinct_port(used_ports: set[int], udp: bool = False) -> int:
    while True:
        port = alloc_udp_port() if udp else alloc_tcp_port()
        if port not in used_ports:
            used_ports.add(port)
            return port


def yaml_scalar(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    return json.dumps(str(value))


def rewrite_yaml(template: Path, output: Path, updates: Dict[Tuple[str, ...], object]) -> None:
    lines = template.read_text(encoding="utf-8").splitlines(keepends=True)
    out_lines: List[str] = []
    stack: List[Tuple[int, str]] = []
    matched: Dict[Tuple[str, ...], int] = {path: 0 for path in updates}

    for line in lines:
        match = YAML_KEY_RE.match(line.rstrip("\n"))
        if not match:
            out_lines.append(line)
            continue

        indent = len(match.group(1))
        key = match.group(2)
        remainder = match.group(3)

        while stack and indent <= stack[-1][0]:
            stack.pop()

        path = tuple([item[1] for item in stack] + [key])
        value_part = remainder.strip()
        is_mapping = value_part == "" or value_part.startswith("#")

        if path in updates:
            out_lines.append(f"{match.group(1)}{key}: {yaml_scalar(updates[path])}\n")
            matched[path] += 1
        else:
            out_lines.append(line)

        if is_mapping:
            stack.append((indent, key))

    missing = [path for path, count in matched.items() if count == 0]
    if missing:
        missing_text = ", ".join(".".join(path) for path in missing)
        raise RuntimeError(f"failed to rewrite {template}: missing keys [{missing_text}]")

    output.write_text("".join(out_lines), encoding="utf-8")


def strip_yaml_inline_comment(value: str) -> str:
    out: List[str] = []
    in_single = False
    in_double = False
    escaped = False

    for ch in value:
        if in_double:
            out.append(ch)
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_double = False
            continue

        if in_single:
            out.append(ch)
            if ch == "'":
                in_single = False
            continue

        if ch == "#" and (not out or out[-1].isspace()):
            break
        if ch == "'":
            in_single = True
        elif ch == '"':
            in_double = True
        out.append(ch)

    return "".join(out).strip()


def parse_yaml_scalar_text(value: str) -> Optional[object]:
    text = strip_yaml_inline_comment(value)
    if not text:
        return None

    lowered = text.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    if YAML_INT_RE.match(text):
        try:
            return int(text)
        except ValueError:
            pass

    if len(text) >= 2 and text[0] == text[-1] and text[0] in ('"', "'"):
        quoted = text[0] == '"'
        if quoted:
            try:
                return json.loads(text)
            except json.JSONDecodeError:
                return text[1:-1]
        return text[1:-1]

    return text


def read_yaml_scalar(path: Path, key_path: Tuple[str, ...]) -> Optional[object]:
    stack: List[Tuple[int, str]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = YAML_KEY_RE.match(line)
        if not match:
            continue

        indent = len(match.group(1))
        key = match.group(2)
        remainder = match.group(3)

        while stack and indent <= stack[-1][0]:
            stack.pop()

        current_path = tuple([item[1] for item in stack] + [key])
        value_part = remainder.strip()
        is_mapping = value_part == "" or value_part.startswith("#")

        if current_path == key_path and not is_mapping:
            return parse_yaml_scalar_text(value_part)

        if is_mapping:
            stack.append((indent, key))
    return None


def resolve_seed_database_config(
    logic_config: Path,
    db_host_arg: str,
    db_port_arg: int,
    db_user_arg: str,
    db_password_arg: str,
    db_name_arg: str,
) -> Tuple[str, int, str, str, str]:
    cfg_host = read_yaml_scalar(logic_config, ("database", "host"))
    cfg_port = read_yaml_scalar(logic_config, ("database", "port"))
    cfg_user = read_yaml_scalar(logic_config, ("database", "user"))
    cfg_password = read_yaml_scalar(logic_config, ("database", "password"))
    cfg_name = read_yaml_scalar(logic_config, ("database", "database"))

    seed_host = db_host_arg if db_host_arg else str(cfg_host or "127.0.0.1")

    seed_port = db_port_arg
    if seed_port <= 0:
        if isinstance(cfg_port, int) and not isinstance(cfg_port, bool):
            seed_port = cfg_port
        elif cfg_port is not None:
            try:
                seed_port = int(str(cfg_port))
            except (TypeError, ValueError):
                seed_port = 5432
        else:
            seed_port = 5432
    if seed_port <= 0:
        seed_port = 5432

    seed_user = db_user_arg if db_user_arg else str(cfg_user or "mir2")
    seed_password = (
        db_password_arg if db_password_arg else str(cfg_password or "mir2_password")
    )
    seed_name = db_name_arg if db_name_arg else str(cfg_name or "mir2_game")

    return seed_host, seed_port, seed_user, seed_password, seed_name


def wait_tcp_ready(host: str, port: int, timeout_sec: float) -> bool:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.1)
    return False


def fetch_metrics(url: str, timeout_sec: float) -> str:
    request = urllib.request.Request(url=url, method="GET")
    with urllib.request.urlopen(request, timeout=timeout_sec) as response:
        return response.read().decode("utf-8", errors="replace")


def parse_labels(raw: str) -> Dict[str, str]:
    result: Dict[str, str] = {}
    if not raw:
        return result
    for match in PROM_LABEL_RE.finditer(raw):
        result[match.group(1)] = bytes(match.group(2), "utf-8").decode("unicode_escape")
    return result


def parse_metric(
    metrics_text: str, metric_name: str, labels: Optional[Dict[str, str]] = None
) -> Optional[float]:
    for raw_line in metrics_text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        match = PROM_METRIC_RE.match(line)
        if not match:
            continue
        name = match.group(1)
        if name != metric_name:
            continue
        raw_labels = match.group(2)
        found_labels = parse_labels(raw_labels[1:-1] if raw_labels else "")
        if labels:
            ok = True
            for key, expected_value in labels.items():
                if found_labels.get(key) != expected_value:
                    ok = False
                    break
            if not ok:
                continue
        return float(match.group(3))
    return None


def parse_metric_any(
    metrics_text: str,
    metric_names: Iterable[str],
    labels: Optional[Dict[str, str]] = None,
) -> Optional[float]:
    for name in metric_names:
        value = parse_metric(metrics_text, name, labels)
        if value is not None:
            return value
    return None


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode_v2_packet(msg_id: int, payload: bytes, sequence: int) -> bytes:
    header_wo_checksum = struct.pack(
        "<IBBHIH",
        PACKET_MAGIC_V2,
        PACKET_VERSION_V2,
        0,  # flags (tcp)
        msg_id & 0xFFFF,
        len(payload),
        sequence & 0xFFFF,
    )
    checksum = crc16_ccitt(header_wo_checksum + payload)
    header = struct.pack(
        "<IBBHIHH",
        PACKET_MAGIC_V2,
        PACKET_VERSION_V2,
        0,
        msg_id & 0xFFFF,
        len(payload),
        sequence & 0xFFFF,
        checksum,
    )
    return header + payload


def decode_v2_header(header: bytes) -> Tuple[int, int, int, int, int, int, int]:
    if len(header) != PACKET_HEADER_SIZE:
        raise PacketDecodeError(f"invalid header size={len(header)}")
    return struct.unpack("<IBBHIHH", header)


def recv_exact(sock: socket.socket, size: int, timeout_sec: float) -> bytes:
    if size <= 0:
        return b""
    sock.settimeout(timeout_sec)
    chunks = bytearray()
    while len(chunks) < size:
        data = sock.recv(size - len(chunks))
        if not data:
            raise ConnectionError("peer closed")
        chunks.extend(data)
    return bytes(chunks)


def recv_v2_packet(sock: socket.socket, timeout_sec: float) -> Tuple[int, bytes]:
    header = recv_exact(sock, PACKET_HEADER_SIZE, timeout_sec)
    magic, version, flags, msg_id, payload_len, sequence, checksum = decode_v2_header(header)

    if magic != PACKET_MAGIC_V2:
        raise PacketDecodeError(f"invalid magic=0x{magic:08x}")
    if version != PACKET_VERSION_V2:
        raise PacketDecodeError(f"invalid version={version}")
    if payload_len > 8 * 1024 * 1024:
        raise PacketDecodeError(f"payload too large={payload_len}")

    payload = recv_exact(sock, payload_len, timeout_sec) if payload_len else b""
    header_wo_checksum = struct.pack(
        "<IBBHIH",
        magic,
        version,
        flags,
        msg_id,
        payload_len,
        sequence,
    )
    expected = crc16_ccitt(header_wo_checksum + payload)
    if checksum != expected:
        raise PacketDecodeError(
            f"checksum mismatch msg_id={msg_id} expected={expected} actual={checksum}"
        )
    return msg_id, payload


def send_v2(sock: socket.socket, msg_id: int, payload: bytes, sequence: int) -> int:
    packet = encode_v2_packet(msg_id, payload, sequence)
    sock.sendall(packet)
    return (sequence + 1) & 0xFFFF


def recv_until_msg(
    sock: socket.socket,
    expected_msg_ids: Sequence[int],
    timeout_sec: float,
) -> Tuple[int, bytes]:
    expected = set(int(v) for v in expected_msg_ids)
    deadline = time.monotonic() + timeout_sec
    last_timeout = False

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        read_timeout = min(max(remaining, 0.05), 0.5)
        try:
            msg_id, payload = recv_v2_packet(sock, read_timeout)
        except TimeoutError:
            last_timeout = True
            continue
        if msg_id in expected:
            return msg_id, payload

    if last_timeout:
        raise TimeoutError("timed out waiting for expected message")
    raise TimeoutError("no expected message received")


def sleep_with_stop(stop_event: threading.Event, deadline: float, sleep_sec: float) -> None:
    if sleep_sec <= 0:
        return
    end = min(deadline, time.monotonic() + sleep_sec)
    while not stop_event.is_set():
        now = time.monotonic()
        if now >= end:
            return
        time.sleep(min(0.05, end - now))


def align_up(value: int, alignment: int) -> int:
    if alignment <= 1:
        return value
    return (value + alignment - 1) // alignment * alignment


def encode_flatbuffer_table(fields: Sequence[Tuple[str, object]]) -> bytes:
    # Minimal table encoder for this script (supports string/u16/u32/u64/bool fields).
    field_offsets = [0] * len(fields)
    offset = 4  # signed vtable offset
    max_align = 4

    for idx, (kind, value) in enumerate(fields):
        if value is None:
            continue
        if kind == "string":
            align, size = 4, 4
        elif kind == "u16":
            align, size = 2, 2
        elif kind == "u32":
            align, size = 4, 4
        elif kind == "u64":
            align, size = 8, 8
        elif kind == "bool":
            align, size = 1, 1
        else:
            raise ValueError(f"unsupported flatbuffer field kind={kind}")

        offset = align_up(offset, align)
        field_offsets[idx] = offset
        offset += size
        if align > max_align:
            max_align = align

    object_size = align_up(offset, max_align)
    vtable_size = 4 + 2 * len(fields)

    root_offset_pos = 0
    vtable_start = 4
    table_start = align_up(vtable_start + vtable_size, max_align)
    data_cursor = align_up(table_start + object_size, 4)

    string_positions: Dict[int, Tuple[int, bytes]] = {}
    for idx, (kind, value) in enumerate(fields):
        if kind != "string" or value is None:
            continue
        encoded = str(value).encode("utf-8")
        str_start = align_up(data_cursor, 4)
        string_positions[idx] = (str_start, encoded)
        data_cursor = str_start + 4 + len(encoded) + 1

    buf = bytearray(data_cursor)

    struct.pack_into("<I", buf, root_offset_pos, table_start)
    struct.pack_into("<H", buf, vtable_start, vtable_size)
    struct.pack_into("<H", buf, vtable_start + 2, object_size)

    for idx, field_offset in enumerate(field_offsets):
        struct.pack_into("<H", buf, vtable_start + 4 + 2 * idx, field_offset)

    struct.pack_into("<i", buf, table_start, table_start - vtable_start)

    for idx, (kind, value) in enumerate(fields):
        field_offset = field_offsets[idx]
        if field_offset == 0:
            continue
        field_pos = table_start + field_offset

        if kind == "string":
            str_start, encoded = string_positions[idx]
            struct.pack_into("<I", buf, field_pos, str_start - field_pos)
            struct.pack_into("<I", buf, str_start, len(encoded))
            buf[str_start + 4 : str_start + 4 + len(encoded)] = encoded
            buf[str_start + 4 + len(encoded)] = 0
        elif kind == "u16":
            struct.pack_into("<H", buf, field_pos, int(value) & 0xFFFF)
        elif kind == "u32":
            struct.pack_into("<I", buf, field_pos, int(value) & 0xFFFFFFFF)
        elif kind == "u64":
            struct.pack_into("<Q", buf, field_pos, int(value) & 0xFFFFFFFFFFFFFFFF)
        elif kind == "bool":
            struct.pack_into("<?", buf, field_pos, bool(value))

    return bytes(buf)


def fb_get_table_start(buf: bytes) -> int:
    if len(buf) < 8:
        raise FlatBufferDecodeError("buffer too small")
    table_start = struct.unpack_from("<I", buf, 0)[0]
    if table_start < 4 or table_start + 4 > len(buf):
        raise FlatBufferDecodeError("invalid table start")
    return table_start


def fb_get_vtable_start(buf: bytes, table_start: int) -> int:
    vtable_offset = struct.unpack_from("<i", buf, table_start)[0]
    vtable_start = table_start - vtable_offset
    if vtable_start < 0 or vtable_start + 4 > len(buf):
        raise FlatBufferDecodeError("invalid vtable start")
    return vtable_start


def fb_field_offset(buf: bytes, table_start: int, field_index: int) -> int:
    vtable_start = fb_get_vtable_start(buf, table_start)
    vtable_size = struct.unpack_from("<H", buf, vtable_start)[0]
    entry_pos = vtable_start + 4 + 2 * field_index
    if entry_pos + 2 > vtable_start + vtable_size:
        return 0
    return struct.unpack_from("<H", buf, entry_pos)[0]


def fb_get_u16(buf: bytes, table_start: int, field_index: int, default: int = 0) -> int:
    offset = fb_field_offset(buf, table_start, field_index)
    if offset == 0:
        return default
    pos = table_start + offset
    if pos + 2 > len(buf):
        raise FlatBufferDecodeError("u16 out of range")
    return struct.unpack_from("<H", buf, pos)[0]


def fb_get_u32(buf: bytes, table_start: int, field_index: int, default: int = 0) -> int:
    offset = fb_field_offset(buf, table_start, field_index)
    if offset == 0:
        return default
    pos = table_start + offset
    if pos + 4 > len(buf):
        raise FlatBufferDecodeError("u32 out of range")
    return struct.unpack_from("<I", buf, pos)[0]


def fb_get_u64(buf: bytes, table_start: int, field_index: int, default: int = 0) -> int:
    offset = fb_field_offset(buf, table_start, field_index)
    if offset == 0:
        return default
    pos = table_start + offset
    if pos + 8 > len(buf):
        raise FlatBufferDecodeError("u64 out of range")
    return struct.unpack_from("<Q", buf, pos)[0]


def fb_get_bool(buf: bytes, table_start: int, field_index: int, default: bool = False) -> bool:
    offset = fb_field_offset(buf, table_start, field_index)
    if offset == 0:
        return default
    pos = table_start + offset
    if pos + 1 > len(buf):
        raise FlatBufferDecodeError("bool out of range")
    return struct.unpack_from("<?", buf, pos)[0]


def fb_get_string_bytes(
    buf: bytes, table_start: int, field_index: int, default: bytes = b""
) -> bytes:
    offset = fb_field_offset(buf, table_start, field_index)
    if offset == 0:
        return default
    field_pos = table_start + offset
    if field_pos + 4 > len(buf):
        raise FlatBufferDecodeError("string offset out of range")

    rel = struct.unpack_from("<I", buf, field_pos)[0]
    str_start = field_pos + rel
    if str_start + 4 > len(buf):
        raise FlatBufferDecodeError("string start out of range")

    length = struct.unpack_from("<I", buf, str_start)[0]
    data_start = str_start + 4
    data_end = data_start + length
    if data_end > len(buf):
        raise FlatBufferDecodeError("string length out of range")
    return bytes(buf[data_start:data_end])


def fb_get_string_utf8(
    buf: bytes, table_start: int, field_index: int, default: str = ""
) -> str:
    raw = fb_get_string_bytes(buf, table_start, field_index, default.encode("utf-8"))
    return raw.decode("utf-8", errors="replace")


def encode_login_req_payload(username: str, password: str, version: str) -> bytes:
    return encode_flatbuffer_table(
        [
            ("string", username),
            ("string", password),
            ("string", version),
        ]
    )


def decode_login_rsp_payload(payload: bytes) -> LoginResultDecoded:
    table_start = fb_get_table_start(payload)
    code = fb_get_u16(payload, table_start, 0, ERR_OK)
    account_id = fb_get_u64(payload, table_start, 1, 0)
    token = fb_get_string_utf8(payload, table_start, 2, "")
    return LoginResultDecoded(ok=(code == ERR_OK),
                              code=code,
                              account_id=account_id,
                              session_token=token)


def encode_role_list_req_payload(account_id: int, session_token: str) -> bytes:
    return encode_flatbuffer_table(
        [
            ("u64", account_id),
            ("string", session_token),
        ]
    )


def decode_role_list_rsp_code(payload: bytes) -> int:
    table_start = fb_get_table_start(payload)
    return fb_get_u16(payload, table_start, 0, ERR_OK)


def encode_kcp_upgrade_req_payload(client_udp_port: int) -> bytes:
    return encode_flatbuffer_table(
        [
            ("u16", client_udp_port),
        ]
    )


def decode_kcp_upgrade_rsp_payload(payload: bytes) -> KcpUpgradeResponseDecoded:
    table_start = fb_get_table_start(payload)
    success = fb_get_bool(payload, table_start, 0, False)
    conv_id = fb_get_u32(payload, table_start, 1, 0)
    server_udp_port = fb_get_u16(payload, table_start, 2, 0)
    token = fb_get_string_bytes(payload, table_start, 3, b"")
    error_code = fb_get_u16(payload, table_start, 4, 0)
    return KcpUpgradeResponseDecoded(
        success=success,
        conv_id=conv_id,
        server_udp_port=server_udp_port,
        session_token_bytes=token,
        error_code=error_code,
    )


def make_legacy_sha256_hash(password: str, salt: str) -> str:
    digest = hashlib.sha256((salt + password).encode("utf-8")).hexdigest()
    return f"sha256${salt}${digest}"


def run_seed_sql_via_psql(
    command: List[str],
    env: Dict[str, str],
) -> Tuple[bool, str]:
    try:
        result = subprocess.run(
            command,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    except Exception as ex:  # noqa: BLE001
        return False, str(ex)
    if result.returncode == 0:
        return True, result.stdout.strip()
    return False, result.stdout.strip()


def seed_auth_accounts(
    mode: str,
    docker_container: str,
    db_host: str,
    db_port: int,
    db_user: str,
    db_password: str,
    db_name: str,
    user_prefix: str,
    user_count: int,
    auth_password: str,
) -> str:
    if user_count <= 0:
        return "skipped(count=0)"
    if mode == "none":
        return "skipped(mode=none)"

    if not SAFE_PREFIX_RE.match(user_prefix):
        raise RuntimeError(
            "--auth-user-prefix must match ^[A-Za-z0-9_]+$ for SQL safety"
        )

    salt = "legend2_load"
    password_hash = make_legacy_sha256_hash(auth_password, salt)
    upper = user_count - 1
    sql = (
        "INSERT INTO accounts (username, password_hash, email, banned) "
        f"SELECT '{user_prefix}_' || g::text, "
        f"'{password_hash}', "
        f"'{user_prefix}_' || g::text || '@load.test', "
        "FALSE "
        f"FROM generate_series(0, {upper}) AS g "
        "ON CONFLICT (username) DO UPDATE "
        "SET password_hash = EXCLUDED.password_hash, "
        "banned = FALSE;"
    )

    env = dict(os.environ)
    env["PGPASSWORD"] = db_password

    tried: List[str] = []
    outputs: List[str] = []

    def try_psql() -> bool:
        psql_bin = shutil.which("psql")
        if not psql_bin:
            return False
        cmd = [
            psql_bin,
            "-h",
            db_host,
            "-p",
            str(db_port),
            "-U",
            db_user,
            "-d",
            db_name,
            "-v",
            "ON_ERROR_STOP=1",
            "-c",
            sql,
        ]
        ok, out = run_seed_sql_via_psql(cmd, env)
        tried.append("psql")
        outputs.append(f"psql: {out}")
        return ok

    def try_docker() -> bool:
        docker_bin = shutil.which("docker")
        if not docker_bin:
            return False
        cmd = [
            docker_bin,
            "exec",
            "-i",
            "-e",
            f"PGPASSWORD={db_password}",
            docker_container,
            "psql",
            "-h",
            db_host,
            "-p",
            str(db_port),
            "-U",
            db_user,
            "-d",
            db_name,
            "-v",
            "ON_ERROR_STOP=1",
            "-c",
            sql,
        ]
        ok, out = run_seed_sql_via_psql(cmd, env)
        tried.append("docker")
        outputs.append(f"docker: {out}")
        return ok

    if mode == "psql":
        if try_psql():
            return "psql"
    elif mode == "docker":
        if try_docker():
            return "docker"
    elif mode == "auto":
        if try_psql():
            return "psql"
        if try_docker():
            return "docker"
    else:
        raise RuntimeError(f"invalid seed mode={mode}")

    detail = " | ".join(outputs) if outputs else "no available psql executor"
    tried_text = ",".join(tried) if tried else "none"
    raise RuntimeError(
        f"failed to seed auth accounts (mode={mode}, tried={tried_text}): {detail}"
    )


def perform_login(
    sock: socket.socket,
    sequence: int,
    username: str,
    password: str,
    version: str,
    timeout_sec: float,
) -> Tuple[LoginResultDecoded, int]:
    login_payload = encode_login_req_payload(username, password, version)
    sequence = send_v2(sock, MSG_ID_LOGIN_REQ, login_payload, sequence)
    _, rsp_payload = recv_until_msg(sock, [MSG_ID_LOGIN_RSP], timeout_sec)
    login_rsp = decode_login_rsp_payload(rsp_payload)
    return login_rsp, sequence


def perform_business(
    sock: socket.socket,
    sequence: int,
    req_msg_id: int,
    rsp_msg_id: int,
    account_id: int,
    session_token: str,
    timeout_sec: float,
) -> Tuple[bool, bool, int]:
    if req_msg_id == MSG_ID_ROLE_LIST_REQ:
        payload = encode_role_list_req_payload(account_id, session_token)
    else:
        payload = b""

    sequence = send_v2(sock, req_msg_id, payload, sequence)

    if rsp_msg_id == 0:
        return True, True, sequence

    _, rsp_payload = recv_until_msg(sock, [rsp_msg_id], timeout_sec)

    if rsp_msg_id == MSG_ID_ROLE_LIST_RSP:
        code = decode_role_list_rsp_code(rsp_payload)
        return True, code == ERR_OK, sequence

    return True, True, sequence


def business_load_worker(
    worker_id: int,
    host: str,
    port: int,
    stop_event: threading.Event,
    deadline: float,
    business_per_connection: int,
    reconnect_pause_sec: float,
    connect_timeout_sec: float,
    login_timeout_sec: float,
    business_timeout_sec: float,
    auth_user_prefix: str,
    auth_password: str,
    auth_version: str,
    auth_account_count: int,
    business_msg_id: int,
    business_rsp_msg_id: int,
    stats: LoadStats,
    stats_lock: threading.Lock,
) -> None:
    while not stop_event.is_set() and time.monotonic() < deadline:
        retry_sleep_sec = reconnect_pause_sec
        with stats_lock:
            stats.connect_attempts += 1

        sock: Optional[socket.socket] = None
        username_index = worker_id % max(auth_account_count, 1)
        username = f"{auth_user_prefix}_{username_index}"
        login_ok = False
        login_result: Optional[LoginResultDecoded] = None

        try:
            sequence = 0
            sock = socket.create_connection((host, port), timeout=connect_timeout_sec)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with stats_lock:
                stats.connect_success += 1
                stats.login_attempts += 1

            try:
                login_result, sequence = perform_login(
                    sock,
                    sequence,
                    username,
                    auth_password,
                    auth_version,
                    login_timeout_sec,
                )
            except TimeoutError:
                with stats_lock:
                    stats.login_timeout += 1
                retry_sleep_sec = max(reconnect_pause_sec, 0.25)
            except (FlatBufferDecodeError, PacketDecodeError):
                with stats_lock:
                    stats.login_decode_errors += 1
                retry_sleep_sec = max(reconnect_pause_sec, 0.25)
            else:
                if login_result.ok:
                    with stats_lock:
                        stats.login_success += 1
                    login_ok = True
                else:
                    with stats_lock:
                        stats.login_failed += 1
                        if login_result.code == ERR_RATE_LIMITED:
                            stats.login_rate_limited += 1
                        elif login_result.code == ERR_ACCOUNT_NOT_FOUND:
                            stats.login_account_not_found += 1
                        elif login_result.code == ERR_PASSWORD_WRONG:
                            stats.login_password_wrong += 1
                        elif login_result.code == ERR_OK:
                            stats.login_code_ok_zero_account += 1
                        elif login_result.code == 1:
                            stats.login_code_unknown += 1
                        else:
                            stats.login_other_code += 1
                    retry_sleep_sec = max(
                        reconnect_pause_sec,
                        1.0 if login_result.code == ERR_RATE_LIMITED else 0.25,
                    )

            if login_ok and login_result is not None:
                while not stop_event.is_set() and time.monotonic() < deadline:
                    should_break = False
                    for _ in range(business_per_connection):
                        if stop_event.is_set() or time.monotonic() >= deadline:
                            should_break = True
                            break
                        with stats_lock:
                            stats.business_requests += 1
                        try:
                            _, rsp_ok, sequence = perform_business(
                                sock,
                                sequence,
                                business_msg_id,
                                business_rsp_msg_id,
                                login_result.account_id,
                                login_result.session_token,
                                business_timeout_sec,
                            )
                        except TimeoutError:
                            with stats_lock:
                                stats.business_rsp_timeout += 1
                            retry_sleep_sec = max(reconnect_pause_sec, 0.25)
                            should_break = True
                            break
                        except (FlatBufferDecodeError, PacketDecodeError):
                            with stats_lock:
                                stats.business_decode_errors += 1
                            retry_sleep_sec = max(reconnect_pause_sec, 0.25)
                            should_break = True
                            break
                        except OSError:
                            with stats_lock:
                                stats.send_errors += 1
                            retry_sleep_sec = max(reconnect_pause_sec, 0.25)
                            should_break = True
                            break

                        with stats_lock:
                            if rsp_ok:
                                stats.business_rsp_ok += 1
                            else:
                                stats.business_rsp_non_ok += 1

                    if should_break:
                        break

                    sleep_with_stop(stop_event, deadline, reconnect_pause_sec)
        except OSError:
            with stats_lock:
                stats.send_errors += 1
            retry_sleep_sec = max(reconnect_pause_sec, 0.25)
        finally:
            if sock is not None:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                sock.close()

        sleep_with_stop(stop_event, deadline, retry_sleep_sec)


def build_kcp_push_payload(conv_id: int, sequence: int, payload_size: int) -> bytes:
    data = bytes([sequence & 0xFF]) * max(payload_size, 1)
    ts_ms = int(time.monotonic() * 1000) & 0xFFFFFFFF
    # IKCP segment: conv|cmd|frg|wnd|ts|sn|una|len|data
    segment = struct.pack(
        "<IBBHIIII",
        conv_id & 0xFFFFFFFF,
        KCP_CMD_PUSH,
        0,
        128,
        ts_ms,
        sequence & 0xFFFFFFFF,
        0,
        len(data),
    ) + data
    return segment


def build_kcp_udp_datagram(conv_id: int, token: bytes, kcp_payload: bytes) -> bytes:
    return struct.pack("<I", conv_id & 0xFFFFFFFF) + token + kcp_payload


def udp_inject_worker(
    worker_id: int,
    host: str,
    tcp_port: int,
    stop_event: threading.Event,
    deadline: float,
    reconnect_pause_sec: float,
    connect_timeout_sec: float,
    login_timeout_sec: float,
    upgrade_timeout_sec: float,
    auth_user_prefix: str,
    auth_password: str,
    auth_version: str,
    auth_account_count: int,
    packets_per_session: int,
    packet_payload_bytes: int,
    stats: UdpInjectStats,
    stats_lock: threading.Lock,
) -> None:
    kcp_sn = 0

    while not stop_event.is_set() and time.monotonic() < deadline:
        retry_sleep_sec = reconnect_pause_sec
        sock: Optional[socket.socket] = None
        udp_sock: Optional[socket.socket] = None
        username_index = worker_id % max(auth_account_count, 1)
        username = f"{auth_user_prefix}_{username_index}"
        login_ok = False

        with stats_lock:
            stats.upgrade_attempts += 1

        try:
            sequence = 0
            sock = socket.create_connection((host, tcp_port), timeout=connect_timeout_sec)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            try:
                login_result, sequence = perform_login(
                    sock,
                    sequence,
                    username,
                    auth_password,
                    auth_version,
                    login_timeout_sec,
                )
            except TimeoutError:
                with stats_lock:
                    stats.upgrade_timeout += 1
                retry_sleep_sec = max(reconnect_pause_sec, 0.25)
            except Exception:  # noqa: BLE001
                with stats_lock:
                    stats.upgrade_failed += 1
                retry_sleep_sec = max(reconnect_pause_sec, 0.25)
            else:
                if login_result.ok:
                    login_ok = True
                else:
                    with stats_lock:
                        stats.upgrade_failed += 1
                    retry_sleep_sec = max(
                        reconnect_pause_sec,
                        1.0 if login_result.code == ERR_RATE_LIMITED else 0.25,
                    )

            upgrade_ok = False
            upgrade_rsp: Optional[KcpUpgradeResponseDecoded] = None

            if login_ok:
                udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                udp_sock.bind((host, 0))
                udp_sock.setblocking(False)
                client_udp_port = int(udp_sock.getsockname()[1])

                upgrade_payload = encode_kcp_upgrade_req_payload(client_udp_port)
                sequence = send_v2(sock, MSG_ID_KCP_UPGRADE_REQ, upgrade_payload, sequence)

                try:
                    _, upgrade_rsp_payload = recv_until_msg(
                        sock,
                        [MSG_ID_KCP_UPGRADE_RSP],
                        upgrade_timeout_sec,
                    )
                    upgrade_rsp = decode_kcp_upgrade_rsp_payload(upgrade_rsp_payload)
                except TimeoutError:
                    with stats_lock:
                        stats.upgrade_timeout += 1
                    retry_sleep_sec = max(reconnect_pause_sec, 0.25)
                except Exception:  # noqa: BLE001
                    with stats_lock:
                        stats.upgrade_failed += 1
                    retry_sleep_sec = max(reconnect_pause_sec, 0.25)
                else:
                    if (
                        upgrade_rsp.success
                        and upgrade_rsp.conv_id != 0
                        and upgrade_rsp.server_udp_port != 0
                        and len(upgrade_rsp.session_token_bytes) != 0
                    ):
                        with stats_lock:
                            stats.upgrade_success += 1
                        upgrade_ok = True
                    else:
                        with stats_lock:
                            stats.upgrade_failed += 1
                        retry_sleep_sec = max(reconnect_pause_sec, 0.25)

            if upgrade_ok and upgrade_rsp is not None and udp_sock is not None:
                token = upgrade_rsp.session_token_bytes
                server_udp_port = upgrade_rsp.server_udp_port

                while not stop_event.is_set() and time.monotonic() < deadline:
                    send_failed = False
                    for _ in range(packets_per_session):
                        if stop_event.is_set() or time.monotonic() >= deadline:
                            break
                        kcp_payload = build_kcp_push_payload(
                            upgrade_rsp.conv_id, kcp_sn, packet_payload_bytes
                        )
                        datagram = build_kcp_udp_datagram(upgrade_rsp.conv_id, token, kcp_payload)
                        kcp_sn += 1
                        try:
                            udp_sock.sendto(datagram, (host, server_udp_port))
                            with stats_lock:
                                stats.udp_packets_sent += 1
                        except OSError:
                            with stats_lock:
                                stats.udp_client_send_errors += 1
                            retry_sleep_sec = max(reconnect_pause_sec, 0.25)
                            send_failed = True
                            break

                        # Drain any immediate ACK datagrams (best-effort).
                        for _ in range(2):
                            try:
                                _data, _addr = udp_sock.recvfrom(4096)
                                with stats_lock:
                                    stats.udp_ack_packets_recv += 1
                            except BlockingIOError:
                                break
                            except OSError:
                                break

                    if send_failed:
                        break

                    sleep_with_stop(stop_event, deadline, reconnect_pause_sec)
        except OSError:
            with stats_lock:
                stats.upgrade_failed += 1
            retry_sleep_sec = max(reconnect_pause_sec, 0.25)
        finally:
            if udp_sock is not None:
                udp_sock.close()
            if sock is not None:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                sock.close()

        sleep_with_stop(stop_event, deadline, retry_sleep_sec)


def write_csv(path: Path, rows: List[SampleRow]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "phase",
                "unix_ts",
                "elapsed_sec",
                "connections",
                "session_unregister_total",
                "session_unregister_delta",
                "disconnect_queue_size",
                "udp_send_errors_total",
                "udp_send_errors_delta",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row.phase,
                    f"{row.unix_ts:.3f}",
                    f"{row.elapsed_sec:.3f}",
                    f"{row.connections:.0f}",
                    f"{row.session_unregister_total:.0f}",
                    f"{row.session_unregister_delta:.0f}",
                    f"{row.disconnect_queue_size:.0f}",
                    f"{row.udp_send_errors_total:.0f}",
                    f"{row.udp_send_errors_delta:.0f}",
                ]
            )


def write_ascii_curve(path: Path, rows: List[SampleRow], field: str, title: str) -> None:
    values = [float(getattr(row, field)) for row in rows]
    max_value = max(values) if values else 0.0
    scale = 40.0 / max_value if max_value > 0 else 0.0
    lines = [title, "=" * len(title)]
    for row, value in zip(rows, values):
        bar_len = int(round(value * scale)) if scale > 0 else 0
        lines.append(f"{row.elapsed_sec:7.1f}s [{row.phase:10}] | {'#' * bar_len} {value:.0f}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def terminate_process(proc: subprocess.Popen[str], name: str) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.terminate()
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    except ProcessLookupError:
        return
    finally:
        if proc.poll() is None:
            raise RuntimeError(f"failed to stop {name} process cleanly")


def tail_file(path: Path, max_lines: int = 80) -> str:
    if not path.exists():
        return f"[missing log file: {path}]"
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if len(lines) <= max_lines:
        return "\n".join(lines)
    return "\n".join(lines[-max_lines:])


def sample_metrics_until(
    phase: str,
    end_monotonic: float,
    start_monotonic: float,
    sample_interval_sec: float,
    gateway_metrics_url: str,
    max_consecutive_metrics_failures: int,
    rows: List[SampleRow],
    sampler: SamplerState,
) -> None:
    while time.monotonic() < end_monotonic:
        tick_start = time.monotonic()

        try:
            metrics_text = fetch_metrics(gateway_metrics_url, timeout_sec=1.5)
            unregister_total = parse_metric_any(metrics_text, UNREGISTER_COUNTER_CANDIDATES) or 0.0
            disconnect_queue_size = (
                parse_metric_any(metrics_text, DISCONNECT_QUEUE_GAUGE_CANDIDATES) or 0.0
            )
            connections = parse_metric_any(metrics_text, CONNECTIONS_GAUGE_CANDIDATES) or 0.0
            udp_send_total = (
                parse_metric_any(metrics_text, ERROR_COUNTER_CANDIDATES, labels={"reason": "udp_send"})
                or 0.0
            )
            sampler.consecutive_metrics_failures = 0
        except (urllib.error.URLError, TimeoutError, OSError):
            sampler.metrics_fetch_errors += 1
            sampler.consecutive_metrics_failures += 1
            if sampler.consecutive_metrics_failures >= max_consecutive_metrics_failures:
                raise RuntimeError(
                    "metrics endpoint timed out repeatedly during sampling "
                    f"({sampler.consecutive_metrics_failures} consecutive failures)"
                )
            unregister_total = sampler.prev_unregister_total
            udp_send_total = sampler.prev_udp_send_total
            disconnect_queue_size = sampler.prev_disconnect_queue_size
            connections = sampler.prev_connections

        if sampler.first_sample:
            unregister_delta = 0.0
            udp_send_delta = 0.0
            sampler.first_sample = False
        else:
            unregister_delta = max(unregister_total - sampler.prev_unregister_total, 0.0)
            udp_send_delta = max(udp_send_total - sampler.prev_udp_send_total, 0.0)

        sampler.prev_unregister_total = unregister_total
        sampler.prev_udp_send_total = udp_send_total
        sampler.prev_disconnect_queue_size = disconnect_queue_size
        sampler.prev_connections = connections

        rows.append(
            SampleRow(
                phase=phase,
                unix_ts=time.time(),
                elapsed_sec=tick_start - start_monotonic,
                connections=connections,
                session_unregister_total=unregister_total,
                session_unregister_delta=unregister_delta,
                disconnect_queue_size=disconnect_queue_size,
                udp_send_errors_total=udp_send_total,
                udp_send_errors_delta=udp_send_delta,
            )
        )
        sampler.sample_count += 1

        target_next = tick_start + sample_interval_sec
        sleep_sec = target_next - time.monotonic()
        if sleep_sec > 0:
            time.sleep(sleep_sec)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run gateway-logic E2E pressure (auth business path + UDP injection) "
            "and capture disconnect/udp_send curves."
        )
    )
    parser.add_argument("--build-dir", default="build-wsl")
    parser.add_argument("--gateway-bin", default="")
    parser.add_argument("--logic-bin", default="")
    parser.add_argument("--gateway-config", default="config/gateway.yaml")
    parser.add_argument("--logic-config", default="config/logic.yaml")

    parser.add_argument("--duration-sec", type=int, default=120,
                        help="Business-phase duration in seconds.")
    parser.add_argument("--udp-inject-duration-sec", type=int, default=30,
                        help="Dedicated UDP injection phase duration in seconds.")
    parser.add_argument("--sample-interval-sec", type=float, default=1.0)
    parser.add_argument(
        "--max-consecutive-metrics-failures",
        type=int,
        default=10,
        help="Abort run when metrics endpoint fails this many times in a row.",
    )

    parser.add_argument("--workers", type=int, default=64,
                        help="Business-phase TCP worker count.")
    parser.add_argument("--burst-per-connection", type=int, default=4,
                        help="Business requests per TCP connection after login.")
    parser.add_argument("--udp-inject-workers", type=int, default=16)
    parser.add_argument("--udp-inject-packets-per-session", type=int, default=200)
    parser.add_argument("--udp-inject-payload-bytes", type=int, default=8)

    parser.add_argument("--business-msg-id", type=int, default=MSG_ID_ROLE_LIST_REQ)
    parser.add_argument("--business-rsp-msg-id", type=int, default=MSG_ID_ROLE_LIST_RSP)

    parser.add_argument("--gateway-io-threads", type=int, default=4)
    parser.add_argument(
        "--logic-io-threads",
        type=int,
        default=1,
        help="LogicServer currently enforces io_threads=1 for ECS safety.",
    )
    parser.add_argument(
        "--gateway-login-rate-limit-capacity",
        type=int,
        default=200000,
        help="Gateway login IP limiter capacity during pressure run.",
    )
    parser.add_argument(
        "--gateway-login-rate-limit-refill-rate",
        type=int,
        default=200000,
        help="Gateway login IP limiter refill_rate during pressure run.",
    )
    parser.add_argument(
        "--logic-login-rate-limit-capacity",
        type=int,
        default=200000,
        help="Logic login username limiter capacity during pressure run.",
    )
    parser.add_argument(
        "--logic-login-rate-limit-refill-rate",
        type=int,
        default=200000,
        help="Logic login username limiter refill_rate during pressure run.",
    )
    parser.add_argument(
        "--logic-login-rate-limit-refill-interval-seconds",
        type=int,
        default=1,
        help="Logic login username limiter refill interval seconds during pressure run.",
    )
    parser.add_argument(
        "--gateway-udp-send-fault-inject-every-n",
        type=int,
        default=0,
        help="Gateway UDP send fault injection interval; 0 disables injection.",
    )

    parser.add_argument("--reconnect-pause-ms", type=int, default=5)
    parser.add_argument("--connect-timeout-ms", type=int, default=1500)
    parser.add_argument("--login-timeout-ms", type=int, default=1500)
    parser.add_argument("--business-timeout-ms", type=int, default=1500)
    parser.add_argument("--kcp-upgrade-timeout-ms", type=int, default=1500)

    parser.add_argument("--host", default="127.0.0.1")

    parser.add_argument("--auth-user-prefix", default="loadtest_user")
    parser.add_argument("--auth-password", default="loadtest_pass")
    parser.add_argument("--auth-version", default="1")
    parser.add_argument(
        "--auth-account-count",
        type=int,
        default=0,
        help="Number of seeded auth accounts; 0 means use --workers.",
    )

    parser.add_argument(
        "--seed-auth-accounts",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Seed auth accounts in DB before pressure run.",
    )
    parser.add_argument(
        "--seed-mode",
        choices=["auto", "psql", "docker", "none"],
        default="auto",
        help="How to execute DB seed SQL.",
    )
    parser.add_argument("--db-docker-container", default="legend2-postgres")

    parser.add_argument("--db-host", default="")
    parser.add_argument("--db-port", type=int, default=0)
    parser.add_argument("--db-user", default="")
    parser.add_argument("--db-password", default="")
    parser.add_argument("--db-name", default="")

    parser.add_argument("--results-dir", default="")
    parser.add_argument(
        "--cleanup-artifacts",
        action="store_true",
        help="Remove run directory after a successful run.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.duration_sec <= 0:
        raise ValueError("--duration-sec must be > 0")
    if args.udp_inject_duration_sec < 0:
        raise ValueError("--udp-inject-duration-sec must be >= 0")
    if args.workers <= 0:
        raise ValueError("--workers must be > 0")
    if args.burst_per_connection <= 0:
        raise ValueError("--burst-per-connection must be > 0")
    if args.sample_interval_sec <= 0:
        raise ValueError("--sample-interval-sec must be > 0")
    if args.max_consecutive_metrics_failures <= 0:
        raise ValueError("--max-consecutive-metrics-failures must be > 0")
    if args.gateway_io_threads <= 0:
        raise ValueError("--gateway-io-threads must be > 0")
    if args.logic_io_threads <= 0:
        raise ValueError("--logic-io-threads must be > 0")
    if args.gateway_login_rate_limit_capacity <= 0:
        raise ValueError("--gateway-login-rate-limit-capacity must be > 0")
    if args.gateway_login_rate_limit_refill_rate <= 0:
        raise ValueError("--gateway-login-rate-limit-refill-rate must be > 0")
    if args.logic_login_rate_limit_capacity <= 0:
        raise ValueError("--logic-login-rate-limit-capacity must be > 0")
    if args.logic_login_rate_limit_refill_rate <= 0:
        raise ValueError("--logic-login-rate-limit-refill-rate must be > 0")
    if args.logic_login_rate_limit_refill_interval_seconds <= 0:
        raise ValueError("--logic-login-rate-limit-refill-interval-seconds must be > 0")
    if args.gateway_udp_send_fault_inject_every_n < 0:
        raise ValueError("--gateway-udp-send-fault-inject-every-n must be >= 0")
    if args.udp_inject_workers <= 0:
        raise ValueError("--udp-inject-workers must be > 0")
    if args.udp_inject_packets_per_session <= 0:
        raise ValueError("--udp-inject-packets-per-session must be > 0")
    if args.udp_inject_payload_bytes <= 0:
        raise ValueError("--udp-inject-payload-bytes must be > 0")

    build_dir = Path(args.build_dir)
    gateway_bin = Path(args.gateway_bin) if args.gateway_bin else build_dir / "bin" / "mir2_gateway"
    logic_bin = Path(args.logic_bin) if args.logic_bin else build_dir / "bin" / "mir2_logic"

    if not gateway_bin.exists():
        raise FileNotFoundError(f"gateway binary not found: {gateway_bin}")
    if not logic_bin.exists():
        raise FileNotFoundError(f"logic binary not found: {logic_bin}")

    gateway_template = Path(args.gateway_config)
    logic_template = Path(args.logic_config)
    if not gateway_template.exists():
        raise FileNotFoundError(f"gateway config not found: {gateway_template}")
    if not logic_template.exists():
        raise FileNotFoundError(f"logic config not found: {logic_template}")

    root = Path.cwd()
    run_dir = (
        Path(args.results_dir)
        if args.results_dir
        else root / "artifacts" / "pressure" / f"gateway_logic_io4_{utc_now_stamp()}"
    )
    run_dir.mkdir(parents=True, exist_ok=True)

    gateway_log_dir = run_dir / "gateway_logs"
    logic_log_dir = run_dir / "logic_logs"
    gateway_log_dir.mkdir(parents=True, exist_ok=True)
    logic_log_dir.mkdir(parents=True, exist_ok=True)

    used_ports: set[int] = set()
    gateway_tcp_port = alloc_distinct_port(used_ports, udp=False)
    logic_tcp_port = alloc_distinct_port(used_ports, udp=False)
    gateway_udp_port = alloc_distinct_port(used_ports, udp=True)
    gateway_metrics_port = alloc_distinct_port(used_ports, udp=False)

    logic_metrics_port = 9091  # LogicServer currently hard-codes metrics port to 9091.

    generated_gateway_cfg = run_dir / "gateway.io4.pressure.yaml"
    generated_logic_cfg = run_dir / "logic.io4.pressure.yaml"

    gateway_updates: Dict[Tuple[str, ...], object] = {
        ("server", "bind_ip"): args.host,
        ("server", "port"): gateway_tcp_port,
        ("server", "udp_port"): gateway_udp_port,
        ("server", "metrics_port"): gateway_metrics_port,
        ("server", "io_threads"): args.gateway_io_threads,
        ("server", "login_ip_rate_limit_capacity"): args.gateway_login_rate_limit_capacity,
        ("server", "login_ip_rate_limit_refill_rate"): args.gateway_login_rate_limit_refill_rate,
        ("server", "udp_send_fault_inject_every_n"): args.gateway_udp_send_fault_inject_every_n,
        ("log", "path"): str(gateway_log_dir),
        ("services", "logic", "host"): args.host,
        ("services", "logic", "port"): logic_tcp_port,
        ("services", "logic", "transport"): "tcp",
    }
    logic_updates: Dict[Tuple[str, ...], object] = {
        ("server", "bind_ip"): args.host,
        ("server", "port"): logic_tcp_port,
        ("server", "metrics_port"): logic_metrics_port,
        ("server", "io_threads"): args.logic_io_threads,
        ("server", "login_username_rate_limit_capacity"): args.logic_login_rate_limit_capacity,
        ("server", "login_username_rate_limit_refill_rate"): args.logic_login_rate_limit_refill_rate,
        ("server", "login_username_rate_limit_refill_interval_seconds"):
            args.logic_login_rate_limit_refill_interval_seconds,
        ("log", "path"): str(logic_log_dir),
        ("services", "logic", "host"): args.host,
        ("services", "logic", "port"): logic_tcp_port,
        ("services", "logic", "transport"): "tcp",
    }

    if args.db_host:
        gateway_updates[("database", "host")] = args.db_host
        logic_updates[("database", "host")] = args.db_host
    if args.db_port > 0:
        gateway_updates[("database", "port")] = args.db_port
        logic_updates[("database", "port")] = args.db_port
    if args.db_user:
        gateway_updates[("database", "user")] = args.db_user
        logic_updates[("database", "user")] = args.db_user
    if args.db_password:
        gateway_updates[("database", "password")] = args.db_password
        logic_updates[("database", "password")] = args.db_password
    if args.db_name:
        gateway_updates[("database", "database")] = args.db_name
        logic_updates[("database", "database")] = args.db_name

    rewrite_yaml(gateway_template, generated_gateway_cfg, gateway_updates)
    rewrite_yaml(logic_template, generated_logic_cfg, logic_updates)

    # Logic config manager loads combat_config.yaml relative to logic config path.
    combat_template = logic_template.parent / "combat_config.yaml"
    if combat_template.exists():
        shutil.copy2(combat_template, run_dir / "combat_config.yaml")

    auth_account_count = args.auth_account_count if args.auth_account_count > 0 else args.workers

    seed_mode_used = "skipped"
    if args.seed_auth_accounts and auth_account_count > 0:
        seed_host, seed_port, seed_user, seed_password, seed_name = resolve_seed_database_config(
            generated_logic_cfg,
            args.db_host,
            args.db_port,
            args.db_user,
            args.db_password,
            args.db_name,
        )

        seed_mode_used = seed_auth_accounts(
            mode=args.seed_mode,
            docker_container=args.db_docker_container,
            db_host=seed_host,
            db_port=seed_port,
            db_user=seed_user,
            db_password=seed_password,
            db_name=seed_name,
            user_prefix=args.auth_user_prefix,
            user_count=auth_account_count,
            auth_password=args.auth_password,
        )

    logic_stdout_path = run_dir / "logic.stdout.log"
    gateway_stdout_path = run_dir / "gateway.stdout.log"

    logic_stdout = logic_stdout_path.open("w", encoding="utf-8")
    gateway_stdout = gateway_stdout_path.open("w", encoding="utf-8")

    logic_proc: Optional[subprocess.Popen[str]] = None
    gateway_proc: Optional[subprocess.Popen[str]] = None

    rows: List[SampleRow] = []
    sampler = SamplerState()

    load_stats = LoadStats()
    load_stats_lock = threading.Lock()
    udp_stats = UdpInjectStats()
    udp_stats_lock = threading.Lock()

    business_stop_event = threading.Event()
    udp_stop_event = threading.Event()
    business_workers: List[threading.Thread] = []
    udp_workers: List[threading.Thread] = []

    run_succeeded = False

    try:
        logic_proc = subprocess.Popen(
            [str(logic_bin), "--config", str(generated_logic_cfg)],
            cwd=str(root),
            stdout=logic_stdout,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        if not wait_tcp_ready(args.host, logic_tcp_port, timeout_sec=20):
            raise RuntimeError(
                "logic server did not become ready on tcp port "
                f"{logic_tcp_port}\n{tail_file(logic_stdout_path)}"
            )

        gateway_proc = subprocess.Popen(
            [str(gateway_bin), "--config", str(generated_gateway_cfg)],
            cwd=str(root),
            stdout=gateway_stdout,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        if not wait_tcp_ready(args.host, gateway_tcp_port, timeout_sec=20):
            raise RuntimeError(
                "gateway server did not become ready on tcp port "
                f"{gateway_tcp_port}\n{tail_file(gateway_stdout_path)}"
            )

        gateway_metrics_url = f"http://{args.host}:{gateway_metrics_port}/metrics"
        metrics_ready = False
        for _ in range(30):
            try:
                fetch_metrics(gateway_metrics_url, timeout_sec=1.0)
                metrics_ready = True
                break
            except (urllib.error.URLError, TimeoutError, OSError):
                time.sleep(0.5)
        if not metrics_ready:
            raise RuntimeError(
                "gateway metrics endpoint is unavailable. "
                "Build with LEGEND2_ENABLE_PROMETHEUS=ON or use a metrics-enabled binary.\n"
                f"gateway log tail:\n{tail_file(gateway_stdout_path)}"
            )

        reconnect_pause_sec = max(args.reconnect_pause_ms, 0) / 1000.0
        connect_timeout_sec = max(args.connect_timeout_ms, 1) / 1000.0
        login_timeout_sec = max(args.login_timeout_ms, 1) / 1000.0
        business_timeout_sec = max(args.business_timeout_ms, 1) / 1000.0
        kcp_upgrade_timeout_sec = max(args.kcp_upgrade_timeout_ms, 1) / 1000.0

        start_monotonic = time.monotonic()

        # Phase 1: authenticated business path load.
        business_deadline = start_monotonic + float(args.duration_sec)
        for worker_id in range(args.workers):
            thread = threading.Thread(
                target=business_load_worker,
                args=(
                    worker_id,
                    args.host,
                    gateway_tcp_port,
                    business_stop_event,
                    business_deadline,
                    args.burst_per_connection,
                    reconnect_pause_sec,
                    connect_timeout_sec,
                    login_timeout_sec,
                    business_timeout_sec,
                    args.auth_user_prefix,
                    args.auth_password,
                    args.auth_version,
                    auth_account_count,
                    args.business_msg_id,
                    args.business_rsp_msg_id,
                    load_stats,
                    load_stats_lock,
                ),
                daemon=True,
            )
            thread.start()
            business_workers.append(thread)

        sample_metrics_until(
            phase="business",
            end_monotonic=business_deadline,
            start_monotonic=start_monotonic,
            sample_interval_sec=args.sample_interval_sec,
            gateway_metrics_url=gateway_metrics_url,
            max_consecutive_metrics_failures=args.max_consecutive_metrics_failures,
            rows=rows,
            sampler=sampler,
        )

        business_stop_event.set()
        for thread in business_workers:
            thread.join(timeout=5)

        # Phase 2: dedicated UDP injection phase.
        if args.udp_inject_duration_sec > 0:
            udp_start = time.monotonic()
            udp_deadline = udp_start + float(args.udp_inject_duration_sec)

            for worker_id in range(args.udp_inject_workers):
                thread = threading.Thread(
                    target=udp_inject_worker,
                    args=(
                        worker_id,
                        args.host,
                        gateway_tcp_port,
                        udp_stop_event,
                        udp_deadline,
                        reconnect_pause_sec,
                        connect_timeout_sec,
                        login_timeout_sec,
                        kcp_upgrade_timeout_sec,
                        args.auth_user_prefix,
                        args.auth_password,
                        args.auth_version,
                        auth_account_count,
                        args.udp_inject_packets_per_session,
                        args.udp_inject_payload_bytes,
                        udp_stats,
                        udp_stats_lock,
                    ),
                    daemon=True,
                )
                thread.start()
                udp_workers.append(thread)

            sample_metrics_until(
                phase="udp_inject",
                end_monotonic=udp_deadline,
                start_monotonic=start_monotonic,
                sample_interval_sec=args.sample_interval_sec,
                gateway_metrics_url=gateway_metrics_url,
                max_consecutive_metrics_failures=args.max_consecutive_metrics_failures,
                rows=rows,
                sampler=sampler,
            )

            udp_stop_event.set()
            for thread in udp_workers:
                thread.join(timeout=5)

        csv_path = run_dir / "metrics_curve.csv"
        write_csv(csv_path, rows)

        disconnect_curve_path = run_dir / "disconnect_delta_curve.txt"
        write_ascii_curve(
            disconnect_curve_path,
            rows,
            "session_unregister_delta",
            "Disconnect Peak Curve (gateway_session_unregister delta/sample)",
        )
        udp_curve_path = run_dir / "udp_send_delta_curve.txt"
        write_ascii_curve(
            udp_curve_path,
            rows,
            "udp_send_errors_delta",
            "udp_send Error Curve (mir2_errors_total{reason=\"udp_send\"} delta/sample)",
        )

        peak_disconnect_delta = max((row.session_unregister_delta for row in rows), default=0.0)
        peak_disconnect_queue = max((row.disconnect_queue_size for row in rows), default=0.0)
        peak_udp_send_delta = max((row.udp_send_errors_delta for row in rows), default=0.0)
        final_udp_send_total = rows[-1].udp_send_errors_total if rows else 0.0

        with load_stats_lock:
            load_snapshot = LoadStats(**vars(load_stats))
        with udp_stats_lock:
            udp_snapshot = UdpInjectStats(**vars(udp_stats))

        summary_lines = [
            "Gateway-Logic io_threads pressure summary",
            f"run_dir={run_dir}",
            f"gateway_tcp_port={gateway_tcp_port}",
            f"gateway_udp_port={gateway_udp_port}",
            f"gateway_metrics_port={gateway_metrics_port}",
            f"logic_tcp_port={logic_tcp_port}",
            f"business_duration_sec={args.duration_sec}",
            f"udp_inject_duration_sec={args.udp_inject_duration_sec}",
            f"gateway_io_threads={args.gateway_io_threads}",
            f"logic_io_threads={args.logic_io_threads}",
            f"gateway_login_rate_limit_capacity={args.gateway_login_rate_limit_capacity}",
            f"gateway_login_rate_limit_refill_rate={args.gateway_login_rate_limit_refill_rate}",
            f"logic_login_rate_limit_capacity={args.logic_login_rate_limit_capacity}",
            f"logic_login_rate_limit_refill_rate={args.logic_login_rate_limit_refill_rate}",
            "logic_login_rate_limit_refill_interval_seconds="
            f"{args.logic_login_rate_limit_refill_interval_seconds}",
            f"gateway_udp_send_fault_inject_every_n={args.gateway_udp_send_fault_inject_every_n}",
            f"workers={args.workers}",
            f"udp_inject_workers={args.udp_inject_workers}",
            f"business_per_connection={args.burst_per_connection}",
            f"business_msg_id={args.business_msg_id}",
            f"business_rsp_msg_id={args.business_rsp_msg_id}",
            f"auth_account_count={auth_account_count}",
            f"seed_mode_used={seed_mode_used}",
            f"samples={sampler.sample_count}",
            f"metrics_fetch_errors={sampler.metrics_fetch_errors}",
            f"peak_disconnect_unregister_delta={peak_disconnect_delta:.0f}",
            f"peak_disconnect_queue_size={peak_disconnect_queue:.0f}",
            f"peak_udp_send_delta={peak_udp_send_delta:.0f}",
            f"final_udp_send_total={final_udp_send_total:.0f}",
            f"connect_attempts={load_snapshot.connect_attempts}",
            f"connect_success={load_snapshot.connect_success}",
            f"send_errors={load_snapshot.send_errors}",
            f"login_attempts={load_snapshot.login_attempts}",
            f"login_success={load_snapshot.login_success}",
            f"login_failed={load_snapshot.login_failed}",
            f"login_rate_limited={load_snapshot.login_rate_limited}",
            f"login_account_not_found={load_snapshot.login_account_not_found}",
            f"login_password_wrong={load_snapshot.login_password_wrong}",
            f"login_code_ok_zero_account={load_snapshot.login_code_ok_zero_account}",
            f"login_code_unknown={load_snapshot.login_code_unknown}",
            f"login_other_code={load_snapshot.login_other_code}",
            f"login_timeout={load_snapshot.login_timeout}",
            f"login_decode_errors={load_snapshot.login_decode_errors}",
            f"business_requests={load_snapshot.business_requests}",
            f"business_rsp_ok={load_snapshot.business_rsp_ok}",
            f"business_rsp_non_ok={load_snapshot.business_rsp_non_ok}",
            f"business_rsp_timeout={load_snapshot.business_rsp_timeout}",
            f"business_decode_errors={load_snapshot.business_decode_errors}",
            f"udp_upgrade_attempts={udp_snapshot.upgrade_attempts}",
            f"udp_upgrade_success={udp_snapshot.upgrade_success}",
            f"udp_upgrade_failed={udp_snapshot.upgrade_failed}",
            f"udp_upgrade_timeout={udp_snapshot.upgrade_timeout}",
            f"udp_packets_sent={udp_snapshot.udp_packets_sent}",
            f"udp_client_send_errors={udp_snapshot.udp_client_send_errors}",
            f"udp_ack_packets_recv={udp_snapshot.udp_ack_packets_recv}",
            f"metrics_csv={csv_path}",
            f"disconnect_curve={disconnect_curve_path}",
            f"udp_send_curve={udp_curve_path}",
        ]

        summary_path = run_dir / "summary.txt"
        summary_path.write_text("\n".join(summary_lines) + "\n", encoding="utf-8")
        print("\n".join(summary_lines))

        run_succeeded = True

    finally:
        business_stop_event.set()
        udp_stop_event.set()

        for thread in business_workers:
            thread.join(timeout=2)
        for thread in udp_workers:
            thread.join(timeout=2)

        if gateway_proc is not None:
            try:
                terminate_process(gateway_proc, "gateway")
            except Exception:
                pass
        if logic_proc is not None:
            try:
                terminate_process(logic_proc, "logic")
            except Exception:
                pass

        gateway_stdout.close()
        logic_stdout.close()

        if args.cleanup_artifacts and run_succeeded:
            shutil.rmtree(run_dir, ignore_errors=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env bash
# wait-for-it.sh
#
# 用于等待 TCP 主机:端口可用的脚本
# 支持超时、重试机制和详细的日志输出
# 适用于 Docker 容器启动时等待依赖服务就绪
#
# 使用示例:
#   wait-for-it.sh db:3306 -t 30 -- echo "Database is ready"
#   wait-for-it.sh redis:6379 gateway:50051 -t 60 -s -- ./start-app.sh

set -e

# 颜色定义
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# 默认配置
TIMEOUT=15
STRICT=0
QUIET=0
WAIT_HOSTS=""
WAIT_BEFORE=0
WAIT_AFTER=0
SLEEP_INTERVAL=1

# 日志函数
log_info() {
    if [[ $QUIET -eq 0 ]]; then
        echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $*"
    fi
}

log_success() {
    if [[ $QUIET -eq 0 ]]; then
        echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $*"
    fi
}

log_warn() {
    if [[ $QUIET -eq 0 ]]; then
        echo -e "${YELLOW}[WARN]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $*" >&2
    fi
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $*" >&2
}

# 显示使用帮助
usage() {
    cat << EOF
使用方法: wait-for-it.sh [主机:端口]... [选项] [-- 命令 参数...]

等待一个或多个 TCP 主机:端口变为可用，然后执行命令（可选）

参数:
    主机:端口              要等待的服务地址（可以指定多个）

选项:
    -h, --help            显示此帮助信息
    -t, --timeout=秒      超时时间（默认: 15 秒，0 表示无限等待）
    -s, --strict          严格模式：如果超时则以错误退出
    -q, --quiet           安静模式：不输出日志信息
    -w, --wait-before=秒  开始等待前的延迟时间（默认: 0）
    -a, --wait-after=秒   所有服务就绪后的额外等待时间（默认: 0）
    -i, --interval=秒     检查间隔时间（默认: 1 秒）

示例:
    # 等待单个服务（默认15秒超时）
    wait-for-it.sh db:3306

    # 等待多个服务（30秒超时）
    wait-for-it.sh db:3306 redis:6379 -t 30

    # 等待服务后执行命令（严格模式）
    wait-for-it.sh db:3306 -t 30 -s -- ./start-server.sh

    # 等待前延迟5秒，等待后延迟2秒
    wait-for-it.sh gateway:50051 -w 5 -a 2 -- ./game-server

    # 安静模式，仅在失败时输出错误
    wait-for-it.sh db:3306 redis:6379 -q -t 60

环境变量:
    WAIT_HOSTS            要等待的主机列表（空格分隔）
    WAIT_HOSTS_TIMEOUT    超时时间（秒）
    WAIT_BEFORE_HOSTS     开始等待前的延迟时间（秒）
    WAIT_AFTER_HOSTS      所有服务就绪后的额外等待时间（秒）
    WAIT_SLEEP_INTERVAL   检查间隔时间（秒）

退出代码:
    0   所有服务就绪（或命令执行成功）
    1   参数错误或命令执行失败
    124 超时（仅在严格模式下）

EOF
    exit 0
}

# 解析主机:端口
parse_host_port() {
    local hostport=$1

    # 检查是否包含端口
    if [[ ! $hostport =~ : ]]; then
        log_error "无效格式: '$hostport' (应为 主机:端口)"
        return 1
    fi

    # 分离主机和端口
    local host="${hostport%:*}"
    local port="${hostport##*:}"

    # 验证端口号
    if ! [[ $port =~ ^[0-9]+$ ]] || [ "$port" -lt 1 ] || [ "$port" -gt 65535 ]; then
        log_error "无效端口号: '$port' (应为 1-65535)"
        return 1
    fi

    echo "$host $port"
}

# 等待单个主机:端口
wait_for_service() {
    local host=$1
    local port=$2
    local timeout=$3
    local start_ts=$(date +%s)

    log_info "等待 $host:$port 可用..."

    while true; do
        # 尝试连接（使用多种方法以提高兼容性）
        if command -v nc &> /dev/null; then
            # 使用 netcat
            if nc -z -w1 "$host" "$port" 2>/dev/null; then
                log_success "$host:$port 已就绪"
                return 0
            fi
        elif command -v timeout &> /dev/null; then
            # 使用 bash 的 /dev/tcp（通过 timeout 避免挂起）
            if timeout 1 bash -c "cat < /dev/null > /dev/tcp/$host/$port" 2>/dev/null; then
                log_success "$host:$port 已就绪"
                return 0
            fi
        elif command -v telnet &> /dev/null; then
            # 使用 telnet（最后的备选方案）
            if echo "quit" | telnet "$host" "$port" 2>/dev/null | grep -q "Connected"; then
                log_success "$host:$port 已就绪"
                return 0
            fi
        else
            # 使用纯 bash 的 /dev/tcp（可能会挂起）
            if timeout 1 bash -c "cat < /dev/null > /dev/tcp/$host/$port" 2>/dev/null; then
                log_success "$host:$port 已就绪"
                return 0
            fi
        fi

        # 检查超时
        if [ "$timeout" -ne 0 ]; then
            local current_ts=$(date +%s)
            local elapsed=$((current_ts - start_ts))

            if [ "$elapsed" -ge "$timeout" ]; then
                log_error "$host:$port 超时（已等待 ${elapsed} 秒）"
                return 1
            fi

            local remaining=$((timeout - elapsed))
            if [[ $QUIET -eq 0 ]] && [ $((elapsed % 5)) -eq 0 ] && [ "$elapsed" -gt 0 ]; then
                log_warn "$host:$port 尚未就绪，剩余 ${remaining} 秒..."
            fi
        fi

        sleep "$SLEEP_INTERVAL"
    done
}

# 等待所有服务
wait_for_all_services() {
    local all_ready=1

    # 开始等待前的延迟
    if [ "$WAIT_BEFORE" -gt 0 ]; then
        log_info "开始等待前延迟 ${WAIT_BEFORE} 秒..."
        sleep "$WAIT_BEFORE"
    fi

    # 遍历所有主机:端口
    for hostport in $WAIT_HOSTS; do
        local parsed
        if ! parsed=$(parse_host_port "$hostport"); then
            all_ready=0
            continue
        fi

        read -r host port <<< "$parsed"

        if ! wait_for_service "$host" "$port" "$TIMEOUT"; then
            all_ready=0
            if [ "$STRICT" -eq 1 ]; then
                log_error "严格模式：$host:$port 未能在超时时间内就绪"
                return 124  # 超时退出码
            fi
        fi
    done

    # 所有服务就绪后的额外等待
    if [ "$WAIT_AFTER" -gt 0 ] && [ "$all_ready" -eq 1 ]; then
        log_info "所有服务就绪，额外等待 ${WAIT_AFTER} 秒..."
        sleep "$WAIT_AFTER"
    fi

    if [ "$all_ready" -eq 1 ]; then
        log_success "所有服务已就绪！"
        return 0
    else
        log_warn "部分服务未就绪"
        return 1
    fi
}

# 主函数
main() {
    local command_to_run=()
    local parsing_hosts=1

    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                usage
                ;;
            -t|--timeout)
                TIMEOUT="$2"
                shift 2
                ;;
            --timeout=*)
                TIMEOUT="${1#*=}"
                shift
                ;;
            -s|--strict)
                STRICT=1
                shift
                ;;
            -q|--quiet)
                QUIET=1
                shift
                ;;
            -w|--wait-before)
                WAIT_BEFORE="$2"
                shift 2
                ;;
            --wait-before=*)
                WAIT_BEFORE="${1#*=}"
                shift
                ;;
            -a|--wait-after)
                WAIT_AFTER="$2"
                shift 2
                ;;
            --wait-after=*)
                WAIT_AFTER="${1#*=}"
                shift
                ;;
            -i|--interval)
                SLEEP_INTERVAL="$2"
                shift 2
                ;;
            --interval=*)
                SLEEP_INTERVAL="${1#*=}"
                shift
                ;;
            --)
                parsing_hosts=0
                shift
                command_to_run=("$@")
                break
                ;;
            -*)
                log_error "未知选项: $1"
                echo "使用 --help 查看帮助信息"
                exit 1
                ;;
            *)
                if [ "$parsing_hosts" -eq 1 ]; then
                    WAIT_HOSTS="$WAIT_HOSTS $1"
                fi
                shift
                ;;
        esac
    done

    # 从环境变量读取配置（如果未通过命令行指定）
    if [ -z "$WAIT_HOSTS" ] && [ -n "$WAIT_HOSTS" ]; then
        WAIT_HOSTS="$WAIT_HOSTS"
    fi

    if [ -n "$WAIT_HOSTS_TIMEOUT" ] && [ "$TIMEOUT" -eq 15 ]; then
        TIMEOUT="$WAIT_HOSTS_TIMEOUT"
    fi

    if [ -n "$WAIT_BEFORE_HOSTS" ] && [ "$WAIT_BEFORE" -eq 0 ]; then
        WAIT_BEFORE="$WAIT_BEFORE_HOSTS"
    fi

    if [ -n "$WAIT_AFTER_HOSTS" ] && [ "$WAIT_AFTER" -eq 0 ]; then
        WAIT_AFTER="$WAIT_AFTER_HOSTS"
    fi

    if [ -n "$WAIT_SLEEP_INTERVAL" ] && [ "$SLEEP_INTERVAL" -eq 1 ]; then
        SLEEP_INTERVAL="$WAIT_SLEEP_INTERVAL"
    fi

    # 去除前导空格
    WAIT_HOSTS=$(echo "$WAIT_HOSTS" | xargs)

    # 验证参数
    if [ -z "$WAIT_HOSTS" ]; then
        log_error "错误: 未指定要等待的主机:端口"
        echo ""
        echo "使用 --help 查看帮助信息"
        exit 1
    fi

    if ! [[ $TIMEOUT =~ ^[0-9]+$ ]]; then
        log_error "无效的超时值: '$TIMEOUT'"
        exit 1
    fi

    if ! [[ $WAIT_BEFORE =~ ^[0-9]+$ ]]; then
        log_error "无效的等待前延迟: '$WAIT_BEFORE'"
        exit 1
    fi

    if ! [[ $WAIT_AFTER =~ ^[0-9]+$ ]]; then
        log_error "无效的等待后延迟: '$WAIT_AFTER'"
        exit 1
    fi

    if ! [[ $SLEEP_INTERVAL =~ ^[0-9]+$ ]]; then
        log_error "无效的检查间隔: '$SLEEP_INTERVAL'"
        exit 1
    fi

    # 显示配置信息
    log_info "配置信息:"
    log_info "  等待服务: $WAIT_HOSTS"
    log_info "  超时时间: ${TIMEOUT}秒 $([ "$TIMEOUT" -eq 0 ] && echo "(无限)")"
    log_info "  严格模式: $([ "$STRICT" -eq 1 ] && echo "是" || echo "否")"
    log_info "  检查间隔: ${SLEEP_INTERVAL}秒"
    [ "$WAIT_BEFORE" -gt 0 ] && log_info "  等待前延迟: ${WAIT_BEFORE}秒"
    [ "$WAIT_AFTER" -gt 0 ] && log_info "  等待后延迟: ${WAIT_AFTER}秒"

    # 等待所有服务
    local exit_code=0
    if ! wait_for_all_services; then
        exit_code=$?
        if [ "$STRICT" -eq 1 ]; then
            log_error "等待服务失败，退出"
            exit "$exit_code"
        fi
    fi

    # 执行命令（如果有）
    if [ ${#command_to_run[@]} -gt 0 ]; then
        log_info "执行命令: ${command_to_run[*]}"
        exec "${command_to_run[@]}"
        exit_code=$?
    fi

    exit "$exit_code"
}

# 处理中断信号
trap 'log_warn "收到中断信号，退出..."; exit 130' INT TERM

# 执行主函数
main "$@"

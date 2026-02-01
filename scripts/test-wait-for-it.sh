#!/usr/bin/env bash
# test-wait-for-it.sh
#
# wait-for-it.sh 脚本的测试套件
# 用于验证各种场景下的功能是否正常

set -e

# 颜色定义
readonly GREEN='\033[0;32m'
readonly RED='\033[0;31m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WAIT_SCRIPT="$SCRIPT_DIR/wait-for-it.sh"
TEST_COUNT=0
PASS_COUNT=0
FAIL_COUNT=0

# 测试结果记录
log_test() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo -e "${BLUE}[TEST $TEST_COUNT]${NC} $*"
}

log_pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    echo -e "${GREEN}[PASS]${NC} $*"
}

log_fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    echo -e "${RED}[FAIL]${NC} $*"
}

log_info() {
    echo -e "${YELLOW}[INFO]${NC} $*"
}

# 清理函数
cleanup() {
    log_info "清理测试环境..."
    # 停止可能启动的测试服务
    pkill -f "nc -l" 2>/dev/null || true
    pkill -f "python -m http.server" 2>/dev/null || true
}

trap cleanup EXIT

# 启动临时测试服务
start_test_service() {
    local port=$1
    local delay=${2:-0}

    log_info "在端口 $port 启动测试服务（延迟 ${delay}s）..."

    # 在后台启动临时 HTTP 服务
    (
        sleep "$delay"
        if command -v nc &> /dev/null; then
            # 使用 netcat 监听端口
            while true; do
                echo -e "HTTP/1.1 200 OK\r\n\r\nTest Service" | nc -l -p "$port" -q 1
            done
        elif command -v python3 &> /dev/null; then
            # 使用 Python HTTP 服务器
            python3 -m http.server "$port" >/dev/null 2>&1
        else
            log_fail "无法启动测试服务：缺少 nc 或 python3"
            exit 1
        fi
    ) &

    local service_pid=$!
    echo "$service_pid"
}

# 测试1: 帮助信息
test_help() {
    log_test "测试帮助信息显示"

    if "$WAIT_SCRIPT" --help | grep -q "使用方法"; then
        log_pass "帮助信息正常显示"
    else
        log_fail "帮助信息未正确显示"
    fi
}

# 测试2: 无效参数
test_invalid_args() {
    log_test "测试无效参数处理"

    # 测试无效选项
    if ! "$WAIT_SCRIPT" --invalid-option 2>&1 | grep -q "未知选项"; then
        log_fail "未正确检测无效选项"
        return
    fi

    # 测试无效端口
    if ! "$WAIT_SCRIPT" localhost:99999 -t 1 2>&1 | grep -q "无效端口号"; then
        log_fail "未正确检测无效端口号"
        return
    fi

    # 测试缺少端口
    if ! "$WAIT_SCRIPT" localhost -t 1 2>&1 | grep -q "无效格式"; then
        log_fail "未正确检测缺少端口号"
        return
    fi

    log_pass "无效参数处理正确"
}

# 测试3: 等待已就绪的服务
test_ready_service() {
    log_test "测试等待已就绪的服务"

    # 启动测试服务
    local test_port=18888
    local pid=$(start_test_service $test_port 0)

    # 等待服务启动
    sleep 2

    # 测试等待脚本
    if "$WAIT_SCRIPT" localhost:$test_port -t 10 -q; then
        log_pass "成功连接到已就绪的服务"
        kill "$pid" 2>/dev/null || true
    else
        log_fail "无法连接到已就绪的服务"
        kill "$pid" 2>/dev/null || true
    fi
}

# 测试4: 等待延迟启动的服务
test_delayed_service() {
    log_test "测试等待延迟启动的服务"

    local test_port=18889
    local delay=3

    # 启动延迟服务（3秒后启动）
    local pid=$(start_test_service $test_port $delay)

    # 测试等待脚本（10秒超时应该足够）
    if "$WAIT_SCRIPT" localhost:$test_port -t 10 -q; then
        log_pass "成功等待延迟启动的服务"
        kill "$pid" 2>/dev/null || true
    else
        log_fail "等待延迟服务失败"
        kill "$pid" 2>/dev/null || true
    fi
}

# 测试5: 超时测试
test_timeout() {
    log_test "测试超时机制"

    # 使用不存在的端口测试超时
    local start_time=$(date +%s)

    if "$WAIT_SCRIPT" localhost:19999 -t 3 -q 2>/dev/null; then
        log_fail "应该超时但却成功了"
        return
    fi

    local end_time=$(date +%s)
    local elapsed=$((end_time - start_time))

    # 检查是否在预期时间内超时（允许1秒误差）
    if [ "$elapsed" -ge 2 ] && [ "$elapsed" -le 5 ]; then
        log_pass "超时机制正常工作（耗时 ${elapsed}s）"
    else
        log_fail "超时时间不符合预期（预期 3s，实际 ${elapsed}s）"
    fi
}

# 测试6: 严格模式
test_strict_mode() {
    log_test "测试严格模式"

    # 严格模式下超时应返回124
    "$WAIT_SCRIPT" localhost:19998 -t 2 -s -q 2>/dev/null
    local exit_code=$?

    if [ "$exit_code" -eq 124 ]; then
        log_pass "严格模式正确返回退出码 124"
    else
        log_fail "严格模式退出码错误（预期 124，实际 $exit_code）"
    fi
}

# 测试7: 多服务等待
test_multiple_services() {
    log_test "测试等待多个服务"

    local port1=18890
    local port2=18891

    # 启动两个测试服务
    local pid1=$(start_test_service $port1 0)
    local pid2=$(start_test_service $port2 1)

    # 等待服务启动
    sleep 2

    # 测试等待多个服务
    if "$WAIT_SCRIPT" localhost:$port1 localhost:$port2 -t 10 -q; then
        log_pass "成功等待多个服务"
    else
        log_fail "等待多个服务失败"
    fi

    kill "$pid1" "$pid2" 2>/dev/null || true
}

# 测试8: 命令执行
test_command_execution() {
    log_test "测试等待后执行命令"

    local test_port=18892
    local pid=$(start_test_service $test_port 0)

    sleep 2

    # 测试执行命令
    local output
    output=$("$WAIT_SCRIPT" localhost:$test_port -t 5 -q -- echo "SUCCESS" 2>&1)

    if echo "$output" | grep -q "SUCCESS"; then
        log_pass "命令执行成功"
    else
        log_fail "命令执行失败"
    fi

    kill "$pid" 2>/dev/null || true
}

# 测试9: 等待前后延迟
test_delays() {
    log_test "测试等待前后延迟"

    local test_port=18893
    local pid=$(start_test_service $test_port 0)

    sleep 2

    local start_time=$(date +%s)

    # 等待前延迟2秒，等待后延迟2秒
    "$WAIT_SCRIPT" localhost:$test_port -w 2 -a 2 -t 10 -q

    local end_time=$(date +%s)
    local elapsed=$((end_time - start_time))

    # 应该至少花费4秒（2+2）
    if [ "$elapsed" -ge 4 ]; then
        log_pass "等待延迟正常工作（耗时 ${elapsed}s）"
    else
        log_fail "等待延迟时间不足（预期 ≥4s，实际 ${elapsed}s）"
    fi

    kill "$pid" 2>/dev/null || true
}

# 测试10: 环境变量配置
test_environment_variables() {
    log_test "测试环境变量配置"

    local test_port=18894
    local pid=$(start_test_service $test_port 0)

    sleep 2

    # 通过环境变量配置
    export WAIT_HOSTS="localhost:$test_port"
    export WAIT_HOSTS_TIMEOUT=10

    if "$WAIT_SCRIPT" -q 2>/dev/null; then
        log_pass "环境变量配置正常工作"
    else
        log_fail "环境变量配置失败"
    fi

    unset WAIT_HOSTS WAIT_HOSTS_TIMEOUT
    kill "$pid" 2>/dev/null || true
}

# 主测试流程
main() {
    echo "========================================="
    echo "wait-for-it.sh 测试套件"
    echo "========================================="
    echo ""

    # 检查脚本是否存在
    if [ ! -f "$WAIT_SCRIPT" ]; then
        log_fail "找不到 wait-for-it.sh 脚本: $WAIT_SCRIPT"
        exit 1
    fi

    # 检查脚本是否可执行
    if [ ! -x "$WAIT_SCRIPT" ]; then
        log_info "添加执行权限..."
        chmod +x "$WAIT_SCRIPT"
    fi

    # 运行所有测试
    test_help
    echo ""

    test_invalid_args
    echo ""

    test_ready_service
    echo ""

    test_delayed_service
    echo ""

    test_timeout
    echo ""

    test_strict_mode
    echo ""

    test_multiple_services
    echo ""

    test_command_execution
    echo ""

    test_delays
    echo ""

    test_environment_variables
    echo ""

    # 显示测试结果
    echo "========================================="
    echo "测试结果汇总"
    echo "========================================="
    echo -e "总测试数: ${BLUE}$TEST_COUNT${NC}"
    echo -e "通过: ${GREEN}$PASS_COUNT${NC}"
    echo -e "失败: ${RED}$FAIL_COUNT${NC}"
    echo ""

    if [ "$FAIL_COUNT" -eq 0 ]; then
        echo -e "${GREEN}✓ 所有测试通过！${NC}"
        exit 0
    else
        echo -e "${RED}✗ 部分测试失败${NC}"
        exit 1
    fi
}

main "$@"

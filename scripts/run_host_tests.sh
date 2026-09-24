#!/usr/bin/env bash
# TianshangPulse host 单测驱动（不需硬件、不需 ESP-IDF 工具链）。
#
# 自动探测 C 编译器（gcc → clang → python -m ziglang cc），编译并运行：
#   - tests/host/test_offline_cache.c   环形缓冲语义（A5 精确 ACK-pop）
#   - tests/host/test_protocol.c        协议契约向量（AGENTS §9.5 DoD #1）
#
# 任一测试非零退出、或输出缺少 "ALL PASS"，整体失败（exit 1）。
# 用法：  ./scripts/run_host_tests.sh [编译器]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out_dir="$root/build/host-tests"
mkdir -p "$out_dir"

if [ "${1:-}" != "" ]; then
    command -v "$1" >/dev/null 2>&1 || { echo "指定的编译器不可用: $1" >&2; exit 1; }
    CC_CMD=("$1")
elif command -v gcc >/dev/null 2>&1; then
    CC_CMD=(gcc)
elif command -v clang >/dev/null 2>&1; then
    CC_CMD=(clang)
elif command -v python >/dev/null 2>&1 && python -c "import ziglang" >/dev/null 2>&1; then
    CC_CMD=(python -m ziglang cc)
elif command -v python3 >/dev/null 2>&1 && python3 -c "import ziglang" >/dev/null 2>&1; then
    CC_CMD=(python3 -m ziglang cc)
else
    echo "未找到 C 编译器。请安装 gcc / clang，或 pip install ziglang（推荐）。" >&2
    exit 1
fi

echo "==> 编译器: ${CC_CMD[*]}"

run_test() {
    local name="$1"; shift
    local exe="$out_dir/$name"
    echo "==> 编译 $name"
    "${CC_CMD[@]}" "$@" \
        -I tests/host/stubs -I firmware/main -O2 -Wall -Wextra \
        -Wno-unused-parameter -o "$exe"
    echo "==> 运行 $name"
    local out
    if ! out="$("$exe" 2>&1)"; then
        echo "$out"
        echo "!! $name: 运行失败" >&2
        return 1
    fi
    echo "$out"
    case "$out" in
        *"ALL PASS"*) : ;;
        *) echo "!! $name: 输出缺少 ALL PASS" >&2; return 1 ;;
    esac
}

cd "$root"
rc=0
run_test test_offline_cache tests/host/test_offline_cache.c firmware/main/ble/offline_cache.c || rc=1
run_test test_protocol      tests/host/test_protocol.c      firmware/main/ble/protocol.c      || rc=1

echo
if [ "$rc" -ne 0 ]; then
    echo "host tests FAILED" >&2
    exit 1
fi
echo "host tests: ALL PASS"

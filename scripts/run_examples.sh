#!/usr/bin/env bash
# 运行所有 examples，并把 stdout/stderr 追加到 logs/run.log
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
LOG_DIR="$ROOT/logs"
LOG="$LOG_DIR/run.log"
mkdir -p "$LOG_DIR"

PREFIX="${BRPC_ROOT:-/usr/local}"
export LD_LIBRARY_PATH="${PREFIX}/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH:-}"

echo "" >> "$LOG"
echo "========== $(date '+%Y-%m-%d %H:%M:%S') run_examples ==========" >> "$LOG"

# 起一个 echo server 作为 RPC 例子（echo_client / backup_request / lb 共用）
echo "[run] start echo_server on 8000" | tee -a "$LOG"
"$BUILD/echo_server" --port=8000 >/dev/null 2>&1 &
SERVER_PID=$!
sleep 2

run() {
    local name="$1"; shift
    echo "---------- [$name] $* ----------" | tee -a "$LOG"
    "$BUILD/$name" "$@" 2>&1 | tee -a "$LOG"
    echo "---------- [$name] exit=$? ----------" | tee -a "$LOG"
}

# 非 RPC 依赖的例子
run bthread_demo
run iobuf_demo
run bvar_demo

# RPC 例子（依赖 8000 端口的 echo_server）
run echo_client --target="127.0.0.1:8000" --count=3

# backup_request：两个 server（8000 + 8003），偶发慢请求触发备份
"$BUILD/echo_server" --port=8003 >/dev/null 2>&1 &
SERVER_BAK_PID=$!
sleep 2
run backup_request --target="list://127.0.0.1:8000,127.0.0.1:8003" --count=5
kill "$SERVER_BAK_PID" 2>/dev/null

# selective_echo：再起第二个 server
"$BUILD/echo_server" --port=8002 >/dev/null 2>&1 &
SERVER2_PID=$!
sleep 2
run selective_echo --port1=8000 --port2=8002 --count=3
kill "$SERVER2_PID" 2>/dev/null

# load_balancing：8000 + 8006 两个节点
"$BUILD/echo_server" --port=8006 >/dev/null 2>&1 &
SERVER3_PID=$!
sleep 2
run load_balancing --target="list://127.0.0.1:8000,127.0.0.1:8006" --count=8
kill "$SERVER3_PID" 2>/dev/null

# kv_server + kv_client：leveldb 作为存储引擎（8004）
"$BUILD/kv_server" --port=8004 --db=/tmp/brpc_kv_demo >/dev/null 2>&1 &
KV_PID=$!
sleep 2
run kv_client --target="127.0.0.1:8004"
kill "$KV_PID" 2>/dev/null

kill "$SERVER_PID" 2>/dev/null
echo "[run] all done. log=$LOG" | tee -a "$LOG"

#!/bin/sh
set -e

BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
DOCS_DIR="$(cd "$(dirname "$0")/../docs" && pwd)"

echo "=== Running NetStream Benchmark Suite ==="

# 1. TCP Server Benchmark
echo "Starting TCP Server for benchmarks..."
"$BUILD_DIR/netstream_server" -p 8080 -t 4 &
SERVER_PID=$!

cleanup() {
    kill -INT "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 1

echo "1. Running TCP 128-byte payload benchmark (16 clients x 250 requests)..."
"$BUILD_DIR/netstream_load_generator" -a 127.0.0.1 -p 8080 -c 16 -r 250 -s 128 -o "$DOCS_DIR/benchmark_tcp_128b"

echo "2. Running TCP 4096-byte payload benchmark (16 clients x 250 requests)..."
"$BUILD_DIR/netstream_load_generator" -a 127.0.0.1 -p 8080 -c 16 -r 250 -s 4096 -o "$DOCS_DIR/benchmark_tcp_4k"

cleanup
trap - EXIT INT TERM

# 2. UDP Streaming Benchmark
echo "3. Running UDP 2MB file transfer benchmark..."
SRC="/tmp/bench_udp_src.bin"
DST="/tmp/bench_udp_dst.bin"
dd if=/dev/urandom of="$SRC" bs=1024 count=2048 2>/dev/null

"$BUILD_DIR/netstream_udp_receiver" -o "$DST" -p 9000 &
RX_PID=$!
sleep 1
"$BUILD_DIR/netstream_udp_sender" -f "$SRC" -p 9000 -i 50
wait "$RX_PID"

rm -f "$SRC" "$DST"

echo "=== Benchmark Suite Complete ==="
echo "Reports saved to: $DOCS_DIR/"

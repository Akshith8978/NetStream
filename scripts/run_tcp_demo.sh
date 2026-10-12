#!/bin/sh
set -e

PORT=${1:-8080}
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"

echo "=== NetStream TCP Demo ==="
echo "Starting netstream_server on port $PORT..."
"$BUILD_DIR/netstream_server" -p "$PORT" -t 4 -q 512 &
SERVER_PID=$!

cleanup() {
    echo "Stopping server (PID: $SERVER_PID)..."
    kill -INT "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    echo "Server stopped."
}
trap cleanup EXIT INT TERM

sleep 1

echo "Sending text messages..."
"$BUILD_DIR/netstream_client" -p "$PORT" -c 5 -s 64

echo "Sending binary payload messages (with embedded null bytes)..."
"$BUILD_DIR/netstream_client" -p "$PORT" -c 5 -s 256 -b

echo "=== TCP Demo Completed Successfully ==="

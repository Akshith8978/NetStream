#!/bin/sh
set -e

PORT=${1:-9000}
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
SRC_FILE="/tmp/netstream_demo_src.bin"
DST_FILE="/tmp/netstream_demo_dst.bin"

echo "=== NetStream UDP File Streamer Demo ==="
echo "Generating 1MB random source file..."
dd if=/dev/urandom of="$SRC_FILE" bs=1024 count=1024 2>/dev/null

ORIG_HASH=$(sha256sum "$SRC_FILE" | awk '{print $1}')
echo "Source SHA-256: $ORIG_HASH"

echo "Starting UDP receiver on port $PORT..."
"$BUILD_DIR/netstream_udp_receiver" -o "$DST_FILE" -p "$PORT" &
RX_PID=$!

sleep 1

echo "Streaming file from UDP sender..."
"$BUILD_DIR/netstream_udp_sender" -f "$SRC_FILE" -p "$PORT" -i 100

echo "Waiting for receiver to finish..."
wait "$RX_PID"

RECON_HASH=$(sha256sum "$DST_FILE" | awk '{print $1}')
echo "Reconstructed SHA-256: $RECON_HASH"

if [ "$ORIG_HASH" = "$RECON_HASH" ]; then
    echo "SUCCESS: Checksums match perfectly! Loss-free file reconstruction verified."
else
    echo "FAILURE: Checksums mismatch!"
    exit 1
fi

rm -f "$SRC_FILE" "$DST_FILE"
echo "=== UDP Demo Completed Successfully ==="

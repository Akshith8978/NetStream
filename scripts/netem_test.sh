#!/bin/sh
set -e

IFACE=${1:-lo}
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"
SRC_FILE="/tmp/netem_src.bin"
DST_FILE="/tmp/netem_dst.bin"

echo "=== Linux Netem Network Impairment Test Suite ==="
echo "Target interface: $IFACE"

if ! command -v tc >/dev/null 2>&1; then
    echo "ERROR: 'tc' command not found. Please install iproute2."
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "WARNING: Root privileges are required to configure traffic control (tc)."
    echo "Please run this script with sudo or as root: sudo $0 $IFACE"
    exit 1
fi

cleanup_qdisc() {
    echo "Cleaning up netem qdisc on $IFACE..."
    tc qdisc del dev "$IFACE" root 2>/dev/null || true
    rm -f "$SRC_FILE" "$DST_FILE"
}
trap cleanup_qdisc EXIT INT TERM

# Generate 512KB random test file
dd if=/dev/urandom of="$SRC_FILE" bs=1024 count=512 2>/dev/null
ORIG_HASH=$(sha256sum "$SRC_FILE" | awk '{print $1}')

run_scenario() {
    NAME="$1"
    NETEM_RULE="$2"

    echo "\n---------------------------------------------------------"
    echo " Scenario: $NAME"
    echo " Netem Rule: tc qdisc add dev $IFACE root netem $NETEM_RULE"
    echo "---------------------------------------------------------"

    tc qdisc del dev "$IFACE" root 2>/dev/null || true
    if [ -n "$NETEM_RULE" ]; then
        tc qdisc add dev "$IFACE" root netem $NETEM_RULE
    fi

    rm -f "$DST_FILE"
    "$BUILD_DIR/netstream_udp_receiver" -o "$DST_FILE" -p 9100 >/tmp/netem_rx.log 2>&1 &
    RX_PID=$!
    sleep 0.5

    "$BUILD_DIR/netstream_udp_sender" -f "$SRC_FILE" -p 9100 -i 200 >/tmp/netem_tx.log 2>&1
    wait "$RX_PID" || true

    cat /tmp/netem_rx.log | grep -E "Summary|Packets|Bytes|Complete|Goodput" || cat /tmp/netem_rx.log
}

# 1. Baseline: No impairment
run_scenario "1. Baseline (No impairment)" ""

# 2. 50ms Delay
run_scenario "2. Fixed Delay (50ms)" "delay 50ms"

# 3. Delay with Jitter
run_scenario "3. Delay with Jitter (50ms +/- 15ms)" "delay 50ms 15ms"

# 4. 1% Packet Loss
run_scenario "4. Low Packet Loss (1%)" "loss 1%"

# 5. 5% Packet Loss
run_scenario "5. High Packet Loss (5%)" "loss 5%"

# 6. Packet Reordering
run_scenario "6. Packet Reordering (25% reordered with 10ms delay)" "delay 10ms reorder 25% 50%"

# 7. Combined Delay and Loss
run_scenario "7. Combined Impairment (30ms jitter + 3% loss)" "delay 30ms 10ms loss 3%"

echo "\n=== All Network Impairment Scenarios Tested ==="

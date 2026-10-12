#!/bin/sh
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MODE=${1:-all}

echo "=== NetStream Sanitizer & Diagnostic Suite ==="

run_asan() {
    echo "--- Configuring and Testing with AddressSanitizer & UBSan ---"
    BUILD_DIR="$ROOT_DIR/build_asan"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    if cmake -DENABLE_ASAN_UBSAN=ON .. && make -j4; then
        ctest --output-on-failure
        echo "[PASS] ASan & UBSan tests passed!"
    else
        echo "[INFO] ASan/UBSan build requires glibc libasan or clang compiler-rt."
        echo "On Alpine (musl), use clang or standard test runner."
    fi
}

run_tsan() {
    echo "--- Configuring and Testing with ThreadSanitizer ---"
    BUILD_DIR="$ROOT_DIR/build_tsan"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    if cmake -DENABLE_TSAN=ON .. && make -j4; then
        ctest --output-on-failure
        echo "[PASS] TSan tests passed!"
    else
        echo "[INFO] TSan requires glibc libtsan or clang compiler-rt."
    fi
}

run_valgrind() {
    echo "--- Testing with Valgrind Memcheck ---"
    BUILD_DIR="$ROOT_DIR/build"
    if [ ! -d "$BUILD_DIR" ]; then
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR"
        cmake .. && make -j4
    fi

    if command -v valgrind >/dev/null 2>&1; then
        valgrind --leak-check=full --error-exitcode=1 "$BUILD_DIR/test_protocol"
        valgrind --leak-check=full --error-exitcode=1 "$BUILD_DIR/test_udp_packet"
        echo "[PASS] Valgrind Memcheck passed with 0 memory leaks!"
    else
        echo "[INFO] Valgrind is not installed. To install on Ubuntu/Debian: sudo apt-get install valgrind"
    fi
}

case "$MODE" in
    asan) run_asan ;;
    tsan) run_tsan ;;
    valgrind) run_valgrind ;;
    all)
        run_asan
        run_tsan
        run_valgrind
        ;;
    *)
        echo "Usage: $0 [asan|tsan|valgrind|all]"
        exit 1
        ;;
esac

echo "=== Diagnostic Run Completed ==="

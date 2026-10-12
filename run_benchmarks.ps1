# NetStream PowerShell Helper: Run Benchmarks
$ErrorActionPreference = "Stop"
Write-Host "Running NetStream Benchmark Suite inside Linux..." -ForegroundColor Cyan

wsl -d docker-desktop /bin/sh -c "mount --bind '/mnt/host/c/Users/N. AKSHITH/.gemini/antigravity-ide/scratch/NetStream' /mnt/host/c/Users/Public/alpine_build/workspace && chroot /mnt/host/c/Users/Public/alpine_build /workspace/scripts/benchmark.sh"

# NetStream PowerShell Helper: Run Automated Test Suite
$ErrorActionPreference = "Stop"
Write-Host "Running NetStream Test Suite (CTest) inside Linux..." -ForegroundColor Cyan

wsl -d docker-desktop /bin/sh -c "mount --bind '/mnt/host/c/Users/N. AKSHITH/.gemini/antigravity-ide/scratch/NetStream' /mnt/host/c/Users/Public/alpine_build/workspace && chroot /mnt/host/c/Users/Public/alpine_build /bin/sh -c 'cd /workspace/build && ctest --output-on-failure --verbose'"

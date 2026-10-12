# NetStream PowerShell Helper: Run UDP Streaming Demo
param (
    [int]$Port = 9000
)

$ErrorActionPreference = "Stop"
Write-Host "Running NetStream UDP File Streaming Demo inside Linux..." -ForegroundColor Cyan

wsl -d docker-desktop /bin/sh -c "mount --bind '/mnt/host/c/Users/N. AKSHITH/.gemini/antigravity-ide/scratch/NetStream' /mnt/host/c/Users/Public/alpine_build/workspace && chroot /mnt/host/c/Users/Public/alpine_build /workspace/scripts/run_udp_demo.sh $Port"

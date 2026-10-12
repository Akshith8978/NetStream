# NetStream PowerShell Helper: Run TCP Demo
param (
    [int]$Port = 8080
)

$ErrorActionPreference = "Stop"
Write-Host "Running NetStream TCP Demo inside Linux..." -ForegroundColor Cyan

wsl -d docker-desktop /bin/sh -c "mount --bind '/mnt/host/c/Users/N. AKSHITH/.gemini/antigravity-ide/scratch/NetStream' /mnt/host/c/Users/Public/alpine_build/workspace && chroot /mnt/host/c/Users/Public/alpine_build /workspace/scripts/run_tcp_demo.sh $Port"

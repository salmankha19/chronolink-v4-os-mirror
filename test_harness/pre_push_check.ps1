# Pre-push regression: build test firmware, run --quick scenarios
# --save-log: logs preserved even on PASS for future regression debugging
$ErrorActionPreference = "Stop"

Write-Host "==> Building test firmware..." -ForegroundColor Cyan
pio run -e single_mcu_test -t upload
if ($LASTEXITCODE -ne 0) { throw "Build/upload failed" }

Write-Host "==> Running quick regression..." -ForegroundColor Cyan
python test_harness/run_all.py --port COM7 --quick --save-log
if ($LASTEXITCODE -ne 0) { throw "Regression scenarios failed" }

Write-Host "==> All checks passed. Safe to push." -ForegroundColor Green

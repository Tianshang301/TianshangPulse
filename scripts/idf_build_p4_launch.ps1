# One-shot launcher: build TianshangPulse for esp32p4 (regression check).
# Generated for an unattended build session; safe to delete afterwards.
$ErrorActionPreference = 'Continue'
. C:\Espressif\esp-idf\export.ps1
Set-Location F:\Projects\Project13\TianshangPulse\firmware
idf.py set-target esp32p4
idf.py build
"BUILD_EXIT_CODE=$LASTEXITCODE"
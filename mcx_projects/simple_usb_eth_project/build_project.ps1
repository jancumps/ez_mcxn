param(
    [switch]$Clean,
    [switch]$Flash
)

if ($Flash) {
    $BinaryFile = Join-Path $PSScriptRoot "build-standalone\simple_usb_project.bin"
    $UsbId = "0x1FC9,0x0158"

    if (-not (Test-Path $BinaryFile)) {
        throw "Firmware binary not found: $BinaryFile. Build first."
    }

    if (-not (Get-Command blhost -ErrorAction SilentlyContinue)) {
        throw "blhost not found in PATH."
    }

    Write-Host "Flashing $BinaryFile"
    Write-Host "WARNING: This will erase all device flash."

    & blhost -u $UsbId -- flash-erase-all
    if ($LASTEXITCODE -ne 0) {
        throw "Flash erase failed."
    }

    & blhost -u $UsbId -- write-memory 0x00000000 $BinaryFile
    if ($LASTEXITCODE -ne 0) {
        throw "Flash programming failed."
    }

    & blhost -u $UsbId -- reset
    if ($LASTEXITCODE -ne 0) {
        throw "Reset failed."
    }

    Write-Host "Flash completed successfully."
    return
}

$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot 'build-standalone'
if ($Clean -and (Test-Path $dir)) { Remove-Item -Recurse -Force $dir }
cmake -S $PSScriptRoot -B $dir -G Ninja -DCMAKE_TOOLCHAIN_FILE="$PSScriptRoot/arm-gcc-toolchain.cmake"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $dir
exit $LASTEXITCODE

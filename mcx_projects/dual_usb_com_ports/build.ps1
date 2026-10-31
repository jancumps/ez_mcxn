param (
    [ValidateSet("build", "debug", "release", "clean", "menuconfig", "list", "flash")]
    [string]$Action = "debug",

    [string]$Board = "ez_mcxn",

    [ValidateSet("armgcc", "iar", "mdk")]
    [string]$Toolchain = "armgcc",

    [string]$SdkRoot = $env:MCUXPRESSO_SDK_ROOT,

    [string]$UsbId = "0x1FC9,0x0158"
)

$ErrorActionPreference = "Stop"

# Select build configuration from the action.
$Config = if ($Action -eq "release") { "release" } else { "debug" }

# ---------------------------------------------------------------------------
# Project paths
# ---------------------------------------------------------------------------

# Directory containing this build.ps1 file.
$ProjectDir = $PSScriptRoot

if (-not $ProjectDir) {
    $ProjectDir = (Get-Location).Path
}

# ---------------------------------------------------------------------------
# Discover board from example.yml if not supplied explicitly
# ---------------------------------------------------------------------------

if ([string]::IsNullOrWhiteSpace($Board)) {

    $ExampleFile = Join-Path $ProjectDir "example.yml"

    if (-not (Test-Path $ExampleFile)) {
        throw "No board specified and example.yml was not found."
    }

    $Lines = Get-Content $ExampleFile

    $BoardsLine = $Lines |
        Select-String '^\s+boards:\s*$' |
        Select-Object -First 1

    if (-not $BoardsLine) {
        throw "Could not find 'boards:' in example.yml."
    }

    for ($i = $BoardsLine.LineNumber; $i -lt $Lines.Count; $i++) {

        if ($Lines[$i] -match '^\s{4}([A-Za-z0-9_-]+):') {
            $Board = $Matches[1]
            break
        }
    }

    if ([string]::IsNullOrWhiteSpace($Board)) {
        throw "Could not determine board from example.yml."
    }
}

# ---------------------------------------------------------------------------

$BuildDir = Join-Path $ProjectDir "output"
$CustomBoardRoot = "C:/DEV/projects/mcx_projects/_boards"

Write-Host ""
Write-Host "MCUXpresso project build"
Write-Host "------------------------"
Write-Host "Project   : $ProjectDir"
Write-Host "Build     : $BuildDir"
Write-Host "SDK       : $SdkRoot"
Write-Host "Board     : $Board"
Write-Host "Toolchain : $Toolchain"
Write-Host "Config    : $Config"
Write-Host "Action    : $Action"
Write-Host ""

# ---------------------------------------------------------------------------
# Flash the existing binary via NXP blhost (no build performed).
# Flash erase-all is destructive; only run when explicitly requested.
# ---------------------------------------------------------------------------

if ($Action -eq "flash") {
    $ProjectName = Split-Path -Leaf $ProjectDir
    $BinaryFile = Join-Path $BuildDir "$ProjectName.bin"

    if (-not (Test-Path -PathType Leaf $BinaryFile)) {
        throw "Firmware binary not found: $BinaryFile. Build the project first."
    }
    if (-not (Get-Command blhost -ErrorAction SilentlyContinue)) {
        throw "'blhost' was not found in PATH. Activate the NXP programming tools environment."
    }

    Write-Host "Programming: $BinaryFile"
    Write-Host "USB device : $UsbId"
    Write-Host "WARNING: flash-erase-all erases the device's flash memory."

    & blhost -u $UsbId -- flash-erase-all
    if ($LASTEXITCODE -ne 0) {
        throw "Flash erase failed (blhost exit code $LASTEXITCODE). Not programming or resetting."
    }

    & blhost -u $UsbId -- write-memory 0x00000000 $BinaryFile
    if ($LASTEXITCODE -ne 0) {
        throw "Flash programming failed (blhost exit code $LASTEXITCODE). Not resetting."
    }

    & blhost -u $UsbId -- reset
    if ($LASTEXITCODE -ne 0) {
        throw "Board reset failed (blhost exit code $LASTEXITCODE)."
    }

    Write-Host "Flash programming and reset completed."
    return
}

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------

if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    throw "MCUXPRESSO_SDK_ROOT environment variable is not set."
}

if (-not (Test-Path $SdkRoot)) {
    throw "MCUXpresso SDK directory not found: $SdkRoot"
}

if (-not (Test-Path (Join-Path $ProjectDir "CMakeLists.txt"))) {
    throw "CMakeLists.txt not found in project directory: $ProjectDir"
}

if (-not (Get-Command west -ErrorAction SilentlyContinue)) {
    throw "'west' was not found in PATH."
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "'ninja' was not found in PATH."
}

# ---------------------------------------------------------------------------
# Run west from inside the MCUXpresso workspace.
#
# This is important because the NXP west extension commands (build,
# list_project, export_app, etc.) are discovered through the SDK's
# .west workspace.
# ---------------------------------------------------------------------------

Push-Location $SdkRoot

try {

    switch ($Action) {

        { $_ -in "build", "debug", "release" } {
            $NinjaFile = Join-Path $BuildDir "build.ninja"
            $BuildInfoFile = Join-Path $BuildDir ".build-ps1-config.json"

            if (Test-Path $NinjaFile) {
                # Never silently reuse a build configured for another target.
                if (-not (Test-Path $BuildInfoFile)) {
                    throw "Existing output has no build.ps1 configuration record. Run '.\build.ps1 clean' once, then build again."
                }

                try {
                    $Previous = Get-Content -Raw $BuildInfoFile | ConvertFrom-Json
                }
                catch {
                    throw "Cannot read $BuildInfoFile. Run '.\build.ps1 clean' and rebuild."
                }

                if ($Previous.Config -ne $Config -or
                    $Previous.Board -ne $Board -or
                    $Previous.Toolchain -ne $Toolchain -or
                    $Previous.SdkRoot -ne $SdkRoot -or
                    $Previous.CustomBoardRoot -ne $CustomBoardRoot) {
                    throw "Output was configured for Config=$($Previous.Config), Board=$($Previous.Board), Toolchain=$($Previous.Toolchain), SDK=$($Previous.SdkRoot). Run '.\build.ps1 clean' before changing build settings."
                }

                Write-Host "Existing configuration matches; running Ninja incrementally."
                & ninja -C $BuildDir
                if ($LASTEXITCODE -ne 0) {
                    throw "Ninja build failed with exit code $LASTEXITCODE"
                }
            }
            else {
                if (Test-Path $BuildDir) {
                    throw "Output directory exists but build.ninja is missing. Run '.\build.ps1 clean' before rebuilding."
                }

                Write-Host "No build directory found; configuring and building with West."
                west build `
                    -b $Board `
                    $ProjectDir `
                    -d $BuildDir `
                    --toolchain $Toolchain `
                    --config $Config `
                    -- `
                    "-DCUSTOM_BOARD_ROOT=$CustomBoardRoot" `
                    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"

                if ($LASTEXITCODE -ne 0) {
                    throw "Build failed with exit code $LASTEXITCODE"
                }

                # Record the settings only after a successful first build.
                @{
                    Config = $Config
                    Board = $Board
                    Toolchain = $Toolchain
                    SdkRoot = $SdkRoot
                    CustomBoardRoot = $CustomBoardRoot
                } | ConvertTo-Json | Set-Content -Encoding UTF8 $BuildInfoFile
            }
        }

        "clean" {
            if (Test-Path $BuildDir) {
                Write-Host "Removing build directory:"
                Write-Host "  $BuildDir"
                Remove-Item -Recurse -Force $BuildDir
            }
            else {
                Write-Host "Build directory does not exist."
            }
        }

        "menuconfig" {
            if (-not (Test-Path $BuildDir)) {
                throw "No build directory exists. Run '.\build.ps1' first."
            }

            west build `
                -d $BuildDir `
                -t menuconfig

            if ($LASTEXITCODE -ne 0) {
                throw "Menuconfig failed with exit code $LASTEXITCODE"
            }
        }

        "list" {
            west list_project `
                -p $ProjectDir `
                -b $Board `
                -t $Toolchain

            if ($LASTEXITCODE -ne 0) {
                throw "List failed with exit code $LASTEXITCODE"
            }
        }
    }

}
finally {
    Pop-Location
}

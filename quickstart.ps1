$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

function Find-CMake {
    $fromPath = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $candidates = @()
    if ($env:ProgramFiles) {
        $candidates += Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"
        $candidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }
    if (${env:ProgramFiles(x86)}) {
        $candidates += Join-Path ${env:ProgramFiles(x86)} "CMake\bin\cmake.exe"
        $candidates += Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }
    if ($env:LOCALAPPDATA) {
        $candidates += Join-Path $env:LOCALAPPDATA "Programs\CMake\bin\cmake.exe"
    }
    if ($env:USERPROFILE) {
        $candidates += Join-Path $env:USERPROFILE "scoop\apps\cmake\current\bin\cmake.exe"
    }
    $candidates += "C:\ProgramData\chocolatey\bin\cmake.exe"

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsInstall = & $vswhere -latest -products "*" -property installationPath
        if ($vsInstall) {
            $candidates += Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        }
    }

    foreach ($candidate in $candidates) {
        $matches = Get-Item -Path $candidate -ErrorAction SilentlyContinue
        foreach ($match in $matches) {
            if (-not $match.PSIsContainer -and $match.Name -ieq "cmake.exe") {
                return $match.FullName
            }
        }
    }
    return $null
}

$cmake = Find-CMake
if (-not $cmake) {
    Write-Error "CMake was not found on PATH or in common CMake/Visual Studio install locations. Install CMake from https://cmake.org/download/ or add its bin folder to PATH, then retry."
    exit 1
}

$env:Path = "$(Split-Path -Parent $cmake);$env:Path"
Write-Host "Using CMake: $cmake"
Push-Location $projectRoot
try {
    & $cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $app = @(
        (Join-Path $projectRoot "build\Release\random_art_gui.exe"),
        (Join-Path $projectRoot "build\random_art_gui.exe")
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1

    if (-not $app) {
        Write-Error "Build completed but the random_art_gui executable was not found."
        exit 1
    }

    Start-Process -FilePath $app -WorkingDirectory $projectRoot
    Write-Host "Random Art Generator is starting."
}
finally {
    Pop-Location
}

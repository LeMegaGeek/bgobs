# SPDX-FileCopyrightText: 2026 Kaito Udagawa <umireon@kaito.tokyo>
#
# SPDX-License-Identifier: Apache-2.0

param (
    [string]$Target = "x64",
    [string]$Configuration = "RelWithDebInfo",
    [string]$OutputDir = "release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 1. Locate and read release metadata
$ScriptDir = $PSScriptRoot
$RootDir = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $RootDir "build_$Target"
$OutputDir = if ([System.IO.Path]::IsPathRooted($OutputDir)) { $OutputDir } else { Join-Path $RootDir $OutputDir }
$BuildSpecFile = Join-Path $RootDir "buildspec.json"
$VersionFile = Join-Path $RootDir "VERSION"
$ManifestFile = Join-Path $RootDir "data\manifest.json"

if (-not (Test-Path $BuildSpecFile)) {
    Write-Error "buildspec.json not found at $BuildSpecFile"
    exit 1
}

try {
    Write-Host "Reading buildspec.json..."
    $Spec = Get-Content $BuildSpecFile -Raw | ConvertFrom-Json
    $PluginName = $Spec.name
    $PluginVersion = $Spec.version
    $Version = (Get-Content $VersionFile -Raw).Trim()
    $Manifest = Get-Content $ManifestFile -Raw | ConvertFrom-Json
}
catch {
    Write-Error "Failed to parse buildspec.json: $_"
    exit 1
}

if (-not $PluginName -or -not $PluginVersion) {
    Write-Error "Could not find 'name' or 'version' in buildspec.json"
    exit 1
}
if ($PluginVersion -ne $Version -or $Manifest.version -ne $Version) {
    Write-Error "VERSION, buildspec.json, and data/manifest.json must match."
    exit 1
}

Write-Host "Packaging Plugin: $PluginName ($PluginVersion)"
Write-Host "Output Directory: $OutputDir"

# 2. Prepare output directory
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# Generate a unique temporary directory path
$TempBase = Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString())
$InstallDir = Join-Path $TempBase "package"
$SymbolsDir = Join-Path $TempBase "symbols"

Write-Host "Using temporary working directory: $TempBase"

try {
    # 3. Run installation
    Write-Host "Installing to temporary directory..."
    # Ensure the parent temp directory exists
    if (-not (Test-Path $TempBase)) { New-Item -ItemType Directory -Path $TempBase -Force | Out-Null }

    cmake --install $BuildDir --config $Configuration --prefix $InstallDir
    if ($LASTEXITCODE -ne 0) { throw "CMake install failed" }

    # 4. Separate symbol files (PDBs)
    Write-Host "Separating PDB files..."
    New-Item -ItemType Directory -Path $SymbolsDir -Force | Out-Null

    Get-ChildItem -Path $InstallDir -Filter "*.pdb" -Recurse | ForEach-Object {
        $RelativePath = $_.DirectoryName.Substring($InstallDir.Length).TrimStart('\', '/')
        $DestDir = Join-Path $SymbolsDir $RelativePath

        if (-not (Test-Path $DestDir)) {
            New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
        }

        Move-Item -Path $_.FullName -Destination $DestDir
    }

    # 5. Create Zip archives (from temp dir to OutputDir)
    Write-Host "Creating Archives..."

    # Copy scripts/windows files to a temp root for zipping
    $TempRoot = Join-Path $TempBase "ziproot"
    New-Item -ItemType Directory -Path $TempRoot -Force | Out-Null

    # Copy plugin install files (preserve structure)
    Copy-Item -Path "$InstallDir\*" -Destination $TempRoot -Recurse -Force

    $PluginBinDir = Join-Path $TempRoot "$PluginName\bin\64bit"
    if (-not (Test-Path $PluginBinDir)) {
        New-Item -ItemType Directory -Path $PluginBinDir -Force | Out-Null
    }

    $OrtPrefix = Join-Path $RootDir ".deps_vendor\ort_x64-prefix"
    $OrtDllSearchDirs = @(
        (Join-Path $OrtPrefix "bin"),
        (Join-Path $OrtPrefix "lib")
    )

    foreach ($Dir in $OrtDllSearchDirs) {
        if (Test-Path $Dir) {
            Get-ChildItem -Path $Dir -Filter "*.dll" -File | ForEach-Object {
                Copy-Item $_.FullName -Destination $PluginBinDir -Force
            }
        }
    }
    if (-not (Test-Path (Join-Path $PluginBinDir "onnxruntime.dll") -PathType Leaf)) {
        throw "onnxruntime.dll was not copied into the package."
    }

    $LibUsbDllSearchDirs = @(
        (Join-Path $RootDir ".deps_vendor\libusb_installed\x64-windows\bin"),
        (Join-Path $RootDir ".deps_vendor\libusb_installed\x64-windows\debug\bin"),
        (Join-Path $RootDir ".deps_vendor\vcpkg_installed\x64-windows\bin"),
        (Join-Path $RootDir ".deps_vendor\vcpkg_installed\x64-windows\debug\bin")
    )

    $LibUsbCopied = $false
    foreach ($Dir in $LibUsbDllSearchDirs) {
        $LibUsbDll = Join-Path $Dir "libusb-1.0.dll"
        if (Test-Path $LibUsbDll) {
            Copy-Item $LibUsbDll -Destination $PluginBinDir -Force
            $LibUsbCopied = $true
            break
        }
    }
    if (-not $LibUsbCopied) {
        throw "libusb-1.0.dll was not found; the Windows package would not support CaCam USB."
    }

    $LibUsbCopyright = Join-Path $RootDir ".deps_vendor\libusb_installed\x64-windows\share\libusb\copyright"
    if (-not (Test-Path $LibUsbCopyright -PathType Leaf)) {
        throw "The libusb copyright file was not found."
    }

    $AdbSearchDirs = @(
        (Join-Path $RootDir ".deps_vendor\platform-tools"),
        (Join-Path $RootDir ".deps_vendor\platform-tools-windows\platform-tools"),
        $env:ANDROID_PLATFORM_TOOLS
    ) | Where-Object { $_ }

    $AdbCopied = $false
    foreach ($Dir in $AdbSearchDirs) {
        $AdbExe = Join-Path $Dir "adb.exe"
        if (-not (Test-Path $AdbExe -PathType Leaf)) {
            continue
        }

        $AdbDir = Join-Path $PluginBinDir "adb"
        New-Item -ItemType Directory -Path $AdbDir -Force | Out-Null
        foreach ($Name in @("adb.exe", "AdbWinApi.dll", "AdbWinUsbApi.dll", "NOTICE.txt", "source.properties")) {
            $Candidate = Join-Path $Dir $Name
            if (Test-Path $Candidate -PathType Leaf) {
                Copy-Item $Candidate -Destination $AdbDir -Force
            }
        }
        $AdbCopied = $true
        break
    }
    if (-not $AdbCopied) {
        throw "Android platform-tools were not found; the Windows package would not support the bundled ADB mode."
    }

    # Copy scripts/windows files to root of zip
    $ScriptsWin = Join-Path $ScriptDir "windows"
    if (Test-Path $ScriptsWin) {
        Get-ChildItem -Path $ScriptsWin -File | ForEach-Object {
            Copy-Item $_.FullName -Destination $TempRoot -Force
        }
    }

    Copy-Item (Join-Path $RootDir "LICENSE") (Join-Path $TempRoot "LICENSE.txt") -Force
    $ThirdPartyDir = Join-Path $TempRoot "THIRD-PARTY-NOTICES"
    New-Item -ItemType Directory -Path $ThirdPartyDir -Force | Out-Null
    Copy-Item $LibUsbCopyright (Join-Path $ThirdPartyDir "libusb.txt") -Force
    Copy-Item (Join-Path $TempRoot "$PluginName\data\BGOBS-SIGNING-STATUS.txt") `
        (Join-Path $TempRoot "BGOBS-SIGNING-STATUS.txt") -Force

    & python (Join-Path $ScriptDir "validate_package.py") `
        --root $TempRoot `
        --platform windows `
        --version $PluginVersion `
        --signing-status unsigned
    if ($LASTEXITCODE -ne 0) { throw "Package content validation failed" }

    $MainZip = Join-Path $OutputDir "$PluginName-$PluginVersion-windows-x64-unsigned.zip"
    if (Test-Path $MainZip) { Remove-Item $MainZip -Force }
    Compress-Archive -Path "$TempRoot\*" -DestinationPath $MainZip -Force

    $PdbFiles = @(Get-ChildItem -Path $SymbolsDir -Filter "*.pdb" -Recurse -File)
    if ($PdbFiles.Count -eq 0) { throw "No PDB debug symbols were produced" }

    $PdbZip = Join-Path $OutputDir "$PluginName-$PluginVersion-windows-x64-debug-symbols.zip"
    if (Test-Path $PdbZip) { Remove-Item $PdbZip -Force }
    Compress-Archive -Path "$SymbolsDir/*" -DestinationPath $PdbZip -Force

    Write-Host "Packaging complete."
    Write-Host "  Binary: $MainZip"
    Write-Host "  Symbols: $PdbZip"

}
catch {
    Write-Error "Packaging failed: $_"
    exit 1
}
finally {
    # 6. Cleanup
    # Remove the temporary directory regardless of success or failure
    if (Test-Path $TempBase) {
        Write-Host "Cleaning up temporary directory..."
        Remove-Item -Path $TempBase -Recurse -Force
    }
}

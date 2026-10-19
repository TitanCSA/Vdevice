param([string]$DriverDirectory,[string]$NativeHelper,[string]$OutputDirectory,[string]$BuildDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$DriverDirectory){$DriverDirectory=Join-Path $root 'InstallerUI\Package'}
if(!$NativeHelper){$NativeHelper=Join-Path $root 'bin\Native\Release\VdeviceSetupNative.exe'}
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'bin\InstallerConsole'}
if(!$BuildDirectory){$BuildDirectory=Join-Path $root 'obj\InstallerConsole'}
. (Join-Path $root 'InstallerUI\Build.Common.ps1')
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$payload=New-VdevicePayload $BuildDirectory $DriverDirectory $NativeHelper
Build-VdeviceManaged -Output (Join-Path $OutputDirectory 'VdeviceSetupConsole.exe') -Sources @((Join-Path $PSScriptRoot 'Program.cs'),(Join-Path $root 'InstallerUI\Package.cs'),(Join-Path $root 'InstallerUI\AssemblyInfo.cs')) -Resources @(($payload.Zip+',Payload.zip'),($payload.Hashes+',PayloadHashes.json')) -Console
$payload.Files|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'payload-manifest.json') -Encoding UTF8
Write-Host 'Built the single-file console installer. No driver was installed.'

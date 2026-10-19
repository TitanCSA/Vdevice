param([string]$DriverDirectory,[string]$NativeHelper,[string]$OutputDirectory,[string]$BuildDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$DriverDirectory){$DriverDirectory=Join-Path $root 'InstallerUI\Package'}
if(!$NativeHelper){$NativeHelper=Join-Path $root 'bin\Native\Release\VdeviceSetupNative.exe'}
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'bin\InstallerUI'}
if(!$BuildDirectory){$BuildDirectory=Join-Path $root 'obj\InstallerUI'}
. (Join-Path $PSScriptRoot 'Build.Common.ps1')
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$payload=New-VdevicePayload $BuildDirectory $DriverDirectory $NativeHelper
$resources=@(($payload.Zip+',Payload.zip'),($payload.Hashes+',PayloadHashes.json'),((Join-Path $PSScriptRoot 'MainWindow.xaml')+',MainWindow.xaml'))
Build-VdeviceManaged -Output (Join-Path $OutputDirectory 'VdeviceSetup.exe') -Sources @((Join-Path $PSScriptRoot 'App.cs'),(Join-Path $PSScriptRoot 'Package.cs'),(Join-Path $PSScriptRoot 'AssemblyInfo.cs')) -Resources $resources -Wpf
$payload.Files|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'payload-manifest.json') -Encoding UTF8
Write-Host 'Built the single-file UI installer. No driver was installed.'

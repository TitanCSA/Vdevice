param([string]$OutputDirectory,[string]$NativeBridge)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'bin\ExamplesUI'}
if(!$NativeBridge){
    foreach($candidate in @((Join-Path $OutputDirectory 'Release\VdeviceBridge.dll'),(Join-Path $OutputDirectory 'VdeviceBridge.dll'),(Join-Path $root 'bin\Native\Release\VdeviceBridge.dll'))){if(Test-Path -LiteralPath $candidate -PathType Leaf){$NativeBridge=$candidate;break}}
}
if(!$NativeBridge -or !(Test-Path -LiteralPath $NativeBridge -PathType Leaf)){throw 'Build the VdeviceBridge native target first.'}
. (Join-Path $root 'InstallerUI\Build.Common.ps1')
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$destination=Join-Path $OutputDirectory 'VdeviceBridge.dll'
if([IO.Path]::GetFullPath($NativeBridge) -ne [IO.Path]::GetFullPath($destination)){Copy-Item -LiteralPath $NativeBridge -Destination $destination -Force}
Build-VdeviceManaged -Output (Join-Path $OutputDirectory 'VdeviceExampleUI.exe') -Sources @((Join-Path $PSScriptRoot 'App.cs'),(Join-Path $PSScriptRoot 'AssemblyInfo.cs')) -Resources @(((Join-Path $PSScriptRoot 'MainWindow.xaml')+',MainWindow.xaml')) -Wpf
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CustomDevice.json') -Destination (Join-Path $OutputDirectory 'CustomDevice.json') -Force
Write-Host 'Built Descriptor Workshop UI and its raw SDK bridge. No device was created.'

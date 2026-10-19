param([string]$OutputDirectory,[string]$NativeBridge)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'obj\WorkshopTests'}
if(!$NativeBridge){$NativeBridge=Join-Path $root 'bin\ExamplesUI\Release\VdeviceBridge.dll'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(!(Test-Path -LiteralPath $NativeBridge -PathType Leaf)){throw 'Build the current raw VdeviceBridge target before running workshop tests.'}
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
Copy-Item -LiteralPath $NativeBridge -Destination (Join-Path $OutputDirectory 'VdeviceBridge.dll') -Force
. (Join-Path $root 'InstallerUI\Build.Common.ps1')
$references=Get-VdeviceReferences
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$exe=Join-Path $OutputDirectory 'WorkshopTests.exe'
# This separate test entry point intentionally has no elevation manifest and
# never constructs MainWindow or Application. VdUiValidate is purely offline.
$arguments=@('/nologo','/noconfig','/nostdlib+','/target:exe','/main:Vdevice.Example.WorkshopTests','/platform:x64','/optimize+','/warn:4','/warnaserror+','/codepage:65001','/utf8output',"/out:$exe")
foreach($name in @('mscorlib.dll','System.dll','System.Core.dll','System.Web.Extensions.dll','System.IO.Compression.dll','WindowsBase.dll','PresentationCore.dll','PresentationFramework.dll','System.Xaml.dll','System.Xml.dll')){
    $path=Join-Path $references $name;if(!(Test-Path -LiteralPath $path)){$path=Join-Path (Join-Path $references 'WPF') $name};$arguments+="/reference:$path"
}
$arguments+=@((Join-Path $root 'ExamplesUI\App.cs'),(Join-Path $PSScriptRoot 'WorkshopTests.cs'))
& $compiler @arguments
if($LASTEXITCODE){throw 'Workshop test harness compilation failed.'}
& $exe $OutputDirectory (Join-Path $root 'ExamplesUI\CustomDevice.json')
if($LASTEXITCODE){throw 'Offline workshop regression failed.'}
$result=Get-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Raw -Encoding UTF8|ConvertFrom-Json
if(!$result.passed -or $result.openedDriver -ne $false -or $result.createdDevices -ne $false){throw 'Invalid offline workshop test result.'}
Write-Host "$($result.assertions) offline workshop checks passed; no UI startup, driver access, or virtual-device creation."

param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'obj\InstallerTests'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $root 'InstallerUI\Build.Common.ps1')
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$refs=Get-VdeviceReferences
$harness=Join-Path $OutputDirectory 'Harness.cs'
@'
using System;
using Vdevice.Setup;
internal static class Harness {
    private static int Main(string[] args) {
        bool accepted=false;
        try { string result=Package.SelfTest(); accepted=result.Contains("\"passed\":true")&&result.Contains("\"installedAnything\":false"); }
        catch (System.IO.InvalidDataException) { }
        if (accepted!=(args.Length==1&&args[0]=="accept")) { Console.Error.WriteLine("Unexpected payload validation result.");return 1; }
        Console.WriteLine(accepted?"Valid payload accepted without installation.":"Invalid payload rejected without installation.");return 0;
    }
}
'@ | Set-Content -LiteralPath $harness -Encoding UTF8
function New-Fixture([string]$Name,[object[]]$Entries,[hashtable]$Manifest,[bool]$Accept) {
    $stage=Join-Path $OutputDirectory $Name
    New-Item -ItemType Directory -Path $stage -Force|Out-Null
    $zip=Join-Path $stage 'Payload.zip';$json=Join-Path $stage 'Hashes.json'
    $stream=[IO.File]::Open($zip,[IO.FileMode]::Create)
    try {
        $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
        try {
            foreach($entry in $Entries) {
                $item=$archive.CreateEntry([string]$entry.Name)
                $output=$item.Open()
                try{[byte[]]$bytes=[Text.Encoding]::ASCII.GetBytes([string]$entry.Data);$output.Write($bytes,0,$bytes.Length)}finally{$output.Dispose()}
            }
        } finally {$archive.Dispose()}
    } finally {$stream.Dispose()}
    [IO.File]::WriteAllText($json,($Manifest|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    $exe=Join-Path $stage 'OfflinePackageTest.exe'
    $arguments=@('/nologo','/noconfig','/nostdlib+','/target:exe','/platform:x64','/optimize+','/warn:4','/warnaserror+',"/out:$exe","/resource:$zip,Payload.zip","/resource:$json,PayloadHashes.json")
    foreach($referenceName in @('mscorlib.dll','System.dll','System.Core.dll','System.Web.Extensions.dll','System.IO.Compression.dll')){$arguments+='/reference:'+(Join-Path $refs $referenceName)}
    $arguments+=@($harness,(Join-Path $root 'InstallerUI\Package.cs'))
    & $compiler @arguments
    if($LASTEXITCODE){throw "Fixture compilation failed: $Name"}
    $expect=if($Accept){'accept'}else{'reject'}
    & $exe $expect | Write-Host
    if($LASTEXITCODE){throw "Payload regression failed: $Name"}
    return [pscustomobject]@{name=$Name;expected=$expect;passed=$true}
}
function Hash-Text([string]$Value){$hash=[Security.Cryptography.SHA256]::Create();try{return [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::ASCII.GetBytes($Value))).Replace('-','')}finally{$hash.Dispose()}}
$valid=@(@{Name='Vdevice.inf';Data='inf'},@{Name='Vdevice.sys';Data='sys'},@{Name='VdeviceSetupNative.exe';Data='native'})
$manifest=@{};foreach($entry in $valid){$manifest[$entry.Name]=Hash-Text $entry.Data}
$results=@()
$results+=New-Fixture 'Valid' $valid $manifest $true
$tamper=@{}+$manifest;$tamper['Vdevice.sys']='0'*64
$results+=New-Fixture 'TamperedHash' $valid $tamper $false
$results+=New-Fixture 'DuplicateEntry' ($valid+@(@{Name='Vdevice.sys';Data='sys'})) $manifest $false
$traversal=@{}+$manifest;$traversal['..\outside.exe']=Hash-Text 'bad'
$results+=New-Fixture 'Traversal' ($valid+@(@{Name='..\outside.exe';Data='bad'})) $traversal $false
$results+=New-Fixture 'UnknownFile' ($valid+@(@{Name='unexpected.dll';Data='bad'})) $manifest $false
$results+=New-Fixture 'MissingFile' @($valid[0],$valid[1]) $manifest $false
$empty=@($valid[0],@{Name='Vdevice.sys';Data=''},$valid[2]);$emptyHashes=@{}+$manifest;$emptyHashes['Vdevice.sys']=Hash-Text ''
$results+=New-Fixture 'EmptyFile' $empty $emptyHashes $false
$invalidHash=@{}+$manifest;$invalidHash['Vdevice.sys']='Z'*64
$results+=New-Fixture 'InvalidHashEncoding' $valid $invalidHash $false

# Reflection deliberately bypasses the executable UAC manifest for entry points
# that only print help/validate resources. Never invoke install/uninstall here.
$console=Join-Path $root 'bin\InstallerConsole\VdeviceSetupConsole.exe'
if(Test-Path -LiteralPath $console){
    $assembly=[Reflection.Assembly]::LoadFile([IO.Path]::GetFullPath($console))
    foreach($case in @(@{Arguments=@('/?');Exit=0},@{Arguments=@('/unknown');Exit=87},@{Arguments=@('/install','extra');Exit=87},@{Arguments=@('--self-test');Exit=0})){
        [object[]]$invoke=@([string[]]$case.Arguments)
        $invoke=[object[]]::new(1);$invoke[0]=[string[]]$case.Arguments
        $actual=$assembly.EntryPoint.Invoke($null,$invoke)
        if([int]$actual -ne $case.Exit){throw "Console parser/self-test returned $actual; expected $($case.Exit)"}
        $results+=[pscustomobject]@{name=('Console '+($case.Arguments -join ' '));passed=$true}
    }
}

# Parse the real views without starting their applications, connecting to a
# driver, creating devices, sending reports, or presenting an elevation prompt.
Add-Type -AssemblyName PresentationFramework
foreach($relative in @('InstallerUI\MainWindow.xaml','ExamplesUI\MainWindow.xaml')){
    $stream=[IO.File]::OpenRead((Join-Path $root $relative))
    try{$view=[Windows.Markup.XamlReader]::Load($stream);if(!$view){throw "Empty view: $relative"}}finally{$stream.Dispose()}
    $results+=[pscustomobject]@{name=('XAML '+$relative);passed=$true}
}
$stateTest=Join-Path $OutputDirectory 'InstallerStateTests.exe'
Build-VdeviceManaged -Output $stateTest -Sources @((Join-Path $PSScriptRoot 'InstallerStateTests.cs')) -Resources @() -Wpf -Console
$stateAssembly=[Reflection.Assembly]::LoadFile($stateTest)
$arguments=[object[]]::new(1);$arguments[0]=[string[]]@((Join-Path $root 'bin\InstallerUI\VdeviceSetup.exe'))
if([int]$stateAssembly.EntryPoint.Invoke($null,$arguments) -ne 0){throw 'Installer state/process/UI tests failed.'}
$results+=[pscustomobject]@{name='Installation states, real UI, restart persistence and process output';passed=$true}
$report=[ordered]@{passed=$true;installedAnything=$false;createdVirtualDevices=$false;testCount=$results.Count;tests=$results}
$report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
Write-Host "$($results.Count) offline installer/UI checks passed. No driver installation or device creation."

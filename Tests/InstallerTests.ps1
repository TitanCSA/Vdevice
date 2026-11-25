param([string]$OutputDirectory,[string]$InstallerRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$InstallerRoot){$InstallerRoot=Join-Path $root 'bin'}
if(!$OutputDirectory){$OutputDirectory=Join-Path $root 'obj\InstallerTests'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $root 'Installer\UI\Build.Common.ps1')
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$refs=Get-VdeviceReferences
$harness=Join-Path $OutputDirectory 'Harness.cs'
@'
using System;
using Vdevice.Setup;
internal static class Harness {
    private static int Main(string[] args) {
        byte[] bytes = new byte[] {1,2,3};
        foreach (int declared in new[] {2, 4})
        {
            try
            {
                Package.DigestEntry(new System.IO.MemoryStream(bytes), declared);
                return 2;
            }
            catch (System.IO.InvalidDataException) { }
        }
        var restart = new SetupSnapshot { LastOperation = "Uninstall", ControllerQuiesced = true,
            RestartRequired = true, ApiAvailable = true, PayloadMatches = true, QueryError = 21 };
        if (!Package.ExpectedRemovalRestart(restart, 3010, 21)
                || Package.ExpectedRemovalRestart(restart, 0, 21)
                || Package.ExpectedRemovalRestart(restart, 3010, 5)) return 3;
        string mutexName = @"Local\VdeviceTest-" + Guid.NewGuid().ToString("N");
        using (var owner = new System.Threading.Mutex(false, mutexName))
        {
            owner.WaitOne();
            int contender = 0;
            var thread = new System.Threading.Thread(delegate()
            {
                contender = Package.Exclusive(
                    delegate { return new SetupResult { ExitCode = 99 }; },
                    delegate { return new System.Threading.Mutex(false, mutexName); }).ExitCode;
            });
            thread.Start();
            thread.Join();
            owner.ReleaseMutex();
            if (contender != 1618 || Package.Exclusive(
                    delegate { return new SetupResult { ExitCode = 0 }; },
                    delegate { return new System.Threading.Mutex(false, mutexName); }).ExitCode != 0)
                return 4;
        }
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
    $arguments+=@($harness,(Join-Path $root 'Installer\UI\Package.cs'))
    & $compiler @arguments
    if($LASTEXITCODE){throw "Fixture compilation failed: $Name"}
    $expect=if($Accept){'accept'}else{'reject'}
    & $exe $expect | Write-Host
    if($LASTEXITCODE){throw "Payload regression failed: $Name"}
    return [pscustomobject]@{name=$Name;expected=$expect;passed=$true}
}
function Hash-Text([string]$Value){$hash=[Security.Cryptography.SHA256]::Create();try{return [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::ASCII.GetBytes($Value))).Replace('-','')}finally{$hash.Dispose()}}
$mode='{"Product":"Vdevice","DriverVersion":"1.0.6.1","Mode":"Development","TrialSeconds":3600,"ClockRequired":true,"InstallationBlocked":true}'
$valid=@(@{Name='Vdevice.inf';Data='inf'},@{Name='Vdevice.sys';Data='sys'},@{Name='VdeviceSetupNative.exe';Data='native'},
    @{Name='TitanDemoClock.sys';Data='clock'},@{Name='Vdevice.cat';Data='catalog'},@{Name='Signing.json';Data=$mode})
$manifest=@{};foreach($entry in $valid){$manifest[$entry.Name]=Hash-Text $entry.Data}
$results=@()
$results+=New-Fixture 'ValidDemoPreview' $valid $manifest $true
$badClock=@{}+$manifest;$badClock['TitanDemoClock.sys']='0'*64
$results+=New-Fixture 'TamperedResidentClock' $valid $badClock $false
$results+=New-Fixture 'MissingResidentClock' @($valid|Where-Object Name -ne 'TitanDemoClock.sys') $manifest $false
$wrongMode=$mode.Replace('"InstallationBlocked":true','"InstallationBlocked":false')
$wrongManifest=@{}+$manifest;$wrongManifest['Signing.json']=Hash-Text $wrongMode
$wrongEntries=@($valid|Where-Object Name -ne 'Signing.json')+@(@{Name='Signing.json';Data=$wrongMode})
$results+=New-Fixture 'UnsignedPackageCannotClaimInstallable' $wrongEntries $wrongManifest $false
$tamper=@{}+$manifest;$tamper['Vdevice.sys']='0'*64
$results+=New-Fixture 'TamperedHash' $valid $tamper $false
$results+=New-Fixture 'DuplicateEntry' ($valid+@(@{Name='Vdevice.sys';Data='sys'})) $manifest $false
$traversal=@{}+$manifest;$traversal['..\outside.exe']=Hash-Text 'bad'
$results+=New-Fixture 'Traversal' ($valid+@(@{Name='..\outside.exe';Data='bad'})) $traversal $false
$results+=New-Fixture 'UnknownFile' ($valid+@(@{Name='unexpected.dll';Data='bad'})) $manifest $false
$results+=New-Fixture 'MissingFile' @($valid[0],$valid[1]) $manifest $false
$empty=@($valid|Where-Object Name -ne 'Vdevice.sys')+@(@{Name='Vdevice.sys';Data=''});$emptyHashes=@{}+$manifest;$emptyHashes['Vdevice.sys']=Hash-Text ''
$results+=New-Fixture 'EmptyFile' $empty $emptyHashes $false
$invalidHash=@{}+$manifest;$invalidHash['Vdevice.sys']='Z'*64
$results+=New-Fixture 'InvalidHashEncoding' $valid $invalidHash $false

# Reflection deliberately bypasses the executable UAC manifest for entry points
# that only print help/validate resources. Never invoke install/uninstall here.
$console=Join-Path $InstallerRoot 'InstallerConsole\VdeviceSetupConsole.exe'
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
foreach($relative in @('Installer\UI\MainWindow.xaml','Examples\UI\MainWindow.xaml')){
    $stream=[IO.File]::OpenRead((Join-Path $root $relative))
    try{$view=[Windows.Markup.XamlReader]::Load($stream);if(!$view){throw "Empty view: $relative"}}finally{$stream.Dispose()}
    $results+=[pscustomobject]@{name=('XAML '+$relative);passed=$true}
}
$stateTest=Join-Path $OutputDirectory 'InstallerStateTests.exe'
Build-VdeviceManaged -Output $stateTest -Sources @((Join-Path $PSScriptRoot 'InstallerStateTests.cs')) -Resources @() -Wpf -Console
$stateAssembly=[Reflection.Assembly]::LoadFile($stateTest)
$arguments=[object[]]::new(1);$arguments[0]=[string[]]@((Join-Path $InstallerRoot 'InstallerUI\VdeviceSetup.exe'))
if([int]$stateAssembly.EntryPoint.Invoke($null,$arguments) -ne 0){throw 'Installer state/process/UI tests failed.'}
$results+=[pscustomobject]@{name='Installation states, real UI, restart persistence and process output';passed=$true}
$report=[ordered]@{passed=$true;installedAnything=$false;createdVirtualDevices=$false;testCount=$results.Count;tests=$results}
$report|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json') -Encoding UTF8
Write-Host "$($results.Count) offline installer/UI checks passed. No driver installation or device creation."

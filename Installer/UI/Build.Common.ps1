$ErrorActionPreference='Stop'
function Get-VdeviceReferences {
    $candidates=@(
        (Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies\Microsoft\Framework\.NETFramework\v4.8'),
        (Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies\Microsoft\Framework\.NETFramework\v4.7.2'),
        (Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319')
    )
    foreach($candidate in $candidates){if(Test-Path -LiteralPath (Join-Path $candidate 'mscorlib.dll')){return $candidate}}
    throw '.NET Framework reference assemblies were not found.'
}
function New-VdevicePayload([string]$BuildDirectory,[string]$DriverDirectory,[string]$NativeHelper) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Path $BuildDirectory -Force|Out-Null
    $files=[ordered]@{}
    $required=@('Vdevice.inf','Vdevice.sys','Vdevice.cat','TitanDemoClock.sys')
    foreach($name in $required) {
        $file=Get-Item -LiteralPath (Join-Path $DriverDirectory $name)
        if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or !$file.Length){throw "Invalid demo payload file: $name"}
        $files[$name]=$file.FullName
    }
    $versionSource=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'Package.cs'))
    $version=[regex]::Match($versionSource,'internal static string Version[^\r\n]+"([0-9.]+)"').Groups[1].Value
    $inf=[IO.File]::ReadAllText($files['Vdevice.inf'])
    if(!$version -or $inf -notmatch ('(?m)^DriverVer=[^,\r\n]+,'+[regex]::Escape($version)+'\r?$') -or
       $inf -notmatch '(?m)^AddService=Vdevice,' -or $inf -notmatch '(?m)^AddService=VdeviceDemoClock,' -or
       $inf -notmatch '(?m)^ServiceBinary=%13%\\TitanDemoClock\.sys\r?$'){throw 'The payload INF does not match this demo version and resident clock.'}
    $microsoft=$true
    foreach($name in 'Vdevice.sys','Vdevice.cat','TitanDemoClock.sys'){
        $signature=Get-AuthenticodeSignature -LiteralPath $files[$name]
        if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notlike 'CN=Microsoft Windows Hardware Compatibility Publisher,*'){$microsoft=$false}
    }
    $policy=[ordered]@{Product='Vdevice';DriverVersion=$version;Mode=$(if($microsoft){'Microsoft'}else{'Development'});TrialSeconds=3600;ClockRequired=$true;InstallationBlocked=!$microsoft}
    $policyPath=Join-Path $BuildDirectory 'Signing.json'
    [IO.File]::WriteAllText($policyPath,($policy|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    $files['Signing.json']=[IO.Path]::GetFullPath($policyPath)
    $helper=Get-Item -LiteralPath $NativeHelper
    if($helper.PSIsContainer -or ($helper.Attributes -band [IO.FileAttributes]::ReparsePoint) -or !$helper.Length){throw 'Invalid native setup helper.'}
    $files['VdeviceSetupNative.exe']=$helper.FullName
    $hashes=[ordered]@{}
    $zipPath=Join-Path $BuildDirectory 'Payload.zip'
    $stream=[IO.File]::Open($zipPath,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
        $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
        try {
            foreach($entry in $files.GetEnumerator()) {
                $hashes[$entry.Key]=(Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash
                $item=$archive.CreateEntry($entry.Key,[IO.Compression.CompressionLevel]::Optimal)
                $item.LastWriteTime=[DateTimeOffset]::new(2000,1,1,0,0,0,[TimeSpan]::Zero)
                $input=[IO.File]::OpenRead($entry.Value);$output=$item.Open()
                try{$input.CopyTo($output)}finally{$output.Dispose();$input.Dispose()}
            }
        } finally {$archive.Dispose()}
    } finally {$stream.Dispose()}
    $hashPath=Join-Path $BuildDirectory 'PayloadHashes.json'
    [IO.File]::WriteAllText($hashPath,($hashes|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    return @{Zip=$zipPath;Hashes=$hashPath;Files=$hashes}
}
function Build-VdeviceManaged([string]$Output,[string[]]$Sources,[string[]]$Resources,[switch]$Wpf,[switch]$Console) {
    $compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    if(!(Test-Path -LiteralPath $compiler)){throw 'The inbox .NET Framework C# compiler was not found.'}
    $references=Get-VdeviceReferences
    $names=@('mscorlib.dll','System.dll','System.Core.dll','System.Web.Extensions.dll','System.IO.Compression.dll')
    if($Wpf){$names+=@('WindowsBase.dll','PresentationCore.dll','PresentationFramework.dll','System.Xaml.dll','System.Xml.dll')}
    $kind=if($Console){'exe'}else{'winexe'}
    $arguments=@('/nologo','/noconfig','/nostdlib+',"/target:$kind",'/platform:x64','/optimize+','/warn:4','/warnaserror+','/codepage:65001','/utf8output',"/out:$Output",('/win32manifest:'+(Join-Path $PSScriptRoot 'App.manifest')))
    foreach($name in $names){$path=Join-Path $references $name;if(!(Test-Path -LiteralPath $path)){$path=Join-Path (Join-Path $references 'WPF') $name};$arguments+="/reference:$path"}
    foreach($resource in $Resources){$arguments+="/resource:$resource"}
    $arguments+=$Sources
    & $compiler @arguments
    if($LASTEXITCODE){throw "C# compilation failed: $Output"}
}

# Vdevice PowerShell SDK

PowerShell7.4+ Windows x64. `Install-Module Vdevice -Scope CurrentUser`.

```powershell
$client=Connect-VdeviceSDK
try { Invoke-VdeviceSDK -Client $client -Operation status } finally { Disconnect-VdeviceSDK -Client $client }
```

Use -Arguments with a hashtable matching [schema](docs/API.md). Native errors retain their Windows code through the inner exception. Matching driver installed separately.

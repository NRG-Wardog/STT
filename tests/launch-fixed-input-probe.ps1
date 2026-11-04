param([Parameter(Mandatory=$true)][string]$Directory,[switch]$Disarmed,[switch]$AllowClipboard,[switch]$AllowTrustedTerminal,[switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$dir=(Resolve-Path -LiteralPath $Directory).Path
$exe=Join-Path $dir 'HebENFixedInputProbe.exe'
if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'Probe executable is missing'}
$metadataPath=Join-Path $dir 'metadata.json'
if(-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)){throw 'Probe metadata is missing'}
$metadata=Get-Content -LiteralPath $metadataPath -Raw -Encoding UTF8 | ConvertFrom-Json
if((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $metadata.sha256){throw 'Probe executable hash differs from staged metadata'}
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public static class ProbeIniRead {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileString(string section,string key,string fallback,StringBuilder value,uint size,string path);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern uint GetPrivateProfileInt(string section,string key,int fallback,string path);
}
'@
$ini=Join-Path $dir 'config.ini';$value=[Text.StringBuilder]::new(64)
[void][ProbeIniRead]::GetPrivateProfileString('General','input_method','unicode',$value,[uint32]$value.Capacity,$ini)
$method=$value.ToString().Trim().ToLowerInvariant()
$trusted=[ProbeIniRead]::GetPrivateProfileInt('General','trusted_terminal_input',0,$ini) -ne 0
if(-not $Disarmed -and $method -eq 'clipboard' -and -not $AllowClipboard){throw 'Clipboard probe requires explicit -AllowClipboard authorization'}
if(-not $Disarmed -and $trusted -and -not $AllowTrustedTerminal){throw 'Trusted terminal probe requires explicit -AllowTrustedTerminal authorization'}
if($ValidateOnly){return [pscustomobject]@{status='VALIDATED_WITHOUT_LAUNCH';input_method=$method;trusted_terminal=$trusted;disarmed=[bool]$Disarmed}}
if(Get-Process HebENFixedInputProbe -ErrorAction SilentlyContinue){throw 'A fixed input probe is already running'}
$trace=Join-Path $dir ('trace-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'-'+[Guid]::NewGuid().ToString('N')+'.jsonl')
$oldTrace=$env:HEBEN_DIAGNOSTICS_PATH;$oldDisabled=$env:HEBEN_PROBE_NO_HOTKEY
try {
    $env:HEBEN_DIAGNOSTICS_PATH=$trace
    if($Disarmed){$env:HEBEN_PROBE_NO_HOTKEY='1'}else{Remove-Item Env:HEBEN_PROBE_NO_HOTKEY -ErrorAction SilentlyContinue}
    $process=Start-Process -FilePath $exe -WorkingDirectory $dir -WindowStyle Hidden -PassThru
} finally {
    $env:HEBEN_DIAGNOSTICS_PATH=$oldTrace;$env:HEBEN_PROBE_NO_HOTKEY=$oldDisabled
}
$registration=$null
for($attempt=0;$attempt -lt 80 -and -not $registration;$attempt++) {
    if(Test-Path -LiteralPath $trace) {
        try {$registration=@(Get-Content -LiteralPath $trace -Encoding UTF8 | ForEach-Object {$_ | ConvertFrom-Json} | Where-Object stage -eq 'hotkey_register') | Select-Object -Last 1}
        catch {$registration=$null}
    }
    if(-not $registration){Start-Sleep -Milliseconds 50}
}
$expected=if($Disarmed){'disabled_for_smoke'}else{'ok'}
if(-not $registration -or $registration.result -ne $expected) {
    if(Get-Process -Id $process.Id -ErrorAction SilentlyContinue) {
        try {& (Join-Path $PSScriptRoot 'stop-fixed-input-probe.ps1') -ProbeProcessId $process.Id -Directory $dir | Out-Null}
        catch {Write-Warning "Probe PID $($process.Id) could not be stopped safely: $($_.Exception.Message)"}
    }
    throw "Probe did not confirm expected hotkey state ($expected); inspect $trace"
}
[pscustomobject]@{pid=$process.Id;trace=$trace;armed=(-not $Disarmed);
    note='No microphone or ASR. SendInput acceptance alone does not verify editor content.'}

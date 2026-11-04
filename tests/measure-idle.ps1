param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$Seconds = 900,
    [int]$IntervalMs = 1000,
    [int]$AttachPid = 0,
    [string]$WindowClass = 'HebENDictationHiddenClass'
)
$ErrorActionPreference = 'Stop'
if ($Seconds -lt 1 -or $IntervalMs -lt 200) { throw 'Invalid sampling duration or interval.' }
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
if (-not $AttachPid -and (Get-Process HebENDictation -ErrorAction SilentlyContinue)) { throw 'An existing HebENDictation process is running; refusing to interfere.' }
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HebenWindow {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr hwnd, string name);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr w, IntPtr l);
}
'@
$outPath = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outPath)) | Out-Null
$clock = [Diagnostics.Stopwatch]::StartNew()
$p = if ($AttachPid) { Get-Process -Id $AttachPid } else { Start-Process -FilePath $exePath -WorkingDirectory ([IO.Path]::GetDirectoryName($exePath)) -WindowStyle Hidden -PassThru }
if ($AttachPid -and $p.Path -ne $exePath) { throw 'Attached process path differs from the requested executable.' }
$window = [IntPtr]::Zero
while ($clock.Elapsed.TotalSeconds -lt 15 -and $window -eq [IntPtr]::Zero) {
    if ($p.HasExited) { throw "Application exited at launch: $($p.ExitCode)" }
    $window = [HebenWindow]::FindWindow($WindowClass, $null)
    if ($window -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 20 }
}
$readyMs = if ($AttachPid) { $null } else { $clock.Elapsed.TotalMilliseconds }
if ($window -eq [IntPtr]::Zero) { throw 'Window not found within 15 seconds; process left running for diagnosis.' }
$windowPid=0; [void][HebenWindow]::GetWindowThreadProcessId($window,[ref]$windowPid)
if ($windowPid -ne $p.Id) { throw 'Window does not belong to the sampled process; no process was stopped.' }
$rows = [Collections.Generic.List[object]]::new()
$previousTime = 0.0
$previousCpu = 0.0
try {
    while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
        $p.Refresh()
        if ($p.HasExited) { throw "Application exited unexpectedly: $($p.ExitCode)" }
        $elapsed = $clock.Elapsed.TotalSeconds
        $cpu = $p.TotalProcessorTime.TotalSeconds
        $cpuPercent = if ($elapsed -gt $previousTime) { 100 * ($cpu - $previousCpu) / ($elapsed - $previousTime) } else { 0 }
        $rows.Add([pscustomobject]@{
            utc = [DateTime]::UtcNow.ToString('o'); elapsed_s = $elapsed; pid = $p.Id;
            cpu_seconds = $cpu; cpu_percent_one_core = $cpuPercent;
            working_set_bytes = $p.WorkingSet64; private_bytes = $p.PrivateMemorySize64;
            virtual_bytes = $p.VirtualMemorySize64; peak_working_set_bytes = $p.PeakWorkingSet64;
            peak_paged_memory_bytes = $p.PeakPagedMemorySize64; handles = $p.HandleCount;
            threads = $p.Threads.Count; capturing = ([HebenWindow]::GetProp($window,'HebEN.Capturing') -ne [IntPtr]::Zero)
        })
        $previousTime = $elapsed; $previousCpu = $cpu
        Start-Sleep -Milliseconds $IntervalMs
    }
} finally {
    $rows | Export-Csv -LiteralPath $outPath -NoTypeInformation -Encoding UTF8
    [pscustomobject]@{exe=$exePath; exe_sha256=$exeHash; pid=$p.Id; window_class=$WindowClass; attached=[bool]$AttachPid; process_start=$p.StartTime.ToUniversalTime().ToString('o'); startup_window_ms=$readyMs; interval_ms=$IntervalMs; requested_seconds=$Seconds; sample_count=$rows.Count; note='CPU percent is relative to one logical CPU; first sample includes startup or pre-attach CPU and should be excluded from interval statistics. Working set and private bytes are process values. No GPU or energy attribution.'} | ConvertTo-Json | Set-Content -LiteralPath ($outPath + '.json') -Encoding UTF8
    if (-not $AttachPid) {
        [void][HebenWindow]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
        if (-not $p.WaitForExit(10000)) { Write-Warning 'App did not exit in 10 seconds; left running for diagnosis.' }
    }
}

param(
    [Parameter(Mandatory=$true)][string]$FixtureProcessJson,
    [Parameter(Mandatory=$true)][string]$Output,
    [int]$Seconds=20
)
$ErrorActionPreference='Stop'
if ($Seconds -lt 1 -or $Seconds -gt 60) { throw 'Seconds must be 1..60' }
if (Test-Path -LiteralPath $Output) { throw 'Output already exists' }
$fixture=Get-Content -LiteralPath $FixtureProcessJson -Raw -Encoding UTF8 | ConvertFrom-Json
$targetId=[int]$fixture.pid
$target=Get-Process -Id $targetId -ErrorAction Stop
if ($target.Path -ne $fixture.exe) { throw 'Fixture process identity mismatch' }
$rows=[Collections.Generic.List[object]]::new()
$end=[DateTime]::UtcNow.AddSeconds($Seconds)
while ([DateTime]::UtcNow -lt $end -and (Get-Process -Id $targetId -ErrorAction SilentlyContinue)) {
    foreach ($counter in @('\GPU Engine(*)\Utilization Percentage','\GPU Process Memory(*)\Dedicated Usage','\GPU Process Memory(*)\Shared Usage')) {
        try {
            $sample=Get-Counter -Counter $counter -ErrorAction SilentlyContinue
            if($null -eq $sample){throw 'Counter query returned no data'}
            foreach ($value in $sample.CounterSamples) {
                if ($value.InstanceName -match ('(^|_)pid_'+$targetId+'(_|$)')) {
                    $rows.Add([pscustomobject]@{utc=[DateTime]::UtcNow.ToString('o');pid=$targetId;instance=$value.InstanceName;path=$value.Path;value=$value.CookedValue;status=$value.Status})
                }
            }
        } catch {
            $rows.Add([pscustomobject]@{utc=[DateTime]::UtcNow.ToString('o');pid=$targetId;instance=$counter;path='COUNTER_ERROR';value='';status=$_.Exception.Message})
        }
    }
}
if ($rows.Count) { $rows | Export-Csv -LiteralPath $Output -NoTypeInformation -Encoding UTF8 }
else { [pscustomobject]@{utc=[DateTime]::UtcNow.ToString('o');pid=$targetId;instance='NO_MATCHING_COUNTERS';path='UNKNOWN_NOT_ZERO';value='';status='No process GPU counters observed'} | Export-Csv -LiteralPath $Output -NoTypeInformation -Encoding UTF8 }

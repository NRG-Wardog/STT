param([string]$Output = 'build\evidence\live-audio\mixed-02.wav')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\candidate\Release\HebENRecordFixture.exe'
$outputPath = [IO.Path]::GetFullPath((Join-Path $root $Output))
if (Test-Path -LiteralPath $outputPath) { throw "Refusing to overwrite $outputPath" }
$form = [Windows.Forms.Form]::new()
$form.Text = 'HebEN microphone test'
$form.Size = [Drawing.Size]::new(760, 320)
$form.StartPosition = 'CenterScreen'
$form.TopMost = $true
$label = [Windows.Forms.Label]::new()
$label.Dock = 'Fill'
$label.Font = [Drawing.Font]::new('Segoe UI', 19)
$label.TextAlign = 'MiddleCenter'
$label.Text = "When ready, press Start.`nSay the sentence shown in the chat when the screen says SPEAK NOW.`nThe audio is saved locally."
$button = [Windows.Forms.Button]::new()
$button.Dock = 'Bottom'; $button.Height = 56; $button.Text = 'Start recording'
$form.Controls.Add($label); $form.Controls.Add($button)
$timer = [Windows.Forms.Timer]::new(); $timer.Interval = 100
$watch = [Diagnostics.Stopwatch]::new()
$script:process = $null
$button.Add_Click({
    $button.Enabled = $false
    $script:process = Start-Process -FilePath $exe -ArgumentList @('"' + $outputPath + '"', '8') -WindowStyle Hidden -PassThru
    $watch.Restart(); $timer.Start()
})
$timer.Add_Tick({
    $elapsed = $watch.Elapsed.TotalSeconds
    if ($script:process.HasExited) {
        $timer.Stop()
        $label.Text = if ($script:process.ExitCode -eq 0) { 'Recording complete. You can close this window and report in the chat.' } else { "Recording failed with exit code $($script:process.ExitCode)." }
    } elseif ($elapsed -lt 5.5) {
        $label.Text = "Get ready... $([Math]::Ceiling(5.5-$elapsed))"
    } else {
        $label.Text = 'SPEAK NOW'
    }
})
$form.Add_FormClosing({ if ($script:process -and -not $script:process.HasExited) { $_.Cancel = $true } })
[void]$form.ShowDialog()
$timer.Dispose(); $form.Dispose()

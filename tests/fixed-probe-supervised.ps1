param(
    [Parameter(Mandatory=$true)][string]$ProbeDirectory,
    [Parameter(Mandatory=$true)][string]$Output,
    [switch]$Supervised,
    [switch]$AllowClipboard,
    [switch]$AllowTrustedTerminal
)
$ErrorActionPreference='Stop'
if(-not $Supervised){throw 'This opens a visible test editor. Run only during an explicitly supervised desktop session with -Supervised.'}
if(Test-Path -LiteralPath $Output){throw 'Refusing to overwrite results'}
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class FixedProbeSupervisedNative {
 [DllImport("user32.dll")] public static extern uint GetClipboardSequenceNumber();
}
'@
$fixed=(-join @([char]0x05e9,[char]0x05dc,[char]0x05d5,[char]0x05dd,' ',[char]0x05e2,[char]0x05d5,[char]0x05dc,[char]0x05dd,' Hello World 123'))
$meta=Get-Content -LiteralPath (Join-Path $ProbeDirectory 'metadata.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$actualHash=(Get-FileHash -LiteralPath (Join-Path $ProbeDirectory 'HebENFixedInputProbe.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
if($actualHash -ne $meta.sha256){throw 'Probe executable does not match its staged hash'}
$cases=@(
    @{name='empty';initial='';expected=$fixed;presses=1;instruction='Click the test field and press the probe shortcut once.'},
    @{name='beginning';initial='ABCD';expected=($fixed+'ABCD');presses=1;instruction='Put the caret before A, then press the probe shortcut once.'},
    @{name='middle';initial='ABCD';expected=('AB'+$fixed+'CD');presses=1;instruction='Put the caret between B and C, then press the probe shortcut once.'},
    @{name='end';initial='ABCD';expected=('ABCD'+$fixed);presses=1;instruction='Put the caret after D, then press the probe shortcut once.'},
    @{name='selection';initial='ABCD';expected=('A'+$fixed+'D');presses=1;instruction='Select B and C, then press the probe shortcut once.'},
    @{name='repeat';initial='';expected=($fixed+$fixed);presses=2;instruction='Click the test field and press the probe shortcut twice.'}
)
$result=[ordered]@{status='INCOMPLETE';scope='supervised test-owned WinForms field; physical shortcut must be pressed by tester';
    expected_probe_sha256=$meta.sha256;hotkey=$meta.hotkey;cases=@();no_enter_or_submission='NOT TESTED outside this controlled text box'}
$probe=$null;$state=@{index=0;recordChanges=$false;lastChangedWhileFocused=$false;complete=$false;hotkeyBaseline=0;sendBaseline=0}
$clipBefore=[FixedProbeSupervisedNative]::GetClipboardSequenceNumber()
try {
    $probe=& (Join-Path $PSScriptRoot 'launch-fixed-input-probe.ps1') -Directory $ProbeDirectory -AllowClipboard:$AllowClipboard -AllowTrustedTerminal:$AllowTrustedTerminal
    $result.probe_pid=$probe.pid;$result.trace=$probe.trace
    $registration=$null
    for($attempt=0;$attempt -lt 100 -and -not $registration;$attempt++) {
        if(Test-Path -LiteralPath $probe.trace) {
            $registration=@(Get-Content -LiteralPath $probe.trace -Encoding UTF8 | ForEach-Object {$_ | ConvertFrom-Json} | Where-Object stage -eq 'hotkey_register') | Select-Object -Last 1
        }
        if(-not $registration){Start-Sleep -Milliseconds 50}
    }
    if(-not $registration -or $registration.result -ne 'ok'){throw 'Probe did not register its shortcut; inspect the trace'}
    $form=[Windows.Forms.Form]::new();$form.Text='HebEN supervised input test';$form.Size=[Drawing.Size]::new(820,310)
    $form.StartPosition='CenterScreen'
    $instruction=[Windows.Forms.Label]::new();$instruction.Location=[Drawing.Point]::new(20,20);$instruction.Size=[Drawing.Size]::new(760,60)
    $box=[Windows.Forms.TextBox]::new();$box.Location=[Drawing.Point]::new(20,90);$box.Size=[Drawing.Size]::new(760,70);$box.Multiline=$true
    $verify=[Windows.Forms.Button]::new();$verify.Text='Verify and next';$verify.Location=[Drawing.Point]::new(20,180);$verify.Size=[Drawing.Size]::new(150,35)
    $form.Controls.AddRange(@($instruction,$box,$verify))
    $box.Add_TextChanged({if($state.recordChanges){$state.lastChangedWhileFocused=($form.ActiveControl -eq $box)}})
    function Get-ProbeEvents {
        if(-not (Test-Path -LiteralPath $probe.trace)){return @()}
        return @(Get-Content -LiteralPath $probe.trace -Encoding UTF8 | ForEach-Object {$_ | ConvertFrom-Json})
    }
    function Set-Case {
        $case=$cases[$state.index];$state.recordChanges=$false;$box.Text=$case.initial;$state.lastChangedWhileFocused=$false
        $events=@(Get-ProbeEvents)
        $state.hotkeyBaseline=@($events | Where-Object {$_.stage -eq 'hotkey' -and $_.result -eq 'received'}).Count
        $state.sendBaseline=@($events | Where-Object {$_.stage -eq 'sendinput' -and $_.result -eq 'events_accepted_editor_unverified'}).Count
        $instruction.Text="Case $($state.index+1)/$($cases.Count): $($case.instruction)  Shortcut: $($meta.hotkey)"
        $state.recordChanges=$true
    }
    $verify.Add_Click({
        $case=$cases[$state.index]
        $match=$box.Text -ceq $case.expected
        $events=@(Get-ProbeEvents)
        $hotkeys=@($events | Where-Object {$_.stage -eq 'hotkey' -and $_.result -eq 'received'}).Count-$state.hotkeyBaseline
        $sends=@($events | Where-Object {$_.stage -eq 'sendinput' -and $_.result -eq 'events_accepted_editor_unverified'}).Count-$state.sendBaseline
        $passed=$match -and $state.lastChangedWhileFocused -and $hotkeys -ge $case.presses -and $sends -ge $case.presses
        $result.cases+=@([pscustomobject]@{name=$case.name;status=if($passed){'PASS'}else{'FAIL'};
            exact_test_field_readback=$match;last_change_while_field_focused=$state.lastChangedWhileFocused;hotkey_events=$hotkeys;sendinput_batches=$sends})
        $state.index++
        if($state.index -ge $cases.Count){$state.complete=$true;$form.Close()}else{Set-Case}
    })
    $form.Add_Shown({Set-Case})
    [Windows.Forms.Application]::Run($form)
    $result.clipboard_sequence_unchanged=([FixedProbeSupervisedNative]::GetClipboardSequenceNumber() -eq $clipBefore)
    if($state.complete -and $result.clipboard_sequence_unchanged -and @($result.cases | Where-Object status -ne 'PASS').Count -eq 0){$result.status='PASS_CONTROLLED_WINFORMS'}
    elseif($state.complete){$result.status='FAIL_CONTROLLED_WINFORMS'}
} catch {$result.error=$_.Exception.Message}
finally {
    if($probe){try{& (Join-Path $PSScriptRoot 'stop-fixed-input-probe.ps1') -ProbeProcessId $probe.pid -Directory $ProbeDirectory | Out-Null}catch{$result.cleanup_error=$_.Exception.Message}}
    $result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Output -Encoding UTF8
}
if($result.status -ne 'PASS_CONTROLLED_WINFORMS'){throw "Test incomplete or failed; see $Output"}

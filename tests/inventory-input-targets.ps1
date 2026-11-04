param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$path=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $path){throw 'Refusing to overwrite inventory'}
$root=Split-Path -Parent $PSScriptRoot
$candidatePaths=@(
    (Join-Path $root 'dist\HebENDictation.exe'),
    (Join-Path $root 'build\recovery-gpu-v6\HebENDictation.exe'),
    (Join-Path $root 'build\packaging\HebEN-hybrid-gpu-trial-v4\HebENDictation.exe'),
    (Join-Path $root 'build\candidate\Release\HebENDictation.exe'),
    (Join-Path $root 'build\cuda-ninja\HebENDictation.exe'),
    (Join-Path $root 'build\candidate\Release\HebENFixedInputProbe.exe'),
    (Join-Path $root 'build\fixed-input-probe-configured\HebENFixedInputProbe.exe')
)
$binaries=foreach($item in $candidatePaths){
    if(Test-Path -LiteralPath $item -PathType Leaf){
        $file=Get-Item -LiteralPath $item
        [pscustomobject]@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant();
            bytes=$file.Length;file_version=$file.VersionInfo.FileVersion;product_version=$file.VersionInfo.ProductVersion}
    }
}
$running=foreach($p in @(Get-Process HebENDictation -ErrorAction SilentlyContinue)){
    [pscustomobject]@{pid=$p.Id;path=$p.Path;start_utc=$p.StartTime.ToUniversalTime().ToString('o')}
}
$startup=[Environment]::GetFolderPath('Startup');$wsh=New-Object -ComObject WScript.Shell
$shortcuts=foreach($link in @(Get-ChildItem -LiteralPath $startup -Filter '*HebEN*' -ErrorAction SilentlyContinue)){
    $s=$wsh.CreateShortcut($link.FullName)
    [pscustomobject]@{path=$link.FullName;target=$s.TargetPath;working_directory=$s.WorkingDirectory;arguments=$s.Arguments}
}
$programs=foreach($name in @('notepad','chrome','Code','WindowsTerminal','codex','powershell','pwsh')){
    $items=@(Get-Process -Name $name -ErrorAction SilentlyContinue)
    [pscustomobject]@{name=$name;running_count=$items.Count;running_paths=@($items | ForEach-Object { try{$_.Path}catch{$null} } | Where-Object {$_} | Select-Object -Unique)}
}
$packages=@(Get-AppxPackage -ErrorAction SilentlyContinue | Where-Object {$_.Name -match 'WindowsTerminal|ChatGPT|OpenAI'} |
    Select-Object Name,Version,InstallLocation)
[pscustomobject]@{timestamp_utc=[DateTime]::UtcNow.ToString('o');binaries=@($binaries);running_hebendictation=@($running);
    startup_shortcuts=@($shortcuts);running_target_processes=@($programs);relevant_appx_packages=$packages;
    limitations='Process presence does not identify the active field; installed-app inventory is not exhaustive. No titles, editor contents, or command lines were read.'} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding UTF8
Write-Output "Saved metadata-only inventory to $path"

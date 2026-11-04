param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$dest=[IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $dest) { throw 'Refusing to overwrite an existing directory' }
New-Item -ItemType Directory -Path $dest | Out-Null
$revision='ee769a2a13b887f91572eec349458bfb0fe79e7a'
$files=@(
    @{name='encoder.int8.onnx';size=70478964;sha256='6c6febcb74a09435315a1d872a6539f6e7fc2c333533f14c74edc0e85c49ffcd'},
    @{name='decoder.int8.onnx';size=1049105;sha256='feb03e7c8aa761593a9284a6fa1e91259be3702de75f630062256c1a59c5a906'},
    @{name='joiner.int8.onnx';size=771805;sha256='1d2c6d8e6dabc5eb52de52e95718e9bdfbffbfe7ead3808fb5ffd74e83bd9cae'},
    @{name='tokens.txt';size=17086;git_sha1='97675cfca093bb75694b0f8e5a115b3fecf51d86'}
)
$receipt=[Collections.Generic.List[object]]::new()
foreach($file in $files) {
    $temporary=Join-Path $dest ($file.name+'.partial'); $target=Join-Path $dest $file.name
    $url='https://huggingface.co/ivrit-ai/zipformer2-66m-streaming-he/resolve/'+$revision+'/sherpa-onnx/'+$file.name
    & curl.exe --fail --location --retry 2 --silent --show-error --output $temporary $url
    if($LASTEXITCODE -ne 0){throw "Download failed: $($file.name)"}
    if((Get-Item -LiteralPath $temporary).Length -ne $file.size){throw "Size mismatch: $($file.name)"}
    $sha=(Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
    if($file.sha256 -and $sha -ne $file.sha256){throw "SHA256 mismatch: $($file.name)"}
    if($file.git_sha1) {
        $bytes=[IO.File]::ReadAllBytes($temporary)
        $prefix=[Text.Encoding]::ASCII.GetBytes(('blob '+$bytes.Length+[char]0))
        $blob=[byte[]]::new($prefix.Length+$bytes.Length)
        [Array]::Copy($prefix,0,$blob,0,$prefix.Length);[Array]::Copy($bytes,0,$blob,$prefix.Length,$bytes.Length)
        $gitHash=[BitConverter]::ToString([Security.Cryptography.SHA1]::Create().ComputeHash($blob)).Replace('-','').ToLowerInvariant()
        if($gitHash -ne $file.git_sha1){throw "Git blob mismatch: $($file.name)"}
    }
    Move-Item -LiteralPath $temporary -Destination $target
    $receipt.Add([pscustomobject]@{name=$file.name;size=$file.size;sha256=$sha;source=$url})
}
[pscustomobject]@{model='ivrit-ai/zipformer2-66m-streaming-he';revision=$revision;license='CC-BY-4.0';files=$receipt} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $dest 'integrity.json') -Encoding UTF8

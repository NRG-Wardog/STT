param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$QuantizerExe
)
$ErrorActionPreference='Stop'
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Refusing to overwrite the output directory'}
$quantizer=(Resolve-Path -LiteralPath $QuantizerExe).Path
New-Item -ItemType Directory -Path $out | Out-Null
$rev='2130c78e4a9cb4914cc4df91a1c3031407789705'
$source=Join-Path $out 'ivrit-whisper-large-v3-turbo-f16.bin'
$partial=$source+'.partial'
$url='https://huggingface.co/ivrit-ai/whisper-large-v3-turbo-ggml/resolve/'+$rev+'/ggml-model.bin'
& curl.exe --fail --location --retry 2 --output $partial $url
if($LASTEXITCODE -ne 0){throw 'Model download failed'}
if((Get-Item -LiteralPath $partial).Length -ne 1624555275){throw 'Model size mismatch'}
$sourceHash=(Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant()
if($sourceHash -ne 'c8090411113357097bfafc2b8e228ec1639fa7f5fe4ecb5d054ac0ccef8641b1'){throw 'Source model SHA256 mismatch'}
Move-Item -LiteralPath $partial -Destination $source
$quant=Join-Path $out 'ivrit-whisper-large-v3-turbo-q5_0.bin'
& $quantizer $source ($quant+'.partial') q5_0 *> (Join-Path $out 'quantize.log')
if($LASTEXITCODE -ne 0){throw 'Quantization failed'}
if((Get-Item -LiteralPath ($quant+'.partial')).Length -ne 574041195){throw 'Quantized model size mismatch'}
$quantHash=(Get-FileHash -LiteralPath ($quant+'.partial') -Algorithm SHA256).Hash.ToLowerInvariant()
if($quantHash -ne '6c1da92e8e41dd64b8cc402eee7eb7a433d2152567e1a4d9cf181fefcc67a572'){
    throw "Quantized model hash differs from the tested v1.9.5 build: $quantHash"
}
Move-Item -LiteralPath ($quant+'.partial') -Destination $quant
[pscustomobject]@{revision=$rev;source_url=$url;source_sha256=$sourceHash;quantized_sha256=$quantHash;
    quantizer=$quantizer;quantizer_sha256=(Get-FileHash -LiteralPath $quantizer -Algorithm SHA256).Hash.ToLowerInvariant();license='Apache-2.0'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'integrity.json') -Encoding UTF8

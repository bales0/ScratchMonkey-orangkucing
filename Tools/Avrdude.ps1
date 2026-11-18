# Forward only explicitly supplied arguments to the project-local AVRDUDE.
$ErrorActionPreference = 'Stop'
$output = Join-Path (Split-Path $PSScriptRoot -Parent) '.pio\tools\avrdude\install'
$executable = Join-Path $output 'bin\avrdude.exe'
$config = Join-Path $output 'etc\avrdude.conf'
if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $config)) {
    throw 'Local AVRDUDE is missing. Run Tools\BuildAvrdude.ps1 first.'
}
& $executable -C $config @args
exit $LASTEXITCODE

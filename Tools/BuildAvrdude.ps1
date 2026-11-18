[CmdletBinding()]
param(
    [string]$SourcePath = (Join-Path (Split-Path $PSScriptRoot -Parent) '..\avrdude'),
    [string]$Msys2Root = 'C:\msys64',
    [ValidateRange(1, 64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$source = (Resolve-Path -LiteralPath $SourcePath).Path
$output = Join-Path $projectRoot '.pio\tools\avrdude'
$build = Join-Path $output 'build'
$install = Join-Path $output 'install'
$toolBin = Join-Path $Msys2Root 'ucrt64\bin'
$msysBin = Join-Path $Msys2Root 'usr\bin'
$cmake = Join-Path $toolBin 'cmake.exe'
$gcc = Join-Path $toolBin 'gcc.exe'
$ninja = Join-Path $toolBin 'ninja.exe'
$bison = Join-Path $msysBin 'bison.exe'
$flex = Join-Path $msysBin 'flex.exe'
foreach ($required in @($cmake, $gcc, $ninja, $bison, $flex)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing build tool: $required" }
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'src\stk500v2.c'))) {
    throw "Not an AVRDUDE source directory: $source"
}
$previousPath = $env:PATH
try {
    $env:PATH = "$toolBin;$msysBin;$previousPath"
    & $cmake -S $source -B $build -G Ninja "-DCMAKE_C_COMPILER=$gcc" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DBISON_EXECUTABLE=$bison" "-DFLEX_EXECUTABLE=$flex" '-DCMAKE_BUILD_TYPE=Release' '-DFORCE_DISABLE_PYTHON_SUPPORT=ON' '-DBUILD_DOC=OFF' '-DUSE_EXTERNAL_LIBS=OFF' "-DCMAKE_INSTALL_PREFIX=$install"
    if ($LASTEXITCODE -ne 0) { throw 'AVRDUDE configuration failed' }
    & $cmake --build $build --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'AVRDUDE build failed' }
    & $cmake --install $build
    if ($LASTEXITCODE -ne 0) { throw 'AVRDUDE local installation failed' }

    # Keep runtime libraries beside the executable, without a global PATH change.
    $bin = Join-Path $install 'bin'
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue((Join-Path $bin 'avrdude.exe'))
    $seen = @{}
    while ($pending.Count) {
        $binary = $pending.Dequeue()
        foreach ($line in (& (Join-Path $toolBin 'objdump.exe') -p $binary)) {
            if ($line -match 'DLL Name:\s*(\S+)') {
                $name = $Matches[1]
                $dependency = Join-Path $toolBin $name
                if (-not $seen.ContainsKey($name) -and (Test-Path -LiteralPath $dependency)) {
                    $seen[$name] = $true
                    $destination = Join-Path $bin $name
                    Copy-Item -LiteralPath $dependency -Destination $destination
                    $pending.Enqueue($destination)
                }
            }
        }
    }
    Write-Output "Local AVRDUDE: $(Join-Path $bin 'avrdude.exe')"
    Write-Output "Configuration: $(Join-Path $install 'etc\avrdude.conf')"
} finally {
    $env:PATH = $previousPath
}

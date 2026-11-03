$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tccInclude = Join-Path $root 'bin\tcc\include'
$winntPath = Join-Path $tccInclude 'winapi\winnt.h'
$mathPath = Join-Path $tccInclude 'math.h'
$tccLib = Join-Path $root 'bin\tcc\lib'

foreach ($path in @($winntPath, $mathPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "No se encontro el encabezado requerido: $path"
    }
}

$winnt = [System.IO.File]::ReadAllText($winntPath)
$winnt = $winnt.Replace(
    '#if defined(__x86_64) && !defined(RC_INVOKED)',
    '#if defined(__x86_64) && !defined(RC_INVOKED) && !defined(__clang__)'
)
$winnt = $winnt.Replace('PM128A FloatingContext[16];', 'PVOID FloatingContext[16];')
[System.IO.File]::WriteAllText($winntPath, $winnt)

$math = [System.IO.File]::ReadAllText($mathPath)
$math = $math.Replace('fstt    %0', 'fstpt   %0')
[System.IO.File]::WriteAllText($mathPath, $math)

$dlltool = Get-Command llvm-dlltool.exe -ErrorAction SilentlyContinue
if (-not $dlltool) {
    throw 'No se encontro llvm-dlltool.exe, necesario para generar las bibliotecas Win32.'
}

foreach ($name in @('kernel32', 'msvcrt', 'user32', 'gdi32')) {
    $definition = Join-Path $tccLib "$name.def"
    $library = Join-Path $tccLib "$name.lib"
    if (-not (Test-Path -LiteralPath $definition)) {
        throw "No se encontro la definicion requerida: $definition"
    }
    & $dlltool.Source -m i386:x86-64 -d $definition -l $library
    if ($LASTEXITCODE -ne 0) {
        throw "llvm-dlltool no pudo generar $library"
    }
}

Write-Output '[OK] Encabezados y bibliotecas de Windows preparados para Clang.'

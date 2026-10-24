@echo off
set TCC=bin\tcc\tcc.exe
if not exist "%TCC%" set TCC=tcc.exe

"%TCC%" -Isrc -o bin\tiger_c.exe src\tiger_arena.c src\tiger_simd.c src\tiger_reader.c src\tiger_value.c src\tiger_env.c src\tiger_lexer.c src\tiger_ast.c src\tiger_parser.c src\tiger_c_interop.c src\tiger_parallel.c src\tiger_cgen.c src\tiger_interp.c src\tiger_main.c

if %ERRORLEVEL% EQU 0 (
    copy /y bin\tiger_c.exe bin\tiger.exe >nul
    echo [EXITO]: bin\tiger_c.exe y bin\tiger.exe compilados exitosamente.
) else (
    echo [ERROR]: Fallo al compilar con TCC.
)


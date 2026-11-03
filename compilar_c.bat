@echo off
setlocal
cd /d "%~dp0"

set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
if exist "%CLANG%" goto clang_ready
set "CLANG=clang.exe"
where clang.exe >nul 2>&1
if errorlevel 1 (
    echo [ERROR]: No se encontro Clang. Instala LLVM o agrega clang.exe al PATH.
    exit /b 1
)
:clang_ready

if not exist "bin\tcc\include\winapi\windows.h" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "tools\prepare_tiger_build.ps1"
    if errorlevel 1 exit /b 1
)

set "LIBS=bin\tcc\lib\kernel32.lib bin\tcc\lib\msvcrt.lib bin\tcc\lib\user32.lib bin\tcc\lib\gdi32.lib"

echo [1/4 Compilando Nucleo Ligero: bin\tiger.exe]...
"%CLANG%" -O3 -mavx2 -mfma -mno-stack-arg-probe -DTIGER_BUILD_STANDALONE_LIB -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib %LIBS% -Wl,-entry:mainCRTStartup -o bin\tiger_c.exe src\tiger_arena.c src\tiger_simd.c src\tiger_reader.c src\tiger_value.c src\tiger_env.c src\tiger_lexer.c src\tiger_ast.c src\tiger_parser.c src\tiger_c_interop.c src\tiger_parallel.c src\tiger_cgen.c src\tiger_native_hw.c src\tiger_interp.c src\tiger_ai_chat.c src\tiger_compat.c src\tiger_empaquetar.c src\tiger_modules.c src\tiger_cache.c src\tiger_compiler_main.c src\api_traduct.c src\tiger_main.c
if errorlevel 1 (
    echo [ERROR]: Clang no pudo compilar el motor C de Tiger.
    exit /b 1
)

copy /y bin\tiger_c.exe bin\tiger.exe >nul
copy /y bin\tiger.exe tiger.exe >nul
if errorlevel 1 (
    echo [ERROR]: No se pudo actualizar bin\tiger.exe.
    exit /b 1
)

echo [2/4 Compilando Compilador Dedicado e Incremental: bin\tiger-compiler.exe]...
"%CLANG%" -O3 -mavx2 -mfma -mno-stack-arg-probe -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib %LIBS% -Wl,-entry:mainCRTStartup -o bin\tiger-compiler.exe src\tiger_arena.c src\tiger_simd.c src\tiger_reader.c src\tiger_value.c src\tiger_env.c src\tiger_lexer.c src\tiger_ast.c src\tiger_parser.c src\tiger_c_interop.c src\tiger_parallel.c src\tiger_cgen.c src\tiger_native_hw.c src\tiger_interp.c src\tiger_ai_chat.c src\tiger_compat.c src\tiger_cache.c src\tiger_modules.c src\api_traduct.c src\tiger_compiler_main.c
if errorlevel 1 (
    echo [ERROR]: Clang no pudo compilar tiger-compiler.exe.
    exit /b 1
)
copy /y bin\tiger-compiler.exe tiger-compiler.exe >nul

echo [3/4 Compilando Empaquetador Multiplataforma: bin\empaquetar.exe]...
"%CLANG%" -O3 -mavx2 -mfma -mno-stack-arg-probe -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib %LIBS% -Wl,-entry:mainCRTStartup -o bin\empaquetar.exe src\tiger_arena.c src\tiger_simd.c src\tiger_reader.c src\tiger_value.c src\tiger_env.c src\tiger_lexer.c src\tiger_ast.c src\tiger_parser.c src\tiger_c_interop.c src\tiger_parallel.c src\tiger_cgen.c src\tiger_native_hw.c src\tiger_interp.c src\tiger_ai_chat.c src\tiger_compat.c src\tiger_cache.c src\tiger_modules.c src\api_traduct.c src\tiger_empaquetar.c
if errorlevel 1 (
    echo [ERROR]: Clang no pudo compilar empaquetar.exe.
    exit /b 1
)
copy /y bin\empaquetar.exe empaquetar.exe >nul

echo [4/4 Compilando Modulos Compartidos Opcionales en bin\ (Carga bajo demanda)]...
"%CLANG%" -shared -O3 -mavx2 -mfma -DTIGER_NO_MAIN_STARTUP -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib bin\tcc\lib\kernel32.lib bin\tcc\lib\msvcrt.lib -o bin\tiger_mod_uix.dll src\modules\tiger_mod_uix.c src\tiger_compat.c
"%CLANG%" -shared -O3 -mavx2 -mfma -DTIGER_NO_MAIN_STARTUP -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib bin\tcc\lib\kernel32.lib bin\tcc\lib\msvcrt.lib -o bin\tiger_mod_ai.dll src\modules\tiger_mod_ai.c src\tiger_compat.c
"%CLANG%" -shared -O3 -mavx2 -mfma -DTIGER_NO_MAIN_STARTUP -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib bin\tcc\lib\kernel32.lib bin\tcc\lib\msvcrt.lib -o bin\tiger_mod_cpu.dll src\modules\tiger_mod_cpu.c src\tiger_compat.c
"%CLANG%" -shared -O3 -mavx2 -mfma -DTIGER_NO_MAIN_STARTUP -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib bin\tcc\lib\kernel32.lib bin\tcc\lib\msvcrt.lib -o bin\tiger_mod_gpu.dll src\modules\tiger_mod_gpu.c src\tiger_compat.c
"%CLANG%" -shared -O3 -mavx2 -mfma -DTIGER_NO_MAIN_STARTUP -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib bin\tcc\lib\kernel32.lib bin\tcc\lib\msvcrt.lib -o bin\tiger_mod_npu.dll src\modules\tiger_mod_npu.c src\tiger_compat.c
"%CLANG%" -shared -O3 -mavx2 -mfma -DTIGER_NO_MAIN_STARTUP -Isrc -Ibin\tcc\include -Ibin\tcc\include\winapi -Wno-everything -nostdlib bin\tcc\lib\kernel32.lib bin\tcc\lib\msvcrt.lib -o bin\tiger_mod_bci.dll src\modules\tiger_mod_bci.c src\tiger_compat.c

echo [EXITO]: Nucleo ligero, Compilador incremental, Empaquetador y Modulos dinamicos compilados con exito.
exit /b 0

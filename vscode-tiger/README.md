# Tiger Language Support for VS Code

Esta extensión registra el lenguaje Tiger (`.tg`) y añade resaltado de sintaxis,
comentarios con `#`, emparejamiento automático de llaves/paréntesis y comillas.
Reconoce keywords en español e inglés, cadenas, números, operadores, rutas de
archivo, funciones y declaraciones de macro.

## Instalar

Empaqueta esta carpeta como VSIX e instálala en VS Code. Desde la raíz del
proyecto Tiger, si tienes `vsce` instalado:

```powershell
Push-Location .\vscode-tiger
vsce package
Pop-Location
code --install-extension .\tiger-language-0.1.0.vsix
```

En el workspace Tiger la asociación `.tg` está configurada en `.vscode/settings.json`.
Para ejecutar el archivo abierto, usa **Terminal → Run Task → Tiger: ejecutar
archivo .tg**.

Esta primera versión proporciona edición y resaltado; todavía no incluye
diagnósticos en vivo, autocompletado ni depurador.

En el workspace Tiger, el botón **Run Code** compila a un ejecutable Windows x64
temporal, lo ejecuta y muestra la salida del programa. La ejecución nativa
requiere LLVM en `PATH` (`clang`, `lld-link` y `llvm-dlltool`). Para guardar
los dos formatos de código fuente junto al archivo abierto, usa las tareas
**Tiger: compilar ASM (.asm)** y **Tiger: compilar WebAssembly (.wat)**.

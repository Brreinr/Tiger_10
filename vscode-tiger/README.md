# Tiger Language Support for VS Code

Soporte oficial para el lenguaje de programación **Tiger 10.2** (`.tg` y `.tgi`) en Visual Studio Code.

## ✨ Características

* **Resaltado de Sintaxis Completo:** Palabras clave bilingües (Español / Inglés), literales numéricos, flotantes, cadenas interpoladas, rutas de archivos y operadores de silicio.
* **Comentarios con `#`:** Detección de comentarios de una línea sin conflictos sintácticos.
* **Comprobación de Sintaxis en Tiempo Real:** Validación no intrusiva usando el parser nativo de Tiger en modo flexible (`tiger --check --fix`), reflejando errores directamente en el panel **Problemas**.
* **Emparejamiento Inteligente:** Cierre automático de llaves `{ }`, corchetes `[ ]`, paréntesis `( )` y comillas `" "`.
* **Snippets de Código:** Atajos rápidos para funciones (`fn`), clases/moldes (`clase`), condicionales (`si`), bucles (`ciclo`), y punteros directos (`ptr`).
* **Iconos y Tema Tiger Neon:** Tema de iconos temáticos y colores optimizados para alto contraste en desarrollo de silicio.

## 🚀 Comandos Integrados

* `Tiger: Ejecutar archivo (.tg)` (`tiger.run`): Ejecuta el script inmediatamente en la terminal interactiva Tiger.
* `Tiger: Ejecutar binario nativo (Windows x64)` (`tiger.runNative`): Compila y corre el binario optimizado en C / LLVM.
* `Tiger: Compilar a Ensamblador (.asm)` (`tiger.compileAsm`): Emite código ensamblador nativo x86_64.
* `Tiger: Compilar a WebAssembly (.wat)` (`tiger.compileWat`): Genera representación WebAssembly en texto.
* `Tiger: Comprobar errores de sintaxis` (`tiger.check`): Diagnósticos de sintaxis bajo demanda.

## 📦 Instalación

Empaqueta esta carpeta como VSIX e instálala en VS Code:

```powershell
Push-Location .\vscode-tiger
vsce package
Pop-Location
code --install-extension .\vscode-tiger\tiger-language-0.6.0.vsix
```

# 🐯 Tiger 10 — Lenguaje de Programación Bilingüe y Metalenguaje Universal

**Tiger 10** es un lenguaje de programación moderno, **100% bilingüe (Español / English)** y un **metalenguaje universal políglota**, diseñado para ofrecer máxima velocidad de desarrollo y rendimiento ultra-rápido (**~10 - 25 ms**) gracias a su arquitectura de **Despachador Dual** en paralelo.

Como **metalenguaje**, Tiger actúa como un orquestador maestro capaz de leer, escribir, compilar y comunicar nativamente código de múltiples lenguajes de programación (**Python, C/C++, JavaScript/Node.js, Ensamblador x86_64 y WebAssembly**) dentro de un mismo flujo de ejecución unificado.

---

## 📁 Estructura del Proyecto (3 Carpetas Principales)

Para mantener la máxima simplicidad y portabilidad para cualquier usuario, todo el entorno se organiza en 3 carpetas esenciales:

```text
Tiger/
├── bin/                 # Todos los ejecutables, scripts de terminal y motores de soporte
│   ├── tiger.exe        # Compilador, intérprete y terminal REPL de Tiger
│   ├── tiger.cmd        # Launcher para consola de Windows
│   ├── tiger.bat        # Script de ejecución por lotes
│   ├── node/            # Motor de JavaScript (Node.js)
│   └── tcc/             # Compilador nativo de C/C++ (Tiny C Compiler)
│
├── tools/               # Librerías de Tiger, dependencias externas y herramientas
│   ├── Libraries/       # Librerías nativas (.tg): hacking, neural, web, juego
│   ├── examples/        # Ejemplos y benchmarks
│   ├── tests/           # Suite de pruebas unitarias
│   ├── libraries_ext.json # Gestor de dependencias externas
│   ├── tiger_compiler.py  # Código fuente del compilador
│   └── tiger_daemon.py    # Servidor de compilación en segundo plano
│
├── vscode-tiger/        # Extensión oficial para Visual Studio Code
│   ├── themes/          # Tema personalizado 'Tiger Neon'
│   ├── syntaxes/        # Gramática y coloreado de sintaxis (.tg)
│   ├── snippets/        # Autocompletado y atajos de código
│   ├── icons/           # Iconos oficiales y tema de archivos Tiger
│   └── tiger-logo.png   # Logotipo oficial del lenguaje
│
├── ejemplo.tg           # Archivo de prueba rápida con las 4 librerías
├── README.md            # Documentación en Español
├── README_EN.md         # Documentation in English
└── LICENSE              # Licencia del proyecto
```

---

## ⚡ Instalación Rápida (1 Solo Comando)

Una vez descargada la carpeta de Tiger, abre tu terminal y ejecuta:

```powershell
tiger setup
```

Tiger configurará automáticamente todo tu entorno en 5 segundos:
1. **Motores de ejecución:** Descarga y configura Node.js y TCC en `bin/`.
2. **Librerías de Python:** Sincroniza las dependencias externas (`libraries_ext.json`).
3. **Extensión de VS Code:** Instala automáticamente la extensión, el tema Tiger Neon y los iconos en Visual Studio Code.
4. **PowerShell global:** Registra el comando `tiger` para que puedas usarlo en cualquier terminal sin necesidad de escribir `.\tiger.exe`.

---

## 🚀 Cómo Usar Tiger

### 1. Ejecutar un archivo `.tg` de forma ultra-rápida:
```powershell
tiger ejemplo.tg --run
```
*(Tiempo promedio de ejecución: **~25 - 30 ms**).*

### 2. Abrir la Terminal Interactiva (REPL):
```powershell
tiger
```
Te abrirá la consola interactiva con la paleta de colores **Tiger Neón**, soporte para historial de comandos y ejecución inmediata:
* `:status` $\rightarrow$ Muestra el estado de los componentes.
* `:setup` $\rightarrow$ Reinstala o actualiza dependencias.
* `salir.` $\rightarrow$ Cierra la terminal.

### 3. Compilación Nativa (Windows x64):
```powershell
tiger ejemplo.tg --run-native
```
Ensambla y enlaza el código como un binario nativo de máquina de 64 bits.

### 4. Compilar a Ensamblador o WebAssembly:
```powershell
tiger ejemplo.tg --target asm -o salida.asm
tiger ejemplo.tg --target wat -o salida.wat
```

### 5. Reparación Automática de Sintaxis:
```powershell
tiger ejemplo.tg --fix --run
```
Si olvidaste algún punto `.` de cierre, Tiger crea una copia reparada (`.fixed.tg`) sin tocar el archivo original.

---

## 🌐 Características Destacadas

### 1. 100% Bilingüe Nativo (Español / English)
Tiger fue concebido para romper la barrera del idioma en la programación. Tiene paridad semántica exacta entre Español e Inglés; no se trata de una simple traducción, sino de un analizador sintáctico bilingüe que procesa ambos idiomas con idéntica velocidad y rigor:
```tiger
# Programación en Español
VAR ENT vida = 100.
SI vida > 0 {
    IMPRIMIR "El jugador sigue en pie.".
} SINO {
    IMPRIMIR "Juego terminado.".
}.

# Programación en Inglés
VAR INT health = 100.
IF health > 0 {
    PRINT "Player is still standing.".
} ELSE {
    PRINT "Game over.".
}.
```

### 2. Metalenguaje Universal (Interoperabilidad Políglota)
A diferencia de los lenguajes tradicionales que se aíslan en su propio ecosistema, **Tiger opera como un metalenguaje puente**: permite incrustar, ejecutar e intercambiar datos directamente con varios lenguajes de programación en un único archivo `.tg`:

* **`TIPO PY` (Python)**: Aprovecha bibliotecas de Inteligencia Artificial (PyTorch, TensorFlow), Machine Learning y Criptografía.
* **`TIPO JS` (JavaScript / Node.js)**: Construye microservidores web, maneja sockets, promesas y JSON.
* **`TIPO C` (C / C++ Nativo)**: Ejecuta rutinas de cálculo crítico a nivel de hardware compiladas en tiempo real mediante TCC.
* **`TIPO ASM` / `TIPO BI` (Ensamblador x86_64 y Binario)**: Control directo sobre registros de la CPU y ejecución de bytecode directo en memoria RAM.

```tiger
USAR "Libraries/hacking.tg".

# 1. Bloque Python (IA y Criptografía)
TIPO PY {
    import hashlib
    clave = "MasterKey_Tiger"
    hash_val = hashlib.sha256(clave.encode()).hexdigest()
    print(f"Hash generado: {hash_val}")
}.

# 2. Bloque JavaScript (Web y APIs)
TIPO JS {
    console.log("Servidor Web inicializado en Node.js.");
}.

# 3. Bloque C Nativo (Ultra rendimiento a nivel procesador)
TIPO C {
    #include <stdio.h>
    printf("Calculo a nivel de procesador completado.\n");
}.
```

---

## 🛠️ Diagnóstico del Sistema

Para comprobar en cualquier momento el estado de todos los motores:
```powershell
tiger status
```

---

## 📄 Licencia

Este proyecto está distribuido bajo la **Licencia de Comunidad y Evaluación de Tiger (TCEL)**. Es completamente gratuito para uso personal, educativo, investigación y aportes de la comunidad, protegiendo los derechos de autor contra reventa o apropiación comercial no autorizada. Consulta el archivo [LICENSE](LICENSE) para más detalles.

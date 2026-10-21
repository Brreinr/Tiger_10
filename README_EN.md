# 🐯 Tiger 10 — Bilingual Programming Language & Universal Metalanguage

**Tiger 10** is a modern, **100% bilingual (Spanish / English)** programming language and **universal polyglot metalanguage**, engineered for maximum developer agility and blazing-fast execution speeds (**~10 - 25 ms**) powered by a parallel **Dual-Worker Dispatcher** architecture.

As a **metalanguage**, Tiger serves as a master orchestrator capable of reading, executing, embedding, and communicating natively with code written in multiple programming languages (**Python, C/C++, JavaScript/Node.js, x86_64 Assembly, and WebAssembly**) within a single, unified workflow.

---

## 📁 Project Structure (3 Essential Folders)

To ensure simplicity, clean organization, and portability for any user, the entire project is structured into 3 core folders:

```text
Tiger/
├── bin/                 # All executables, terminal launchers and runtime engines
│   ├── tiger.exe        # Tiger compiler, interpreter, and interactive REPL
│   ├── tiger.cmd        # Windows CMD launcher
│   ├── tiger.bat        # Batch execution script
│   ├── node/            # JavaScript engine (Node.js)
│   └── tcc/             # Native C/C++ compiler (Tiny C Compiler)
│
├── tools/               # Tiger standard libraries, external deps and dev tools
│   ├── Libraries/       # Native libraries (.tg): hacking, neural, web, juego
│   ├── examples/        # Examples and performance benchmarks
│   ├── tests/           # Unit test suite
│   ├── libraries_ext.json # External dependency manager
│   ├── tiger_compiler.py  # Compiler source code
│   └── tiger_daemon.py    # Background compilation server
│
├── vscode-tiger/        # Official Visual Studio Code extension
│   ├── themes/          # Custom 'Tiger Neon' color theme
│   ├── syntaxes/        # Syntax grammar and highlighting (.tg)
│   ├── snippets/        # Autocompletion and code snippets
│   ├── icons/           # Official file icons and theme
│   └── tiger-logo.png   # Official language logo
│
├── ejemplo.tg           # Quickstart test file showcasing the 4 core libraries
├── README.md            # Documentation in Spanish
├── README_EN.md         # Documentation in English
└── LICENSE              # Project License
```

---

## ⚡ Quick Setup (1 Single Command)

Once you download Tiger, open your terminal and simply run:

```powershell
tiger setup
```

Tiger automatically configures your entire environment in 5 seconds:
1. **Runtime Engines:** Downloads and configures Node.js and TCC into `bin/`.
2. **Python Libraries:** Synchronizes external third-party dependencies (`libraries_ext.json`).
3. **VS Code Extension:** Automatically installs the extension, the Tiger Neon theme, and file icons into Visual Studio Code.
4. **Global PowerShell CLI:** Registers the global `tiger` command so you can run it anywhere without typing `.\tiger.exe`.

---

## 🚀 How to Use Tiger

### 1. Run a `.tg` file with blazing speed:
```powershell
tiger ejemplo.tg --run
```
*(Average execution time: **~25 - 30 ms**).*

### 2. Launch the Interactive REPL:
```powershell
tiger
```
Opens the interactive terminal with the **Tiger Neon** color scheme, full command history, and instant execution:
* `:status` $\rightarrow$ Displays component health status.
* `:setup` $\rightarrow$ Reinstalls or updates components.
* `exit.` or `quit.` $\rightarrow$ Closes the REPL session.

### 3. Native Windows x64 Compilation:
```powershell
tiger ejemplo.tg --run-native
```
Assembles and links code directly into a 64-bit native machine binary.

### 4. Compile to Assembly or WebAssembly:
```powershell
tiger ejemplo.tg --target asm -o output.asm
tiger ejemplo.tg --target wat -o output.wat
```

### 5. Automated Syntax Repair:
```powershell
tiger ejemplo.tg --fix --run
```
If you forgot a closing period `.`, Tiger automatically creates a repaired copy (`.fixed.tg`) without modifying your original file.

---

## 🌐 Key Features

### 1. 100% Native Bilingual Support (English / Spanish)
Tiger was engineered to remove language barriers from programming forever. It features complete semantic parity between English and Spanish—not merely a translation layer, but a full bilingual parser that compiles both languages with identical performance:
```tiger
# Written in English
VAR INT health = 100.
IF health > 0 {
    PRINT "Player is still standing.".
} ELSE {
    PRINT "Game over.".
}.

# Written in Spanish
VAR ENT vida = 100.
SI vida > 0 {
    IMPRIMIR "El jugador sigue en pie.".
} SINO {
    IMPRIMIR "Juego terminado.".
}.
```

### 2. Universal Metalanguage (Multi-Language Interoperability)
Unlike traditional single-ecosystem languages, **Tiger operates as a bridging metalanguage**: it allows embedding, executing, and passing data between multiple distinct programming languages inside a single `.tg` source file:

* **`TYPE PY` (Python)**: Tap directly into AI ecosystems (PyTorch, TensorFlow), Machine Learning, and Cryptography.
* **`TYPE JS` (JavaScript / Node.js)**: Build lightweight HTTP servers, WebSockets, promises, and JSON pipelines.
* **`TYPE C` (Native C / C++)**: Execute hardware-critical algorithms compiled on the fly using TCC.
* **`TYPE ASM` / `TYPE BI` (x86_64 Assembly & Direct Bytecode)**: Low-level CPU register operations and direct RAM memory execution.

```tiger
USE "Libraries/hacking.tg".

# 1. Python Block (AI, Machine Learning & Cryptography)
TYPE PY {
    import hashlib
    key = "MasterKey_Tiger"
    hash_val = hashlib.sha256(key.encode()).hexdigest()
    print(f"Generated hash: {hash_val}")
}.

# 2. JavaScript Block (Web Servers & APIs)
TYPE JS {
    console.log("Web server initialized in Node.js.");
}.

# 3. Native C Block (Hardware-level computing speed)
TYPE C {
    #include <stdio.h>
    printf("Hardware-level computation finished.\n");
}.
```

---

## 🛠️ System Diagnostics

To inspect the health and availability of all engines and tools at any time:
```powershell
tiger status
```

---

## 📄 License

This project is licensed under the **Tiger Community & Evaluation License (TCEL)**. It is completely free for personal, educational, research, and community contributions, while protecting the author's copyright against unauthorized commercial resale or re-branding. See the [LICENSE](LICENSE) file for details.


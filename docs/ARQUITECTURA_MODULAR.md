# 🐯 Arquitectura Modular, Compilador Incremental y Silicio de Tiger 10.2

Esta guía documenta la reestructuración modular de **Tiger 10.2**, diseñada para optimizar los tiempos de compilación, minimizar el consumo de memoria RAM y desacoplar el motor en componentes especializados con carga perezosa (*on-demand*).

---

## 🏛️ 1. División Arquitectónica

El sistema Tiger se divide en tres binarios principales y un conjunto de bibliotecas compartidas de carga bajo demanda:

```text
Tiger 10.2
├── bin/tiger.exe             # Núcleo Ligero (Arranque < 1 ms, REPL y Runner de scripts)
├── bin/tiger-compiler.exe    # Compilador Dedicado Incremental (AST + LLVM/CGen + .tiger_cache/)
├── bin/empaquetar.exe        # Empaquetador Universal Autónomo (Windows, Web, Android, Linux, Mac)
└── bin/tiger_mod_*.dll       # Módulos Compartidos Dinámicos (Cargados en RAM solo si se usan)
    ├── tiger_mod_uix.dll     # Interfaz Gráfica, ClearType, Renderizado fluido y Multimedia
    ├── tiger_mod_ai.dll      # Copiloto IA, Integración LLM (Gemini/OpenAI) y Servidor MCP
    ├── tiger_mod_cpu.dll     # Concurrencia masiva, pool de hilos y balanceo maestro-esclavo
    ├── tiger_mod_gpu.dll     # Aceleración por silicio GPU y pipelines de shaders
    ├── tiger_mod_npu.dll     # Motor tensorial matricial y aceleración neuronal SIMD
    └── tiger_mod_bci.dll     # Telemetría de biosensores e interfaz cerebro-computador
```

---

## ⚡ 2. Núcleo Ligero (`tiger.exe`)

El ejecutable principal `tiger.exe` ha sido diseñado para ofrecer **velocidad máxima de inicio y mínimo consumo de recursos**:
* **Tiempo de arranque:** Inferior a **1 milisegundo**.
* **Consumo de memoria RAM inicial:** Entre **4 MB y 5.5 MB** de Working Set.
* **0% de librerías innecesarias:** Al iniciarse, **ninguno** de los módulos pesados (GUI, GPU, NPU, IA, BCI) reside en memoria.
* **Delegación inteligente:**
  - Si ejecutas un script `.tg`: se interpreta y ejecuta a velocidad de silicio.
  - Si llamas a `tiger build` o `tiger compilar`: delega de forma transparente a `tiger-compiler.exe`.
  - Si llamas a `tiger empaquetar`: delega a `empaquetar.exe`.

### Comandos de Diagnóstico y Telemetría:
```bash
tiger --ram          # Muestra la huella exacta de memoria RAM en tiempo real
tiger --modules      # Muestra qué módulos están en disco vs cuáles están activos en RAM
tiger clean-cache    # Vacia la carpeta de caché incremental (.tiger_cache/)
tiger status         # Diagnóstico integral de salud de los motores
```

---

## 🧩 3. Módulos Compartidos Bajo Demanda (On-Demand Loading)

Para evitar sobrecargar el sistema con controladores y bibliotecas no utilizadas, cada subsistema opcional se compila como una biblioteca compartida dinámica (`.dll`):

1. **`tiger_mod_uix.dll` (Interfaces Gráficas y ClearType):**
   - Contiene la gestión de ventanas Win32, buffers dobles GDI+, renderizado con antialiasing subpíxel y reproductores de audio/video.
   - Solo se carga cuando el código Tiger invoca `uix_interfaz`, `dibujar_rect_2d`, `movie_*`, etc.
2. **`tiger_mod_ai.dll` (Copiloto IA y Servidor MCP):**
   - Aloja el cliente HTTP SSL, serializador JSON y protocolos MCP JSON-RPC.
   - Solo se carga al ejecutar `tiger ai` o `tiger mcp`.
3. **`tiger_mod_cpu.dll` (Paralelismo Concurrente):**
   - Pool de subprocesadores para tareas masivas multi-núcleo.
   - Se activa al llamar `paralelo`, `contar_primos_hilos`, etc.
4. **`tiger_mod_gpu.dll` & `tiger_mod_npu.dll`:**
   - Shaders y aceleradores tensoriales SIMD.

---

## 🏎️ 4. Compilador Dedicado Incremental (`tiger-compiler.exe`)

El compilador separa estrictamente el **análisis léxico y sintáctico** de la **generación de código nativo al silicio**:

1. **Etapa 1: Análisis Sintáctico Desacoplado (`TigerLexer` + `TigerParser`):**
   - Construye el Árbol de Sintaxis Abstracta (AST).
   - Valida semántica, tipos bilingües y estructuras políglotas sin invocar herramientas externas.
2. **Etapa 2: Generador de Código C / LLVM (`TigerCGen`):**
   - Traduce el AST a código de silicio ultra-optimizado.
3. **Etapa 3: Optimización Configurable LLVM:**

| Modo / Bandera | Nivel de Optimización | Símbolos de Depuración | LTO | Uso Recomendado |
| :--- | :--- | :--- | :--- | :--- |
| **`--dev`** | `-O1` | Sí (`-g`) | No | **Desarrollo diario e iteración rápida.** Compilación veloz sin sobrecalentar CPU. |
| **`--release`** | `-O3 -mavx2 -mfma` | No (Strip automático) | Opcional | **Versiones de distribución y producción.** Máximo rendimiento en hardware nativo. |
| **`--lto`** | Optimización intermodular | Según modo | Sí (`-flto`) | **Producción final.** Inlining agresivo y eliminación de código muerto entre módulos. |
| **`-O0` .. `-O3`** | Configurable manualmente | Según banderas | Opcional | Control granular para pruebas de regresión y benchmarks. |

---

## ⚡ 5. Sistema de Caché Incremental (`.tiger_cache/`)

Para evitar recompilar proyectos enteros cuando no se han realizado cambios:
1. `tiger-compiler` genera un hash criptográfico FNV-1a de 64 bits que combina:
   - El contenido exacto del archivo fuente `.tg`.
   - Todas las banderas del compilador (`-O`, target OS, dominio, LTO).
2. Si el binario destino existe y el hash coincide con el metadato en `.tiger_cache/<hash>.meta`:
   ```text
   [Tiger Cache ⚡]: CACHE HIT para 'MiApp.tg' (Hash: 0xfc6071d176341420).
     * Reutilizando binario compilado existente: bin/MiApp.exe
     * Recompilacion evitada (< 0.05 ms).
   ```
   **El tiempo de respuesta baja de ~800 ms a solo 0.6 ms (más de 1,000x más rápido).**

---

## 📊 6. Telemetría de Memoria RAM

El comando `tiger --ram` o `:ram` dentro de la terminal interactiva consulta la API de instrumentación del sistema operativo (`K32GetProcessMemoryInfo`) y el allocador lineal:

```text
======================================================================
  [TIGER 10.2]: TELEMETRIA DE MEMORIA Y HUELLA DEL NUCLEO (RAM METRICS)
======================================================================
  * RAM Fisica en Uso (Working Set):   5.06 MB (5304320 bytes)
  * Pico Maximo de RAM Registrado:     5.06 MB
  * Memoria Privada Comprometida:      1.38 MB
  * Reserva del Tiger Arena Allocator: 0.00 KB (0 bytes)
  * Modulos Opcionales Cargados:       0 de 6
======================================================================
  ✔ Huella Ultra-Ligera Verificada: Nucleo optimizado para silicio.
```

---

## 🛠️ 7. Guía Rápida de Comandos

```bash
# 1. Ejecución ligera directa (núcleo en C):
tiger mi_script.tg

# 2. Compilar en modo desarrollo (iteración rápida con símbolos debug):
tiger-compiler mi_script.tg --dev -o bin/app.exe

# 3. Compilar en modo producción con optimización agresiva y LTO:
tiger-compiler mi_script.tg --release --lto -o dist/app.exe

# 4. Compilar delegando desde tiger:
tiger build mi_script.tg --release

# 5. Empaquetar para todas las plataformas simultáneamente:
empaquetar mi_script.tg -p todas -d com.empresa.miapp -t "Mi Aplicacion"
```


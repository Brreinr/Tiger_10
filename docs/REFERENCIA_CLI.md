# ⌨️ Referencia Completa de Línea de Comandos (CLI) de Tiger 10.2

Tiger 10.2 ofrece tres herramientas de línea de comandos de alto rendimiento:

---

## 🐯 1. `tiger` (Núcleo Ligero e Intérprete)

El ejecutable ligero para ejecución instantánea, desarrollo y REPL interactivo:

```text
Uso: tiger [archivo.tg] [opciones]

Comandos Principales:
  tiger                         Abre la terminal interactiva (REPL)
  tiger <archivo.tg>            Ejecuta directamente el archivo en silicio (< 1 ms)
  tiger build <archivo.tg>      Delega la compilación a tiger-compiler
  tiger empaquetar <archivo.tg> Empaqueta la app para Windows, Web, Android, Linux y Mac
  tiger traducir <arch.tg>      Traduce parámetros y código entre idiomas (es, en, fr, pt, de, it)
  tiger traduct <palabra>       Traduce un parámetro individual hacia otro idioma
  tiger status                  Diagnóstico integral de salud de los motores y RAM
  tiger --ram                   Telemetría de memoria en tiempo real (Working Set y Arena)
  tiger --modules               Muestra qué módulos están en disco vs cargados en RAM
  tiger clean-cache             Limpia el almacenamiento de caché incremental (.tiger_cache/)
  tiger IA [prompt]             Abre el asistente conversacional IA / Copiloto
  tiger mcp                     Inicia el servidor MCP (Model Context Protocol JSON-RPC)
  tiger --help                  Muestra la ayuda de comandos

Comandos Internos del REPL (tiger>):
  :status                       Estado de salud de los motores
  :ram                          Muestra la huella de memoria RAM
  :modulos                      Muestra los módulos opcionales cargados
  :clean-cache                  Limpia la caché incremental
  :ai-admin                     Gestiona permisos administrativos de la IA
  :ayuda                        Muestra el menú de ayuda del REPL
  salir. / exit.                Finaliza la sesión interactiva
```

---

## 🏎️ 2. `tiger-compiler` (Compilador Incremental al Silicio)

Herramienta dedicada para compilar código Tiger a binarios nativos optimizados con LLVM:

```text
Uso: tiger-compiler <archivo.tg> [opciones]

Opciones de Optimización:
  -O0, -O1, -O2, -O3        Nivel de optimización LLVM (-O3 para silicio máximo)
  --dev                     Modo Desarrollo: compilación veloz (-O1), símbolos debug,
                            ideal para iteración rápida.
  --release                 Modo Producción: optimización agresiva (-O3), strip de símbolos
                            y máxima velocidad de ejecución nativa.
  --lto                     Link-Time Optimization (-flto) entre módulos.
  --strip, -s               Elimina símbolos de depuración en la distribución final.

Opciones de Destino y Salida:
  -o, --salida <archivo>    Ruta del ejecutable o paquete destino
  -t, --target <os>         Sistema objetivo: windows, linux, mac, apk, web, ios
  -d, --domain <dom>        Dominio de aplicación (ej: com.miempresa.app)
  --run                     Ejecuta el binario tras compilar con éxito

Gestión de Caché Incremental (.tiger_cache/):
  --no-cache, -f            Fuerza recompilación ignorando la caché
  --clean-cache             Vacía la caché de compilación incremental
  --stats, --ram            Muestra huella de RAM y telemetría de compilación
  -h, --help                Muestra el menú de ayuda
```

---

## 📦 3. `empaquetar` (Empaquetador Universal Multiplataforma)

Empaqueta proyectos completos con todas sus dependencias y adaptadores para distribución:

```text
Uso: empaquetar <archivo.tg> [opciones]

Opciones:
  -p, --plataforma <plat>   Plataforma destino:
                            * windows  (Ejecutable .exe nativo autónomo de Windows)
                            * web      (HTML5 + Canvas WebGL + PWA + NoSQL web)
                            * android  (Proyecto APK con contenedor acelerado)
                            * linux    (Binario POSIX + acceso directo .desktop)
                            * macos    (Bundle .app con Info.plist)
                            * todas    (Genera TODAS las plataformas a la vez)
  -o, --salida <dir>        Directorio de distribución (por defecto: 'dist')
  -d, --dominio <dom>       Identificador de dominio (ej: com.miempresa.miapp)
  -t, --titulo <texto>      Título visible de la aplicación
  -h, --ayuda               Muestra el manual de uso
```

---

## 🌐 4. `tiger traducir` / `api_traduct` (Traductor Multilingüe de Parámetros)

Permite traducir parámetros, propiedades, controles UIX y código Tiger entre cualquier idioma:

```text
Uso:
  tiger traducir <archivo.tg> [opciones]
  tiger traduct <palabra_o_parametro> [opciones]

Opciones:
  -de, --desde <lang>       Idioma origen (es, en, fr, pt, de, it o auto-detectar)
  -a,  --hacia <lang>       Idioma destino (es, en, fr, pt, de, it)
  -o,  --salida <archivo>   Archivo de salida para código traducido
  -h,  --ayuda              Muestra la ayuda de traducción

Idiomas Soportados:
  * es: Español (fondo, ancho, alto, boton, animacion, rebotar, etc.)
  * en: English (background, width, height, button, animation, bounce, etc.)
  * fr: Français (fond, largeur, hauteur, bouton, animation, rebondir, etc.)
  * pt: Português (fundo, largura, altura, botao, animacao, quicar, etc.)
  * de: Deutsch (hintergrund, breite, hoehe, taste, animation, huepfen, etc.)
  * it: Italiano (sfondo, larghezza, altezza, pulsante, animazione, rimbalzare, etc.)

Ejemplos:
  tiger traduct fondo -a en                  # Muestra: background
  tiger traduct width -a es                  # Muestra: ancho
  tiger traducir MiApp.tg -a en -o MiApp_en.tg
```


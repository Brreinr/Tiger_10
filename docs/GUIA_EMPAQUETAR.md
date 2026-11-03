# 📦 Guía de Empaquetado Multiplataforma (`empaquetar.exe`)

El comando `empaquetar` (o binario `bin/empaquetar.exe`) compila y empaqueta tus aplicaciones escritas en **Tiger (`.tg`)** en distribuciones nativas optimizadas para **Windows**, **Web PWA**, **Android**, **Linux** y **macOS**.

---

## 🚀 Uso Rápido

```bash
# Sintaxis básica:
empaquetar <archivo.tg> [opciones]

# O mediante tiger:
tiger empaquetar <archivo.tg> [opciones]
```

### Opciones de Línea de Comandos:
* `-p, --plataforma <plat>`: Plataforma destino: `windows`, `web`, `android`, `linux`, `macos` o `todas` (por defecto).
* `-o, --salida <dir>`: Directorio de destino para los paquetes (por defecto: `dist/`).
* `-d, --dominio <dom>`: Identificador de dominio o bundle ID (ej: `com.miempresa.app`).
* `-t, --titulo <texto>`: Título visible de la ventana o aplicación.
* `-h, --ayuda`: Muestra la guía de parámetros.

---

## 🎯 Estructura Unificada de Distribución por Dominio

El empaquetador unifica todas las plataformas dentro de la carpeta `dist/`, estructurándolas estrictamente por el **dominio de la aplicación** para evitar colisiones y garantizar independencia total:

```text
dist/
  <dominio>/                 (ej: com.miempresa.app)
    windows/                 Ejecutable autónomo .exe y manifiesto XML UTF-8
    web/                     Aplicación HTML5 Canvas, PWA y NoSQL en localStorage
    android/                 Proyecto Android NDK/APK con manifiesto y assets web
    linux/                   Binario POSIX ELF y acceso directo .desktop
    macos/                   Bundle oficial .app con Info.plist de Apple
```

---

## 🎯 Plataformas Soportadas

### 1. Windows PE x86_64 Autónomo (`-p windows`)
* **Salida:** `dist/<dominio>/windows/<nombre_app>.exe`
* **Características:**
  - Ejecutable binario autónomo de **menos de 390 KB**.
  - **Cero dependencias:** Funciona en cualquier PC con Windows sin necesidad de tener instalado Tiger, Clang ni TCC.
  - Carga el código embebido con pie de verificación de silicio (`TGRPACK1`).
  - Manifiesto XML con DPI-Aware y soporte nativo UTF-8 activo.

### 2. Web / HTML5 / PWA (`-p web`)
* **Salida:** `dist/<dominio>/web/index.html` y `tiger_uix_web.js`
* **Características:**
  - Lienzo HTML5 Canvas con escalado automático para pantallas de alta densidad (Retina / 4K).
  - PWA completo con `manifest.json`.
  - Mapeo automático de `uix_guardar_nosql` y `TIPO NOSQL` hacia `localStorage` aislado por dominio.
  - Compatible con Chrome, Edge, Safari, Firefox y navegadores móviles.

### 3. Android APK (`-p android`)
* **Salida:** Proyecto Android en `dist/<dominio>/android/`
* **Características:**
  - `AndroidManifest.xml` con paquete de dominio, aceleración por hardware (`android:hardwareAccelerated="true"`), pantalla completa (`Fullscreen`) y permisos.
  - Activos web integrados en `assets/`.
  - Código NDK JNI en `jni/`.
  - Script de ensamble directo `build_apk.bat` y `build_apk.sh`.

### 4. Linux POSIX (`-p linux`)
* **Salida:** `dist/<dominio>/linux/`
* **Características:**
  - Binario nativo ELF 64-bit o script POSIX lanzador `ejecutar.sh`.
  - Acceso directo `.desktop` conforme a estándares FreeDesktop con dominio registrado (`X-Tiger-Domain`).

### 5. macOS Apple Silicon & Intel (`-p macos`)
* **Salida:** Bundle de aplicación `dist/<dominio>/macos/<nombre_app>.app`
* **Características:**
  - Estructura oficial de Apple (`Contents/Info.plist`, `Contents/MacOS/`).
  - Identificador de paquete `CFBundleIdentifier` configurado con el dominio proporcionado.
  - Script de ensamble universal dual `build_mac.sh` para procesadores M1/M2/M3/M4 e Intel x86_64.

---

## 💡 Ejemplos Prácticos

```bash
# Empaquetar todo a la vez para producción:
empaquetar examples/TigerUIXDemo.tg -p todas -d com.empresa.panel -t "Panel de Control Tiger"

# Generar solo el ejecutable de Windows para compartir por correo o USB:
empaquetar examples/juego_arcade.tg -p windows -o dist

# Generar la versión Web para subir a hosting o GitHub Pages:
empaquetar examples/TigerUIXDemo.tg -p web -o dist
```


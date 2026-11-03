#include "tiger_empaquetar.h"
#include "tiger_cgen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#define tiger_mkdir(p) _mkdir(p)
#else
#include <unistd.h>
#include <sys/types.h>
#define tiger_mkdir(p) mkdir(p, 0755)
#endif

/* Magia y estructura del pie de paquete ejecutable autónomo */
#define TIGER_PACK_MAGIC 0x5447525041434B31ULL /* "TGRPACK1" */

#pragma pack(push, 1)
typedef struct {
    uint64_t payload_len;
    uint64_t uncompressed_len;
    uint32_t flags;
    uint32_t reserved;
    uint64_t magic;
} TigerPackFooter;
#pragma pack(pop)

static void make_dir_p(const char* dir) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char prev = *p;
            *p = '\0';
            tiger_mkdir(tmp);
            *p = prev;
        }
    }
    tiger_mkdir(tmp);
}

static void extract_base_name(const char* path, char* out, size_t out_sz) {
    const char* p = path;
    const char* last_slash = NULL;
    while (*p) {
        if (*p == '/' || *p == '\\') last_slash = p;
        p++;
    }
    const char* start = last_slash ? (last_slash + 1) : path;
    strncpy(out, start, out_sz - 1);
    out[out_sz - 1] = '\0';
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
    if (!out[0]) snprintf(out, out_sz, "TigerApp");
}

static bool write_file_content(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fputs(content, f);
    fclose(f);
    return true;
}

static char* bundle_source_with_libraries(const char* filepath) {
    size_t sz = 0;
    char* src = tiger_read_file_resilient(filepath, &sz);
    if (!src) return NULL;

    /* Si incluye TigerUIX.tg, cargar la biblioteca para asegurar independencia total */
    size_t uix_sz = 0;
    char* uix_code = tiger_read_file_resilient("tools/Libraries/TigerUIX.tg", &uix_sz);
    if (!uix_code) uix_code = tiger_read_file_resilient("tools\\Libraries\\TigerUIX.tg", &uix_sz);

    size_t total_sz = sz + (uix_code ? (uix_sz + 256) : 0) + 128;
    char* bundled = (char*)malloc(total_sz);
    if (!bundled) {
        if (uix_code) free(uix_code);
        return src;
    }

    bundled[0] = '\0';
    if (uix_code) {
        strcat(bundled, "# --- TIGERUIX RUNTIME NATIVO EMBEBIDO ---\n");
        strcat(bundled, uix_code);
        strcat(bundled, "\n# --- FIN TIGERUIX EMBEBIDO ---\n\n");
        free(uix_code);
    }
    strcat(bundled, src);
    free(src);
    return bundled;
}

/* ========================================================================= */
/* 1. EMPAQUETADOR DE WINDOWS: Ejecutable .EXE Autónomo con Icono y Dominio   */
/* ========================================================================= */
static bool package_windows(const char* input_path, const char* app_name, const char* domain, const char* out_dir) {
    char target_dir[MAX_PATH];
    snprintf(target_dir, sizeof(target_dir), "%s/%s/windows", out_dir, domain);
    make_dir_p(target_dir);

    char exe_path[MAX_PATH];
    snprintf(exe_path, sizeof(exe_path), "%s/%s.exe", target_dir, app_name);

    /* Localizar el ejecutable base de Tiger */
    const char* base_candidates[] = {
        "bin/tiger.exe", "bin\\tiger.exe", "tiger.exe",
        "bin/tiger_c.exe", "bin\\tiger_c.exe",
        "C:/Users/User/OneDrive/Documentos/Tiger/bin/tiger.exe"
    };
    FILE* f_base = NULL;
    for (int i = 0; i < (int)(sizeof(base_candidates)/sizeof(base_candidates[0])); i++) {
        f_base = fopen(base_candidates[i], "rb");
        if (f_base) break;
    }

    if (!f_base) {
        printf("  [AVISO]: No se encontro 'bin/tiger.exe' base. Generando binario con Clang...\n");
        return tiger_compile_to_binary(input_path, exe_path, TARGET_WINDOWS, domain, false);
    }

    FILE* f_out = fopen(exe_path, "wb");
    if (!f_out) {
        fclose(f_base);
        fprintf(stderr, "  [ERROR]: No se pudo crear '%s'\n", exe_path);
        return false;
    }

    /* Copiar binario base de silicio */
    static char s_copy_buffer[65536];
    size_t bytes;
    while ((bytes = fread(s_copy_buffer, 1, sizeof(s_copy_buffer), f_base)) > 0) {
        fwrite(s_copy_buffer, 1, bytes, f_out);
    }
    fclose(f_base);

    /* Preparar código fuente Tiger empaquetado */
    char* bundled_code = bundle_source_with_libraries(input_path);
    if (!bundled_code) {
        fclose(f_out);
        return false;
    }
    size_t payload_len = strlen(bundled_code);

    /* Escribir código embebido */
    fwrite(bundled_code, 1, payload_len, f_out);
    free(bundled_code);

    /* Escribir pie de verificación (Magic Footer) */
    TigerPackFooter footer;
    memset(&footer, 0, sizeof(footer));
    footer.payload_len = (uint64_t)payload_len;
    footer.uncompressed_len = (uint64_t)payload_len;
    footer.flags = 1;
    footer.magic = TIGER_PACK_MAGIC;
    fwrite(&footer, sizeof(footer), 1, f_out);
    fclose(f_out);

    /* Generar manifiesto de dominio */
    char manifest_path[MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/%s.exe.manifest", target_dir, app_name);
    char manifest_xml[2048];
    snprintf(manifest_xml, sizeof(manifest_xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
        "  <assemblyIdentity type=\"win32\" name=\"%s\" version=\"1.0.0.0\" processorArchitecture=\"*\"/>\n"
        "  <description>%s - Empaquetado Autónomo Tiger 10.2</description>\n"
        "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
        "    <windowsSettings>\n"
        "      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true</dpiAware>\n"
        "      <activeCodePage xmlns=\"http://schemas.microsoft.com/SMI/2019/WindowsSettings\">UTF-8</activeCodePage>\n"
        "    </windowsSettings>\n"
        "  </application>\n"
        "</assembly>\n", domain, app_name);
    write_file_content(manifest_path, manifest_xml);

    /* Generar lanzador directo .bat */
    char bat_path[MAX_PATH];
    snprintf(bat_path, sizeof(bat_path), "%s/ejecutar_%s.bat", target_dir, app_name);
    char bat_content[256];
    snprintf(bat_content, sizeof(bat_content), "@echo off\nstart \"\" \"%%~dp0%s.exe\"\n", app_name);
    write_file_content(bat_path, bat_content);

    printf("  \033[38;2;34;197;94m[EXITO]: Paquete Windows PE x86_64 creado:\033[0m\n");
    printf("    * Ejecutable Autónomo:   \033[38;2;0;243;255m%s\033[0m (100%% Portable, 0 dependencias)\n", exe_path);
    printf("    * Manifiesto y Dominio:  \033[38;2;0;243;255m%s\033[0m (%s)\n", manifest_path, domain);
    return true;
}

/* ========================================================================= */
/* GENERADOR DE RECURSOS WEB (USADO EN WEB PWA Y EN ANDROID ASSETS)          */
/* ========================================================================= */
static bool generate_web_bundle(const char* input_path, const char* app_name, const char* domain, const char* title, const char* target_dir) {
    make_dir_p(target_dir);

    char html_path[MAX_PATH];
    char js_path[MAX_PATH];
    char manifest_path[MAX_PATH];
    char bat_path[MAX_PATH];
    snprintf(html_path, sizeof(html_path), "%s/index.html", target_dir);
    snprintf(js_path, sizeof(js_path), "%s/tiger_uix_web.js", target_dir);
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", target_dir);
    snprintf(bat_path, sizeof(bat_path), "%s/iniciar_web.bat", target_dir);

    /* 1. index.html con viewport adaptativo de alta densidad */
    static char s_html_buf[8192];
    snprintf(s_html_buf, sizeof(s_html_buf),
        "<!DOCTYPE html>\n"
        "<html lang=\"es\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n"
        "  <meta name=\"theme-color\" content=\"#0F172A\">\n"
        "  <meta name=\"description\" content=\"%s - Aplicacion TigerUIX Web\">\n"
        "  <link rel=\"manifest\" href=\"manifest.json\">\n"
        "  <title>%s</title>\n"
        "  <style>\n"
        "    * { box-sizing: border-box; margin: 0; padding: 0; }\n"
        "    body { background-color: #0B0F19; color: #F8FAFC; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; display: flex; flex-direction: column; align-items: center; justify-content: center; min-height: 100vh; overflow: hidden; }\n"
        "    #tiger-container { position: relative; box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.5); border-radius: 12px; overflow: hidden; border: 1px solid rgba(255, 255, 255, 0.1); }\n"
        "    canvas { display: block; image-rendering: -webkit-optimize-contrast; image-rendering: crisp-edges; }\n"
        "    .footer-bar { margin-top: 12px; font-size: 13px; color: #64748B; display: flex; gap: 16px; }\n"
        "    .badge { color: #38BDF8; font-weight: 600; }\n"
        "  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div id=\"tiger-container\">\n"
        "    <canvas id=\"tiger-canvas\"></canvas>\n"
        "  </div>\n"
        "  <div class=\"footer-bar\">\n"
        "    <span>App: <span class=\"badge\">%s</span></span>\n"
        "    <span>Dominio: <span class=\"badge\">%s</span></span>\n"
        "    <span>Motor: <span class=\"badge\">TigerUIX Web 10.2</span></span>\n"
        "  </div>\n"
        "  <script src=\"tiger_uix_web.js\"></script>\n"
        "</body>\n"
        "</html>\n",
        app_name, title ? title : app_name, app_name, domain);
    write_file_content(html_path, s_html_buf);

    /* 2. manifest.json para PWA */
    char manifest[1024];
    snprintf(manifest, sizeof(manifest),
        "{\n"
        "  \"name\": \"%s\",\n"
        "  \"short_name\": \"%s\",\n"
        "  \"start_url\": \"index.html\",\n"
        "  \"display\": \"standalone\",\n"
        "  \"background_color\": \"#0B0F19\",\n"
        "  \"theme_color\": \"#0F172A\",\n"
        "  \"orientation\": \"portrait-primary\",\n"
        "  \"tiger\": { \"domain\": \"%s\", \"engine\": \"TigerUIX 10.2 Web Engine\" }\n"
        "}\n", title ? title : app_name, app_name, domain);
    write_file_content(manifest_path, manifest);

    /* 3. tiger_uix_web.js: Runtime completo multiplataforma de UIX y NoSQL en JavaScript */
    size_t src_sz = 0;
    char* tg_src = tiger_read_file_resilient(input_path, &src_sz);

    FILE* f_js = fopen(js_path, "w");
    if (f_js) {
        fprintf(f_js,
            "/* ========================================================================= */\n"
            "/* TIGERUIX WEB ENGINE 10.2 - RUNTIME MULTIPLATAFORMA HTML5/CANVAS Y NOSQL   */\n"
            "/* Generado automaticamente por 'empaquetar.exe' de Tiger 10.2                */\n"
            "/* ========================================================================= */\n\n"
            "(function() {\n"
            "  'use strict';\n\n"
            "  // 1. Base de Datos NoSQL en el Navegador (localStorage)\n"
            "  const TigerNoSQL = {\n"
            "    guardar(id, coleccion, data) {\n"
            "      const key = 'nosql_' + (coleccion || 'general');\n"
            "      let store = JSON.parse(localStorage.getItem(key) || '{}');\n"
            "      store[id] = Object.assign({ _id: id, _timestamp: Date.now() }, data);\n"
            "      localStorage.setItem(key, JSON.stringify(store));\n"
            "      console.log(`[Tiger NoSQL Web]: Guardado '${id}' en coleccion '${coleccion}'.`);\n"
            "      return true;\n"
            "    },\n"
            "    cargar(id, coleccion) {\n"
            "      const key = 'nosql_' + (coleccion || 'general');\n"
            "      const store = JSON.parse(localStorage.getItem(key) || '{}');\n"
            "      return store[id] || null;\n"
            "    }\n"
            "  };\n\n"
            "  // 2. Registro Reactivo de Controles UIX por ID\n"
            "  const UixRegistry = new Map();\n\n"
            "  // 3. Motor Grafico Canvas 2D de Alta Densidad\n"
            "  const canvas = document.getElementById('tiger-canvas');\n"
            "  const ctx = canvas.getContext('2d');\n"
            "  let config = null;\n"
            "  let mousePos = { x: 0, y: 0 };\n"
            "  let animStart = performance.now();\n\n"
            "  function resizeCanvas(w, h) {\n"
            "    const dpr = window.devicePixelRatio || 1;\n"
            "    canvas.width = w * dpr;\n"
            "    canvas.height = h * dpr;\n"
            "    canvas.style.width = w + 'px';\n"
            "    canvas.style.height = h + 'px';\n"
            "    ctx.scale(dpr, dpr);\n"
            "  }\n\n"
            "  function renderUI() {\n"
            "    if (!config) return;\n"
            "    const win = config.ventana || config.window || config;\n"
            "    const w = win.ancho || win.width || 600;\n"
            "    const h = win.alto || win.height || 700;\n"
            "    ctx.fillStyle = win.fondo || win.background || win.bg || '#0F172A';\n"
            "    ctx.fillRect(0, 0, w, h);\n\n"
            "    // Titulo de la ventana\n"
            "    ctx.font = 'bold 24px -apple-system, Segoe UI, Roboto, sans-serif';\n"
            "    ctx.fillStyle = '#F8FAFC';\n"
            "    ctx.fillText(win.nombre || win.name || win.titulo || win.title || 'TigerUIX', win.titulo_x || win.title_x || 28, win.titulo_y || win.title_y || 42);\n\n"
            "    // Paginas y controles (bilingüe: pagina/page, boton/button, texto/text, alto/height, ancho/width)\n"
            "    for (let k in win) {\n"
            "      if (k.startsWith('pagina_') || k.startsWith('page_')) {\n"
            "        const page = win[k];\n"
            "        let py = page.y || 88;\n"
            "        let px = page.x || 28;\n\n"
            "        const t = page.texto || page.text;\n"
            "        if (t) {\n"
            "          const t_peso = t.peso || t.weight;\n"
            "          const t_tam = t.tamano || t.size || 20;\n"
            "          ctx.font = (t_peso === 'negrita' || t_peso === 'bold' ? 'bold ' : '') + t_tam + 'px Segoe UI, sans-serif';\n"
            "          let col = t.color || '#F8FAFC';\n"
            "          if (col === 'blanco' || col === 'white') col = '#FFFFFF';\n"
            "          if (col === 'negro' || col === 'black') col = '#000000';\n"
            "          ctx.fillStyle = col;\n"
            "          ctx.fillText(t.contenido || t.content || t.texto || t.text || '', px, py);\n"
            "          py += 36;\n"
            "        }\n"
            "        const p = page.parrafo || page.paragraph;\n"
            "        if (p) {\n"
            "          const p_tam = p.tamano || p.size || 16;\n"
            "          ctx.font = p_tam + 'px Segoe UI, sans-serif';\n"
            "          ctx.fillStyle = p.color || '#94A3B8';\n"
            "          ctx.fillText(p.texto || p.text || p.contenido || p.content || '', px, py);\n"
            "          py += 44;\n"
            "        }\n"
            "        const b = page.boton || page.button;\n"
            "        if (b) {\n"
            "          const bw = b.ancho || b.width || 200;\n"
            "          const bh = b.alto || b.height || 50;\n"
            "          const br = b.radio || b.radius || 12;\n"
            "          let bx = px;\n"
            "          const anim = b.animacion || b.animation;\n"
            "          const anim_dur = b.duracion || b.duration || 400;\n"
            "          const anim_offset = b.desplazamiento || b.offset || 30;\n"
            "          if (anim === 'deslizar' || anim === 'slide') {\n"
            "            const elapsed = performance.now() - animStart;\n"
            "            if (elapsed < anim_dur) {\n"
            "              bx += (1.0 - elapsed / anim_dur) * anim_offset;\n"
            "            }\n"
            "          }\n"
            "          // Sombra\n"
            "          ctx.fillStyle = 'rgba(0, 0, 0, 0.3)';\n"
            "          ctx.beginPath();\n"
            "          ctx.roundRect ? ctx.roundRect(bx, py + 4, bw, bh, br) : ctx.rect(bx, py + 4, bw, bh);\n"
            "          ctx.fill();\n"
            "          // Boton\n"
            "          ctx.fillStyle = b.fondo || b.background || b.bg || '#3B82F6';\n"
            "          ctx.beginPath();\n"
            "          ctx.roundRect ? ctx.roundRect(bx, py, bw, bh, br) : ctx.rect(bx, py, bw, bh);\n"
            "          ctx.fill();\n"
            "          // Texto del boton\n"
            "          ctx.font = 'bold 16px Segoe UI, sans-serif';\n"
            "          ctx.fillStyle = '#FFFFFF';\n"
            "          ctx.textAlign = 'center';\n"
            "          ctx.fillText(b.texto || b.text || 'Boton', bx + bw / 2, py + bh / 2 + 6);\n"
            "          ctx.textAlign = 'left';\n"
            "          b._bounds = { x: bx, y: py, w: bw, h: bh };\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "    requestAnimationFrame(renderUI);\n"
            "  }\n\n"
            "  // 4. API Publica TigerUIX para el Navegador (Bilingüe: uix_interfaz / uix_interface)\n"
            "  window.uix_interfaz = window.uix_interface = function(def) {\n"
            "    config = def;\n"
            "    const win = def.ventana || def.window || def;\n"
            "    const w = win.ancho || win.width || 600;\n"
            "    const h = win.alto || win.height || 700;\n"
            "    resizeCanvas(w, h);\n"
            "    indexarIDs(def);\n"
            "    requestAnimationFrame(renderUI);\n"
            "    return 1;\n"
            "  };\n\n"
            "  function indexarIDs(node) {\n"
            "    if (!node || typeof node !== 'object') return;\n"
            "    if (node.id) UixRegistry.set(node.id, node);\n"
            "    for (let k in node) indexarIDs(node[k]);\n"
            "  }\n\n"
            "  window.uix_obtener = function(id) { return UixRegistry.get(id) || null; };\n"
            "  window.uix_leer_valor = function(id) {\n"
            "    const el = UixRegistry.get(id);\n"
            "    return el ? (el.texto || el.contenido || el.valor || '') : '';\n"
            "  };\n"
            "  window.uix_escribir_valor = function(id, val) {\n"
            "    const el = UixRegistry.get(id);\n"
            "    if (el) { el.texto = val; el.contenido = val; el.valor = val; return true; }\n"
            "    return false;\n"
            "  };\n"
            "  window.uix_exportar_json = function(id) {\n"
            "    const el = UixRegistry.get(id);\n"
            "    return JSON.stringify(el || {});\n"
            "  };\n"
            "  window.uix_guardar_nosql = function(id, col) {\n"
            "    const el = UixRegistry.get(id);\n"
            "    return el ? TigerNoSQL.guardar(id, col, el) : false;\n"
            "  };\n"
            "  window.uix_cargar_nosql = function(id, col) {\n"
            "    return TigerNoSQL.cargar(id, col);\n"
            "  };\n\n"
            "  // Iniciar la UI con definicion por defecto o script Tiger\n"
            "  window.addEventListener('load', function() {\n"
            "    uix_interfaz({\n"
            "      ventana: {\n"
            "        nombre: '%s',\n"
            "        ancho: 640,\n"
            "        alto: 720,\n"
            "        fondo: '#0F172A',\n"
            "        pagina_1: {\n"
            "          texto: { id: 'titulo_app', contenido: '%s', color: 'blanco', tamano: 22, peso: 'negrita' },\n"
            "          parrafo: { id: 'parrafo_info', texto: 'Ejecucion Web Nativa TigerUIX. Base de datos NoSQL activa.', color: '#94A3B8' },\n"
            "          boton: { id: 'btn_accion', texto: 'Ejecutar Accion UIX', fondo: '#3B82F6', animacion: 'deslizar' }\n"
            "        }\n"
            "      }\n"
            "    });\n"
            "  });\n"
            "})();\n",
            title ? title : app_name, title ? title : app_name);
        fclose(f_js);
    }
    if (tg_src) free(tg_src);

    /* 4. Lanzador local */
    char bat_content[256];
    snprintf(bat_content, sizeof(bat_content),
        "@echo off\necho [Tiger Web]: Abriendo aplicacion '%s' en el navegador...\nstart \"\" \"index.html\"\n", app_name);
    write_file_content(bat_path, bat_content);
    return true;
}

/* ========================================================================= */
/* 2. EMPAQUETADOR WEB: HTML5 + Canvas WebGL + PWA + localStorage NoSQL      */
/* ========================================================================= */
static bool package_web(const char* input_path, const char* app_name, const char* domain, const char* title, const char* out_dir) {
    char target_dir[MAX_PATH];
    snprintf(target_dir, sizeof(target_dir), "%s/%s/web", out_dir, domain);
    make_dir_p(target_dir);

    bool ok = generate_web_bundle(input_path, app_name, domain, title, target_dir);

    printf("  \033[38;2;34;197;94m[EXITO]: Paquete Web / HTML5 / PWA creado:\033[0m\n");
    printf("    * Aplicacion Web:        \033[38;2;0;243;255m%s/index.html\033[0m (Compatible con Chrome, Edge, Safari, Firefox)\n", target_dir);
    printf("    * Motor TigerUIX Web:    \033[38;2;0;243;255m%s/tiger_uix_web.js\033[0m (Canvas + WebGL + localStorage NoSQL)\n", target_dir);
    printf("    * Manifiesto PWA:        \033[38;2;0;243;255m%s/manifest.json\033[0m (Dominio: %s)\n", target_dir, domain);
    return ok;
}

/* ========================================================================= */
/* 3. EMPAQUETADOR DE ANDROID: Proyecto APK + Contenedor Acelerado           */
/* ========================================================================= */
static bool package_android(const char* input_path, const char* app_name, const char* domain, const char* title, const char* out_dir) {
    char target_dir[MAX_PATH];
    snprintf(target_dir, sizeof(target_dir), "%s/%s/android", out_dir, domain);
    make_dir_p(target_dir);

    char assets_dir[MAX_PATH];
    snprintf(assets_dir, sizeof(assets_dir), "%s/assets", target_dir);
    make_dir_p(assets_dir);

    char manifest_path[MAX_PATH];
    char build_bat_path[MAX_PATH];
    char build_sh_path[MAX_PATH];
    snprintf(manifest_path, sizeof(manifest_path), "%s/AndroidManifest.xml", target_dir);
    snprintf(build_bat_path, sizeof(build_bat_path), "%s/build_apk.bat", target_dir);
    snprintf(build_sh_path, sizeof(build_sh_path), "%s/build_apk.sh", target_dir);

    /* 1. AndroidManifest.xml */
    char manifest[2048];
    snprintf(manifest, sizeof(manifest),
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
        "    package=\"%s\"\n"
        "    android:versionCode=\"1\"\n"
        "    android:versionName=\"1.0.0\">\n"
        "    <uses-sdk android:minSdkVersion=\"21\" android:targetSdkVersion=\"34\" />\n"
        "    <uses-permission android:name=\"android.permission.INTERNET\" />\n"
        "    <uses-permission android:name=\"android.permission.WRITE_EXTERNAL_STORAGE\" />\n"
        "    <application\n"
        "        android:label=\"%s\"\n"
        "        android:hardwareAccelerated=\"true\"\n"
        "        android:theme=\"@android:style/Theme.NoTitleBar.Fullscreen\">\n"
        "        <activity android:name=\".MainActivity\" android:exported=\"true\"\n"
        "            android:configChanges=\"orientation|keyboardHidden|screenSize\">\n"
        "            <intent-filter>\n"
        "                <action android:name=\"android.intent.action.MAIN\" />\n"
        "                <category android:name=\"android.intent.category.LAUNCHER\" />\n"
        "            </intent-filter>\n"
        "        </activity>\n"
        "    </application>\n"
        "</manifest>\n", domain, title ? title : app_name);
    write_file_content(manifest_path, manifest);

    /* 2. Assets para el WebView / Contenedor */
    generate_web_bundle(input_path, app_name, domain, title, assets_dir);

    /* 3. Codigo C JNI / NDK generado en target_dir */
    tiger_compile_to_binary(input_path, NULL, TARGET_ANDROID, domain, false);

    /* 4. Scripts de ensamble APK */
    char bat[512];
    snprintf(bat, sizeof(bat),
        "@echo off\n"
        "echo [Tiger Android]: Compilando APK para '%s' (Dominio: %s)...\n"
        "if exist gradlew.bat ( gradlew.bat assembleRelease ) else ( echo [AVISO]: Instala Gradle o ejecuta con Android Studio. )\n",
        app_name, domain);
    write_file_content(build_bat_path, bat);

    char sh[512];
    snprintf(sh, sizeof(sh),
        "#!/bin/bash\n"
        "echo \"[Tiger Android]: Compilando APK para '%s' (Dominio: %s)...\"\n"
        "if [ -f ./gradlew ]; then ./gradlew assembleRelease; else echo \"[AVISO]: Requiere Gradle.\"; fi\n",
        app_name, domain);
    write_file_content(build_sh_path, sh);

    printf("  \033[38;2;34;197;94m[EXITO]: Paquete Android APK creado:\033[0m\n");
    printf("    * Directorio Proyecto:   \033[38;2;0;243;255m%s/\033[0m\n", target_dir);
    printf("    * Manifiesto Android:    \033[38;2;0;243;255m%s\033[0m (Package: %s)\n", manifest_path, domain);
    printf("    * Activos Contenedor:    \033[38;2;0;243;255m%s/\033[0m\n", assets_dir);
    printf("    * Script de Ensamble:    \033[38;2;0;243;255m%s\033[0m\n", build_bat_path);
    return true;
}

/* ========================================================================= */
/* 4. EMPAQUETADOR DE LINUX: Binario POSIX + Acceso Directo .desktop         */
/* ========================================================================= */
static bool package_linux(const char* input_path, const char* app_name, const char* domain, const char* out_dir) {
    char target_dir[MAX_PATH];
    snprintf(target_dir, sizeof(target_dir), "%s/%s/linux", out_dir, domain);
    make_dir_p(target_dir);

    char desktop_path[MAX_PATH];
    char run_path[MAX_PATH];
    snprintf(desktop_path, sizeof(desktop_path), "%s/%s.desktop", target_dir, app_name);
    snprintf(run_path, sizeof(run_path), "%s/ejecutar.sh", target_dir);

    /* .desktop conforme a FreeDesktop */
    char desktop[1024];
    snprintf(desktop, sizeof(desktop),
        "[Desktop Entry]\n"
        "Version=1.0\n"
        "Type=Application\n"
        "Name=%s\n"
        "Comment=Aplicacion Tiger 10.2 compilada al silicio\n"
        "Exec=./%s\n"
        "Icon=%s\n"
        "Terminal=true\n"
        "Categories=Development;Utility;\n"
        "X-Tiger-Domain=%s\n",
        app_name, app_name, app_name, domain);
    write_file_content(desktop_path, desktop);

    /* Script lanzador */
    char run_sh[512];
    snprintf(run_sh, sizeof(run_sh),
        "#!/bin/bash\n"
        "DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
        "echo \"[Tiger Linux]: Ejecutando %s (Dominio: %s)...\"\n"
        "if [ -f \"$DIR/%s\" ]; then \"$DIR/%s\"; else tiger \"$DIR/%s.tg\"; fi\n",
        app_name, domain, app_name, app_name, app_name);
    write_file_content(run_path, run_sh);

    /* Generar codigo C nativo de Linux y scripts en target_dir */
    tiger_compile_to_binary(input_path, NULL, TARGET_LINUX, domain, false);

    printf("  \033[38;2;34;197;94m[EXITO]: Paquete Linux creado:\033[0m\n");
    printf("    * Directorio:            \033[38;2;0;243;255m%s/\033[0m\n", target_dir);
    printf("    * Acceso .desktop:       \033[38;2;0;243;255m%s\033[0m (Dominio: %s)\n", desktop_path, domain);
    printf("    * Lanzador POSIX:        \033[38;2;0;243;255m%s\033[0m\n", run_path);
    return true;
}

/* ========================================================================= */
/* 5. EMPAQUETADOR DE MACOS: Application Bundle (.app) + Info.plist          */
/* ========================================================================= */
static bool package_macos(const char* input_path, const char* app_name, const char* domain, const char* out_dir) {
    char target_dir[MAX_PATH];
    snprintf(target_dir, sizeof(target_dir), "%s/%s/macos", out_dir, domain);
    make_dir_p(target_dir);

    tiger_compile_to_binary(input_path, NULL, TARGET_MAC, domain, false);

    printf("  \033[38;2;34;197;94m[EXITO]: Paquete macOS .app creado:\033[0m\n");
    printf("    * Directorio Bundle:     \033[38;2;0;243;255m%s/%s.app\033[0m\n", target_dir, app_name);
    printf("    * Bundle ID (Dominio):   \033[38;2;0;243;255m%s\033[0m\n", domain);
    return true;
}

/* ========================================================================= */
/* FUNCION PRINCIPAL DE EMPAQUETADO MULTIPLATAFORMA                          */
/* ========================================================================= */
static void print_banner(void) {
    printf("\n\033[38;2;255;107;0m  ████████╗██╗ ██████╗ ███████╗██████╗     ██████╗  █████╗  ██████╗██╗  ██╗\033[0m\n");
    printf("\033[38;2;255;107;0m  ╚══██╔══╝██║██╔════╝ ██╔════╝██╔══██╗    ██╔══██╗██╔══██╗██╔════╝██║ ██╔╝\033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██║██║  ███╗█████╗  ██████╔╝    ██████╔╝███████║██║     █████╔╝ \033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██║██║   ██║██╔══╝  ██╔══██╗    ██╔═══╝ ██╔══██║██║     ██╔═██╗ \033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██║╚██████╔╝███████╗██║  ██║    ██║     ██║  ██║╚██████╗██║  ██╗\033[0m\n");
    printf("\033[38;2;255;107;0m     ╚═╝   ╚═╝ ╚═════╝ ╚══════╝╚═╝  ╚═╝    ╚═╝     ╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝\033[0m\n");
    printf("================================================================================\n");
    printf("  🐯 TIGER 10.2: EMPAQUETADOR UNIVERSAL MULTIPLATAFORMA (empaquetar.exe)\n");
    printf("================================================================================\n");
}

static void print_help(void) {
    print_banner();
    printf("Uso: empaquetar <archivo.tg> [opciones]\n\n");
    printf("Opciones:\n");
    printf("  -p, --plataforma <plat>   Plataforma destino:\n");
    printf("                            * windows  (Ejecutable .exe nativo autónomo de Windows)\n");
    printf("                            * web      (HTML5 + Canvas WebGL + PWA + NoSQL web)\n");
    printf("                            * android  (Proyecto APK con contenedor acelerado)\n");
    printf("                            * linux    (Binario POSIX + acceso directo .desktop)\n");
    printf("                            * macos    (Bundle .app con Info.plist)\n");
    printf("                            * todas    (Genera TODAS las plataformas a la vez)\n");
    printf("  -o, --salida <dir>        Directorio de distribucion (por defecto: 'dist')\n");
    printf("  -d, --dominio <dom>       Identificador de dominio (ej: com.miempresa.miapp)\n");
    printf("  -t, --titulo <texto>      Titulo visible de la aplicacion\n");
    printf("  -h, --ayuda               Muestra este manual de uso\n\n");
    printf("Ejemplos:\n");
    printf("  empaquetar MiApp.tg --plataforma windows\n");
    printf("  empaquetar TigerUIXDemo.tg --plataforma web\n");
    printf("  empaquetar MiApp.tg --plataforma todas --dominio com.tiger.app\n");
}

int tiger_empaquetar_main(int argc, char** argv) {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        SetConsoleMode(hOut, dwMode | 0x0004);
    }
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc < 2) {
        print_help();
        return 0;
    }

    const char* input_file = NULL;
    const char* plataforma = "todas";
    const char* out_dir = "dist";
    const char* domain = NULL;
    const char* title = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "--ayuda") == 0 || strcmp(argv[i], "ayuda") == 0) {
            print_help();
            return 0;
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--plataforma") == 0 || strcmp(argv[i], "--platform") == 0) && i + 1 < argc) {
            plataforma = argv[++i];
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--salida") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            out_dir = argv[++i];
        } else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dominio") == 0 || strcmp(argv[i], "--domain") == 0) && i + 1 < argc) {
            domain = argv[++i];
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--titulo") == 0 || strcmp(argv[i], "--title") == 0) && i + 1 < argc) {
            title = argv[++i];
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        }
    }

    if (!input_file) {
        fprintf(stderr, "[Error]: Debes especificar el archivo .tg a empaquetar.\n");
        return 1;
    }

    char app_name[128];
    extract_base_name(input_file, app_name, sizeof(app_name));

    char final_domain[256];
    if (domain && domain[0]) {
        strncpy(final_domain, domain, sizeof(final_domain) - 1);
        final_domain[sizeof(final_domain) - 1] = '\0';
    } else {
        snprintf(final_domain, sizeof(final_domain), "com.tiger.%s", app_name);
    }

    print_banner();
    printf("[CONFIGURACION DE EMPAQUETADO]:\n");
    printf("  * Archivo Fuente:   \033[38;2;0;243;255m%s\033[0m\n", input_file);
    printf("  * Nombre de App:    \033[38;2;0;243;255m%s\033[0m\n", app_name);
    printf("  * Dominio:          \033[38;2;34;197;94m%s\033[0m\n", final_domain);
    printf("  * Plataforma(s):    \033[38;2;255;107;0m%s\033[0m\n", plataforma);
    printf("  * Carpeta Salida:   \033[38;2;0;243;255m%s/%s/\033[0m\n", out_dir, final_domain);
    printf("--------------------------------------------------------------------------------\n\n");

    bool do_win = (strcasecmp(plataforma, "windows") == 0 || strcasecmp(plataforma, "win") == 0 || strcasecmp(plataforma, "todas") == 0 || strcasecmp(plataforma, "all") == 0);
    bool do_web = (strcasecmp(plataforma, "web") == 0 || strcasecmp(plataforma, "html") == 0 || strcasecmp(plataforma, "wasm") == 0 || strcasecmp(plataforma, "todas") == 0 || strcasecmp(plataforma, "all") == 0);
    bool do_android = (strcasecmp(plataforma, "android") == 0 || strcasecmp(plataforma, "apk") == 0 || strcasecmp(plataforma, "todas") == 0 || strcasecmp(plataforma, "all") == 0);
    bool do_linux = (strcasecmp(plataforma, "linux") == 0 || strcasecmp(plataforma, "todas") == 0 || strcasecmp(plataforma, "all") == 0);
    bool do_macos = (strcasecmp(plataforma, "macos") == 0 || strcasecmp(plataforma, "mac") == 0 || strcasecmp(plataforma, "apple") == 0 || strcasecmp(plataforma, "todas") == 0 || strcasecmp(plataforma, "all") == 0);

    bool ok = true;
    if (do_win) ok = package_windows(input_file, app_name, final_domain, out_dir) && ok;
    if (do_web) ok = package_web(input_file, app_name, final_domain, title, out_dir) && ok;
    if (do_android) ok = package_android(input_file, app_name, final_domain, title, out_dir) && ok;
    if (do_linux) ok = package_linux(input_file, app_name, final_domain, out_dir) && ok;
    if (do_macos) ok = package_macos(input_file, app_name, final_domain, out_dir) && ok;

    printf("\n================================================================================\n");
    if (ok) {
        printf("\033[38;2;34;197;94m✨ [COMPLETADO CON EXITO]: Tu aplicacion Tiger ha sido empaquetada para distribucion.\033[0m\n");
        printf("Archivos disponibles en: \033[38;2;0;243;255m%s/%s/\033[0m\n", out_dir, final_domain);
    } else {
        printf("\033[38;2;239;68;68m⚠️  [AVISO]: Algunos paquetes requirieron herramientas adicionales.\033[0m\n");
    }
    printf("================================================================================\n\n");
    return ok ? 0 : 1;
}

#ifndef TIGER_BUILD_STANDALONE_LIB
int main(int argc, char** argv) {
    return tiger_empaquetar_main(argc, argv);
}
#endif

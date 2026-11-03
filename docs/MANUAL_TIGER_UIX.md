# 🎨 Manual de Referencia de TigerUIX y Backend NoSQL

**TigerUIX** es el framework declarativo y reactivo nativo de Tiger 10.2 para construir interfaces de usuario gráficas fluidas con antialiasing ClearType, animaciones de silicio y conexión directa con bases de datos **JSON** y **NoSQL**.

---

## 🧱 1. Estructura Declarativa de una Interfaz

Una aplicación TigerUIX se define mediante un árbol de diccionarios pasado a `uix_interfaz`:

```tiger
USAR LIBRERIA "TigerUIX.tg"

VAR interfaz_app = {
    "ventana": {
        "id": "ventana_principal",
        "nombre": "Panel de Control TigerUIX",
        "ancho": 640,
        "alto": 720,
        "fondo": "#0F172A",
        "pagina_1": {
            "id": "pagina_dashboard",
            "x": 32,
            "y": 80,
            "texto": {
                "id": "titulo_app",
                "contenido": "Sistema de Control Nativo",
                "color": "blanco",
                "tamano": 22,
                "peso": "negrita"
            },
            "parrafo": {
                "id": "parrafo_info",
                "texto": "Renderizado fluido con ClearType y antialiasing subpíxel.",
                "color": "#94A3B8",
                "tamano": 15
            },
            "boton": {
                "id": "btn_procesar",
                "texto": "Iniciar Tarea",
                "ancho": 220,
                "alto": 50,
                "fondo": "#3B82F6",
                "animacion": "deslizar",
                "duracion": 400
            }
        }
    }
}

uix_interfaz(interfaz_app)
```

---

## 🔑 2. El Parámetro `id` y Control Reactivo

Todo elemento con una propiedad `"id"` se indexa automáticamente en el registro global de TigerUIX, permitiendo manipularlo o leerlo desde cualquier parte del código:

### Funciones Reactivas Principales:
* **`uix_obtener(id)`**: Recupera el elemento como diccionario para inspeccionar o modificar sus propiedades.
* **`uix_leer_valor(id)`**: Obtiene el contenido textual o valor actual del control.
* **`uix_escribir_valor(id, nuevo_valor)`**: Actualiza dinámicamente el texto o estado del control en pantalla.
* **`uix_lista_ids()`**: Devuelve una lista con todos los controles registrados.

```tiger
# Leer valor de un texto por su ID:
VAR titulo = uix_leer_valor("titulo_app")

# Modificar reactivamente el contenido en pantalla:
uix_escribir_valor("titulo_app", "Estado: Producción Activa")
```

---

## 💾 3. Conexión con JSON y Backend NoSQL

TigerUIX permite convertir cualquier elemento visual en un documento estructurado e insertarlo directamente en almacenamiento NoSQL:

### 1. Serialización JSON:
```tiger
# Exportar un solo control a cadena JSON:
VAR json_boton = uix_exportar_json("btn_procesar")

# Exportar toda la jerarquía de la ventana a JSON:
VAR json_arbol = uix_exportar_todo_json()
```

### 2. Persistencia NoSQL mediante API:
```tiger
# Guardar control en la colección 'componentes_ui':
uix_guardar_nosql("btn_procesar", "componentes_ui")

# Recuperar datos desde la colección NoSQL:
VAR datos = uix_cargar_nosql("btn_procesar", "componentes_ui")
```

### 3. Persistencia Directa con Bloques `TIPO NOSQL`:
Puedes enlazar el `id` de cualquier elemento dentro de bloques de metalenguaje `TIPO NOSQL`:

```tiger
TIPO NOSQL {
    coleccion: "componentes_ui",
    boton: btn_procesar,
    estado: "activo",
    timestamp: 1730310000
}
```
*TigerUIX detecta automáticamente que `btn_procesar` es un ID registrado, extrae su estado visual actual y lo persiste en la colección NoSQL.*

---

## 🎨 4. Propiedades Visuales y Estilos

| Propiedad | Tipo | Valores / Descripción |
| :--- | :--- | :--- |
| `fondo` | Color Hex / Nombre | `#0F172A`, `#1E293B`, `#3B82F6`, `blanco`, `negro` |
| `color` | Color Hex / Nombre | Color del texto o borde |
| `tamano` | Entero | Tamaño en píxeles de la fuente |
| `peso` | Cadena | `"normal"`, `"negrita"`, `"bold"` |
| `radio` | Entero | Radio de esquinas redondeadas en botones (ej: `12`) |
| `animacion`| Cadena | `"deslizar"` (deslizamiento suave), `"pulso"` (pulsación sutil) |
| `duracion` | Entero | Duración de la animación en milisegundos (ej: `400`) |


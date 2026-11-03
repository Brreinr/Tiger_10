#ifndef TIGER_API_TRADUCT_H
#define TIGER_API_TRADUCT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Idiomas soportados por el motor de traducción Tiger */
typedef enum {
    TIGER_LANG_AUTO = 0,
    TIGER_LANG_ES, /* Español */
    TIGER_LANG_EN, /* English */
    TIGER_LANG_FR, /* Français */
    TIGER_LANG_PT, /* Português */
    TIGER_LANG_DE, /* Deutsch */
    TIGER_LANG_IT  /* Italiano */
} TigerLang;

/* Nombres legibles e identificadores de idiomas */
const char* tiger_traduct_lang_name(TigerLang lang);
const char* tiger_traduct_lang_code(TigerLang lang);
TigerLang tiger_traduct_lang_from_code(const char* code);

/* Detección automática del idioma del texto o código Tiger */
TigerLang tiger_traduct_detect_lang(const char* text);

/* Verifica si una palabra es un parámetro estándar del sistema Tiger / TigerUIX */
bool tiger_traduct_is_standard(const char* word);

/* Traducción de un parámetro, propiedad, nodo o valor individual */
const char* tiger_traduct_param(const char* word, TigerLang from_lang, TigerLang to_lang);

/* Normalización a identificador canónico interno */
const char* tiger_traduct_canonical(const char* word);

/* Traducción de un par clave-valor (ej: fondo: blanco -> background: white) */
bool tiger_traduct_pair(const char* key, const char* value, TigerLang to_lang,
                        char* out_key, size_t out_key_sz,
                        char* out_val, size_t out_val_sz);

/* Traducción de un código fuente o bloque TigerUIX completo */
char* tiger_traduct_code(const char* src, size_t src_len, TigerLang from_lang, TigerLang to_lang);

/* Punto de entrada CLI para la herramienta de traducción */
int tiger_traduct_main(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif /* TIGER_API_TRADUCT_H */


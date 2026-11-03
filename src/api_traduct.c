#include "api_traduct.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
#endif

/* Estructura de diccionario multilingüe para parámetros y valores */
typedef struct {
    const char* canonical; /* Identificador canónico interno */
    const char* es;        /* Español */
    const char* en;        /* English */
    const char* fr;        /* Français */
    const char* pt;        /* Português */
    const char* de;        /* Deutsch */
    const char* it;        /* Italiano */
} TigerTraductEntry;

static const TigerTraductEntry s_dictionary[] = {
    /* UIX Propiedades */
    { "fondo",          "fondo",          "background",     "fond",          "fundo",          "hintergrund",     "sfondo" },
    { "color_fondo",    "color_fondo",    "bgcolor",        "couleur_fond",  "cor_fundo",      "hintergrundfarbe","colore_sfondo" },
    { "ancho",          "ancho",          "width",          "largeur",       "largura",        "breite",          "larghezza" },
    { "alto",           "alto",           "height",         "hauteur",       "altura",         "hoehe",           "altezza" },
    { "texto",          "texto",          "text",           "texte",         "texto",          "text",            "testo" },
    { "contenido",      "contenido",      "content",        "contenu",       "conteudo",       "inhalt",          "contenuto" },
    { "peso",           "peso",           "weight",         "poids",         "peso",           "gewicht",         "peso" },
    { "tamano",         "tamano",         "size",           "taille",        "tamanho",        "groesse",         "dimensione" },
    { "radio",          "radio",          "radius",         "rayon",         "raio",           "radius",          "raggio" },
    { "animacion",      "animacion",      "animation",      "animation",     "animacao",       "animation",       "animazione" },
    { "duracion",       "duracion",       "duration",       "duree",         "duracao",        "dauer",           "durata" },
    { "desplazamiento", "desplazamiento", "offset",         "deplacement",   "deslocamento",   "verschiebung",    "spostamento" },
    { "titulo",         "titulo",         "title",          "titre",         "titulo",         "titel",           "titolo" },
    { "nombre",         "nombre",         "name",           "nom",           "nome",           "name",            "nome" },
    { "id",             "id",             "id",             "id",            "id",             "id",              "id" },
    { "identificador",  "identificador",  "identifier",     "identifiant",   "identificador",  "identifikator",   "identificatore" },
    { "coleccion",      "coleccion",      "collection",     "collection",    "colecao",        "sammlung",        "collezione" },
    { "fila",           "fila",           "row",            "ligne",         "linha",          "zeile",           "riga" },
    { "columna",        "columna",        "column",         "colonne",       "coluna",         "spalte",          "colonna" },
    { "alineacion",     "alineacion",     "alignment",      "alignement",    "alinhamento",    "ausrichtung",     "allineamento" },
    { "margen",         "margen",         "margin",         "marge",         "margem",         "rand",            "margine" },
    { "relleno",        "relleno",        "padding",        "remplissage",   "preenchimento",  "abstand",         "riempimento" },
    { "opacidad",       "opacidad",       "opacity",        "opacite",       "opacidade",      "deckkraft",       "opacita" },
    { "visible",        "visible",        "visible",        "visible",       "visivel",        "sichtbar",        "visibile" },
    { "habilitado",     "habilitado",     "enabled",        "active",        "habilitado",     "aktiviert",       "abilitato" },
    { "deshabilitado",  "deshabilitado",  "disabled",       "desactive",     "desabilitado",   "deaktiviert",     "disabilitato" },
    { "al_clic",        "al_clic",        "on_click",       "au_clic",       "ao_clicar",      "bei_klick",       "al_clic" },
    { "color",          "color",          "color",          "couleur",       "cor",            "farbe",           "colore" },
    { "borde",          "borde",          "border",         "bordure",       "borda",          "rahmen",          "bordo" },
    { "sombra",         "sombra",         "shadow",         "ombre",         "sombra",         "schatten",        "ombra" },

    /* UIX Nodos y Controles */
    { "ventana",        "ventana",        "window",         "fenetre",       "janela",         "fenster",         "finestra" },
    { "boton",          "boton",          "button",         "bouton",        "botao",          "taste",           "pulsante" },
    { "parrafo",        "parrafo",        "paragraph",      "paragraphe",    "paragrafo",      "absatz",          "paragrafo" },
    { "pagina",         "pagina",         "page",           "page",          "pagina",         "seite",           "pagina" },
    { "etiqueta",       "etiqueta",       "label",          "etiquette",     "rotulo",         "etikett",         "etichetta" },
    { "entrada",        "entrada",        "input",          "saisie",        "entrada",        "eingabe",         "input" },
    { "imagen",         "imagen",         "image",          "image",         "imagem",         "bild",            "immagine" },
    { "lienzo",         "lienzo",         "canvas",         "toile",         "tela",           "leinwand",        "tela" },
    { "lista",          "lista",          "list",           "liste",         "lista",          "liste",           "lista" },
    { "cuadricula",     "cuadricula",     "grid",           "grille",        "grade",          "gitter",          "griglia" },
    { "tabla",          "tabla",          "table",          "tableau",       "tabela",         "tabelle",         "tabella" },

    /* Colores y Estilos */
    { "blanco",         "blanco",         "white",          "blanc",         "branco",         "weiss",           "bianco" },
    { "negro",          "negro",          "black",          "noir",          "preto",          "schwarz",         "nero" },
    { "azul",           "azul",           "blue",           "bleu",          "azul",           "blau",            "blu" },
    { "rojo",           "rojo",           "red",            "rouge",         "vermelho",       "rot",             "rosso" },
    { "verde",          "verde",          "green",          "vert",          "verde",          "gruen",           "verde" },
    { "amarillo",       "amarillo",       "yellow",         "jaune",         "amarelo",        "gelb",            "giallo" },
    { "gris",           "gris",           "gray",           "gris",          "cinza",          "grau",            "grigio" },
    { "naranja",        "naranja",        "orange",         "orange",        "laranja",        "orange",          "arancione" },
    { "morado",         "morado",         "purple",         "violet",        "roxo",           "lila",            "viola" },
    { "negrita",        "negrita",        "bold",           "gras",          "negrito",        "fett",            "grassetto" },
    { "cursiva",        "cursiva",        "italic",         "italique",      "italico",        "kursiv",          "corsivo" },
    { "normal",         "normal",         "normal",         "normal",        "normal",         "normal",          "normale" },

    /* Animaciones */
    { "deslizar",       "deslizar",       "slide",          "glisser",       "deslizar",       "gleiten",         "scivolare" },
    { "desvanecer",     "desvanecer",     "fade",           "fondre",        "desvanecer",     "verblassen",      "sfumare" },
    { "rebotar",        "rebotar",        "bounce",         "rebondir",      "quicar",         "huepfen",         "rimbalzare" },
    { "rotar",          "rotar",          "rotate",         "tourner",       "girar",          "drehen",          "ruotare" },
    { "escalar",        "escalar",        "scale",          "echelle",       "escalar",        "skalieren",       "scalare" },

    /* Palabras Clave y Bloques de Lenguaje */
    { "tipo",           "tipo",           "type",           "type",          "tipo",           "typ",             "tipo" },
    { "nosql",          "nosql",          "nosql",          "nosql",         "nosql",          "nosql",           "nosql" },
    { "json",           "json",           "json",           "json",          "json",           "json",            "json" },
    { "si",             "si",             "if",             "si",            "se",             "wenn",            "se" },
    { "sino",           "sino",           "else",           "sinon",         "senao",          "sonst",           "altrimenti" },
    { "mientras",       "mientras",       "while",          "tant_que",      "enquanto",       "waehrend",        "mentre" },
    { "para",           "para",           "for",            "pour",          "para",           "fuer",            "per" },
    { "funcion",        "funcion",        "function",       "fonction",      "funcao",         "funktion",        "funzione" },
    { "retornar",       "retornar",       "return",         "retourner",     "retornar",       "zurueckgeben",    "ritorna" },
    { "imprimir",       "imprimir",       "print",          "afficher",      "imprimir",       "drucken",         "stampa" },
    { "verdadero",      "verdadero",      "true",           "vrai",          "verdadeiro",     "wahr",            "vero" },
    { "falso",          "falso",          "false",          "faux",          "falso",          "falsch",          "falso" },
    { "nulo",           "nulo",           "null",           "nul",           "nulo",           "null",            "nullo" }
};

static const size_t s_dictionary_count = sizeof(s_dictionary) / sizeof(s_dictionary[0]);

/* Normalización de tildes y caracteres especiales a ASCII simple para comparación */
static void normalize_word(const char* in, char* out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < out_sz; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0xC3) { /* UTF-8 tildes */
            unsigned char c2 = (unsigned char)in[++i];
            if (!c2) break;
            if (c2 == 0xA1 || c2 == 0x81) out[j++] = 'a';      /* á, Á */
            else if (c2 == 0xA9 || c2 == 0x89) out[j++] = 'e'; /* é, É */
            else if (c2 == 0xAD || c2 == 0x8D) out[j++] = 'i'; /* í, Í */
            else if (c2 == 0xB3 || c2 == 0x93) out[j++] = 'o'; /* ó, Ó */
            else if (c2 == 0xBA || c2 == 0x9A) out[j++] = 'u'; /* ú, Ú */
            else if (c2 == 0xB1 || c2 == 0x91) out[j++] = 'n'; /* ñ, Ñ */
            else if (c2 == 0xA0) out[j++] = 'a';               /* à */
            else if (c2 == 0xA8) out[j++] = 'e';               /* è */
            else if (c2 == 0xA7) out[j++] = 'c';               /* ç */
            else if (c2 == 0xA4) out[j++] = 'a';               /* ä */
            else if (c2 == 0xB6) out[j++] = 'o';               /* ö */
            else if (c2 == 0xBC) out[j++] = 'u';               /* ü */
            else if (c2 == 0x9F) { out[j++] = 's'; if (j + 1 < out_sz) out[j++] = 's'; } /* ß */
            else out[j++] = tolower(c2);
        } else {
            out[j++] = tolower(c);
        }
    }
    out[j] = '\0';
}

const char* tiger_traduct_lang_name(TigerLang lang) {
    switch (lang) {
        case TIGER_LANG_ES: return "Español";
        case TIGER_LANG_EN: return "English";
        case TIGER_LANG_FR: return "Français";
        case TIGER_LANG_PT: return "Português";
        case TIGER_LANG_DE: return "Deutsch";
        case TIGER_LANG_IT: return "Italiano";
        default: return "Auto-Detectar";
    }
}

const char* tiger_traduct_lang_code(TigerLang lang) {
    switch (lang) {
        case TIGER_LANG_ES: return "es";
        case TIGER_LANG_EN: return "en";
        case TIGER_LANG_FR: return "fr";
        case TIGER_LANG_PT: return "pt";
        case TIGER_LANG_DE: return "de";
        case TIGER_LANG_IT: return "it";
        default: return "auto";
    }
}

TigerLang tiger_traduct_lang_from_code(const char* code) {
    if (!code) return TIGER_LANG_AUTO;
    if (strcasecmp(code, "es") == 0 || strcasecmp(code, "spa") == 0 || strcasecmp(code, "espanol") == 0 || strcasecmp(code, "spanish") == 0) return TIGER_LANG_ES;
    if (strcasecmp(code, "en") == 0 || strcasecmp(code, "eng") == 0 || strcasecmp(code, "ingles") == 0 || strcasecmp(code, "english") == 0) return TIGER_LANG_EN;
    if (strcasecmp(code, "fr") == 0 || strcasecmp(code, "fra") == 0 || strcasecmp(code, "frances") == 0 || strcasecmp(code, "french") == 0) return TIGER_LANG_FR;
    if (strcasecmp(code, "pt") == 0 || strcasecmp(code, "por") == 0 || strcasecmp(code, "portugues") == 0 || strcasecmp(code, "portuguese") == 0) return TIGER_LANG_PT;
    if (strcasecmp(code, "de") == 0 || strcasecmp(code, "ger") == 0 || strcasecmp(code, "aleman") == 0 || strcasecmp(code, "german") == 0) return TIGER_LANG_DE;
    if (strcasecmp(code, "it") == 0 || strcasecmp(code, "ita") == 0 || strcasecmp(code, "italiano") == 0 || strcasecmp(code, "italian") == 0) return TIGER_LANG_IT;
    return TIGER_LANG_AUTO;
}

static const char* get_field_by_lang(const TigerTraductEntry* e, TigerLang lang) {
    switch (lang) {
        case TIGER_LANG_ES: return e->es;
        case TIGER_LANG_EN: return e->en;
        case TIGER_LANG_FR: return e->fr;
        case TIGER_LANG_PT: return e->pt;
        case TIGER_LANG_DE: return e->de;
        case TIGER_LANG_IT: return e->it;
        default: return e->canonical;
    }
}

/* ========================================================================= */
/* TABLA HASH O(1) PRE-NORMALIZADA PARA TRADUCCIÓN INSTANTÁNEA               */
/* ========================================================================= */
#define TRADUCT_HASH_SIZE 1024
#define TRADUCT_HASH_MASK (TRADUCT_HASH_SIZE - 1)

typedef struct {
    uint64_t hash;
    char norm_key[64];
    const TigerTraductEntry* entry;
    uint8_t lang_mask; /* Bitmask de idiomas donde coincide esta palabra */
} TraductHashSlot;

static TraductHashSlot s_traduct_hash[TRADUCT_HASH_SIZE];
static bool s_traduct_hash_ready = false;

static inline uint64_t hash_norm_str(const char* s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

static void insert_traduct_slot(const char* raw_word, const TigerTraductEntry* entry, uint8_t lang_bit) {
    char norm[64];
    normalize_word(raw_word, norm, sizeof(norm));
    uint64_t h = hash_norm_str(norm);
    size_t idx = (size_t)(h & TRADUCT_HASH_MASK);
    for (size_t step = 0; step < TRADUCT_HASH_SIZE; step++) {
        size_t pos = (idx + step) & TRADUCT_HASH_MASK;
        if (s_traduct_hash[pos].hash == 0) {
            s_traduct_hash[pos].hash = h;
            strncpy(s_traduct_hash[pos].norm_key, norm, sizeof(s_traduct_hash[pos].norm_key) - 1);
            s_traduct_hash[pos].norm_key[sizeof(s_traduct_hash[pos].norm_key) - 1] = '\0';
            s_traduct_hash[pos].entry = entry;
            s_traduct_hash[pos].lang_mask = lang_bit;
            return;
        }
        if (s_traduct_hash[pos].hash == h && strcmp(s_traduct_hash[pos].norm_key, norm) == 0) {
            s_traduct_hash[pos].lang_mask |= lang_bit;
            return;
        }
    }
}

static void ensure_traduct_hash_init(void) {
    if (s_traduct_hash_ready) return;
    memset(s_traduct_hash, 0, sizeof(s_traduct_hash));
    for (size_t i = 0; i < s_dictionary_count; i++) {
        const TigerTraductEntry* e = &s_dictionary[i];
        insert_traduct_slot(e->es,        e, (1 << TIGER_LANG_ES));
        insert_traduct_slot(e->en,        e, (1 << TIGER_LANG_EN));
        insert_traduct_slot(e->fr,        e, (1 << TIGER_LANG_FR));
        insert_traduct_slot(e->pt,        e, (1 << TIGER_LANG_PT));
        insert_traduct_slot(e->de,        e, (1 << TIGER_LANG_DE));
        insert_traduct_slot(e->it,        e, (1 << TIGER_LANG_IT));
        insert_traduct_slot(e->canonical, e, (1 << TIGER_LANG_ES));
    }
    s_traduct_hash_ready = true;
}

static inline const TraductHashSlot* lookup_traduct_slot(const char* word) {
    ensure_traduct_hash_init();
    char norm[64];
    normalize_word(word, norm, sizeof(norm));
    uint64_t h = hash_norm_str(norm);
    size_t idx = (size_t)(h & TRADUCT_HASH_MASK);
    for (size_t step = 0; step < TRADUCT_HASH_SIZE; step++) {
        size_t pos = (idx + step) & TRADUCT_HASH_MASK;
        if (s_traduct_hash[pos].hash == 0) return NULL;
        if (s_traduct_hash[pos].hash == h && strcmp(s_traduct_hash[pos].norm_key, norm) == 0) {
            return &s_traduct_hash[pos];
        }
    }
    return NULL;
}

bool tiger_traduct_is_standard(const char* word) {
    if (!word || !word[0]) return false;

    /* Páginas numeradas estándar: pagina_1, page_1, etc. */
    if (strncasecmp(word, "pagina_", 7) == 0 && isdigit((unsigned char)word[7])) return true;
    if (strncasecmp(word, "page_", 5) == 0 && isdigit((unsigned char)word[5])) return true;

    return lookup_traduct_slot(word) != NULL;
}

const char* tiger_traduct_param(const char* word, TigerLang from_lang, TigerLang to_lang) {
    if (!word || !word[0]) return word;
    if (to_lang == TIGER_LANG_AUTO) to_lang = TIGER_LANG_EN;

    /* Manejo bilingüe de páginas numeradas estándar: pagina_N <-> page_N */
    if (strncasecmp(word, "pagina_", 7) == 0 && isdigit((unsigned char)word[7])) {
        if (to_lang == TIGER_LANG_EN) {
            static char s_page_buf_en[64];
            snprintf(s_page_buf_en, sizeof(s_page_buf_en), "page_%s", word + 7);
            return s_page_buf_en;
        }
        return word;
    }
    if (strncasecmp(word, "page_", 5) == 0 && isdigit((unsigned char)word[5])) {
        if (to_lang == TIGER_LANG_ES) {
            static char s_page_buf_es[64];
            snprintf(s_page_buf_es, sizeof(s_page_buf_es), "pagina_%s", word + 5);
            return s_page_buf_es;
        }
        return word;
    }

    const TraductHashSlot* slot = lookup_traduct_slot(word);
    if (slot && slot->entry) {
        if (from_lang == TIGER_LANG_AUTO || (slot->lang_mask & (1 << from_lang))) {
            return get_field_by_lang(slot->entry, to_lang);
        }
    }
    return word; /* Si no es un parámetro estándar del sistema, conservar original */
}

const char* tiger_traduct_canonical(const char* word) {
    if (!word || !word[0]) return word;
    if (strncasecmp(word, "page_", 5) == 0 && isdigit((unsigned char)word[5])) {
        static char s_page_canon[64];
        snprintf(s_page_canon, sizeof(s_page_canon), "pagina_%s", word + 5);
        return s_page_canon;
    }
    return tiger_traduct_param(word, TIGER_LANG_AUTO, TIGER_LANG_ES);
}

TigerLang tiger_traduct_detect_lang(const char* text) {
    if (!text) return TIGER_LANG_ES;

    int score_es = 0, score_en = 0, score_fr = 0, score_pt = 0, score_de = 0, score_it = 0;

    /* Extraer tokens alfanuméricos y contar coincidencias en O(1) */
    const char* p = text;
    char token[128];
    while (*p) {
        while (*p && !isalnum((unsigned char)*p) && *p != '_') p++;
        if (!*p) break;
        size_t len = 0;
        while (*p && (isalnum((unsigned char)*p) || *p == '_') && len + 1 < sizeof(token)) {
            token[len++] = *p++;
        }
        token[len] = '\0';

        const TraductHashSlot* slot = lookup_traduct_slot(token);
        if (slot) {
            if (slot->lang_mask & (1 << TIGER_LANG_ES)) score_es += 2;
            if (slot->lang_mask & (1 << TIGER_LANG_EN)) score_en += 2;
            if (slot->lang_mask & (1 << TIGER_LANG_FR)) score_fr += 2;
            if (slot->lang_mask & (1 << TIGER_LANG_PT)) score_pt += 2;
            if (slot->lang_mask & (1 << TIGER_LANG_DE)) score_de += 2;
            if (slot->lang_mask & (1 << TIGER_LANG_IT)) score_it += 2;
        }
    }

    if (score_en > score_es && score_en > score_fr && score_en > score_pt && score_en > score_de && score_en > score_it) return TIGER_LANG_EN;
    if (score_fr > score_es && score_fr > score_en && score_fr > score_pt && score_fr > score_de && score_fr > score_it) return TIGER_LANG_FR;
    if (score_pt > score_es && score_pt > score_en && score_pt > score_fr && score_pt > score_de && score_pt > score_it) return TIGER_LANG_PT;
    if (score_de > score_es && score_de > score_en && score_de > score_fr && score_de > score_pt && score_de > score_it) return TIGER_LANG_DE;
    if (score_it > score_es && score_it > score_en && score_it > score_fr && score_it > score_pt && score_it > score_de) return TIGER_LANG_IT;
    return TIGER_LANG_ES;
}

bool tiger_traduct_pair(const char* key, const char* value, TigerLang to_lang,
                        char* out_key, size_t out_key_sz,
                        char* out_val, size_t out_val_sz) {
    if (!key || !out_key) return false;
    const char* tr_key = tiger_traduct_param(key, TIGER_LANG_AUTO, to_lang);
    strncpy(out_key, tr_key, out_key_sz - 1);
    out_key[out_key_sz - 1] = '\0';

    if (value && out_val && out_val_sz > 0) {
        const char* tr_val = tiger_traduct_param(value, TIGER_LANG_AUTO, to_lang);
        strncpy(out_val, tr_val, out_val_sz - 1);
        out_val[out_val_sz - 1] = '\0';
    }
    return true;
}

char* tiger_traduct_code(const char* src, size_t src_len, TigerLang from_lang, TigerLang to_lang) {
    if (!src) return NULL;
    if (src_len == 0) src_len = strlen(src);
    if (from_lang == TIGER_LANG_AUTO) from_lang = tiger_traduct_detect_lang(src);
    if (to_lang == TIGER_LANG_AUTO) to_lang = (from_lang == TIGER_LANG_ES) ? TIGER_LANG_EN : TIGER_LANG_ES;

    size_t out_cap = src_len * 2 + 1024;
    char* out = (char*)malloc(out_cap);
    if (!out) return NULL;

    size_t in_i = 0;
    size_t out_i = 0;

    while (in_i < src_len) {
        char c = src[in_i];

        /* Ignorar y preservar comentarios (# o //) */
        if (c == '#' || (c == '/' && in_i + 1 < src_len && src[in_i + 1] == '/')) {
            while (in_i < src_len && src[in_i] != '\n') {
                if (out_i + 2 >= out_cap) {
                    out_cap *= 2;
                    out = (char*)realloc(out, out_cap);
                }
                out[out_i++] = src[in_i++];
            }
            continue;
        }

        /* Ignorar y preservar cadenas de texto entre comillas */
        if (c == '"' || c == '\'') {
            char quote = c;
            out[out_i++] = src[in_i++];
            while (in_i < src_len && src[in_i] != quote) {
                if (src[in_i] == '\\' && in_i + 1 < src_len) {
                    out[out_i++] = src[in_i++];
                }
                if (out_i + 4 >= out_cap) {
                    out_cap *= 2;
                    out = (char*)realloc(out, out_cap);
                }
                out[out_i++] = src[in_i++];
            }
            if (in_i < src_len) out[out_i++] = src[in_i++];
            continue;
        }

        /* Procesar identificadores y palabras clave */
        if (isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0xC0) {
            char token[256];
            size_t tlen = 0;
            while (in_i < src_len && (isalnum((unsigned char)src[in_i]) || src[in_i] == '_' || (unsigned char)src[in_i] >= 0x80)) {
                if (tlen + 1 < sizeof(token)) token[tlen++] = src[in_i];
                in_i++;
            }
            token[tlen] = '\0';

            /* Solo se traducen los parametros estandar del sistema Tiger / TigerUIX (Espanol <-> Ingles).
               Variables del programador, funciones de usuario y datos se mantienen 100% intactos. */
            const char* translated = token;
            if (tiger_traduct_is_standard(token)) {
                translated = tiger_traduct_param(token, from_lang, to_lang);
            }
            size_t tr_len = strlen(translated);
            while (out_i + tr_len + 4 >= out_cap) {
                out_cap *= 2;
                out = (char*)realloc(out, out_cap);
            }
            memcpy(out + out_i, translated, tr_len);
            out_i += tr_len;
            continue;
        }

        /* Caracteres regulares (separadores, puntuación, saltos de línea) */
        if (out_i + 2 >= out_cap) {
            out_cap *= 2;
            out = (char*)realloc(out, out_cap);
        }
        out[out_i++] = src[in_i++];
    }

    out[out_i] = '\0';
    return out;
}

/* ========================================================================= */
/* HERRAMIENTA CLI: tiger traducir / api_traduct                             */
/* ========================================================================= */
static void print_banner(void) {
    printf("\n\033[38;2;255;107;0m  ████████╗██████╗  █████╗ ██████╗ ██╗   ██╗ ██████╗████████╗\033[0m\n");
    printf("\033[38;2;255;107;0m  ╚══██╔══╝██╔══██╗██╔══██╗██╔══██╗██║   ██║██╔════╝╚══██╔══╝\033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██████╔╝███████║██║  ██║██║   ██║██║        ██║   \033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██╔══██╗██╔══██║██║  ██║██║   ██║██║        ██║   \033[0m\n");
    printf("\033[38;2;255;107;0m     ██║   ██║  ██║██║  ██║██████╔╝╚██████╔╝╚██████╗   ██║   \033[0m\n");
    printf("\033[38;2;255;107;0m     ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚═════╝  ╚═════╝  ╚═════╝   ╚═╝   \033[0m\n");
    printf("================================================================================\n");
    printf("  🐯 TIGER 10.2: TRADUCTOR MULTILINGÜE DE PARÁMETROS Y CÓDIGO (api_traduct)\n");
    printf("================================================================================\n");
}

static void print_help(void) {
    print_banner();
    printf("Uso:\n");
    printf("  tiger traducir <archivo.tg> [opciones]\n");
    printf("  tiger traduct <palabra_o_parametro> [opciones]\n\n");
    printf("Opciones:\n");
    printf("  -de, --desde <lang>       Idioma de origen (es, en, fr, pt, de, it, auto)\n");
    printf("  -a,  --hacia <lang>       Idioma de destino (es, en, fr, pt, de, it)\n");
    printf("  -o,  --salida <archivo>   Archivo donde guardar el código traducido\n");
    printf("  -h,  --ayuda              Muestra este manual de ayuda\n\n");
    printf("Idiomas Soportados:\n");
    printf("  * es: Español\n");
    printf("  * en: English\n");
    printf("  * fr: Français\n");
    printf("  * pt: Português\n");
    printf("  * de: Deutsch\n");
    printf("  * it: Italiano\n\n");
    printf("Ejemplos:\n");
    printf("  tiger traduct fondo -a en\n");
    printf("  tiger traducir MiApp.tg -de es -a en -o MiApp_en.tg\n");
    printf("  tiger traducir TigerUIXDemo.tg -a fr\n");
}

int tiger_traduct_main(int argc, char** argv) {
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

    const char* target_word = NULL;
    const char* input_file = NULL;
    const char* output_file = NULL;
    TigerLang from_lang = TIGER_LANG_AUTO;
    TigerLang to_lang = TIGER_LANG_EN;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "--ayuda") == 0) {
            print_help();
            return 0;
        } else if ((strcmp(argv[i], "-de") == 0 || strcmp(argv[i], "--desde") == 0 || strcmp(argv[i], "-from") == 0) && i + 1 < argc) {
            from_lang = tiger_traduct_lang_from_code(argv[++i]);
        } else if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--hacia") == 0 || strcmp(argv[i], "-to") == 0) && i + 1 < argc) {
            to_lang = tiger_traduct_lang_from_code(argv[++i]);
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--salida") == 0 || strcmp(argv[i], "-output") == 0) && i + 1 < argc) {
            output_file = argv[++i];
        } else if (argv[i][0] != '-') {
            if (!target_word) target_word = argv[i];
            else if (!input_file) input_file = argv[i];
        }
    }

    /* Caso 1: Traducción de archivo .tg */
    if (target_word && (strstr(target_word, ".tg") || strstr(target_word, ".txt"))) {
        input_file = target_word;
        target_word = NULL;
    }

    if (input_file) {
        FILE* f = fopen(input_file, "rb");
        if (!f) {
            fprintf(stderr, "[Error]: No se pudo abrir el archivo fuente '%s'\n", input_file);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* buf = (char*)malloc(sz + 1);
        if (!buf) { fclose(f); return 1; }
        fread(buf, 1, sz, f);
        buf[sz] = '\0';
        fclose(f);

        if (from_lang == TIGER_LANG_AUTO) from_lang = tiger_traduct_detect_lang(buf);

        print_banner();
        printf("[TRADUCCIÓN DE CÓDIGO TIGER]:\n");
        printf("  * Archivo Entrada:   \033[38;2;0;243;255m%s\033[0m\n", input_file);
        printf("  * Idioma Detectado:  \033[38;2;34;197;94m%s\033[0m (%s)\n", tiger_traduct_lang_name(from_lang), tiger_traduct_lang_code(from_lang));
        printf("  * Idioma Destino:    \033[38;2;255;107;0m%s\033[0m (%s)\n", tiger_traduct_lang_name(to_lang), tiger_traduct_lang_code(to_lang));
        printf("--------------------------------------------------------------------------------\n\n");

        char* translated = tiger_traduct_code(buf, sz, from_lang, to_lang);
        free(buf);

        if (!translated) {
            fprintf(stderr, "[Error]: Falló la traducción del código.\n");
            return 1;
        }

        if (output_file) {
            FILE* fout = fopen(output_file, "w");
            if (fout) {
                fputs(translated, fout);
                fclose(fout);
                printf("\033[38;2;34;197;94m✨ [ÉXITO]: Código traducido guardado en '%s'.\033[0m\n\n", output_file);
            } else {
                fprintf(stderr, "[Error]: No se pudo escribir en '%s'\n", output_file);
            }
        } else {
            printf("\033[38;2;0;243;255m--- CÓDIGO TIGER TRADUCIDO ---\033[0m\n\n");
            printf("%s\n", translated);
            printf("\033[38;2;0;243;255m------------------------------\033[0m\n\n");
        }
        free(translated);
        return 0;
    }

    /* Caso 2: Traducción de parámetro individual */
    if (target_word) {
        const char* result = tiger_traduct_param(target_word, from_lang, to_lang);
        print_banner();
        printf("[TRADUCCIÓN DE PARÁMETRO]:\n");
        printf("  * Parámetro Original: \033[38;2;0;243;255m%s\033[0m (%s)\n", target_word, tiger_traduct_lang_name(from_lang));
        printf("  * Parámetro Traducido:\033[38;2;34;197;94m%s\033[0m (%s)\n\n", result, tiger_traduct_lang_name(to_lang));
        return 0;
    }

    print_help();
    return 0;
}


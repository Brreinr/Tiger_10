#ifndef TIGER_AI_CHAT_H
#define TIGER_AI_CHAT_H

#include "tiger.h"
#include "tiger_interp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configuración de la IA y sesión de chat */
typedef struct {
    char api_key[256];
    char provider[32];           /* "gemini", "openai", "anthropic", "groq", "deepseek", "openrouter", "ollama", "custom" */
    char model[128];             /* Modelo específico */
    char custom_endpoint[512];   /* URL personalizada para OpenAI-compatible (ej. Ollama, LM Studio, etc.) */
    char system_prompt[1024];
    char current_chat_file[512];
    bool is_configured;
    bool auto_fallback;          /* true por defecto: auto-reintento en cascada si se acaban los créditos */
} TigerAIChatConfig;

/* Carga y guarda la configuración de la IA en chats/.ai_config.json */
void tiger_ai_load_config(TigerAIChatConfig* cfg);
void tiger_ai_save_config(const TigerAIChatConfig* cfg);

/* Consulta directa al modelo de IA externo (Gemini / LLM) */
bool tiger_ai_query_gemini(const char* api_key, const char* system_prompt, const char* user_prompt, char* out_text, size_t out_max);
bool tiger_ai_query_gemini_ex(const char* api_key, const char* model, const char* system_prompt, const char* user_prompt, char* out_text, size_t out_max, bool* out_is_quota);
bool tiger_ai_query_openai_compat(const char* endpoint, const char* api_key, const char* model, const char* system_prompt, const char* user_prompt, char* out_text, size_t out_max, bool* out_is_quota);
bool tiger_ai_query_anthropic(const char* api_key, const char* model, const char* system_prompt, const char* user_prompt, char* out_text, size_t out_max, bool* out_is_quota);

/* Consulta orquestada con multi-proveedor y recuperación automática ante fin de créditos */
bool tiger_ai_query_smart(TigerAIChatConfig* cfg, const char* user_prompt, char* out_text, size_t out_max);

/* Servidor MCP oficial de Tiger (Model Context Protocol JSON-RPC) */
int tiger_mcp_server_run(void);

/* Punto de entrada CLI para comandos:
   tiger IA [prompt]
   tiger AI [chat_datos.txt] [API llave] [SISTEMP actua...] [prompt]
*/
int tiger_ai_chat_cli(int argc, char** argv);

/* Inicia la sesión interactiva de chat o ejecuta un comando asistido */
void tiger_ai_start_chat_session(const char* chat_filename, const char* initial_prompt, const char* api_key, const char* system_prompt);

#ifdef __cplusplus
}
#endif

#endif /* TIGER_AI_CHAT_H */

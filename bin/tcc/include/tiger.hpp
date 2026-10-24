/**
 * ============================================================================
 * TIGER ENGINE — NATIVE C++ RUNTIME WRAPPER (tiger.hpp)
 * ============================================================================
 * Envoltorio moderno en C++ para desarrollo nativo con Tiger.
 * Permite usar el espacio de nombres 'tiger::', RAII y sobrecarga de operadores.
 * ============================================================================
 */

#ifndef TIGER_HPP
#define TIGER_HPP

#include "tiger.h"
#include <string>
#include <iostream>
#include <sstream>

namespace tiger {

    inline void imprimir(const std::string& msg) {
        TIGER->imprimir("%s", msg.c_str());
    }

    inline void alerta(const std::string& msg) {
        TIGER->alerta(msg.c_str());
    }

    inline void error(const std::string& msg) {
        TIGER->error(msg.c_str());
    }

    inline void exito(const std::string& msg) {
        TIGER->exito(msg.c_str());
    }

    inline void log(const std::string& tag, const std::string& msg) {
        TIGER->log(tag.c_str(), msg.c_str());
    }

    /* Manejo tipado de variables */
    inline void set_int(const std::string& clave, long long valor) {
        TIGER->set_int(clave.c_str(), valor);
    }

    inline long long get_int(const std::string& clave) {
        return TIGER->get_int(clave.c_str());
    }

    inline void set_str(const std::string& clave, const std::string& valor) {
        TIGER->set_str(clave.c_str(), valor.c_str());
    }

    inline std::string get_str(const std::string& clave) {
        return std::string(TIGER->get_str(clave.c_str()));
    }

    inline void set_float(const std::string& clave, double valor) {
        TIGER->set_float(clave.c_str(), valor);
    }

    inline double get_float(const std::string& clave) {
        return TIGER->get_float(clave.c_str());
    }

    inline bool get_bool(const std::string& clave) {
        return TIGER->get_bool(clave.c_str()) != 0;
    }

    inline void listar() {
        TIGER->listar_variables();
    }

    /* Temporizador RAII de alta precisión */
    class AutoTimer {
    private:
        std::string tag_;
    public:
        explicit AutoTimer(std::string tag = "Operación") : tag_(std::move(tag)) {
            TIGER->cronometro_iniciar();
        }
        ~AutoTimer() {
            double ms = TIGER->cronometro_ms();
            std::cout << TIGER_COLOR_CYAN << "[Timer Tiger] " << tag_ << ": " 
                      << ms << " ms" << TIGER_COLOR_RESET << std::endl;
        }
    };

} // namespace tiger

#endif /* TIGER_HPP */

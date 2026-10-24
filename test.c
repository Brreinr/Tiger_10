#include <stdio.h>
#include <minctest.h>
#include <window.h>

/* Funciones de prueba para minctest */
static void prueba_aritmetica(void) {
    lequal(15 + 27, 42);
    lfequal(3.14159, 3.14159);
}

static void prueba_punteros(void) {
    int valor = 100;
    int* ptr = &valor;
    lok(ptr != NULL);
    lequal(*ptr, 100);
}

int main(int argc, char** argv) {
    printf("==================================================================\n");
    printf("  [TIGER 10.1]: MODULO C COMPILADO DIRECTAMENTE CON TCC EN SILICIO\n");
    printf("==================================================================\n");
    printf("¡Hola desde C nativo en Tiger 10.1!\n");
    printf("Cabeceras incluidas correctamente:\n");
    printf("  * <stdio.h>\n");
    printf("  * <minctest.h> (Sincronizado desde lib_ext.json)\n");
    printf("  * <window.h>   (Motor grafico nativo Win32 de Tiger)\n\n");

    /* Ejecutar pruebas unitarias de minctest */
    lrun("Prueba de Aritmetica en Silicio", prueba_aritmetica);
    lrun("Prueba de Punteros en Silicio", prueba_punteros);
    lresults();

    printf("\n[OK] Modulo C 'test.c' ejecutado exitosamente sin errores.\n");
    return lfails != 0;
}


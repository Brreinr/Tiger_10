import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))
import contextlib
import io
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tiger_compiler import (
    CompilerError,
    Parser,
    compile_file,
    compile_source,
    main,
    run_file,
    run_native_file,
    tokenize,
)

HAS_WINDOWS_NATIVE_TOOLCHAIN = os.name == "nt" and all(
    shutil.which(tool) for tool in ("clang", "lld-link", "llvm-dlltool")
)


class TigerCompilerTests(unittest.TestCase):
    def test_decimal_and_filename_periods_are_not_terminators(self) -> None:
        tokens = tokenize('USAR avatar.png. VAR numero = 3.14.')
        values = [(token.kind, token.value) for token in tokens]
        self.assertIn(("FILE", "avatar.png"), values)
        self.assertIn(("NUMBER", "3.14"), values)
        self.assertEqual(sum(value == "." for kind, value in values if kind == "OP"), 2)

    def test_example_emits_native_and_wat(self) -> None:
        source = 'IMPRIMIR "Hola mundo", VAR Dato = 10, IMPRIMIR Dato.'
        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("syscall", assembly)
        self.assertIn("0x48, 0x6f, 0x6c, 0x61", assembly)
        self.assertIn('(import "env" "tiger_print"', wat)
        self.assertIn('"tiger_print_i64"', wat)
        self.assertEqual(assembly.count("tiger_str_0:"), 1)

    def test_variables_conditionals_pointers_and_macros_compile(self) -> None:
        source = """
        COMANDO SALUDAR { IMPRIMIR "Hola Tiger" }.
        FN principal() {
            VAR edad = 10,
            PTR sensor = @0x7FFF0000,
            SI edad >= 10 {
                SALUDAR,
                IMPRIMIR "Correcto"
            } SINO {
                IMPRIMIR "Incorrecto"
            }
        }.
        """
        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("mov rax, 10\n    mov r12, rax", assembly)
        self.assertIn("sensor equ 0x7fff0000", assembly)
        self.assertIn("i64.ge_s", wat)
        self.assertIn("$ptr_sensor", wat)

    def test_interpreter_still_runs_simple_program(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "programa.tg"
            source.write_text('IMPRIMIR "Hola Tiger".', encoding="utf-8")
            self.assertEqual(run_file(source), "Hola Tiger\n")

    @unittest.skipUnless(HAS_WINDOWS_NATIVE_TOOLCHAIN, "LLVM Windows x64 toolchain is not installed")
    def test_native_run_executes_machine_code_and_returns_only_program_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "programa.tg"
            source.write_text(
                'IMPRIMIR "Hola Tiger", VAR dato = 42, IMPRIMIR dato.',
                encoding="utf-8",
            )
            self.assertEqual(run_native_file(source), b"Hola Tiger\n42\n")

    @unittest.skipUnless(HAS_WINDOWS_NATIVE_TOOLCHAIN, "LLVM Windows x64 toolchain is not installed")
    def test_native_run_executes_arithmetic_and_short_circuit_logic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "operadores.tg"
            source.write_text(
                'VAR base = 4, VAR resultado = base + 3 * 2, '
                'IMPRIMIR resultado, '
                'SI 0 Y (1 / 0) { IMPRIMIR "error" } '
                'SINO { IMPRIMIR "bien" }.',
                encoding="utf-8",
            )
            self.assertEqual(run_native_file(source), b"10\nbien\n")

    def test_prints_numeric_variables_in_all_targets(self) -> None:
        source = "VAR Dato = 10. IMPRIMIR Dato."
        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("mov rax, 10\n    mov r12, rax", assembly)
        self.assertIn("tiger_num_buffer: resb 32", assembly)
        self.assertIn('"tiger_print_i64"', wat)
        self.assertIn("(local.get $v_Dato)", wat)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "numero.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "10\n")

    def test_arithmetic_precedence_and_logical_operators(self) -> None:
        source = (
            'VAR base = 4, '
            'VAR resultado = base + 3 * 2, '
            'IMPRIMIR resultado, '
            'SI (resultado == 10 Y NO (base < 0)) O 0 { '
            'IMPRIMIR "bien" '
            '} SINO { IMPRIMIR "mal" }.'
        )
        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("imul rax, rcx", assembly)
        self.assertIn(".Llogic_branch_", assembly)
        self.assertIn("i64.mul", wat)
        self.assertIn("i64.extend_i32_u", wat)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "operadores.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "10\nbien\n")

    def test_compound_assignments_and_comparison_operators(self) -> None:
        source = (
            "VAR total = 4, "
            "total += 3, "
            "total *= 2, "
            "total -= 5, "
            'SI total === 9 Y total != 8 Y total > 8 Y total < 10 { '
            'IMPRIMIR "bien" } SINO { IMPRIMIR "mal" }.'
        )
        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("imul rax, rcx", assembly)
        self.assertIn("i64.mul", wat)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "operadores_compuestos.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "bien\n")

    def test_arrays_support_grouping_indexing_and_assignment_in_interpreter(self) -> None:
        source = (
            "VAR INT valores = [10, 20], "
            "valores += [30], "
            "valores[3] <- (valores[0] + 5) * 2, "
            "IMPRIMIR valores[2], IMPRIMIR valores[3]."
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "arrays.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "30\n30\n")

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "indice_invalido.tg"
            path.write_text("VAR valores = [1], IMPRIMIR valores[2].", encoding="utf-8")
            with self.assertRaisesRegex(CompilerError, "índice o clave inexistente"):
                run_file(path)

        for target in ("asm", "wat"):
            with self.subTest(target=target), self.assertRaises(CompilerError) as caught:
                compile_source("VAR valores = [1, 2], IMPRIMIR valores[0].", target)
            self.assertEqual(caught.exception.code, "TIGER_BACKEND_UNSUPPORTED")

    def test_bracket_dictionaries_allow_mixed_values_and_dynamic_keys_in_interpreter(self) -> None:
        source = (
            'VAR config = [puerto: 8080, modo: "seguro"], '
            'config["debug"] <- 1, '
            "IMPRIMIR config.puerto, IMPRIMIR config.modo, IMPRIMIR config.debug."
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "diccionario.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "8080\nseguro\n1\n")

    def test_dictionaries_support_string_keys_fields_and_mutation(self) -> None:
        source = (
            'VAR usuario = {nombre: "Tiger", "nivel": 1}, '
            'usuario.nivel <- usuario["nivel"] + 1, '
            "IMPRIMIR usuario.nivel."
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "diccionario.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "2\n")

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "clave_inexistente.tg"
            path.write_text('VAR usuario = {nombre: "Tiger"}, IMPRIMIR usuario["edad"].', encoding="utf-8")
            with self.assertRaisesRegex(CompilerError, "índice o clave inexistente"):
                run_file(path)

    def test_classes_create_mutable_instances_and_compile_to_asm_and_wat(self) -> None:
        source = (
            'persona { CRE INT vida = 100, CRE var = 10, FN saludar { '
            'vida <- vida - 1, contador <- contador + 1, IMPRIMIR "hola" }. }. '
            "VAR contador = 0, "
            "jugador(persona) { vida <- 90 }, "
            "jugador-vida <- jugador-vida + 5, "
            "IMPRIMIR jugador-vida, jugador-saludar, IMPRIMIR jugador-vida, "
            "persona-saludar, IMPRIMIR contador."
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "clases.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "95\nhola\n94\nhola\n2\n")

        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("mov r13, rax", assembly)
        self.assertIn("$v_jugador__vida", wat)
        self.assertIn("(local.set $v_jugador__vida", wat)

        method_call = "Jugador { COMANDO metodo_1 -> mov. }. Jugador-metodo_1."
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metodo.tg"
            path.write_text(method_call, encoding="utf-8")
            with self.assertRaises(CompilerError) as caught:
                run_file(path)
            self.assertEqual(caught.exception.code, "TIGER_METHOD_BACKEND_REQUIRED")

    def test_typed_functions_inline_into_both_backends(self) -> None:
        source = (
            "FN sumar(INT a, INT b) { RETORNAR a + b }. "
            "FN principal() { VAR total = sumar(20, 22), IMPRIMIR total }."
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "funciones.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "42\n")

        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("add rax, rcx", assembly)
        self.assertIn("i64.add", wat)
        with self.assertRaisesRegex(CompilerError, "espera 2 argumentos"):
            compile_source(
                "FN sumar(INT a, INT b) { RETORNAR a + b }. "
                "FN principal() { IMPRIMIR sumar(1) }.",
                "wat",
            )
        with self.assertRaisesRegex(CompilerError, "espera int, no string"):
            compile_source(
                'FN duplicar(INT a) { RETORNAR a + a }. '
                'FN principal() { IMPRIMIR duplicar("texto") }.',
                "asm",
            )

    def test_function_parameters_default_to_int_when_untyped(self) -> None:
        source = (
            "FN sumar(num, nivel) { RETORNAR num + nivel }. "
            "FN principal() { VAR res = sumar(10, 20), IMPRIMIR res }."
        )
        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("add rax, rcx", assembly)
        self.assertIn("i64.add", wat)

    def test_parameterized_commands_expand_arguments_and_validate_arity(self) -> None:
        source = (
            "COMANDO mostrar(valor) { IMPRIMIR valor }. "
            'FN principal() { mostrar(7), mostrar(8), mostrar("Tiger") }.'
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "macros.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "7\n8\nTiger\n")

        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertEqual(assembly.count("mov rax, 7"), 1)
        self.assertIn("(i64.const 8)", wat)
        self.assertIn("Tiger", assembly)
        self.assertIn("Tiger", wat)
        with self.assertRaisesRegex(CompilerError, "espera 1 argumentos"):
            compile_source(
                "COMANDO mostrar(valor) { IMPRIMIR valor }. "
                "FN principal() { mostrar() }.",
                "wat",
            )

    def test_pointc_int_pointer_reads_and_writes_in_asm_and_wat(self) -> None:
        source = (
            "PTR INT sensor = @16, sensor -> 9, "
            "VAR dato = *sensor, IMPRIMIR dato."
        )
        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("mov qword [r11], rax", assembly)
        self.assertIn("mov rax, qword [r11]", assembly)
        self.assertIn("(i64.store (global.get $ptr_sensor)", wat)
        self.assertIn("(i64.load (global.get $ptr_sensor))", wat)
        self.assertIn("(memory (export \"memory\") 1 65536)", wat)
        large_offset = compile_source(
            "PTR INT sensor = @131072, sensor -> 1.",
            "wat",
        )
        self.assertIn("(memory.grow (i32.const 2))", large_offset)

        with self.assertRaisesRegex(CompilerError, "PointC requiere ASM/WAT"):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "pointer.tg"
                path.write_text(source, encoding="utf-8")
                run_file(path)

    def test_hyphen_member_access_does_not_change_numeric_subtraction(self) -> None:
        source = (
            "Juego { CRE INT num = 10 }. "
            "juego(Juego) {}, "
            "juego-num <- juego-num + 2, "
            "VAR resultado = 15 - 6, "
            "IMPRIMIR juego-num, IMPRIMIR resultado."
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "campo_guion.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "12\n9\n")

    def test_english_and_symbolic_logical_aliases(self) -> None:
        cases = (
            ('SI 1 AND NOT 0 OR 0 { IMPRIMIR "bien" } SINO { IMPRIMIR "mal" }.', "bien\n"),
            ('SI 0 && (1 / 0) || !0 { IMPRIMIR "bien" } SINO { IMPRIMIR "mal" }.', "bien\n"),
            ('SI 1 ES 1 Y 1 IS 1 { IMPRIMIR "bien" } SINO { IMPRIMIR "mal" }.', "bien\n"),
            ('SI 1 IS 0 O 1 ES 1 { IMPRIMIR "bien" } SINO { IMPRIMIR "mal" }.', "bien\n"),
        )
        with tempfile.TemporaryDirectory() as directory:
            for index, (source, expected) in enumerate(cases):
                path = Path(directory) / f"logica_{index}.tg"
                path.write_text(source, encoding="utf-8")
                self.assertEqual(run_file(path), expected)

    def test_typed_variables_infer_validate_and_reassign(self) -> None:
        source = (
            'INT VAR edad = 10, '
            'VAR FLO precio = 3.5, '
            'STR VAR saludo = "Hola", '
            'INYECTAR ENT VAR pasos <- 4, '
            '12 -> edad, '
            'precio <- precio * 2.0, '
            'INYECTAR saludo <- "Tiger", '
            'INYECTAR pasos <- pasos + 1, '
            'edad -> pasos, '
            'IMPRIMIR edad, IMPRIMIR precio, IMPRIMIR saludo, IMPRIMIR pasos.'
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tipos.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "12\n7.0\nTiger\n12\n")

        with self.assertRaisesRegex(CompilerError, "no se puede reasignar CONST"):
            compile_source('CONST dato = 1, dato <- 2.')
        with self.assertRaisesRegex(CompilerError, "se declaró como int"):
            compile_source('INT VAR dato = "texto".')
        with self.assertRaisesRegex(CompilerError, "INPUT requiere una variable con tipo declarado"):
            compile_source("VAR dato = INPUT.")

    def test_injecting_a_new_typed_variable_and_copying_text(self) -> None:
        source = (
            'INYECTAR ENT VAR cantidad <- 8, '
            'INYECTAR CAD VAR original <- "Tiger", '
            'STR VAR copia = "", '
            'original -> copia, '
            'IMPRIMIR cantidad, IMPRIMIR copia.'
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "inyectar.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "8\nTiger\n")

    def test_typed_input_and_string_assignment_compile_for_all_targets(self) -> None:
        source = (
            'INT VAR edad = 0, FLO VAR peso = 0.0, STR VAR nombre = "", '
            'INYECTAR -> INPUT -> edad, '
            'INPUT -> peso, '
            'INYECTAR -> INPUT -> nombre, '
            'IMPRIMIR edad, IMPRIMIR peso, IMPRIMIR nombre.'
        )
        assembly = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn("extern scanf", assembly)
        self.assertIn("tiger_fmt_input_float", assembly)
        self.assertIn("f64", wat)
        self.assertIn('"tiger_read_i64"', wat)
        self.assertIn('"tiger_read_f64"', wat)
        self.assertIn('"tiger_read_string"', wat)
        self.assertIn('"tiger_print_f64"', wat)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "entrada.tg"
            path.write_text(source, encoding="utf-8")
            with patch("builtins.input", side_effect=["21", "64.5", "Tiger"]):
                self.assertEqual(run_file(path), "21\n64.5\nTiger\n")

    def test_entrada_and_input_variations(self) -> None:
        source = (
            'ENTRADA -> directo, '
            'ENTRADA "Nombre: " -> con_mensaje, '
            'ENTRADA("Alias: ") -> con_parentesis, '
            '"Ciudad: " -> ENTRADA -> desde_cadena, '
            'inverso <- ENTRADA, '
            'inverso_msg <- ENTRADA "Pais: ", '
            'ENT VAR edad = ENTRADA "Edad: ", '
            'ENTRADA "Nivel: " -> ENT nivel, '
            'Jugador { CAD VAR nombre = "", ENT VAR puntos = 0 }. '
            'j(Jugador) { nombre <- "Anon", puntos <- 10 }, '
            'ENTRADA "Jugador: " -> j-nombre, '
            'j-puntos <- ENTRADA "Puntos: ", '
            'VAR dic = {"rol": "base"}, '
            'ENTRADA "Rol: " -> dic.rol, '
            'dic["extra"] <- ENTRADA "Extra: ", '
            'VAR lista = [0, 0], '
            'ENTRADA "Item0: " -> lista[0], '
            'lista[1] <- ENTRADA "Item1: ", '
            'IMPRIMIR directo, '
            'IMPRIMIR con_mensaje, '
            'IMPRIMIR con_parentesis, '
            'IMPRIMIR desde_cadena, '
            'IMPRIMIR inverso, '
            'IMPRIMIR inverso_msg, '
            'IMPRIMIR edad + nivel, '
            'IMPRIMIR j-nombre, '
            'IMPRIMIR j-puntos, '
            'IMPRIMIR dic.rol, '
            'IMPRIMIR dic["extra"], '
            'IMPRIMIR lista[0] + lista[1], '
            'IMPRIMIR "Hola " + ENTRADA "Saludo: ".'
        )
        prompts: list[str] = []
        responses = iter([
            "uno",
            "Balam",
            "Tigre",
            "Quito",
            "dos",
            "Ecuador",
            "20",
            "5",
            "Jag",
            "99",
            "admin",
            "vip",
            "10",
            "30",
            "Mundo",
        ])

        def fake_input(prompt: str = "") -> str:
            prompts.append(prompt)
            return next(responses)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "entrada_variaciones.tg"
            path.write_text(source, encoding="utf-8")
            with patch("builtins.input", side_effect=fake_input):
                output = run_file(path)
        self.assertEqual(
            output,
            "uno\nBalam\nTigre\nQuito\ndos\nEcuador\n25\nJag\n99\nadmin\nvip\n40\nHola Mundo\n",
        )
        self.assertEqual(
            prompts,
            [
                "",
                "Nombre: ",
                "Alias: ",
                "Ciudad: ",
                "",
                "Pais: ",
                "Edad: ",
                "Nivel: ",
                "Jugador: ",
                "Puntos: ",
                "Rol: ",
                "Extra: ",
                "Item0: ",
                "Item1: ",
                "Saludo: ",
            ],
        )

    @unittest.skipUnless(HAS_WINDOWS_NATIVE_TOOLCHAIN, "LLVM Windows x64 toolchain is not installed")
    def test_native_typed_input_float_and_string_output(self) -> None:
        source = (
            'INT VAR edad = 0, FLO VAR peso = 0.0, STR VAR nombre = "", '
            'INYECTAR -> INPUT -> edad, '
            'INPUT -> peso, '
            'INYECTAR -> INPUT -> nombre, '
            'IMPRIMIR edad, IMPRIMIR peso, IMPRIMIR nombre.'
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "entrada_nativa.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(
                run_native_file(path, input_data=b"21 64.5 Tiger\n"),
                b"21\n64.5\nTiger\n",
            )

    @unittest.skipUnless(HAS_WINDOWS_NATIVE_TOOLCHAIN, "LLVM Windows x64 toolchain is not installed")
    def test_native_float_arithmetic_and_comparison(self) -> None:
        source = (
            'FLO VAR peso = 3.5, '
            'peso <- peso * 2.0, '
            'SI peso >= 7.0 { IMPRIMIR peso } SINO { IMPRIMIR 0.0 }.'
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "flotantes.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_native_file(path), b"7\n")

    def test_division_and_remainder_use_truncation_toward_zero(self) -> None:
        source = "VAR cociente = -7 / 3, VAR resto = -7 % 3, IMPRIMIR cociente, IMPRIMIR resto."
        self.assertIn("idiv rcx", compile_source(source, "asm"))
        wat = compile_source(source, "wat")
        self.assertIn("i64.div_s", wat)
        self.assertIn("i64.rem_s", wat)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "division.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "-2\n-1\n")

    def test_logical_and_short_circuits_division_by_zero(self) -> None:
        source = 'SI 0 Y (1 / 0) { IMPRIMIR "error" } SINO { IMPRIMIR "seguro" }.'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "corto_circuito.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "seguro\n")
            self.assertIn(".Llogic_branch_", compile_source(source, "asm"))
            self.assertIn("(if (result i64)", compile_source(source, "wat"))

    def test_division_by_zero_reports_runtime_error(self) -> None:
        source = "VAR resultado = 5 / 0."
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "division_cero.tg"
            path.write_text(source, encoding="utf-8")
            with self.assertRaisesRegex(CompilerError, "división entre cero"):
                run_file(path)

    def test_imports_and_macro_expansion(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "modulo.tg").write_text(
                'COMANDO SALUDO { IMPRIMIR "importado" }.\n',
                encoding="utf-8",
            )
            (root / "principal.tg").write_text(
                'USAR "modulo.tg". FN principal { SALUDO }.',
                encoding="utf-8",
            )
            output = compile_file(root / "principal.tg")
            self.assertIn("0x69, 0x6d, 0x70, 0x6f, 0x72, 0x74, 0x61, 0x64, 0x6f", output)

    def test_rejects_unsafe_or_unimplemented_keywords(self) -> None:
        with self.assertRaisesRegex(CompilerError, "aún no se admite"):
            compile_source("SYSCALL 0.")

    def test_deduplicates_print_strings(self) -> None:
        output = compile_source('IMPRIMIR "igual", IMPRIMIR "igual".')
        self.assertEqual(output.count("tiger_str_0:"), 1)

    def test_process_requires_final_period(self) -> None:
        with self.assertRaises(CompilerError) as caught:
            compile_source('IMPRIMIR "falta cierre"')
        self.assertEqual(caught.exception.code, "TIGER_MISSING_PERIOD")
        self.assertEqual(caught.exception.line, 1)

    def test_subprocesses_require_commas(self) -> None:
        with self.assertRaises(CompilerError) as caught:
            compile_source('IMPRIMIR "uno" IMPRIMIR "dos".')
        self.assertEqual(caught.exception.code, "TIGER_MISSING_SEPARATOR")

    def test_trailing_comma_and_semicolon_are_rejected_clearly(self) -> None:
        with self.assertRaises(CompilerError) as trailing:
            compile_source('IMPRIMIR "uno",.')
        self.assertEqual(trailing.exception.code, "TIGER_TRAILING_COMMA")

        with self.assertRaisesRegex(CompilerError, "no con ';'"):
            compile_source('IMPRIMIR "uno";')

    def test_unclosed_string_and_block_have_specific_diagnostics(self) -> None:
        with self.assertRaises(CompilerError) as string_error:
            compile_source('IMPRIMIR "sin cierre.')
        self.assertEqual(string_error.exception.code, "TIGER_UNTERMINATED_STRING")
        self.assertEqual(string_error.exception.line, 1)

        with self.assertRaises(CompilerError) as block_error:
            compile_source('FN principal { IMPRIMIR "x"')
        self.assertEqual(block_error.exception.code, "TIGER_UNCLOSED_BLOCK")

    def test_fix_writes_copy_and_keeps_original(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "sin_cierre.tg"
            output = root / "sin_cierre.asm"
            original = 'IMPRIMIR "arreglado" # comentario final'
            source.write_text(original, encoding="utf-8")

            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                result = main([str(source), "--target", "asm", "--fix", "-o", str(output)])

            fixed_source = root / "sin_cierre.fixed.tg"
            self.assertEqual(result, 0)
            self.assertEqual(source.read_text(encoding="utf-8"), original)
            self.assertTrue(fixed_source.exists())
            self.assertIn('IMPRIMIR "arreglado" # comentario final\n.', fixed_source.read_text(encoding="utf-8"))
            self.assertIn("0x61, 0x72, 0x72, 0x65, 0x67, 0x6c, 0x61, 0x64, 0x6f", output.read_text(encoding="utf-8"))
            self.assertIn("Archivo corregido", stderr.getvalue())

            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                repeated_result = main([str(source), "--target", "asm", "--fix", "-o", str(output)])
            self.assertEqual(repeated_result, 0)

    def test_fix_does_not_overwrite_an_existing_fixed_copy(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "sin_cierre.tg"
            fixed = root / "sin_cierre.fixed.tg"
            source.write_text('IMPRIMIR "x"', encoding="utf-8")
            fixed.write_text("no sobrescribir", encoding="utf-8")

            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                result = main([str(source), "--fix"])

            self.assertEqual(result, 1)
            self.assertEqual(fixed.read_text(encoding="utf-8"), "no sobrescribir")

    def test_tipo_py_reads_and_executes_python_files_and_shares_context(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "calculo.py").write_text(
                "print('Ejecutando calculo.py')\n"
                "resultado_py = base_tg * 3\n"
                "def potenciar(x, n):\n"
                "    return x ** n\n",
                encoding="utf-8",
            )
            (root / "reporte.py").write_text(
                "salida = f'Total={resultado_py}'\n"
                "print(salida)\n",
                encoding="utf-8",
            )
            tg_file = root / "app.tg"
            tg_file.write_text(
                "VAR base_tg = 7.\n"
                "TIPO PY { calculo.py }.\n"
                "IMPRIMIR resultado_py.\n"
                "VAR p = potenciar(2, 5).\n"
                "IMPRIMIR p.\n"
                'TYPE PY { "reporte.py" -> guardado }.\n'
                "IMPRIMIR guardado.\n",
                encoding="utf-8",
            )
            output = run_file(tg_file)
            self.assertEqual(
                output.splitlines(),
                [
                    "Ejecutando calculo.py",
                    "21",
                    "32",
                    "Total=21",
                    "Total=21",
                ],
            )

    def test_tipo_py_supports_multiple_arrow_and_assignment_variants(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "script1.py").write_text("valor = 100\n", encoding="utf-8")
            (root / "script2.py").write_text(" __result__ = valor + 50\n".lstrip(), encoding="utf-8")
            (root / "script3.py").write_text("print('hola desde py')\n", encoding="utf-8")
            tg_file = root / "variantes.tg"
            tg_file.write_text(
                "Caja {\n"
                "    VAR dato = 0.\n"
                "}.\n"
                "c (Caja) { }.\n"
                'VAR d = {"k": 0}.\n'
                "VAR lista = [0].\n"
                'TIPO PY { LEER "script1.py", c-dato <- "script2.py", "script3.py" -> d["k"] }.\n'
                'lista[0] <- TIPO PY "script2.py".\n'
                "IMPRIMIR valor, IMPRIMIR c-dato, IMPRIMIR d[\"k\"], IMPRIMIR lista[0].\n",
                encoding="utf-8",
            )
            output = run_file(tg_file)
            self.assertEqual(output.splitlines(), ["hola desde py", "100", "150", "hola desde py", "150"])

    def test_tipo_py_missing_file_raises_compiler_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tg_file = root / "error_py.tg"
            tg_file.write_text('TIPO PY { "no_existe.py" }.\n', encoding="utf-8")
            with self.assertRaisesRegex(CompilerError, "no se pudo encontrar el archivo Python"):
                run_file(tg_file)

    def test_flexible_comando_allows_redefinition_overloading_and_repeated_vars(self) -> None:
        source = (
            'COMANDO accion { IMPRIMIR "v1" }.\n'
            'COMANDO accion { VAR temp = 10, IMPRIMIR temp }.\n'
            'COMANDO accion(ENT x) { VAR temp = x * 2, IMPRIMIR temp }.\n'
            'COMANDO doble(ENT n) -> RETORNAR n * 2.\n'
            "FN principal() {\n"
            "    accion,\n"
            "    accion(7),\n"
            "    accion(9),\n"
            "    VAR r = doble(15),\n"
            "    IMPRIMIR r\n"
            "}.\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "comando_flexible.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "10\n14\n18\n30\n")

    def test_pipe_separator_and_hyphen_dictionary_access(self) -> None:
        source = (
            "COMANDO mostrar(a | b) { IMPRIMIR a + b }.\n"
            "FN calcular(ENT x | ENT y | ENT z) { RETORNAR x * y + z }.\n"
            "FN principal() {\n"
            "    mostrar(10 | 5),\n"
            "    VAR total = calcular(3 | 4 | 2),\n"
            "    IMPRIMIR total,\n"
            "    VAR arr = [100 | 200 | 300],\n"
            "    VAR tupla = (7 | 8 | 9),\n"
            "    IMPRIMIR arr[1] + tupla[2],\n"
            "    VAR ejemplo = [key: 0 | otro: 5],\n"
            "    ejemplo-key = 1,\n"
            "    ejemplo-nuevo = 42,\n"
            "    ejemplo-otro += 10,\n"
            "    IMPRIMIR ejemplo-key,\n"
            "    IMPRIMIR ejemplo-nuevo,\n"
            "    IMPRIMIR ejemplo-otro\n"
            "}.\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pipe_y_dict.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "15\n14\n209\n1\n42\n15\n")

    def test_loops_compile_to_asm_wat_and_run_in_interpreter_and_native(self) -> None:
        source = (
            "FN principal() {\n"
            "    VAR suma = 0,\n"
            "    CICLO (VAR i = 1 | i <= 6 | i += 1) {\n"
            "        SI i == 2 { CONTINUAR },\n"
            "        SI i == 6 { SALIR },\n"
            "        suma += i\n"
            "    },\n"
            "    IMPRIMIR suma,\n"
            "    VAR veces_cont = 0,\n"
            "    CICLO 3 VECES {\n"
            "        veces_cont += 10\n"
            "    },\n"
            "    IMPRIMIR veces_cont,\n"
            "    VAR r = 0,\n"
            "    CICLO (paso | 1 | 4) {\n"
            "        r += paso\n"
            "    },\n"
            "    IMPRIMIR r,\n"
            "    VAR flecha = 0,\n"
            "    CICLO k = 1 -> 3 {\n"
            "        flecha += k\n"
            "    },\n"
            "    IMPRIMIR flecha\n"
            "}.\n"
        )
        asm = compile_source(source, "asm")
        wat = compile_source(source, "wat")
        self.assertIn(".Lloop_start_", asm)
        self.assertIn(".Lloop_step_", asm)
        self.assertIn(".Lloop_end_", asm)
        self.assertIn("(loop $loop_start_", wat)
        self.assertIn("(block $loop_break_", wat)
        self.assertIn("(block $loop_step_", wat)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ciclos.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "13\n30\n6\n3\n")
            if HAS_WINDOWS_NATIVE_TOOLCHAIN:
                self.assertEqual(run_native_file(path), b"13\n30\n6\n3\n")

    def test_usar_imports_tiger_libraries_and_tg_files(self) -> None:
        lib_source = (
            "USAR (matematicas | base | consola).\n"
            "FN principal() {\n"
            "    IMPRIMIR potencia_ent(2 | 5),\n"
            "    IMPRIMIR factorial_ent(5),\n"
            "    IMPRIMIR fibonacci_ent(7),\n"
            "    IMPRIMIR cuadrado_ent(6),\n"
            "    IMPRIMIR max_ent(14 | 9)\n"
            "}.\n"
        )
        asm = compile_source(lib_source, "asm")
        wat = compile_source(lib_source, "wat")
        self.assertIn(".Lloop_start_", asm)
        self.assertIn("(loop $loop_start_", wat)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "utilidades.tg").write_text(
                "FN sumar_triple(ENT a | ENT b | ENT c) { RETORNAR a + b + c }.\n",
                encoding="utf-8",
            )
            libs_dir = root / "libs"
            libs_dir.mkdir()
            (libs_dir / "extra.tg").write_text(
                "COMANDO imprimir_doble(v) { IMPRIMIR v * 2 }.\n",
                encoding="utf-8",
            )
            app = root / "app.tg"
            app.write_text(
                "USAR matematicas.\n"
                "USAR utilidades.tg.\n"
                "USAR extra.\n"
                "FN principal() {\n"
                "    IMPRIMIR potencia_ent(3 | 3),\n"
                "    IMPRIMIR sumar_triple(10 | 20 | 30),\n"
                "    imprimir_doble(21)\n"
                "}.\n",
                encoding="utf-8",
            )
            self.assertEqual(run_file(app), "27\n60\n42\n")
            if HAS_WINDOWS_NATIVE_TOOLCHAIN:
                self.assertEqual(run_native_file(app), b"27\n60\n42\n")

    def test_usar_en_asm_wat_both_header_directive(self) -> None:
        source_asm = "USAR EN ASM.\nIMPRIMIR 42.\n"
        out_asm = compile_source(source_asm)
        self.assertIn("BITS 64", out_asm)

        source_wat = "USAR EN WAT.\nIMPRIMIR 42.\n"
        out_wat = compile_source(source_wat)
        self.assertIn("(module", out_wat)

        source_both = "USAR EN ASM Y WAT.\nIMPRIMIR 42.\n"
        out_both = compile_source(source_both)
        self.assertIn("; === ASM ===", out_both)
        self.assertIn("BITS 64", out_both)
        self.assertIn(";; === WAT ===", out_both)
        self.assertIn("(module", out_both)

    def test_compilation_architecture_two_parallel_executors(self) -> None:
        # Párrafo único: se usa 1 solo ejecutador
        single_paragraph = "IMPRIMIR 100.\n"
        asm_single = compile_source(single_paragraph, "asm")
        self.assertIn("BITS 64", asm_single)

        # Múltiples procesos empaquetados: 2 ejecutadores en paralelo saltando de 2 en 2
        multi_process = (
            "VAR a = 1.\n"
            "VAR b = 2.\n"
            "VAR c = 3.\n"
            "VAR d = 4.\n"
            "IMPRIMIR a + b + c + d.\n"
        )
        asm_multi = compile_source(multi_process, "asm")
        wat_multi = compile_source(multi_process, "wat")
        self.assertIn("BITS 64", asm_multi)
        self.assertIn("(module", wat_multi)

    def test_asistente_integrated_agent_repair_and_intent(self) -> None:
        import tiger_compiler
        # Prueba con transporte simulado (mock)
        def mock_transport(config, system, prompt):
            self.assertEqual(config["model"], "gemini-test")
            return "VAR x = 99.\nIMPRIMIR x.\n"

        tiger_compiler.ASSISTANT_TRANSPORT = mock_transport
        try:
            source = (
                'USAR ASSISTANT API("test_key" | "gemini-test" | "no").\n'
                'ASISTENTE INI\n'
                '--- codigo con errores ---\n'
                'ASISTENTE FIN "arregla el codigo".\n'
            )
            parsed = Parser(source).parse()
            self.assertTrue(len(parsed) >= 2)
        finally:
            tiger_compiler.ASSISTANT_TRANSPORT = None

        # Prueba con reparador local offline
        source_offline = (
            'USAR ASSISTANT API("" | "gemini-1.5-flash" | "no").\n'
            'ASISTENTE INI\n'
            '--- print("Hola Tiger"); ---\n'
            'ASISTENTE FIN "corrige errores".\n'
        )
        parsed_offline = Parser(source_offline).parse()
        self.assertTrue(len(parsed_offline) >= 1)

    def test_usar_web_tg_and_uppercase_class_keyword(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Libraries").mkdir()
            (root / "Libraries" / "web.tg").write_text(
                "Servidor {\n"
                "    CRE ENT puerto = 8080,\n"
                "    FN iniciar(ENT p) {\n"
                "        puerto <- p,\n"
                "        IMPRIMIR puerto,\n"
                "        RETORNAR puerto\n"
                "    }\n"
                "}.\n",
                encoding="utf-8",
            )
            app = root / "app.tg"
            app.write_text(
                "USAR web.tg.\n"
                "FN principal() {\n"
                "    servidor_app(SERVIDOR) { puerto = 8080 },\n"
                "    servidor_app-iniciar(3000)\n"
                "}.\n",
                encoding="utf-8",
            )
            self.assertEqual(run_file(app).strip(), "3000")


    def test_boolean_literals_and_spanish_keywords(self) -> None:
        source = (
            "BOOL VAR a = TRUE, "
            "BOOLEANO VAR b = FALSO, "
            "VAR c = VERDADERO, "
            "VAR d = FALSE, "
            "IMPRIMIR a, "
            "IMPRIMIR b, "
            "IMPRIMIR c, "
            "IMPRIMIR d."
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "booleanos.tg"
            path.write_text(source, encoding="utf-8")
            output = run_file(path)
            self.assertEqual(output, "TRUE\nFALSO\nVERDADERO\nFALSE\n")

    def test_comparisons_and_logic_return_boolean_strings(self) -> None:
        source = (
            "VAR mayor = 10 > 5, "
            "VAR menor = 3 >= 9, "
            "VAR igual = 4 == 4, "
            "VAR neg = NO (1 == 1), "
            "IMPRIMIR mayor, "
            "IMPRIMIR menor, "
            "IMPRIMIR igual, "
            "IMPRIMIR neg."
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "logica_bool.tg"
            path.write_text(source, encoding="utf-8")
            output = run_file(path)
            self.assertEqual(output, "TRUE\nFALSE\nTRUE\nFALSE\n")

    def test_boolean_conditionals_and_emission(self) -> None:
        source = (
            "SI VERDADERO { IMPRIMIR \"ok\" } SINO { IMPRIMIR \"fail\" }.\n"
            "SI FALSO { IMPRIMIR \"fail\" } SINO { IMPRIMIR \"pasa\" }.\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cond_bool.tg"
            path.write_text(source, encoding="utf-8")
            self.assertEqual(run_file(path), "ok\npasa\n")
        wat = compile_source("BOOL VAR x = TRUE, IMPRIMIR x.", "wat")
        self.assertIn("TRUE\\n", wat)
        asm = compile_source("BOOL VAR x = TRUE, IMPRIMIR x.", "asm")
        self.assertIn("TRUE\\n", asm)

    def test_bilingual_keywords_english_and_spanish(self) -> None:
        source_en = (
            "INTEGER LET total = 42, "
            "FLOAT CONST pi = 3.14, "
            "BOOLEAN LET activo = TRUE, "
            "WRITE total, "
            "SHOW pi, "
            "WRITE activo."
        )
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "bilingual_en.tg"
            p.write_text(source_en, encoding="utf-8")
            self.assertEqual(run_file(p), "42\n3.14\nTRUE\n")

        source_es = (
            "ENTERO VARIABLE total = 100, "
            "DECIMAL CONSTANTE descuento = 0.5, "
            "BOOLEANO VARIABLE flag = FALSO, "
            "ESCRIBIR total, "
            "MOSTRAR descuento, "
            "ESCRIBIR flag."
        )
        with tempfile.TemporaryDirectory() as directory:
            p2 = Path(directory) / "bilingual_es.tg"
            p2.write_text(source_es, encoding="utf-8")
            self.assertEqual(run_file(p2), "100\n0.5\nFALSO\n")

    def test_bilingual_error_messages(self) -> None:
        try:
            compile_source('IMPRIMIR "sin punto final"')
            self.fail("Expected CompilerError")
        except CompilerError as e:
            self.assertIn("falta '.' para cerrar el proceso", str(e))
            self.assertIn("missing '.'", str(e))

        try:
            compile_source('IMPRIMIR "error";')
            self.fail("Expected CompilerError")
        except CompilerError as e:
            self.assertIn("Tiger cierra procesos con '.'", str(e))
            self.assertIn("Tiger terminates statements with '.'", str(e))

    def test_flexible_parsing_mode(self) -> None:
        from tiger_compiler import Parser
        # Missing trailing period at EOF in flexible mode:
        statements = Parser('LET x = 10, WRITE x', flexible=True).parse()
        self.assertEqual(len(statements), 2)

        # Semicolon in flexible mode:
        statements_semi = Parser('LET a = 1; LET b = 2;', flexible=True).parse()
        self.assertEqual(len(statements_semi), 2)

        # Trailing comma before closing brace in flexible mode:
        statements_block = Parser('FN test() { LET a = 1, }.', flexible=True).parse()
        self.assertEqual(len(statements_block), 1)

    def test_direct_header_target_compilation_and_fast_execution(self) -> None:
        source_wat = "USAR EN WAT.\nLET x = 7.\nWRITE x."
        wat = compile_source(source_wat)
        self.assertIn('(module', wat)
        self.assertIn('$v_x', wat)

        source_both = "USAR EN ASM Y WAT.\nLET y = 8.\nWRITE y."
        both = compile_source(source_both)
        self.assertIn("=== ASM ===", both)
        self.assertIn("=== WAT ===", both)

    @unittest.skipUnless(HAS_WINDOWS_NATIVE_TOOLCHAIN, "LLVM Windows x64 toolchain is not installed")
    def test_native_cache_acceleration(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / "test_cache.tg"
            p.write_text('LET a = 50, WRITE a.', encoding="utf-8")
            res1 = run_native_file(p)
            self.assertEqual(res1, b"50\n")
            # Second run uses cached binary instantly:
            res2 = run_native_file(p)
            self.assertEqual(res2, b"50\n")

    def test_variable_matching_type_keyword_name(self) -> None:
        source = (
            "Boton {\n"
            '    CRE CAD texto = "Hola",\n'
            '    FN cambiar(CAD t) {\n'
            '        texto <- t,\n'
            '        RETORNAR 1\n'
            '    }\n'
            "}.\n"
            'IMPRIMIR "ok".'
        )
        output = compile_source(source, "wat")
        self.assertIn("(module", output)

        source2 = (
            "Boton {\n"
            '    CRE CAD clase = "btn",\n'
            '    FN render() {\n'
            '        IMPRIMIR clase,\n'
            '        RETORNAR 1\n'
            '    }\n'
            "}.\n"
            'IMPRIMIR "ok".'
        )
        self.assertIn("(module", compile_source(source2, "wat"))


if __name__ == "__main__":
    unittest.main()

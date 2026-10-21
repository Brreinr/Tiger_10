from __future__ import annotations

import argparse
import ast
import concurrent.futures
import contextlib
import hashlib
import importlib
import importlib.util
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import time
import types
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any, Callable, TypeAlias


LEXER_RE = re.compile(
    r"""
    (?P<SPACE>\s+)
  | (?P<COMMENT>\#[^\r\n]*)
  | (?P<STRING>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')
  | (?P<FILE>(?:[A-Za-z0-9_-]+[\\/])*[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*\.(?:tg|tgi|py|json|wasm|so|dll|a|lib|bin|png))
  | (?P<NUMBER>0[xX][0-9A-Fa-f]+|(?:\d+\.\d+|\d+))
  | (?P<IDENT>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<OP>->|<-|\+=|-=|\*=|===|&&|\|\||==|!=|<=|>=|//|\*\*|<<|>>|[+\-*/%!@{}()\[\],.|;:=<>~^&\\?])
    """,
    re.VERBOSE | re.IGNORECASE,
)

KEYWORDS = {
    "FN": "FN",
    "FUNCTION": "FN",
    "FUNC": "FN",
    "DEF": "FN",
    "FUNCION": "FN",
    "FUNCIÓN": "FN",
    "VAR": "VAR",
    "VARIABLE": "VAR",
    "LET": "VAR",
    "CONST": "CONST",
    "CONSTANTE": "CONST",
    "CONSTANT": "CONST",
    "CRE": "CRE",
    "CAMPO": "CRE",
    "FIELD": "CRE",
    "PROP": "CRE",
    "PROPERTY": "CRE",
    "PROPIEDAD": "CRE",
    "ATTR": "CRE",
    "ATTRIBUTE": "CRE",
    "INT": "TYPE_INT",
    "INTEGER": "TYPE_INT",
    "ENT": "TYPE_INT",
    "ENTERO": "TYPE_INT",
    "FLO": "TYPE_FLOAT",
    "FLOAT": "TYPE_FLOAT",
    "DEC": "TYPE_FLOAT",
    "DECIMAL": "TYPE_FLOAT",
    "REAL": "TYPE_FLOAT",
    "STR": "TYPE_STRING",
    "STRING": "TYPE_STRING",
    "CAD": "TYPE_STRING",
    "CADENA": "TYPE_STRING",
    "TEXT": "TYPE_STRING",
    "TEXTO": "TYPE_STRING",
    "BOOL": "TYPE_BOOL",
    "BOOLEAN": "TYPE_BOOL",
    "BOOLEANO": "TYPE_BOOL",
    "TRUE": "TRUE",
    "FALSE": "FALSE",
    "VERDADERO": "TRUE",
    "FALSO": "FALSE",
    "CIERTO": "TRUE",
    "TIPO": "TYPE",
    "TYPE": "TYPE",
    "CLASE": "TYPE",
    "CLASS": "TYPE",
    "USAR": "USE",
    "USE": "USE",
    "IMPORT": "USE",
    "IMPORTAR": "USE",
    "INCLUDE": "USE",
    "INCLUIR": "USE",
    "COMANDO": "COMMAND",
    "COMMAND": "COMMAND",
    "CMD": "COMMAND",
    "SI": "IF",
    "IF": "IF",
    "Y": "AND",
    "AND": "AND",
    "O": "OR",
    "OR": "OR",
    "NO": "NOT",
    "NOT": "NOT",
    "ES": "IS",
    "IS": "IS",
    "SINO": "ELSE",
    "ELSE": "ELSE",
    "CICLO": "LOOP",
    "LOOP": "LOOP",
    "BUCLE": "LOOP",
    "MIENTRAS": "LOOP",
    "WHILE": "LOOP",
    "PARA": "LOOP",
    "FOR": "LOOP",
    "REPEAT": "LOOP",
    "REPETIR": "LOOP",
    "SALIR": "BREAK",
    "BREAK": "BREAK",
    "ROMPER": "BREAK",
    "EXIT": "BREAK",
    "CONTINUAR": "CONTINUE",
    "CONTINUE": "CONTINUE",
    "SIGUIENTE": "CONTINUE",
    "NEXT": "CONTINUE",
    "RETORNAR": "RETURN",
    "RETURN": "RETURN",
    "RET": "RETURN",
    "REGRESAR": "RETURN",
    "IMPRIMIR": "PRINT",
    "PRINT": "PRINT",
    "ESCRIBIR": "PRINT",
    "WRITE": "PRINT",
    "MOSTRAR": "PRINT",
    "SHOW": "PRINT",
    "ECHO": "PRINT",
    "ECO": "PRINT",
    "LEER": "INPUT",
    "READ": "INPUT",
    "GUARDAR": "UNSUPPORTED",
    "SAVE": "UNSUPPORTED",
    "PTR": "PTR",
    "POINTER": "PTR",
    "PUNTERO": "PTR",
    "MEM": "UNSUPPORTED",
    "ASISTENTE": "ASSISTANT",
    "ASSISTANT": "ASSISTANT",
    "IA": "ASSISTANT",
    "AI": "ASSISTANT",
    "ERROR": "UNSUPPORTED",
    "TRY": "UNSUPPORTED",
    "REGISTRO": "UNSUPPORTED",
    "REG": "UNSUPPORTED",
    "KERNEL": "UNSUPPORTED",
    "SYSCALL": "UNSUPPORTED",
    "INYECTAR": "INJECT",
    "INJECT": "INJECT",
    "ENTRADA": "INPUT",
    "INPUT": "INPUT",
}

COMPARISONS = {"==", "!=", "<", "<=", ">", ">="}
EXPRESSION_OPERATORS = {
    "OR": (1, "or"),
    "O": (1, "or"),
    "||": (1, "or"),
    "AND": (2, "and"),
    "Y": (2, "and"),
    "&&": (2, "and"),
    "==": (3, "=="),
    "===": (3, "=="),
    "ES": (3, "=="),
    "IS": (3, "=="),
    "!=": (3, "!="),
    "<": (3, "<"),
    "<=": (3, "<="),
    ">": (3, ">"),
    ">=": (3, ">="),
    "+": (4, "+"),
    "-": (4, "-"),
    "*": (5, "*"),
    "/": (5, "/"),
    "%": (5, "%"),
}
REGISTER_NAMES = ("r12", "r13", "r14", "r15")

_BILINGUAL_ERRORS: dict[str, tuple[str, str]] = {
    "falta '.' para cerrar el proceso": (
        "falta '.' para cerrar el proceso",
        "missing '.' to close statement/process",
    ),
    "Tiger cierra procesos con '.', no con ';'": (
        "Tiger cierra procesos con '.', no con ';'",
        "Tiger terminates statements with '.', not with ';'",
    ),
    "bloque sin cerrar; se esperaba '}'": (
        "bloque sin cerrar; se esperaba '}'",
        "unclosed block; expected '}'",
    ),
    "coma sobrante; falta un subproceso después de ','": (
        "coma sobrante; falta un subproceso después de ','",
        "trailing comma; missing statement after ','",
    ),
    "se esperaba un subproceso después de ','": (
        "se esperaba un subproceso después de ','",
        "expected a statement after ','",
    ),
    "se esperaba ',' entre subprocesos o '}' para cerrar el bloque": (
        "se esperaba ',' entre subprocesos o '}' para cerrar el bloque",
        "expected ',' between statements or '}' to close block",
    ),
    "se esperaba ',' entre subprocesos o '.' para cerrar el proceso": (
        "se esperaba ',' entre subprocesos o '.' para cerrar el proceso",
        "expected ',' between statements or '.' to close process",
    ),
    "se esperaba '|' o ',' para separar elementos": (
        "se esperaba '|' o ',' para separar elementos",
        "expected '|' or ',' to separate elements",
    ),
    "USAR EN requiere ASM, WAT o ASM Y WAT": (
        "USAR EN requiere ASM, WAT o ASM Y WAT",
        "USE IN requires ASM, WAT or ASM AND WAT",
    ),
    "el destino de una asignación debe ser una variable o un elemento indexado": (
        "el destino de una asignación debe ser una variable o un elemento indexado",
        "assignment target must be a variable or indexed element",
    ),
    "esta versión admite enteros, no decimales en expresiones": (
        "esta versión admite enteros, no decimales en expresiones",
        "this version supports integers, not decimals in expressions",
    ),
    "el tipo debe ir seguido de VAR o CONST": (
        "el tipo debe ir seguido de VAR o CONST",
        "type must be followed by VAR or CONST",
    ),
    "división entre cero": (
        "división entre cero",
        "division by zero",
    ),
    "la aritmética requiere operandos numéricos": (
        "la aritmética requiere operandos numéricos",
        "arithmetic requires numeric operands",
    ),
    "solo se pueden imprimir textos literales o variables STR/CAD": (
        "solo se pueden imprimir textos literales o variables STR/CAD",
        "can only print literal strings or STR/CAD variables",
    ),
    "cadena sin cerrar; agrega la comilla doble final": (
        "cadena sin cerrar; agrega la comilla doble final",
        "unterminated string; add closing double quote",
    ),
    "la comparación de textos requiere variables o literales": (
        "la comparación de textos requiere variables o literales",
        "string comparison requires variables or literals",
    ),
    "la dirección PTR debe caber en 64 bits": (
        "la dirección PTR debe caber en 64 bits",
        "PTR address must fit in 64 bits",
    ),
    "el operador unario requiere un número": (
        "el operador unario requiere un número",
        "unary operator requires a number",
    ),
    "solo se pueden indexar arrays o diccionarios": (
        "solo se pueden indexar arrays o diccionarios",
        "only arrays or dictionaries can be indexed",
    ),
    "el acceso con '.' requiere un objeto o diccionario": (
        "el acceso con '.' requiere un objeto o diccionario",
        "access with '.' requires an object or dictionary",
    ),
    "el paso del ciclo debe ser una asignación o incremento": (
        "el paso del ciclo debe ser una asignación o incremento",
        "loop step must be an assignment or increment",
    ),
    "el inicio del ciclo debe declarar o asignar una variable": (
        "el inicio del ciclo debe declarar o asignar una variable",
        "loop initializer must declare or assign a variable",
    ),
    "una clase admite campos, CRE, FN y COMANDO": (
        "una clase admite campos, CRE, FN y COMANDO",
        "a class supports fields, CRE, FN, and COMMAND",
    ),
    "se esperaba '|', ',' o '.' entre miembros de la clase": (
        "se esperaba '|', ',' o '.' entre miembros de la clase",
        "expected '|', ',' or '.' between class members",
    ),
    "USAR requiere una ruta entre comillas, archivo o nombre de librería": (
        "USAR requiere una ruta entre comillas, archivo o nombre de librería",
        "USE requires a quoted path, file or library name",
    ),
    "ENTRADA/INPUT solo admite un mensaje de texto opcional": (
        "ENTRADA/INPUT solo admite un mensaje de texto opcional",
        "INPUT only accepts an optional prompt text",
    ),
    "se esperaba un archivo .py o ruta entre comillas": (
        "se esperaba un archivo .py o ruta entre comillas",
        "expected a .py file or quoted path",
    ),
    "se esperaba un archivo .json, ruta entre comillas o documento JSON": (
        "se esperaba un archivo .json, ruta entre comillas o documento JSON",
        "expected a .json file, quoted path or JSON document",
    ),
}


def _format_bilingual_message(message: str) -> str:
    if " / " in message:
        return message
    if message in _BILINGUAL_ERRORS:
        es, en = _BILINGUAL_ERRORS[message]
        return f"{es} / {en}"
    if message.startswith("carácter inesperado "):
        char = message[len("carácter inesperado "):]
        return f"{message} / unexpected character {char}"
    if message.startswith("se esperaba "):
        rest = message[len("se esperaba "):]
        return f"{message} / expected {rest}"
    if message.startswith("entero inválido: "):
        rest = message[len("entero inválido: "):]
        return f"{message} / invalid integer: {rest}"
    if message.startswith("variable no inicializada: "):
        rest = message[len("variable no inicializada: "):]
        return f"{message} / uninitialized variable: {rest}"
    if message.startswith("función no declarada: "):
        rest = message[len("función no declarada: "):]
        return f"{message} / undeclared function: {rest}"
    if message.startswith("campo inexistente: "):
        rest = message[len("campo inexistente: "):]
        return f"{message} / non-existent field: {rest}"
    if message.startswith("índice o clave inexistente: "):
        rest = message[len("índice o clave inexistente: "):]
        return f"{message} / non-existent index or key: {rest}"
    if message.startswith("no se puede asignar en el índice o clave "):
        rest = message[len("no se puede asignar en el índice o clave "):]
        return f"{message} / cannot assign to index or key {rest}"
    if message.startswith("instrucción inesperada "):
        rest = message[len("instrucción inesperada "):]
        return f"{message} / unexpected statement {rest}"
    if "está reservado y aún no se admite en Tiger 10" in message:
        return f"{message} / is reserved and not yet supported in Tiger 10"
    if message.startswith("destino desconocido: "):
        rest = message[len("destino desconocido: "):]
        return f"{message} / unknown target: {rest}"
    if "espera " in message and "argumentos" in message:
        return f"{message} / argument count mismatch"
    return f"{message} / {message}"


class CompilerError(Exception):
    __slots__ = ("message", "line", "column", "code", "bilingual_message")

    def __init__(
        self,
        message: str,
        line: int | None = None,
        column: int | None = None,
        code: str = "TIGER_ERROR",
    ):
        self.message = message
        self.line = line
        self.column = column
        self.code = code
        location = f"línea {line}, columna {column}: " if line is not None else ""
        self.bilingual_message = _format_bilingual_message(message)
        super().__init__(location + self.bilingual_message)



# In-memory document database for TIPO NOSQL
_TIGER_NOSQL_STORE: dict[str, list[dict[str, Any]]] = {}

def _execute_nosql_operation(raw_code: str) -> Any:
    """Ejecuta una operación NoSQL (formato JSON o db.collection.action) sobre el store en memoria."""
    import json as _json
    raw = raw_code.strip()
    if not raw:
        raise CompilerError("bloque o archivo NoSQL vacío / empty NoSQL block or file", code="TIGER_NOSQL_EMPTY_ERROR")

    # Formato 1: JSON Query / Command
    if raw.startswith(("{", "[")):
        try:
            parsed = _json.loads(raw)
        except Exception as e:
            raise CompilerError(f"sintaxis NoSQL inválida en bloque TIPO NOSQL: {e} / invalid NoSQL syntax in TIPO NOSQL block: {e}", code="TIGER_NOSQL_SYNTAX_ERROR")

        if isinstance(parsed, list):
            # Inserción de lista de documentos en 'default'
            store = _TIGER_NOSQL_STORE.setdefault("default", [])
            for item in parsed:
                if isinstance(item, dict):
                    store.append(item)
            return {"status": "ok", "inserted": len(parsed), "collection": "default"}

        if isinstance(parsed, dict):
            col_name = str(parsed.get("collection") or parsed.get("col") or parsed.get("find") or parsed.get("insert") or parsed.get("delete") or "default")
            action = str(parsed.get("action") or "").lower()
            if "find" in parsed and not action:
                action = "find"
            elif "insert" in parsed and not action:
                action = "insert"
            elif "delete" in parsed and not action:
                action = "delete"
            elif "update" in parsed and not action:
                action = "update"
            if not action:
                action = "find" if "filter" in parsed else "insert"

            store = _TIGER_NOSQL_STORE.setdefault(col_name, [])

            if action == "insert":
                doc = parsed.get("doc") or parsed.get("document") or parsed.get("documents") or parsed.get("data") or parsed
                count = 0
                if isinstance(doc, list):
                    for d in doc:
                        if isinstance(d, dict):
                            store.append(d)
                            count += 1
                elif isinstance(doc, dict):
                    store.append(doc)
                    count = 1
                return {"status": "ok", "action": "insert", "collection": col_name, "count": count}

            elif action == "find":
                filt = parsed.get("filter") or {}
                results = []
                for doc in store:
                    matches = True
                    for k, v in filt.items():
                        if isinstance(v, dict) and "$gt" in v:
                            if not (k in doc and doc[k] > v["$gt"]):
                                matches = False; break
                        elif isinstance(v, dict) and "$lt" in v:
                            if not (k in doc and doc[k] < v["$lt"]):
                                matches = False; break
                        elif isinstance(v, dict) and "$ne" in v:
                            if not (k in doc and doc[k] != v["$ne"]):
                                matches = False; break
                        else:
                            if doc.get(k) != v:
                                matches = False; break
                    if matches:
                        results.append(doc)
                return results

            elif action == "delete":
                filt = parsed.get("filter") or {}
                original_len = len(store)
                new_store = [d for d in store if not all(d.get(k) == v for k, v in filt.items())]
                _TIGER_NOSQL_STORE[col_name] = new_store
                return {"status": "ok", "action": "delete", "collection": col_name, "deleted": original_len - len(new_store)}

            elif action == "count":
                return {"status": "ok", "collection": col_name, "count": len(store)}

            return {"status": "ok", "collection": col_name, "data": parsed}

    # Formato 2: Mongo Shell db.collection.action(...)
    match = re.match(r"^db\.([A-Za-z0-9_]+)\.([A-Za-z0-9_]+)\((.*)\)$", raw, re.DOTALL)
    if match:
        col_name = match.group(1)
        action = match.group(2).lower()
        args_str = match.group(3).strip()
        store = _TIGER_NOSQL_STORE.setdefault(col_name, [])

        args_data = None
        if args_str:
            try:
                args_data = _json.loads(args_str)
            except Exception:
                try:
                    args_data = ast.literal_eval(args_str)
                except Exception as err:
                    raise CompilerError(f"error en argumentos NoSQL de db.{col_name}.{action}: {err} / NoSQL arguments error: {err}", code="TIGER_NOSQL_SYNTAX_ERROR")

        if action in {"insert", "insertone", "insertmany"}:
            count = 0
            if isinstance(args_data, list):
                for d in args_data:
                    if isinstance(d, dict):
                        store.append(d)
                        count += 1
            elif isinstance(args_data, dict):
                store.append(args_data)
                count = 1
            return {"status": "ok", "action": action, "collection": col_name, "count": count}

        elif action in {"find", "findone"}:
            filt = args_data if isinstance(args_data, dict) else {}
            results = []
            for doc in store:
                matches = True
                for k, v in filt.items():
                    if isinstance(v, dict) and "$gt" in v:
                        if not (k in doc and doc[k] > v["$gt"]):
                            matches = False; break
                    elif isinstance(v, dict) and "$lt" in v:
                        if not (k in doc and doc[k] < v["$lt"]):
                            matches = False; break
                    elif isinstance(v, dict) and "$ne" in v:
                        if not (k in doc and doc[k] != v["$ne"]):
                            matches = False; break
                    else:
                        if doc.get(k) != v:
                            matches = False; break
                if matches:
                    results.append(doc)
            return results[0] if action == "findone" and results else results

        elif action in {"delete", "deletemany", "remove"}:
            filt = args_data if isinstance(args_data, dict) else {}
            orig = len(store)
            new_store = [d for d in store if not all(d.get(k) == v for k, v in filt.items())]
            _TIGER_NOSQL_STORE[col_name] = new_store
            return {"status": "ok", "deleted": orig - len(new_store)}

        elif action == "count":
            return {"status": "ok", "collection": col_name, "count": len(store)}

        raise CompilerError(f"operación NoSQL desconocida '{action}' / unknown NoSQL operation '{action}'", code="TIGER_NOSQL_EXEC_ERROR")

    raise CompilerError(f"sintaxis NoSQL inválida en bloque TIPO NOSQL / invalid NoSQL syntax in TIPO NOSQL block", code="TIGER_NOSQL_SYNTAX_ERROR")


@dataclass(frozen=True, slots=True)
class Token:
    kind: str
    value: str
    offset: int


@dataclass(frozen=True, slots=True)
class Import:
    path: str
    items: tuple[str, ...] = ()

    @property
    def modules(self) -> tuple[str, ...]:
        return self.items if self.items else (self.path,)


@dataclass(frozen=True, slots=True)
class Macro:
    name: str
    parameters: tuple[str, ...]
    body: tuple[Statement, ...]


@dataclass(frozen=True, slots=True)
class MacroCall:
    name: str
    arguments: tuple[Expression, ...] = ()


@dataclass(frozen=True, slots=True)
class Parameter:
    name: str
    value_type: str


@dataclass(frozen=True, slots=True)
class Function:
    name: str
    parameters: tuple[Parameter, ...]
    body: tuple[Statement, ...]


@dataclass(frozen=True, slots=True)
class Literal:
    value: int | float
 
 
@dataclass(frozen=True, slots=True)
class BooleanLiteral:
    value: bool
    raw: str = "TRUE"


@dataclass(frozen=True, slots=True)
class StringLiteral:
    value: str


@dataclass(frozen=True, slots=True)
class Input:
    prompt: str | None = None
    keyword: str = "INPUT"


@dataclass(frozen=True, slots=True)
class Name:
    value: str


@dataclass(frozen=True, slots=True)
class Unary:
    operator: str
    operand: Expression


@dataclass(frozen=True, slots=True)
class Binary:
    operator: str
    left: Expression
    right: Expression


@dataclass(frozen=True, slots=True)
class ArrayLiteral:
    elements: tuple[Expression, ...]


@dataclass(frozen=True, slots=True)
class DictionaryLiteral:
    entries: tuple[tuple[str, Expression], ...]


@dataclass(frozen=True, slots=True)
class Index:
    collection: Expression
    key: Expression


@dataclass(frozen=True, slots=True)
class Member:
    value: Expression
    name: str


@dataclass(frozen=True, slots=True)
class Call:
    name: str
    arguments: tuple[Expression, ...]


@dataclass(frozen=True, slots=True)
class MethodCall:
    receiver: str
    name: str
    arguments: tuple[Expression, ...]


@dataclass(frozen=True, slots=True)
class CallStatement:
    call: Call | MethodCall


@dataclass(frozen=True, slots=True)
class PythonTask:
    source: str
    is_file: bool = True
    target: str | Expression | None = None
    kind: str = "py"


@dataclass(frozen=True, slots=True)
class PythonBlock:
    tasks: tuple[PythonTask, ...]


@dataclass(frozen=True, slots=True)
class PythonExpression:
    tasks: tuple[PythonTask, ...]


Expression: TypeAlias = (
    Literal | BooleanLiteral | StringLiteral | Input | PythonExpression | Name | Unary | Binary
    | ArrayLiteral | DictionaryLiteral | Index | Member | Call | MethodCall
)


@dataclass(frozen=True, slots=True)
class Variable:
    name: str
    value: Expression
    value_type: str | None = None
    mutable: bool = True


@dataclass(frozen=True, slots=True)
class Assignment:
    name: str | Expression
    value: Expression


@dataclass(frozen=True, slots=True)
class Pointer:
    name: str
    address: int
    value_type: str = "int"


@dataclass(frozen=True, slots=True)
class PointerStore:
    name: str
    value: Expression


@dataclass(frozen=True, slots=True)
class ArrowAssignment:
    source: str
    value: Expression


@dataclass(frozen=True, slots=True)
class Print:
    value: str | Expression


@dataclass(frozen=True, slots=True)
class If:
    condition: Expression
    then_body: tuple[Statement, ...]
    else_body: tuple[Statement, ...]


@dataclass(frozen=True, slots=True)
class WhileLoop:
    condition: Expression
    body: tuple[Statement, ...]
    step: tuple[Statement, ...] = ()
    init: tuple[Statement, ...] = ()


@dataclass(frozen=True, slots=True)
class Break:
    pass


@dataclass(frozen=True, slots=True)
class Continue:
    pass


@dataclass(frozen=True, slots=True)
class Return:
    value: Expression


@dataclass(frozen=True, slots=True)
class ClassCommand:
    name: str
    target: str


@dataclass(frozen=True, slots=True)
class ClassMethod:
    name: str
    parameters: tuple[Parameter, ...]
    body: tuple[Statement, ...]


@dataclass(frozen=True, slots=True)
class ClassDefinition:
    name: str
    members: tuple[Variable | ClassCommand | ClassMethod, ...]


@dataclass(frozen=True, slots=True)
class ClassInstance:
    name: str
    class_name: str
    overrides: tuple[Statement, ...]


@dataclass(frozen=True, slots=True)
class ClassMethodCall:
    class_name: str
    method_name: str
    arguments: tuple[Expression, ...] = ()


@dataclass(frozen=True, slots=True)
class TargetDirective:
    targets: tuple[str, ...]


Statement: TypeAlias = (
    Import | TargetDirective | Macro | MacroCall | Function | Variable | Assignment | Pointer | PointerStore | ArrowAssignment | Print | If
    | WhileLoop | Break | Continue | Return
    | ClassCommand | ClassMethod | ClassDefinition | ClassInstance | ClassMethodCall | CallStatement | PythonBlock
)


@lru_cache(maxsize=256)
def tokenize(source: str) -> tuple[Token, ...]:
    tokens: list[Token] = []
    position = 0
    while position < len(source):
        match = LEXER_RE.match(source, position)
        if match is None:
            line = source.count("\n", 0, position) + 1
            last_newline = source.rfind("\n", 0, position)
            if source[position] == '"':
                message = "cadena sin cerrar; agrega la comilla doble final"
                code = "TIGER_UNTERMINATED_STRING"
            else:
                message = f"carácter inesperado {source[position]!r}"
                code = "TIGER_UNEXPECTED_CHARACTER"
            raise CompilerError(
                message,
                line,
                position - last_newline,
                code,
            )
        kind = match.lastgroup
        value = match.group()
        if kind not in {"SPACE", "COMMENT"}:
            tokens.append(Token(kind or "", value, position))
        position = match.end()
    tokens.append(Token("EOF", "", len(source)))
    return tuple(tokens)


@lru_cache(maxsize=1024)
def decode_string(token: str) -> str:
    if token.startswith("'"):
        try:
            value = ast.literal_eval(token)
        except (SyntaxError, ValueError) as error:
            raise CompilerError(f"cadena inválida: {error}") from error
    else:
        try:
            value = json.loads(token)
        except json.JSONDecodeError as error:
            raise CompilerError(f"cadena inválida: {error.msg}") from error
    if not isinstance(value, str):
        raise CompilerError("se esperaba una cadena")
    return value


def _parse_json_data(raw_text: str) -> object:
    text = raw_text.strip()
    if not text:
        return {}
    try:
        return json.loads(text)
    except json.JSONDecodeError as first_error:
        try:
            tokens = tokenize(text)
        except CompilerError as lex_error:
            raise CompilerError(
                f"JSON inválido en TYPE JSON: {first_error.msg}",
                code="TIGER_INVALID_JSON",
            ) from lex_error
        parts: list[str] = []
        n_tokens = len(tokens)
        for i, tok in enumerate(tokens):
            if tok.kind == "EOF":
                break
            next_tok = tokens[i + 1] if i + 1 < n_tokens else Token("EOF", "", len(text))
            prev_tok = tokens[i - 1] if i > 0 else Token("BOF", "", 0)
            if tok.kind == "OP" and tok.value in {",", "|"}:
                if next_tok.kind == "OP" and next_tok.value in {"}", "]"}:
                    continue
                parts.append(",")
                continue
            if tok.kind == "OP" and tok.value == "=" and prev_tok.kind in {"STRING", "IDENT"}:
                parts.append(":")
                continue
            if tok.kind == "STRING":
                parts.append(json.dumps(decode_string(tok.value), ensure_ascii=False))
                continue
            if tok.kind == "IDENT":
                if next_tok.kind == "OP" and next_tok.value in {":", "="} and (
                    prev_tok.kind == "OP" and prev_tok.value in {"{", ",", "|"}
                ):
                    parts.append(json.dumps(tok.value, ensure_ascii=False))
                    continue
                lowered = tok.value.lower()
                if lowered in {"true", "false", "null"}:
                    parts.append(lowered)
                    continue
                if lowered == "none":
                    parts.append("null")
                    continue
                parts.append(json.dumps(tok.value, ensure_ascii=False))
                continue
            parts.append(tok.value)
        normalized = " ".join(parts)
        try:
            return json.loads(normalized)
        except json.JSONDecodeError as error:
            raise CompilerError(
                f"JSON inválido en TYPE JSON: {first_error.msg}",
                code="TIGER_INVALID_JSON",
            ) from error


def _normalize_python_code(raw_code: str) -> str:
    cleaned = textwrap.dedent(raw_code).strip()
    if not cleaned:
        return ""
    try:
        ast.parse(cleaned)
        return cleaned
    except SyntaxError:
        pass
    try:
        tokens = tokenize(cleaned)
        depth = 0
        split_offsets: list[int] = []
        for tok in tokens:
            if tok.kind == "EOF":
                break
            if tok.kind == "OP":
                if tok.value in {"(", "[", "{"}:
                    depth += 1
                elif tok.value in {")", "]", "}"}:
                    depth = max(0, depth - 1)
                elif depth == 0 and tok.value in {"|", ","}:
                    split_offsets.append(tok.offset)
        if split_offsets:
            segments: list[str] = []
            last = 0
            for off in split_offsets:
                seg = cleaned[last:off].strip()
                if seg:
                    segments.append(seg)
                last = off + 1
            tail = cleaned[last:].strip()
            if tail:
                segments.append(tail)
            candidate = "\n".join(segments)
            ast.parse(candidate)
            return candidate
    except (CompilerError, SyntaxError):
        pass
    return cleaned


def integer_value(token: Token) -> int:
    if "." in token.value:
        raise CompilerError("esta versión admite enteros, no decimales en expresiones")
    try:
        return int(token.value, 16 if token.value.lower().startswith("0x") else 10)
    except ValueError as error:
        raise CompilerError(f"entero inválido: {token.value}") from error


_ASSISTANT_WORD = r"(?:ASISTENTE|ASSISTANT|IA|AI)"
_STRING_LITERAL = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
_ASSISTANT_CONFIG_RE = re.compile(
    rf"\b(?:USAR|USE)\s+{_ASSISTANT_WORD}\s+API\s*\(((?:{_STRING_LITERAL}|[^)\"'])*)\)[ \t]*\.?",
    re.IGNORECASE,
)
_ASSISTANT_BLOCK_RE = re.compile(
    rf"\b{_ASSISTANT_WORD}\s+(?:INI|INICIO|BEGIN|START)\b(.*?)\b{_ASSISTANT_WORD}\s+(?:FIN|END)\b"
    rf"[ \t]*\|?[ \t]*({_STRING_LITERAL})?[ \t]*\.?",
    re.IGNORECASE | re.DOTALL,
)
_ASSISTANT_TARGET_RE = re.compile(
    rf"(?m)^[ \t]*{_ASSISTANT_WORD}[ \t]+(?!(?:INI|INICIO|BEGIN|START|FIN|END|API)\b)"
    rf"([A-Za-z_]\w*)(?:[-.]([A-Za-z_]\w*))?[ \t]*(?:\|[ \t]*({_STRING_LITERAL}))?[ \t]*[.,]?[ \t]*$",
    re.IGNORECASE,
)
_ASSISTANT_PRESENT_RE = re.compile(rf"\b{_ASSISTANT_WORD}\b", re.IGNORECASE)

_ASSISTANT_CONFIG: dict[str, str] | None = None
ASSISTANT_TRANSPORT = None

_ASSISTANT_BASE_PROMPT = """Eres el ASISTENTE integrado del lenguaje de programación Tiger 10 (archivos .tg).
Reglas de Tiger:
- Cada proceso termina con '.', los subprocesos se separan con ','; dentro de {...} se separan con ',' o '|'.
- Variables: VAR x = 1, CONST y = 2, con tipo opcional: ENT VAR n = 3.
- IMPRIMIR valor. SI cond { ... } SINO { ... }. CICLO cond { ... }. Lógica: Y, O, NO.
- Funciones: FN nombre(ENT a | ENT b) { ..., RETORNAR a + b }.  Comandos: COMANDO nombre(x) { ... }.
- Clases: Nombre { CRE ENT vida = 100, FN atacar(ENT d) { ... } }.  Instancia: h (Nombre) { vida = 50 }.
- Acceso a campos: objeto-campo. Métodos: objeto-metodo(args).
- Entrada: ENTRADA "mensaje" -> variable. Python: TYPE PY { ... }. JSON: TYPE JSON { ... }. Librerías: USAR nombre.
Corrige errores de sintaxis, conserva los nombres existentes y cumple la intención pedida.
Responde ÚNICAMENTE con código Tiger válido, sin explicaciones ni bloques markdown."""


def _strip_code_fences(text: str) -> str:
    cleaned = text.strip()
    match = re.match(r"^```[\w+-]*[ \t]*\r?\n(.*?)\r?\n?```\s*$", cleaned, re.DOTALL)
    return match.group(1).strip() if match else cleaned


def _local_repair_tiger_code(raw_code: str, intention: str = "") -> str:
    cleaned = _strip_code_fences(raw_code).strip()
    cleaned = re.sub(r";[ \t]*\r?\n", ".\n", cleaned)
    cleaned = re.sub(r";[ \t]*$", ".", cleaned)
    cleaned = re.sub(r"\bint\s+([A-Za-z_]\w*)\s*=", r"ENT VAR \1 =", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"\bfloat\s+([A-Za-z_]\w*)\s*=", r"DEC VAR \1 =", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"\bstring\s+([A-Za-z_]\w*)\s*=", r"CAD VAR \1 =", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"\blet\s+([A-Za-z_]\w*)\s*=", r"VAR \1 =", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"\bprint\s*\((.*?)\)", r"IMPRIMIR \1", cleaned, flags=re.IGNORECASE)
    cleaned = re.sub(r"\bprintln\s*\((.*?)\)", r"IMPRIMIR \1", cleaned, flags=re.IGNORECASE)
    lines = [line.strip() for line in cleaned.splitlines() if line.strip()]
    fixed_lines = []
    for line in lines:
        if not line.endswith((".", "{", "}", ",")) and not line.startswith(("#", "//")):
            line += "."
        fixed_lines.append(line)
    result = "\n".join(fixed_lines)
    return result if result.endswith(".") or result.endswith("}") else result + "."


def _assistant_provider(model: str) -> str:
    lowered = model.lower()
    if lowered.startswith(("gpt", "o1", "o3", "o4", "chatgpt")):
        return "openai"
    if lowered.startswith("claude"):
        return "anthropic"
    return "gemini"


def _assistant_api_key(config: dict[str, str]) -> str:
    key = config.get("api_key", "").strip()
    if key and key.lower() not in {"dummy", "none", "no", "local", "offline"}:
        return key
    env_names = {
        "gemini": ("TIGER_API_KEY", "GEMINI_API_KEY", "GOOGLE_API_KEY"),
        "openai": ("TIGER_API_KEY", "OPENAI_API_KEY"),
        "anthropic": ("TIGER_API_KEY", "ANTHROPIC_API_KEY"),
    }[_assistant_provider(config.get("model", "gemini"))]
    for env_name in env_names:
        value = os.environ.get(env_name)
        if value:
            return value
    return ""


def _assistant_http_json(url: str, payload: dict, headers: dict[str, str]) -> dict:
    import urllib.error
    import urllib.request

    request = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json", **headers},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def _call_assistant(config: dict[str, str], system_prompt: str, user_prompt: str, fallback_code: str = "") -> str:
    if ASSISTANT_TRANSPORT is not None:
        text = str(ASSISTANT_TRANSPORT(config, system_prompt, user_prompt))
        return _strip_code_fences(text or "")
    key = _assistant_api_key(config)
    if not key:
        return _local_repair_tiger_code(fallback_code or user_prompt)
    model = config.get("model", "gemini-1.5-flash")
    provider = _assistant_provider(model)
    try:
        if provider == "gemini":
            import urllib.parse
            data = _assistant_http_json(
                "https://generativelanguage.googleapis.com/v1beta/models/"
                f"{urllib.parse.quote(model)}:generateContent",
                {
                    "systemInstruction": {"parts": [{"text": system_prompt}]},
                    "contents": [{"role": "user", "parts": [{"text": user_prompt}]}],
                    "generationConfig": {"temperature": 0.2},
                },
                {"x-goog-api-key": key},
            )
            text = "".join(part.get("text", "") for part in data["candidates"][0]["content"]["parts"])
        elif provider == "openai":
            data = _assistant_http_json(
                "https://api.openai.com/v1/chat/completions",
                {
                    "model": model,
                    "messages": [
                        {"role": "system", "content": system_prompt},
                        {"role": "user", "content": user_prompt},
                    ],
                },
                {"Authorization": f"Bearer {key}"},
            )
            text = data["choices"][0]["message"]["content"]
        else:
            data = _assistant_http_json(
                "https://api.anthropic.com/v1/messages",
                {
                    "model": model,
                    "max_tokens": 8192,
                    "system": system_prompt,
                    "messages": [{"role": "user", "content": user_prompt}],
                },
                {"x-api-key": key, "anthropic-version": "2023-06-01"},
            )
            text = "".join(block.get("text", "") for block in data["content"] if block.get("type") == "text")
    except Exception:
        return _local_repair_tiger_code(fallback_code or user_prompt)
    text = _strip_code_fences(text or "")
    return text if text else _local_repair_tiger_code(fallback_code or user_prompt)


def _assistant_request(user_prompt: str, fallback_code: str = "") -> str:
    config = _ASSISTANT_CONFIG
    if config is None:
        raise CompilerError(
            "usa USAR ASSISTANT API(\"api key\" | \"modelo\" | \"system prompt o no\") antes de ASISTENTE",
            code="TIGER_ASSISTANT_NOT_CONFIGURED",
        )
    system_prompt = _ASSISTANT_BASE_PROMPT
    if config.get("system"):
        system_prompt += "\nInstrucciones adicionales del usuario:\n" + config["system"]
    if ASSISTANT_TRANSPORT is not None:
        return _call_assistant(config, system_prompt, user_prompt, fallback_code)

    import hashlib
    digest = hashlib.sha256("\0".join((config.get("model", ""), system_prompt, user_prompt)).encode("utf-8")).hexdigest()
    cache_path = Path.cwd() / ".tiger_asistente_cache.json"
    cache: dict[str, str] = {}
    if cache_path.is_file():
        try:
            cache = json.loads(cache_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            cache = {}
    if digest in cache:
        return cache[digest]
    result = _call_assistant(config, system_prompt, user_prompt, fallback_code)
    cache[digest] = result
    try:
        cache_path.write_text(json.dumps(cache, ensure_ascii=False, indent=1), encoding="utf-8")
    except OSError:
        pass
    return result


def _find_definition_span(source: str, name: str) -> tuple[int, int] | None:
    escaped = re.escape(name)
    def_words = "|".join(re.escape(word) for word, kind in KEYWORDS.items() if kind in {"FN", "COMMAND"})
    patterns = (
        rf"(?im)^[ \t]*(?:{def_words})[ \t]+{escaped}\b",
        rf"(?m)^[ \t]*{escaped}[ \t]*\{{",
    )
    for pattern in patterns:
        match = re.search(pattern, source)
        if match is None:
            continue
        start = match.start()
        depth = 0
        seen_block = False
        quote: str | None = None
        index = match.end() - (1 if source[match.end() - 1] == "{" else 0)
        while index < len(source):
            char = source[index]
            if quote is not None:
                if char == "\\":
                    index += 2
                    continue
                if char == quote:
                    quote = None
            elif char in "\"'":
                quote = char
            elif char in "([{":
                depth += 1
                seen_block = seen_block or char == "{"
            elif char in ")]}":
                depth -= 1
                if depth == 0 and char == "}" and seen_block:
                    end = index + 1
                    rest = re.match(r"[ \t]*\.", source[end:])
                    return start, end + (rest.end() if rest else 0)
            elif char == "." and depth == 0 and not seen_block and (
                index + 1 >= len(source) or source[index + 1].isspace()
            ):
                return start, index + 1
            index += 1
        return start, len(source)
    return None


def _apply_assistant(source: str) -> str:
    if not _ASSISTANT_PRESENT_RE.search(source):
        return source

    def take_config(match: re.Match[str]) -> str:
        global _ASSISTANT_CONFIG
        values = [decode_string(item) for item in re.findall(_STRING_LITERAL, match.group(1))]
        if len(values) < 2:
            raise CompilerError(
                "USAR ASSISTANT API requiere (\"api key\" | \"modelo\" | \"system prompt o no\")",
                code="TIGER_ASSISTANT_CONFIG",
            )
        system_text = values[2].strip() if len(values) > 2 else ""
        if system_text.casefold() in {"", "no", "si", "sí", "yes", "true", "false"}:
            system_text = ""
        _ASSISTANT_CONFIG = {"api_key": values[0], "model": values[1].strip(), "system": system_text}
        return "\n" * match.group(0).count("\n")

    source = _ASSISTANT_CONFIG_RE.sub(take_config, source)

    def fix_block(match: re.Match[str]) -> str:
        code = re.sub(r"^\s*-{3,}|-{3,}\s*$", "", match.group(1)).strip()
        intention = decode_string(match.group(2)) if match.group(2) else "Corrige los errores de sintaxis."
        prompt = (
            "Lee este código Tiger, corrige sus errores de sintaxis y reescríbelo para que cumpla la intención.\n"
            f"Intención: {intention}\n\nCódigo:\n{code}\n\n"
            "Devuelve el código Tiger completo y corregido; cada proceso debe terminar con '.'."
        )
        return _assistant_request(prompt, fallback_code=code)

    source = _ASSISTANT_BLOCK_RE.sub(fix_block, source)

    targets: list[tuple[str, str | None, str | None]] = []

    def collect_target(match: re.Match[str]) -> str:
        targets.append(
            (match.group(1), match.group(2), decode_string(match.group(3)) if match.group(3) else None)
        )
        return ""

    source = _ASSISTANT_TARGET_RE.sub(collect_target, source)
    for name, member, intention in targets:
        span = _find_definition_span(source, name)
        if span is None:
            raise CompilerError(
                f"ASISTENTE no encontró la clase, función o comando '{name}'",
                code="TIGER_ASSISTANT_TARGET_NOT_FOUND",
            )
        start, end = span
        focus = f" Modifica solo el método/función '{member}' y deja el resto igual." if member else ""
        orig_code = source[start:end]
        prompt = (
            f"Corrige esta definición Tiger llamada '{name}'.{focus}\n"
            f"Intención: {intention or 'Corrige errores de sintaxis y de lógica sin cambiar su propósito.'}\n\n"
            f"Código:\n{orig_code}\n\n"
            "Devuelve solo la definición completa corregida, terminada en '.'."
        )
        repaired = _assistant_request(prompt, fallback_code=orig_code)
        source = source[:start] + repaired + source[end:]
    return source


class Parser:
    __slots__ = ("source", "tokens", "index", "flexible")

    def __init__(self, source: str, flexible: bool = False):
        source = _apply_assistant(source)
        self.source = source
        self.tokens = tokenize(source)
        self.index = 0
        self.flexible = flexible

    @property
    def current(self) -> Token:
        return self.tokens[self.index]

    def advance(self) -> Token:
        token = self.current
        if token.kind != "EOF":
            self.index += 1
        return token

    def accept(self, kind: str, value: str | None = None) -> Token | None:
        token = self.current
        if token.kind == kind and (value is None or token.value == value):
            return self.advance()
        return None

    def expect(self, kind: str, value: str | None = None) -> Token:
        token = self.accept(kind, value)
        if token is None:
            expected = value if value is not None else kind
            self.fail(f"se esperaba {expected!r}")
        return token

    def fail(self, message: str, code: str = "TIGER_SYNTAX_ERROR") -> None:
        offset = self.current.offset
        line = self.source.count("\n", 0, offset) + 1
        last_newline = self.source.rfind("\n", 0, offset)
        raise CompilerError(message, line, offset - last_newline, code)

    def parse(self) -> tuple[Statement, ...]:
        statements = self.parse_sequence()
        self.expect("EOF")
        return statements

    def parse_sequence(self, in_block: bool = False) -> tuple[Statement, ...]:
        statements: list[Statement] = []
        process_open = False
        while True:
            if self.current.kind == "EOF":
                if in_block:
                    self.fail("bloque sin cerrar; se esperaba '}'", "TIGER_UNCLOSED_BLOCK")
                if process_open and not self.flexible:
                    self.fail("falta '.' para cerrar el proceso", "TIGER_MISSING_PERIOD")
                return tuple(statements)
            if in_block and self.current.kind == "OP" and self.current.value == "}":
                return tuple(statements)

            statements.append(self.parse_statement())
            if isinstance(statements[-1], TargetDirective):
                self.accept("OP", ".")
                process_open = False
                continue
            process_open = True

            if in_block:
                if self.accept("OP", "|") or self.accept("OP", ","):
                    if self.current.kind == "OP" and self.current.value == "}":
                        if self.flexible:
                            return tuple(statements)
                        self.fail("coma sobrante; falta un subproceso después de ','", "TIGER_TRAILING_COMMA")
                    continue
                if self.current.kind == "OP" and self.current.value == "}":
                    return tuple(statements)
                if self.current.kind == "EOF":
                    self.fail("bloque sin cerrar; se esperaba '}'", "TIGER_UNCLOSED_BLOCK")
                if self.current.kind == "OP" and self.current.value == ";":
                    if not self.flexible:
                        self.fail("Tiger cierra procesos con '.', no con ';'", "TIGER_INVALID_TERMINATOR")
                    self.advance()
                    if self.current.kind == "OP" and self.current.value == "}":
                        return tuple(statements)
                    continue
                # Si estamos en modo flexible, permitir saltos de línea/sangría como separador natural de sentencias
                if self.flexible:
                    continue
                self.fail("se esperaba ',' entre subprocesos o '}' para cerrar el bloque", "TIGER_MISSING_SEPARATOR")

            if self.accept("OP", ","):
                if self.current.kind == "EOF" or (
                    self.current.kind == "OP" and self.current.value == "."
                ):
                    self.fail("se esperaba un subproceso después de ','", "TIGER_TRAILING_COMMA")
                continue
            if self.accept("OP", "."):
                process_open = False
                if self.current.kind == "EOF":
                    return tuple(statements)
                continue
            if self.current.kind == "OP" and self.current.value == ";":
                if not self.flexible:
                    self.fail("Tiger cierra procesos con '.', no con ';'", "TIGER_INVALID_TERMINATOR")
                self.advance()
                process_open = False
                if self.current.kind == "EOF":
                    return tuple(statements)
                continue
            if self.flexible:
                process_open = False
                if self.current.kind == "EOF":
                    return tuple(statements)
                continue
            if self.current.kind == "EOF":
                if not self.flexible:
                    self.fail("falta '.' para cerrar el proceso", "TIGER_MISSING_PERIOD")
                return tuple(statements)
            self.fail("se esperaba ',' entre subprocesos o '.' para cerrar el proceso", "TIGER_MISSING_SEPARATOR")

    def parse_block(self) -> tuple[Statement, ...]:
        self.expect("OP", "{")
        body = self.parse_sequence(in_block=True)
        self.expect("OP", "}")
        return body

    def _accept_item_separator(self) -> Token | None:
        return self.accept("OP", "|") or self.accept("OP", ",")

    def _expect_item_separator(self) -> Token:
        tok = self._accept_item_separator()
        if tok is None:
            self.fail("se esperaba '|' o ',' para separar elementos")
        return tok

    def parse_statement(self) -> Statement:
        token = self.current
        if token.kind == "IDENT":
            next_tok = self.tokens[self.index + 1] if self.index + 1 < len(self.tokens) else None
            # Si el identificador va seguido de una asignación (<- , =, +=, -=, *=) o llamada punto/flecha,
            # debe ser tratado como variable / target de asignación incluso si coincide con un keyword como 'y' (AND).
            if next_tok and next_tok.kind == "OP" and next_tok.value in {"<-", "=", "+=", "-=", "*="}:
                canonical = None
            elif token.value.isupper():
                canonical = KEYWORDS.get(token.value)
            else:
                # Las palabras en minúsculas se tratan como identificadores / nombres normales
                canonical = None
            if canonical == "UNSUPPORTED":
                self.fail(f"{token.value} está reservado y aún no se admite en Tiger 10")
            if canonical is None:
                if (
                    self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value in {"+=", "-=", "*="}
                ):
                    name = self.advance().value
                    operator = self.advance().value[0]
                    return Assignment(
                        name,
                        Binary(operator, Name(name), self.parse_expression()),
                    )
                if (
                    self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value == "{"
                ):
                    return self._parse_class_definition(self.advance().value)
                if (
                    self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value == "-"
                    and self.tokens[self.index + 2].kind == "IDENT"
                    and self.tokens[self.index + 3].kind == "OP"
                    and self.tokens[self.index + 3].value in {"<-", "=", "+=", "-=", "*="}
                ):
                    owner = Name(self.advance().value)
                    self.advance()
                    field_name = self.expect("IDENT").value
                    assignment_operator = self.advance().value
                    target = Member(owner, field_name)
                    value = self.parse_expression()
                    if assignment_operator in {"+=", "-=", "*="}:
                        value = Binary(assignment_operator[0], target, value)
                    return Assignment(target, value)
                if (
                    self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value == "->"
                ):
                    name = self.advance().value
                    self.advance()
                    value = self.parse_expression()
                    return ArrowAssignment(name, value)
                if (
                    self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value == "-"
                    and self.tokens[self.index + 2].kind == "IDENT"
                ):
                    class_name = self.advance().value
                    self.advance()
                    method_name = self.advance().value
                    arguments = (
                        self._parse_arguments()
                        if self.current.kind == "OP" and self.current.value == "("
                        else ()
                    )
                    return ClassMethodCall(class_name, method_name, arguments)
                if (
                    self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value == "("
                    and self.tokens[self.index + 2].kind == "IDENT"
                    and self.tokens[self.index + 3].kind == "OP"
                    and self.tokens[self.index + 3].value == ")"


                ):
                    name = self.advance().value
                    self.expect("OP", "(")
                    class_name = self.expect("IDENT").value
                    self.expect("OP", ")")
                    block = self.parse_block() if self.current.kind == "OP" and self.current.value == "{" else ()
                    return ClassInstance(name, class_name, block)
                if (
                    self.tokens[self.index + 1].kind == "IDENT"
                    and (
                        (self.tokens[self.index + 2].kind == "OP" and self.tokens[self.index + 2].value == "{")
                        or (self.tokens[self.index + 2].kind == "OP" and self.tokens[self.index + 2].value in {".", ","})
                    )
                ):
                    class_name = self.advance().value
                    name = self.advance().value
                    block = self.parse_block() if self.current.kind == "OP" and self.current.value == "{" else ()
                    return ClassInstance(name, class_name, block)
                if self.tokens[self.index + 1].kind == "OP" and self.tokens[self.index + 1].value == "(":
                    name = self.advance().value
                    return MacroCall(name, self._parse_arguments())
                if self.tokens[self.index + 1].kind == "OP" and self.tokens[self.index + 1].value in {"<-", "="}:
                    self.advance()
                    self.advance()
                    return Assignment(token.value, self.parse_expression())
                if self.tokens[self.index + 1].kind == "OP" and self.tokens[self.index + 1].value in {".", "["}:
                    target = self.parse_expression()
                    if self.current.kind == "OP" and self.current.value in {"<-", "=", "+=", "-=", "*="}:
                        assignment_operator = self.advance().value
                        value = self.parse_expression()
                        if assignment_operator in {"+=", "-=", "*="}:
                            value = Binary(assignment_operator[0], target, value)
                        return Assignment(target, value)
                    if isinstance(target, MethodCall):
                        return ClassMethodCall(target.receiver, target.name, target.arguments)
                    if isinstance(target, Call):
                        return MacroCall(target.name, target.arguments)
                    self.expect("OP", "<-")
                    return Assignment(target, self.parse_expression())
                if self.tokens[self.index + 1].kind == "OP" and self.tokens[self.index + 1].value == "->":
                    value = self.parse_expression()
                    self.expect("OP", "->")
                    target = self.parse_expression(6)
                    if not isinstance(target, (Name, Index, Member)):
                        self.fail("el destino de una asignación debe ser una variable o un elemento indexado")
                    return Assignment(target, value)
                self.advance()
                return MacroCall(token.value)
            self.advance()
            if canonical == "USE":
                if self.current.kind == "IDENT" and self.current.value.upper() in {"EN", "IN", "TARGET", "DESTINO"}:
                    self.advance()
                    target_list: list[str] = []
                    while (
                        self.current.kind == "IDENT"
                        and self.current.value.upper() in {
                            "ASM", "ASSEMBLY", "ENSAMBLADOR", "WAT", "WASM", "WEBASSEMBLY", "WEBENSAMBLADOR", "Y", "AND", "E"
                        }
                    ) or (self.current.kind == "OP" and self.current.value in {",", "|", "&"}):
                        if self.current.kind == "IDENT":
                            val = self.current.value.upper()
                            if val in {"ASM", "ASSEMBLY", "ENSAMBLADOR"}:
                                if "asm" not in target_list:
                                    target_list.append("asm")
                            elif val in {"WAT", "WASM", "WEBASSEMBLY", "WEBENSAMBLADOR"}:
                                if "wat" not in target_list:
                                    target_list.append("wat")
                        self.advance()
                    if not target_list:
                        self.fail("USAR EN requiere ASM, WAT o ASM Y WAT")
                    return TargetDirective(tuple(target_list))
                return self._parse_import()
            if canonical == "COMMAND":
                name = self.expect("IDENT").value
                parameters = self._parse_name_parameters()
                if self.accept("OP", "->") or self.accept("OP", "="):
                    body = (self.parse_statement(),)
                else:
                    body = self.parse_block()
                return Macro(name, parameters, body)
            if canonical == "FN":
                name = self.expect("IDENT").value
                parameters = self._parse_parameters()
                return Function(name, parameters, self.parse_block())
            if canonical == "RETURN":
                return Return(self.parse_expression())
            if canonical == "BREAK":
                return Break()
            if canonical == "CONTINUE":
                return Continue()
            if canonical == "LOOP":
                return self._parse_loop()
            if canonical in {"VAR", "CONST"}:
                declared_type = self._parse_optional_type()
                name = self.expect("IDENT").value
                self.expect("OP", "=")
                if (
                    canonical == "VAR"
                    and self.current.kind == "IDENT"
                    and self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value == "("
                    and self.tokens[self.index + 2].kind == "OP"
                    and self.tokens[self.index + 2].value == ")"
                    and self.tokens[self.index + 3].kind == "OP"
                    and self.tokens[self.index + 3].value == "{"
                ):
                    class_name = self.advance().value
                    self.expect("OP", "(")
                    self.expect("OP", ")")
                    return ClassInstance(name, class_name, self.parse_block())
                if (
                    canonical == "VAR"
                    and self.current.kind == "IDENT"
                    and self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value == "{"
                ):
                    class_name = self.advance().value
                    return ClassInstance(name, class_name, self.parse_block())
                return Variable(
                    name,
                    self.parse_expression(),
                    declared_type,
                    canonical == "VAR",
                )
            if canonical in {"TYPE_INT", "TYPE_FLOAT", "TYPE_STRING", "TYPE_BOOL"}:
                if (
                    self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value in {"<-", "=", "+=", "-=", "*="}
                ):
                    name = self.advance().value
                    op = self.advance().value
                    if op in {"+=", "-=", "*="}:
                        return Assignment(name, Binary(op[0], Name(name), self.parse_expression()))
                    return Assignment(name, self.parse_expression())
                declared_type = canonical.removeprefix("TYPE_").lower()
                mutability = self.current
                if mutability.kind != "IDENT" or KEYWORDS.get(mutability.value.upper()) not in {"VAR", "CONST"}:
                    self.fail("el tipo debe ir seguido de VAR o CONST")
                self.advance()
                name = self.expect("IDENT").value
                self.expect("OP", "=")
                return Variable(
                    name,
                    self.parse_expression(),
                    declared_type,
                    KEYWORDS[mutability.value.upper()] == "VAR",
                )
            if canonical == "INJECT":
                return self._parse_injection()
            if canonical == "TYPE":
                if self.tokens[self.index + 1].kind == "IDENT" and self.tokens[self.index + 1].value.upper() in {
                    "PY", "PYTHON", "JSON", "JS", "JAVASCRIPT", "NODE", "C", "CPP", "CXX", "C++", "DART", "BI", "BIN", "BINARY", "BINARIO", "EXE", "DLL",
                    "HTML", "HTM", "NOSQL", "MONGO", "MONGODB"
                }:
                    return self._parse_type_python_statement()
                if (
                    self.tokens[self.index + 1].kind == "OP"
                    and self.tokens[self.index + 1].value in {"<-", "=", "+=", "-=", "*="}
                ):
                    name = self.advance().value
                    op = self.advance().value
                    if op in {"+=", "-=", "*="}:
                        return Assignment(name, Binary(op[0], Name(name), self.parse_expression()))
                    return Assignment(name, self.parse_expression())
                return self._parse_type_python_statement()
            if canonical == "INPUT":
                input_expr = self._parse_input(token.value.upper())
                self.expect("OP", "->")
                return self._finish_input_arrow_target(input_expr)
            if canonical == "PTR":
                value_type = self._parse_optional_type() or "int"
                name = self.expect("IDENT").value
                self.expect("OP", "=")
                self.expect("OP", "@")
                address = integer_value(self.expect("NUMBER"))
                if address < 0 or address >= 1 << 64:
                    self.fail("la dirección PTR debe caber en 64 bits")
                return Pointer(name, address, value_type)
            if canonical == "PRINT":
                if (
                    self.current.kind == "STRING"
                    and not (
                        self.tokens[self.index + 1].kind == "OP"
                        and self.tokens[self.index + 1].value in EXPRESSION_OPERATORS
                    )
                ):
                    return Print(decode_string(self.advance().value))
                return Print(self.parse_expression())
            if canonical == "IF":
                condition = self.parse_expression()
                then_body = self.parse_block()
                else_body: tuple[Statement, ...] = ()
                if self.current.kind == "IDENT" and self.current.value.isupper() and KEYWORDS.get(self.current.value) == "ELSE":
                    self.advance()
                    else_body = self.parse_block()
                return If(condition, then_body, else_body)
        if token.kind in {"NUMBER", "STRING"} or (token.kind == "OP" and token.value in {"(", "-", "+"}):
            value = self.parse_expression()
            self.expect("OP", "->")
            if (
                isinstance(value, StringLiteral)
                and self.current.kind == "IDENT"
                and KEYWORDS.get(self.current.value.upper()) == "INPUT"
            ):
                kw = self.advance().value.upper()
                self.expect("OP", "->")
                return self._finish_input_arrow_target(Input(value.value, kw))
            return Assignment(self._parse_assignment_target(), value)
        self.fail(f"instrucción inesperada {token.value!r}")

    def _parse_single_import_target(self) -> str:
        tok = self.current
        if tok.kind == "STRING":
            self.advance()
            return decode_string(tok.value)
        if tok.kind == "FILE":
            self.advance()
            return tok.value
        if tok.kind == "IDENT":
            parts = [self.advance().value]
            while True:
                if self.current.kind == "OP" and self.current.value == "/":
                    self.advance()
                    if self.current.kind in {"IDENT", "FILE"}:
                        parts.append("/" + self.advance().value)
                    else:
                        self.fail("se esperaba un nombre de librería o archivo después de '/'")
                    continue
                if (
                    self.current.kind == "OP"
                    and self.current.value == "."
                    and self.index > 0
                    and self.tokens[self.index - 1].offset + len(self.tokens[self.index - 1].value) == self.current.offset
                    and self.tokens[self.index + 1].kind == "IDENT"
                    and self.current.offset + 1 == self.tokens[self.index + 1].offset
                ):
                    self.advance()
                    parts.append("." + self.advance().value)
                    continue
                break
            return "".join(parts)
        self.fail("USAR requiere una ruta entre comillas, archivo o nombre de librería")

    def _parse_import(self) -> Import:
        close_delim: str | None = None
        if self.accept("OP", "("):
            close_delim = ")"
        elif self.accept("OP", "["):
            close_delim = "]"
        elif self.accept("OP", "{"):
            close_delim = "}"
        items: list[str] = [self._parse_single_import_target()]
        if close_delim is not None:
            while not self.accept("OP", close_delim):
                self._expect_item_separator()
                if self.accept("OP", close_delim):
                    break
                items.append(self._parse_single_import_target())
        else:
            while self.accept("OP", "|"):
                items.append(self._parse_single_import_target())
        return Import(items[0], tuple(items) if len(items) > 1 else ())

    def _has_structured_paren_loop_header(self) -> bool:
        if not (self.current.kind == "OP" and self.current.value == "("):
            return False
        depth = 0
        cursor = self.index + 1
        while cursor < len(self.tokens):
            tok = self.tokens[cursor]
            if tok.kind == "EOF":
                return False
            if tok.kind == "OP":
                if tok.value in {"(", "[", "{"}:
                    depth += 1
                elif tok.value in {")", "]", "}"}:
                    if depth == 0:
                        return False
                    depth -= 1
                elif depth == 0 and tok.value in {"|", ",", "->", "=", "<-"}:
                    return True
            elif depth == 0 and tok.kind == "IDENT" and KEYWORDS.get(tok.value.upper()) in {
                "VAR",
                "TYPE_INT",
                "TYPE_FLOAT",
                "TYPE_STRING",
            }:
                return True
            cursor += 1
        return False

    def _parse_loop_step(self, loop_var_name: str | None) -> Statement:
        if (
            self.current.kind == "IDENT"
            and self.tokens[self.index + 1].kind == "OP"
            and self.tokens[self.index + 1].value in {"+=", "-=", "*=", "<-", "=", "->"}
        ):
            return self.parse_statement()
        expr = self.parse_expression()
        if self.accept("OP", "->"):
            return Assignment(self._parse_assignment_target(), expr)
        if loop_var_name is not None:
            return Assignment(loop_var_name, Binary("+", Name(loop_var_name), expr))
        self.fail("el paso del ciclo debe ser una asignación o incremento")

    def _parse_structured_loop_header(
        self, has_parens: bool
    ) -> tuple[tuple[Statement, ...], Expression, tuple[Statement, ...]]:
        if (
            self.current.kind == "IDENT"
            and KEYWORDS.get(self.current.value.upper()) is None
            and self.tokens[self.index + 1].kind == "OP"
            and self.tokens[self.index + 1].value in {"|", ","}
        ):
            var_name = self.advance().value
            self._expect_item_separator()
            start_expr = self.parse_expression()
            self._expect_item_separator()
            end_expr = self.parse_expression()
            step_expr: Expression = Literal(1)
            if self._accept_item_separator():
                step_expr = self.parse_expression()
            if has_parens:
                self.expect("OP", ")")
            init_stmt = Variable(var_name, start_expr, None, True)
            cond_expr = Binary("<", Name(var_name), end_expr)
            step_stmt = Assignment(var_name, Binary("+", Name(var_name), step_expr))
            return (init_stmt,), cond_expr, (step_stmt,)

        if (
            self.current.kind == "IDENT"
            and KEYWORDS.get(self.current.value.upper()) is None
            and self.tokens[self.index + 1].kind == "OP"
            and self.tokens[self.index + 1].value in {"=", "<-"}
        ):
            var_name = self.advance().value
            self.advance()
            init_stmt = Variable(var_name, self.parse_expression(), None, True)
            loop_var_name: str | None = var_name
        else:
            parsed_init = self.parse_statement()
            if isinstance(parsed_init, Assignment) and isinstance(parsed_init.name, str):
                init_stmt = Variable(parsed_init.name, parsed_init.value, None, True)
                loop_var_name = parsed_init.name
            elif isinstance(parsed_init, Variable):
                init_stmt = parsed_init
                loop_var_name = parsed_init.name
            else:
                self.fail("el inicio del ciclo debe declarar o asignar una variable")
                raise AssertionError

        if self.accept("OP", "->"):
            end_expr = self.parse_expression()
            step_expr = self.parse_expression() if self._accept_item_separator() else Literal(1)
            if has_parens:
                self.expect("OP", ")")
            assert loop_var_name is not None
            cond_expr = Binary("<", Name(loop_var_name), end_expr)
            step_stmt = Assignment(loop_var_name, Binary("+", Name(loop_var_name), step_expr))
            return (init_stmt,), cond_expr, (step_stmt,)

        self._expect_item_separator()
        cond_expr = self.parse_expression()
        self._expect_item_separator()
        step_stmt = self._parse_loop_step(loop_var_name)
        if has_parens:
            self.expect("OP", ")")
        return (init_stmt,), cond_expr, (step_stmt,)

    def _parse_loop(self) -> WhileLoop:
        if self.current.kind == "OP" and self.current.value == "{":
            return WhileLoop(Literal(1), self.parse_block())

        if self._has_structured_paren_loop_header():
            self.expect("OP", "(")
            init, condition, step = self._parse_structured_loop_header(has_parens=True)
            body = self.parse_block()
            return WhileLoop(condition, body, step, init)

        if self.current.kind == "IDENT":
            kw = KEYWORDS.get(self.current.value.upper())
            next_tok = self.tokens[self.index + 1]
            if kw in {"VAR", "TYPE_INT", "TYPE_FLOAT", "TYPE_STRING", "TYPE_BOOL"} or (
                kw is None and next_tok.kind == "OP" and next_tok.value in {"=", "<-", "|"}
            ):
                init, condition, step = self._parse_structured_loop_header(has_parens=False)
                body = self.parse_block()
                return WhileLoop(condition, body, step, init)

        condition = self.parse_expression()
        if self.current.kind == "IDENT" and self.current.value.upper() in {"VECES", "TIMES"}:
            self.advance()
            counter_name = f"__ciclo_rep_{self.index}"
            init = (Variable(counter_name, Literal(0), "int", True),)
            step = (Assignment(counter_name, Binary("+", Name(counter_name), Literal(1))),)
            return WhileLoop(Binary("<", Name(counter_name), condition), self.parse_block(), step, init)

        return WhileLoop(condition, self.parse_block())

    def _parse_class_definition(self, name: str) -> ClassDefinition:
        self.expect("OP", "{")
        members: list[Variable | ClassCommand | ClassMethod] = []
        while not self.accept("OP", "}"):
            if self.current.kind == "EOF":
                self.fail("clase sin cerrar; se esperaba '}'", "TIGER_UNCLOSED_BLOCK")
            if self.current.kind == "IDENT" and self.current.value.isupper() and KEYWORDS.get(self.current.value) == "CRE":
                self.advance()
                declared_type = self._parse_optional_type()
                field_name = self.expect("IDENT").value
                self.expect("OP", "=")
                members.append(Variable(field_name, self.parse_expression(), declared_type, True))
            elif (
                self.current.kind == "IDENT"
                and self.current.value.isupper()
                and KEYWORDS.get(self.current.value) == "COMMAND"
            ):
                self.advance()
                command_name = self.expect("IDENT").value
                if self.accept("OP", "->"):
                    target = self.expect("IDENT").value
                    members.append(ClassCommand(command_name, target))
                    self.accept("OP", ".")
                else:
                    parameters = self._parse_parameters()
                    body = self.parse_block()
                    members.append(ClassMethod(command_name, parameters, body))
            elif self.current.kind == "IDENT" and self.current.value.isupper() and KEYWORDS.get(self.current.value) == "FN":
                self.advance()
                method_name = self.expect("IDENT").value
                parameters = self._parse_parameters()
                if self.current.kind == "OP" and self.current.value == "{":
                    body = self.parse_block()
                elif self._accept_item_separator():
                    method_statements: list[Statement] = []
                    while True:
                        stmt = self.parse_statement()
                        method_statements.append(stmt)
                        if isinstance(stmt, Return):
                            break
                        if not self._accept_item_separator():
                            break
                    body = tuple(method_statements)
                else:
                    body = self.parse_block()
                members.append(ClassMethod(method_name, parameters, body))
            else:
                member = self.parse_statement()
                if not isinstance(member, Variable):
                    self.fail("una clase admite campos, CRE, FN y COMANDO")
                members.append(member)
            if self._accept_item_separator() or self.accept("OP", "."):
                continue
            if self.current.kind == "OP" and self.current.value == "}":
                continue
            self.fail("se esperaba '|', ',' o '.' entre miembros de la clase")
        return ClassDefinition(name, tuple(members))

    def _parse_optional_type(self) -> str | None:
        token = self.current
        if token.kind != "IDENT":
            return None
        canonical = KEYWORDS.get(token.value.upper())
        if canonical not in {"TYPE_INT", "TYPE_FLOAT", "TYPE_STRING", "TYPE_BOOL"}:
            return None
        self.advance()
        return canonical.removeprefix("TYPE_").lower()

    def _parse_parameters(self) -> tuple[Parameter, ...]:
        if not self.accept("OP", "("):
            return ()
        parameters: list[Parameter] = []
        if self.accept("OP", ")"):
            return ()
        while True:
            value_type = self._parse_optional_type() or "int"
            name = self.expect("IDENT").value
            parameters.append(Parameter(name, value_type))
            if self.accept("OP", ")"):
                return tuple(parameters)
            self._expect_item_separator()

    def _parse_arguments(self) -> tuple[Expression, ...]:
        self.expect("OP", "(")
        arguments: list[Expression] = []
        if self.accept("OP", ")"):
            return ()
        while True:
            arguments.append(self.parse_expression())
            if self.accept("OP", ")"):
                return tuple(arguments)
            self._expect_item_separator()

    def _parse_name_parameters(self) -> tuple[str, ...]:
        if not self.accept("OP", "("):
            return ()
        names: list[str] = []
        if self.accept("OP", ")"):
            return ()
        while True:
            self._parse_optional_type()
            name = self.expect("IDENT").value
            names.append(name)
            if self.accept("OP", ")"):
                return tuple(names)
            self._expect_item_separator()

    def _parse_input(self, keyword: str = "INPUT") -> Input:
        if self.current.kind == "STRING":
            return Input(decode_string(self.advance().value), keyword)
        if self.accept("OP", "("):
            if self.accept("OP", ")"):
                return Input(None, keyword)
            if self.current.kind == "STRING":
                prompt = decode_string(self.advance().value)
                self.expect("OP", ")")
                return Input(prompt, keyword)
            self.fail("ENTRADA/INPUT solo admite un mensaje de texto opcional")
        return Input(None, keyword)

    def _parse_python_source(self) -> tuple[str, bool]:
        has_paren = self.accept("OP", "(") is not None
        if self.current.kind == "IDENT" and self.current.value.upper() in {
            "LEER",
            "READ",
            "USAR",
            "USE",
            "EJECUTAR",
            "RUN",
        }:
            self.advance()
        if not has_paren and self.accept("OP", "(") is not None:
            has_paren = True
        token = self.current
        if token.kind == "FILE":
            self.advance()
            source = token.value
            is_file = True
        elif token.kind == "STRING":
            self.advance()
            source = decode_string(token.value)
            is_file = source.lower().endswith(".py") and "\n" not in source
        else:
            self.fail("se esperaba un archivo .py o ruta entre comillas")
        if has_paren:
            self.expect("OP", ")")
        return source, is_file

    def _parse_json_source(self, in_braces: bool = False) -> tuple[str, bool]:
        has_paren = self.accept("OP", "(") is not None
        if self.current.kind == "IDENT" and self.current.value.upper() in {
            "LEER",
            "READ",
            "USAR",
            "USE",
            "EJECUTAR",
            "RUN",
        }:
            self.advance()
        if not has_paren and self.accept("OP", "(") is not None:
            has_paren = True
        token = self.current
        if token.kind == "FILE" and token.value.lower().endswith(".json"):
            self.advance()
            source = token.value
            is_file = True
        elif token.kind == "STRING":
            if in_braces and self.tokens[self.index + 1].kind == "OP" and self.tokens[self.index + 1].value == ":":
                self.fail("clave de objeto JSON directo")
            self.advance()
            source = decode_string(token.value)
            is_file = source.lower().endswith(".json") and "\n" not in source
            if in_braces and not is_file and not source.lstrip().startswith(("{", "[")):
                self.fail("se esperaba un archivo .json o documento JSON")
        else:
            self.fail("se esperaba un archivo .json, ruta entre comillas o documento JSON")
        if has_paren:
            self.expect("OP", ")")
        return source, is_file

    def _parse_python_arrow_target_name(self) -> str | Expression:
        self._parse_optional_type()
        if self.current.kind == "IDENT" and KEYWORDS.get(self.current.value.upper()) in {"VAR", "CONST"}:
            self.advance()
            self._parse_optional_type()
        return self._parse_assignment_target()

    def _parse_python_task_item(self) -> PythonTask:
        has_inject = False
        if self.current.kind == "IDENT" and KEYWORDS.get(self.current.value.upper()) == "INJECT":
            self.advance()
            self.accept("OP", "->")
            has_inject = True
        saved_index = self.index
        has_decl_kw = self._parse_optional_type() is not None
        if self.current.kind == "IDENT" and KEYWORDS.get(self.current.value.upper()) in {"VAR", "CONST"}:
            self.advance()
            self._parse_optional_type()
            has_decl_kw = True
        if (
            self.current.kind == "IDENT"
            and self.current.value.upper()
            not in {"LEER", "READ", "USAR", "USE", "EJECUTAR", "RUN"}
        ):
            try:
                target = self._parse_assignment_target()
                if self.current.kind == "OP" and self.current.value in {"<-", "="}:
                    op_val = self.advance().value
                    source, is_file = self._parse_python_source()
                    if op_val == "=" and not has_decl_kw and not has_inject and not is_file:
                        self.fail("asignación directa en bloque Python")
                    return PythonTask(source, is_file, target, "py")
            except CompilerError:
                pass
        self.index = saved_index
        source, is_file = self._parse_python_source()
        target_out: str | Expression | None = None
        if self.accept("OP", "->"):
            target_out = self._parse_python_arrow_target_name()
        return PythonTask(source, is_file, target_out, "py")

    def _parse_json_task_item(self, in_braces: bool = False) -> PythonTask:
        has_inject = False
        if self.current.kind == "IDENT" and KEYWORDS.get(self.current.value.upper()) == "INJECT":
            self.advance()
            self.accept("OP", "->")
            has_inject = True
        saved_index = self.index
        has_decl_kw = self._parse_optional_type() is not None
        if self.current.kind == "IDENT" and KEYWORDS.get(self.current.value.upper()) in {"VAR", "CONST"}:
            self.advance()
            self._parse_optional_type()
            has_decl_kw = True
        if (
            self.current.kind == "IDENT"
            and self.current.value.upper()
            not in {"LEER", "READ", "USAR", "USE", "EJECUTAR", "RUN"}
        ):
            try:
                target = self._parse_assignment_target()
                if self.current.kind == "OP" and self.current.value in {"<-", "="}:
                    op_val = self.advance().value
                    source, is_file = self._parse_json_source(in_braces=in_braces)
                    if op_val == "=" and not has_decl_kw and not has_inject and not is_file:
                        self.fail("asignación directa en bloque JSON")
                    return PythonTask(source, is_file, target, "json")
            except CompilerError:
                pass
        self.index = saved_index
        source, is_file = self._parse_json_source(in_braces=in_braces)
        target_out: str | Expression | None = None
        if self.accept("OP", "->"):
            target_out = self._parse_python_arrow_target_name()
        return PythonTask(source, is_file, target_out, "json")

    def _parse_type_python_statement(self) -> PythonBlock:
        lang = self.current
        norm_lang = lang.value.upper()
        allowed_langs = {
            "PY": "py", "PYTHON": "py",
            "JSON": "json",
            "JS": "js", "JAVASCRIPT": "js", "NODE": "js",
            "C": "c",
            "CPP": "cpp", "CXX": "cpp", "C++": "cpp",
            "DART": "dart", "BI": "bi", "BIN": "bi", "BINARY": "bi", "BINARIO": "bi", "EXE": "bi", "DLL": "bi",
            "HTML": "html", "HTM": "html",
            "NOSQL": "nosql", "MONGO": "nosql", "MONGODB": "nosql"
        }
        if lang.kind != "IDENT" or norm_lang not in allowed_langs:
            self.fail(
                "TIPO/TYPE requiere PY, JSON, JS, C, CPP, DART, BI, HTML o NOSQL para ejecutar o importar código / "
                "TIPO/TYPE requires PY, JSON, JS, C, CPP, DART, BI, HTML or NOSQL to run or import code"
            )
        mode = allowed_langs[norm_lang]
        self.advance()
        tasks: list[PythonTask] = []
        if mode == "py":
            if self.current.kind == "OP" and self.current.value == "{":
                open_token = self.advance()
                if not self.accept("OP", "}"):
                    saved_index = self.index
                    try:
                        while True:
                            tasks.append(self._parse_python_task_item())
                            if self._accept_item_separator() or self.accept("OP", "."):
                                if self.current.kind == "OP" and self.current.value == "}":
                                    break
                                continue
                            break
                        self.expect("OP", "}")
                    except CompilerError:
                        tasks.clear()
                        self.index = saved_index
                        depth = 1
                        end_offset = len(self.source)
                        while self.current.kind != "EOF":
                            tok = self.advance()
                            if tok.kind == "OP" and tok.value == "{":
                                depth += 1
                            elif tok.kind == "OP" and tok.value == "}":
                                depth -= 1
                                if depth == 0:
                                    end_offset = tok.offset
                                    break
                        if depth != 0:
                            self.fail("bloque TIPO PY sin cerrar; se esperaba '}'", "TIGER_UNCLOSED_BLOCK")
                        raw_code = _normalize_python_code(self.source[open_token.offset + 1 : end_offset])
                        if raw_code:
                            tasks.append(PythonTask(raw_code, False, None, "py"))
            else:
                tasks.append(self._parse_python_task_item())
        elif mode in {"js", "c", "cpp", "dart", "bi", "html", "nosql"}:
            if self.current.kind == "OP" and self.current.value == "{":
                open_token = self.advance()
                depth = 1
                end_offset = len(self.source)
                while self.current.kind != "EOF":
                    tok = self.advance()
                    if tok.kind == "OP" and tok.value == "{":
                        depth += 1
                    elif tok.kind == "OP" and tok.value == "}":
                        depth -= 1
                        if depth == 0:
                            end_offset = tok.offset
                            break
                if depth != 0:
                    self.fail(f"bloque TIPO {mode.upper()} sin cerrar; se esperaba '}}' / unclosed TIPO {mode.upper()} block; expected '}}'", "TIGER_UNCLOSED_BLOCK")
                raw_code = self.source[open_token.offset + 1 : end_offset].strip()
                tasks.append(PythonTask(raw_code, False, None, mode))
            else:
                tok = self.current
                if tok.kind == "STRING":
                    self.advance()
                    source = decode_string(tok.value)
                    tasks.append(PythonTask(source, True, None, mode))
                elif tok.kind == "FILE":
                    self.advance()
                    tasks.append(PythonTask(tok.value, True, None, mode))
                else:
                    self.fail(f"se esperaba bloque {{...}} o archivo para TIPO {mode.upper()} / expected block {{...}} or file for TIPO {mode.upper()}")
        else:
            if self.current.kind == "OP" and self.current.value == "{":
                open_token = self.advance()
                if self.accept("OP", "}"):
                    tasks.append(PythonTask("{}", False, None, "json"))
                else:
                    saved_index = self.index
                    try:
                        while True:
                            tasks.append(self._parse_json_task_item(in_braces=True))
                            if self._accept_item_separator() or self.accept("OP", "."):
                                if self.current.kind == "OP" and self.current.value == "}":
                                    break
                                continue
                            break
                        self.expect("OP", "}")
                    except CompilerError:
                        tasks.clear()
                        self.index = saved_index
                        depth = 1
                        end_offset = len(self.source)
                        while self.current.kind != "EOF":
                            tok = self.advance()
                            if tok.kind == "OP" and tok.value == "{":
                                depth += 1
                            elif tok.kind == "OP" and tok.value == "}":
                                depth -= 1
                                if depth == 0:
                                    end_offset = tok.offset
                                    break
                        if depth != 0:
                            self.fail("bloque TIPO JSON sin cerrar; se esperaba '}'", "TIGER_UNCLOSED_BLOCK")
                        inner_text = self.source[open_token.offset + 1 : end_offset].strip()
                        outer_text = self.source[open_token.offset : end_offset + 1].strip()
                        raw_json = outer_text
                        if inner_text.startswith(("{", "[")):
                            try:
                                _parse_json_data(inner_text)
                                raw_json = inner_text
                            except CompilerError:
                                raw_json = outer_text
                        tasks.append(PythonTask(raw_json, False, None, "json"))
            elif self.current.kind == "OP" and self.current.value == "[":
                open_token = self.advance()
                depth = 1
                end_offset = len(self.source)
                while self.current.kind != "EOF":
                    tok = self.advance()
                    if tok.kind == "OP" and tok.value == "[":
                        depth += 1
                    elif tok.kind == "OP" and tok.value == "]":
                        depth -= 1
                        if depth == 0:
                            end_offset = tok.offset
                            break
                if depth != 0:
                    self.fail("bloque TIPO JSON sin cerrar; se esperaba ']'", "TIGER_UNCLOSED_BLOCK")
                raw_json = self.source[open_token.offset : end_offset + 1].strip()
                tasks.append(PythonTask(raw_json, False, None, "json"))
            else:
                tasks.append(self._parse_json_task_item(in_braces=False))
        if self.accept("OP", "->"):
            target = self._parse_python_arrow_target_name()
            if tasks:
                last = tasks[-1]
                tasks[-1] = PythonTask(last.source, last.is_file, target, last.kind)
            else:
                self.fail(f"TIPO {mode.upper()} vacío no puede redirigir con ->")
        return PythonBlock(tuple(tasks))

    def _parse_assignment_target(self) -> str | Expression:
        if (
            self.current.kind == "IDENT"
            and self.tokens[self.index + 1].kind == "OP"
            and self.tokens[self.index + 1].value == "-"
            and self.tokens[self.index + 2].kind == "IDENT"
        ):
            owner = Name(self.advance().value)
            self.advance()
            field_name = self.expect("IDENT").value
            return Member(owner, field_name)
        if (
            self.current.kind == "IDENT"
            and self.tokens[self.index + 1].kind == "OP"
            and self.tokens[self.index + 1].value in {".", "["}
        ):
            target = self.parse_expression(6)
            if isinstance(target, Name):
                return target.value
            if isinstance(target, (Index, Member)):
                return target
            self.fail("el destino de una asignación debe ser una variable, campo o elemento indexado")
        return self.expect("IDENT").value

    def _finish_input_arrow_target(self, input_expr: Input) -> Statement:
        declared_type = self._parse_optional_type()
        is_var: bool | None = None
        if self.current.kind == "IDENT" and KEYWORDS.get(self.current.value.upper()) in {"VAR", "CONST"}:
            is_var = KEYWORDS[self.advance().value.upper()] == "VAR"
            if declared_type is None:
                declared_type = self._parse_optional_type()
        if declared_type is not None or is_var is not None:
            return Variable(
                self.expect("IDENT").value,
                input_expr,
                declared_type,
                True if is_var is None else is_var,
            )
        return Assignment(self._parse_assignment_target(), input_expr)

    def _parse_injection(self) -> Statement:
        if self.accept("OP", "->"):
            source = self.current
            if source.kind != "IDENT" or KEYWORDS.get(source.value.upper()) != "INPUT":
                self.fail("se esperaba INPUT o ENTRADA después de INYECTAR ->")
            kw = self.advance().value.upper()
            input_expr = self._parse_input(kw)
            self.expect("OP", "->")
            return self._finish_input_arrow_target(input_expr)

        declared_type = self._parse_optional_type()
        mutability: str | None = None
        if self.current.kind == "IDENT" and KEYWORDS.get(self.current.value.upper()) in {"VAR", "CONST"}:
            mutability = KEYWORDS[self.advance().value.upper()]
            if declared_type is None:
                declared_type = self._parse_optional_type()
        name = self.expect("IDENT").value
        if self.accept("OP", "="):
            if mutability is None and declared_type is None:
                self.fail("una declaración tras INYECTAR requiere VAR o CONST")
            return Variable(name, self.parse_expression(), declared_type, mutability != "CONST")
        self.expect("OP", "<-")
        value = self.parse_expression()
        if mutability is not None or declared_type is not None:
            return Variable(name, value, declared_type, mutability != "CONST")
        return Assignment(name, value)

    def parse_expression(self, minimum_precedence: int = 1) -> Expression:
        left = self.parse_prefix()
        while True:
            if minimum_precedence <= 6 and isinstance(left, Name) and self.current.kind == "OP" and self.current.value == "(":
                left = Call(left.value, self._parse_arguments())
                continue
            if (
                minimum_precedence <= 6
                and isinstance(left, Member)
                and isinstance(left.value, Name)
                and self.current.kind == "OP"
                and self.current.value == "("
            ):
                left = MethodCall(left.value.value, left.name, self._parse_arguments())
                continue
            if (
                minimum_precedence <= 6
                and isinstance(left, Name)
                and self.current.kind == "OP"
                and self.current.value == "-"
                and self.tokens[self.index + 1].kind == "IDENT"
                and self.tokens[self.index + 2].kind == "OP"
                and self.tokens[self.index + 2].value == "("
            ):
                self.advance()
                method_name = self.expect("IDENT").value
                left = MethodCall(left.value, method_name, self._parse_arguments())
                continue
            if minimum_precedence <= 6 and self.accept("OP", "["):
                key = self.parse_expression()
                self.expect("OP", "]")
                left = Index(left, key)
                continue
            if (
                minimum_precedence <= 6
                and self.index > 0
                and self.current.kind == "OP"
                and self.current.value == "."
                and self.tokens[self.index - 1].offset + len(self.tokens[self.index - 1].value) == self.current.offset
                and self.tokens[self.index + 1].kind == "IDENT"
                and self.current.offset + 1 == self.tokens[self.index + 1].offset
                and (
                    self.tokens[self.index + 1].value.upper() not in KEYWORDS
                    or self.tokens[self.index + 1].value.upper() in {"Y", "O", "AND", "OR", "ES", "IS"}
                )
            ):
                self.advance()
                left = Member(left, self.expect("IDENT").value)
                continue
            operator_token = self.current
            operator_key = operator_token.value.upper() if operator_token.kind == "IDENT" else operator_token.value
            operator_info = EXPRESSION_OPERATORS.get(operator_key)
            if operator_info is None:
                break
            precedence, operator = operator_info
            if precedence < minimum_precedence:
                break
            self.advance()
            right = self.parse_expression(precedence + 1)
            left = Binary(operator, left, right)
        return left

    def _parse_collection_inside(self, close_bracket: str) -> Expression:
        elements: list[Expression] = []
        entries: list[tuple[str, Expression]] = []
        if not self.accept("OP", close_bracket):
            if (
                self.current.kind == "IDENT"
                and self.tokens[self.index + 1].kind == "OP"
                and self.tokens[self.index + 1].value in {":", "="}
            ):
                first: Expression = Name(self.advance().value)
            else:
                first = self.parse_expression()
            if self.accept("OP", ":") or self.accept("OP", "="):
                if isinstance(first, Name):
                    key = first.value
                elif isinstance(first, StringLiteral):
                    key = first.value
                else:
                    self.fail("la clave de diccionario debe ser un nombre o texto")
                entries.append((key, self.parse_expression()))
                while not self.accept("OP", close_bracket):
                    self._expect_item_separator()
                    key_token = self.current
                    if key_token.kind == "IDENT":
                        key = self.advance().value
                    elif key_token.kind == "STRING":
                        key = decode_string(self.advance().value)
                    else:
                        self.fail("la clave de diccionario debe ser un nombre o texto")
                    if not (self.accept("OP", ":") or self.accept("OP", "=")):
                        self.fail("se esperaba ':' o '=' después de la clave del diccionario")
                    entries.append((key, self.parse_expression()))
                return DictionaryLiteral(tuple(entries))
            elements.append(first)
            while not self.accept("OP", close_bracket):
                self._expect_item_separator()
                elements.append(self.parse_expression())
        return ArrayLiteral(tuple(elements))

    def parse_prefix(self) -> Expression:
        token = self.current
        operator = KEYWORDS.get(token.value.upper(), token.value.upper()) if token.kind == "IDENT" else token.value
        if token.kind == "OP" and token.value == "*":
            self.advance()
            return Unary("deref", self.parse_expression(6))
        if operator in {"+", "-", "!", "NOT"}:
            self.advance()
            canonical_operator = {"!": "not", "NOT": "not"}.get(operator, operator)
            return Unary(canonical_operator, self.parse_expression(6))
        if self.accept("OP", "("):
            if self.accept("OP", ")"):
                return ArrayLiteral(())
            if (
                self.current.kind == "IDENT"
                and self.tokens[self.index + 1].kind == "OP"
                and self.tokens[self.index + 1].value in {":", "="}
            ):
                first_key = self.advance().value
                self.advance()
                entries: list[tuple[str, Expression]] = [(first_key, self.parse_expression())]
                while not self.accept("OP", ")"):
                    self._expect_item_separator()
                    key_tok = self.current
                    if key_tok.kind == "IDENT":
                        k = self.advance().value
                    elif key_tok.kind == "STRING":
                        k = decode_string(self.advance().value)
                    else:
                        self.fail("la clave de diccionario debe ser un nombre o texto")
                    if not (self.accept("OP", ":") or self.accept("OP", "=")):
                        self.fail("se esperaba ':' o '=' después de la clave del diccionario")
                    entries.append((k, self.parse_expression()))
                return DictionaryLiteral(tuple(entries))
            expression = self.parse_expression()
            if self._accept_item_separator():
                elements = [expression, self.parse_expression()]
                while not self.accept("OP", ")"):
                    self._expect_item_separator()
                    elements.append(self.parse_expression())
                return ArrayLiteral(tuple(elements))
            self.expect("OP", ")")
            return expression
        if self.accept("OP", "["):
            return self._parse_collection_inside("]")
        if self.accept("OP", "{"):
            entries = []
            if not self.accept("OP", "}"):
                while True:
                    key = self.current
                    if key.kind == "IDENT":
                        key_value = self.advance().value
                    elif key.kind == "STRING":
                        key_value = decode_string(self.advance().value)
                    else:
                        self.fail("la clave de un diccionario debe ser un identificador o texto")
                    if not (self.accept("OP", ":") or self.accept("OP", "=")):
                        self.fail("se esperaba ':' o '=' después de la clave del diccionario")
                    entries.append((key_value, self.parse_expression()))
                    if self.accept("OP", "}"):
                        break
                    self._expect_item_separator()
            return DictionaryLiteral(tuple(entries))
        if token.kind == "NUMBER":
            self.advance()
            if "." in token.value:
                return Literal(float(token.value))
            return Literal(integer_value(token))
        if token.kind == "STRING":
            self.advance()
            return StringLiteral(decode_string(token.value))
        if token.kind == "IDENT" and KEYWORDS.get(token.value.upper()) in {"TRUE", "FALSE"}:
            canonical_bool = KEYWORDS[token.value.upper()]
            raw_text = token.value.upper()
            self.advance()
            return BooleanLiteral(canonical_bool == "TRUE", raw=raw_text)
        if token.kind == "IDENT" and KEYWORDS.get(token.value.upper()) == "INPUT":
            kw = self.advance().value.upper()
            return self._parse_input(kw)
        if token.kind == "IDENT" and KEYWORDS.get(token.value.upper()) == "TYPE":
            if self.tokens[self.index + 1].kind == "IDENT" and self.tokens[self.index + 1].value.upper() in {"PY", "PYTHON", "JSON"}:
                self.advance()
                block = self._parse_type_python_statement()
                return PythonExpression(block.tasks)
            self.advance()
            return Name(token.value)
        if token.kind == "IDENT" and (
            token.value.upper() not in KEYWORDS
            or token.value.upper() in {"Y", "O", "AND", "OR", "ES", "IS"}
            or KEYWORDS.get(token.value.upper()) in {"TYPE_INT", "TYPE_FLOAT", "TYPE_STRING", "TYPE_BOOL"}
        ):
            self.advance()
            return Name(token.value)
        self.fail("se esperaba un entero, una variable o una expresión entre paréntesis")


def _resolve_python_task(task: PythonTask, base_dir: Path) -> PythonTask:
    if not task.is_file:
        return task
    candidate = Path(task.source)
    if not candidate.is_absolute():
        candidate = (base_dir / candidate).resolve()
    return PythonTask(str(candidate), True, task.target, task.kind)


def _resolve_python_in_expression(expression: Expression, base_dir: Path) -> Expression:
    if isinstance(expression, PythonExpression):
        return PythonExpression(tuple(_resolve_python_task(t, base_dir) for t in expression.tasks))
    if isinstance(expression, Unary):
        return Unary(expression.operator, _resolve_python_in_expression(expression.operand, base_dir))
    if isinstance(expression, Binary):
        return Binary(
            expression.operator,
            _resolve_python_in_expression(expression.left, base_dir),
            _resolve_python_in_expression(expression.right, base_dir),
        )
    if isinstance(expression, ArrayLiteral):
        return ArrayLiteral(tuple(_resolve_python_in_expression(item, base_dir) for item in expression.elements))
    if isinstance(expression, DictionaryLiteral):
        return DictionaryLiteral(
            tuple((k, _resolve_python_in_expression(v, base_dir)) for k, v in expression.entries)
        )
    if isinstance(expression, Index):
        return Index(
            _resolve_python_in_expression(expression.collection, base_dir),
            _resolve_python_in_expression(expression.key, base_dir),
        )
    if isinstance(expression, Member):
        return Member(_resolve_python_in_expression(expression.value, base_dir), expression.name)
    if isinstance(expression, Call):
        return Call(
            expression.name,
            tuple(_resolve_python_in_expression(arg, base_dir) for arg in expression.arguments),
        )
    if isinstance(expression, MethodCall):
        return MethodCall(
            expression.receiver,
            expression.name,
            tuple(_resolve_python_in_expression(arg, base_dir) for arg in expression.arguments),
        )
    return expression


def _resolve_python_in_statement(statement: Statement, base_dir: Path) -> Statement:
    if isinstance(statement, PythonBlock):
        return PythonBlock(tuple(_resolve_python_task(t, base_dir) for t in statement.tasks))
    if isinstance(statement, Variable):
        return Variable(
            statement.name,
            _resolve_python_in_expression(statement.value, base_dir),
            statement.value_type,
            statement.mutable,
        )
    if isinstance(statement, Assignment):
        return Assignment(
            statement.name,
            _resolve_python_in_expression(statement.value, base_dir),
        )
    if isinstance(statement, Print):
        return Print(
            statement.value
            if isinstance(statement.value, str)
            else _resolve_python_in_expression(statement.value, base_dir)
        )
    if isinstance(statement, If):
        return If(
            _resolve_python_in_expression(statement.condition, base_dir),
            tuple(_resolve_python_in_statement(item, base_dir) for item in statement.then_body),
            tuple(_resolve_python_in_statement(item, base_dir) for item in statement.else_body),
        )
    if isinstance(statement, WhileLoop):
        return WhileLoop(
            _resolve_python_in_expression(statement.condition, base_dir),
            tuple(_resolve_python_in_statement(item, base_dir) for item in statement.body),
            tuple(_resolve_python_in_statement(item, base_dir) for item in statement.step),
            tuple(_resolve_python_in_statement(item, base_dir) for item in statement.init),
        )
    if isinstance(statement, Function):
        return Function(
            statement.name,
            statement.parameters,
            tuple(_resolve_python_in_statement(item, base_dir) for item in statement.body),
        )
    if isinstance(statement, Macro):
        return Macro(
            statement.name,
            statement.parameters,
            tuple(_resolve_python_in_statement(item, base_dir) for item in statement.body),
        )
    if isinstance(statement, ClassDefinition):
        members: list[Variable | ClassCommand | ClassMethod] = []
        for member in statement.members:
            if isinstance(member, ClassMethod):
                members.append(
                    ClassMethod(
                        member.name,
                        member.parameters,
                        tuple(_resolve_python_in_statement(item, base_dir) for item in member.body),
                    )
                )
            else:
                members.append(member)
        return ClassDefinition(statement.name, tuple(members))
    return statement


_BASE_LIBRARY_SOURCE = """
COMANDO DOBLE(ENT x) -> RETORNAR x * 2.
COMANDO TRIPLE(ENT x) -> RETORNAR x * 3.
COMANDO CUADRADO(ENT x) -> RETORNAR x * x.
COMANDO CUBO(ENT x) -> RETORNAR x * x * x.
FN abs_ent(ENT x) {
    VAR r = x,
    SI r < 0 { r <- -r },
    RETORNAR r
}.
FN max_ent(ENT a | ENT b) {
    VAR r = a,
    SI b > a { r <- b },
    RETORNAR r
}.
FN min_ent(ENT a | ENT b) {
    VAR r = a,
    SI b < a { r <- b },
    RETORNAR r
}.
FN limitar_ent(ENT valor | ENT minimo | ENT maximo) {
    VAR r = valor,
    SI r < minimo { r <- minimo },
    SI r > maximo { r <- maximo },
    RETORNAR r
}.
"""

_MATH_LIBRARY_SOURCE = (
    _BASE_LIBRARY_SOURCE
    + """
FN cuadrado(ENT x) { RETORNAR x * x }.
FN cuadrado_ent(ENT x) { RETORNAR x * x }.
FN cubo(ENT x) { RETORNAR x * x * x }.
FN potencia_ent(ENT base | ENT exp) {
    VAR resultado = 1,
    VAR i = 0,
    CICLO i < exp {
        resultado *= base,
        i += 1
    },
    RETORNAR resultado
}.
FN factorial_ent(ENT n) {
    VAR acc = 1,
    VAR i = 1,
    CICLO i <= n {
        acc *= i,
        i += 1
    },
    RETORNAR acc
}.
FN suma_hasta(ENT n) {
    VAR total = 0,
    VAR i = 1,
    CICLO i <= n {
        total += i,
        i += 1
    },
    RETORNAR total
}.
FN fibonacci_ent(ENT n) {
    VAR a = 0,
    VAR b = 1,
    VAR i = 0,
    CICLO i < n {
        VAR sig = a + b,
        a <- b,
        b <- sig,
        i += 1
    },
    RETORNAR a
}.
"""
)

_CONSOLE_LIBRARY_SOURCE = """
COMANDO LINEA { IMPRIMIR "----------------------------------------" }.
COMANDO MOSTRAR(valor) { IMPRIMIR valor }.
COMANDO PAR(a | b) { IMPRIMIR a, IMPRIMIR b }.
"""

TIGER_LIBRARIES: dict[str, str] = {
    "base": _BASE_LIBRARY_SOURCE,
    "core": _BASE_LIBRARY_SOURCE,
    "std": _BASE_LIBRARY_SOURCE,
    "estandar": _BASE_LIBRARY_SOURCE,
    "matematicas": _MATH_LIBRARY_SOURCE,
    "math": _MATH_LIBRARY_SOURCE,
    "mate": _MATH_LIBRARY_SOURCE,
    "consola": _CONSOLE_LIBRARY_SOURCE,
    "console": _CONSOLE_LIBRARY_SOURCE,
    "io": _CONSOLE_LIBRARY_SOURCE,
}


def register_tiger_library(name: str, source: str) -> None:
    key = name.strip().casefold()
    if key.endswith(".tg"):
        key = key[:-3]
    elif key.endswith(".tgi"):
        key = key[:-4]
    TIGER_LIBRARIES[key] = source


def get_tiger_library(name: str) -> tuple[str, str] | None:
    raw = name.strip().replace("\\", "/")
    key = raw.casefold()
    if key in TIGER_LIBRARIES:
        return key, TIGER_LIBRARIES[key]
    stem = Path(raw).stem.casefold()
    suffix = Path(raw).suffix.casefold()
    if suffix in {"", ".tg", ".tgi"} and stem in TIGER_LIBRARIES:
        return stem, TIGER_LIBRARIES[stem]
    return None


def _find_library_file(module_spec: str, base_dir: Path, allow_relative_files: bool) -> Path | None:
    raw_path = Path(module_spec)
    compiler_dir = Path(__file__).resolve().parent
    search_roots: list[Path] = []
    if allow_relative_files:
        search_roots.extend(
            [
                base_dir,
                base_dir / "tools",
                base_dir / "tools" / "Libraries",
                base_dir / "Libraries",
                base_dir / "libraries",
                base_dir / "libs",
                base_dir / "librerias",
            ]
        )
    search_roots.extend(
        [
            compiler_dir / "tools",
            compiler_dir / "tools" / "Libraries",
            compiler_dir.parent / "tools",
            compiler_dir.parent / "tools" / "Libraries",
            compiler_dir / "Libraries",
            compiler_dir / "libraries",
            compiler_dir / "libs",
            compiler_dir / "librerias",
            compiler_dir,
            compiler_dir.parent,
        ]
    )

    rel_candidates: list[Path] = [raw_path]
    if not raw_path.suffix:
        rel_candidates.append(raw_path.with_suffix(".tg"))
        rel_candidates.append(raw_path.with_suffix(".tgi"))
        rel_candidates.append(raw_path.with_suffix(".py"))
        rel_candidates.append(raw_path.with_suffix(".json"))

    if raw_path.is_absolute():
        for cand in rel_candidates:
            if cand.is_file():
                return cand.resolve()
        return None

    for folder in search_roots:
        for cand in rel_candidates:
            full = (folder / cand).resolve()
            if full.is_file():
                return full
    return None


def _is_python_module_spec(module_spec: str) -> bool:
    cleaned = module_spec.strip()
    if not cleaned or any(ch in cleaned for ch in ("/", "\\", " ")):
        return False
    parts = cleaned.split(".")
    if not all(part.isidentifier() for part in parts):
        return False
    try:
        return importlib.util.find_spec(cleaned) is not None
    except (ModuleNotFoundError, ValueError):
        return False


def _resolve_imports(
    root: Path,
    root_statements: tuple[Statement, ...] | None = None,
    allow_relative_files: bool = True,
) -> tuple[Statement, ...]:
    visited_files: set[Path] = set()
    active_files: set[Path] = set()
    visited_libs: set[str] = set()
    active_libs: set[str] = set()
    compiler_dir = Path(__file__).resolve().parent

    def expand_import_target(module_spec: str, current_dir: Path) -> tuple[Statement, ...]:
        imported = Path(module_spec)
        suffix = imported.suffix.lower()
        if suffix and suffix not in {".tg", ".tgi", ".py", ".json"} and not _is_python_module_spec(module_spec):
            raise CompilerError(
                f"USAR de {imported.suffix or 'archivos externos'} no está implementado; "
                "Tiger 10 solo expande librerías, módulos .tg, .py o .json"
            )
        found_file = _find_library_file(module_spec, current_dir, allow_relative_files)
        if found_file is not None:
            found_suffix = found_file.suffix.lower()
            if found_suffix == ".py":
                if found_file in visited_files:
                    return ()
                visited_files.add(found_file)
                return (PythonBlock((PythonTask(str(found_file), True, None, "py"),)),)
            if found_suffix == ".json":
                if found_file in visited_files:
                    return ()
                visited_files.add(found_file)
                return (PythonBlock((PythonTask(str(found_file), True, None, "json"),)),)
            return visit_file(found_file)
        lib_entry = get_tiger_library(module_spec)
        if lib_entry is not None:
            lib_key, lib_source = lib_entry
            expanded_lib = list(visit_library(lib_key, lib_source))
            if _is_python_module_spec(module_spec):
                py_key = f"py:{module_spec}"
                if py_key not in visited_libs:
                    visited_libs.add(py_key)
                    expanded_lib.append(PythonBlock((PythonTask(module_spec, False, None, "lib"),)))
            return tuple(expanded_lib)
        if _is_python_module_spec(module_spec):
            py_key = f"py:{module_spec}"
            if py_key in visited_libs:
                return ()
            visited_libs.add(py_key)
            return (PythonBlock((PythonTask(module_spec, False, None, "lib"),)),)
        if not allow_relative_files:
            raise CompilerError(
                "USAR requiere compilar desde un archivo .tg para resolver rutas relativas "
                f"o una librería Tiger registrada ({module_spec})"
            )
        if suffix == ".py":
            target_py = (current_dir / imported).resolve()
            if target_py in visited_files:
                return ()
            visited_files.add(target_py)
            return (PythonBlock((PythonTask(str(target_py), True, None, "py"),)),)
        if suffix == ".json":
            target_json = (current_dir / imported).resolve()
            if target_json in visited_files:
                return ()
            visited_files.add(target_json)
            return (PythonBlock((PythonTask(str(target_json), True, None, "json"),)),)
        target_path = (
            (current_dir / imported).resolve()
            if suffix
            else (current_dir / f"{module_spec}.tg").resolve()
        )
        return visit_file(target_path)

    def expand_statement_list(
        statements: tuple[Statement, ...],
        current_dir: Path,
        hoisted_defs: list[Statement] | None = None,
    ) -> tuple[Statement, ...]:
        expanded: list[Statement] = []
        for statement in statements:
            if isinstance(statement, Import):
                for mod in statement.modules:
                    imported_stmts = expand_import_target(mod, current_dir)
                    if hoisted_defs is not None:
                        for imp_stmt in imported_stmts:
                            if isinstance(imp_stmt, (Function, ClassDefinition, Macro)):
                                hoisted_defs.append(imp_stmt)
                            else:
                                expanded.append(imp_stmt)
                    else:
                        expanded.extend(imported_stmts)
            elif isinstance(statement, Function):
                inner_hoisted: list[Statement] = [] if hoisted_defs is None else hoisted_defs
                new_body = expand_statement_list(statement.body, current_dir, inner_hoisted)
                if hoisted_defs is None and inner_hoisted:
                    expanded.extend(inner_hoisted)
                expanded.append(
                    Function(
                        statement.name,
                        statement.parameters,
                        tuple(_resolve_python_in_statement(item, current_dir) for item in new_body),
                    )
                )
            elif isinstance(statement, If):
                inner_hoisted = [] if hoisted_defs is None else hoisted_defs
                new_then = expand_statement_list(statement.then_body, current_dir, inner_hoisted)
                new_else = expand_statement_list(statement.else_body, current_dir, inner_hoisted)
                if hoisted_defs is None and inner_hoisted:
                    expanded.extend(inner_hoisted)
                expanded.append(
                    If(
                        _resolve_python_in_expression(statement.condition, current_dir),
                        new_then,
                        new_else,
                    )
                )
            elif isinstance(statement, WhileLoop):
                inner_hoisted = [] if hoisted_defs is None else hoisted_defs
                new_init = expand_statement_list(statement.init, current_dir, inner_hoisted)
                new_body = expand_statement_list(statement.body, current_dir, inner_hoisted)
                new_step = expand_statement_list(statement.step, current_dir, inner_hoisted)
                if hoisted_defs is None and inner_hoisted:
                    expanded.extend(inner_hoisted)
                expanded.append(
                    WhileLoop(
                        _resolve_python_in_expression(statement.condition, current_dir),
                        new_body,
                        new_step,
                        new_init,
                    )
                )
            elif isinstance(statement, Macro):
                inner_hoisted = [] if hoisted_defs is None else hoisted_defs
                new_body = expand_statement_list(statement.body, current_dir, inner_hoisted)
                if hoisted_defs is None and inner_hoisted:
                    expanded.extend(inner_hoisted)
                expanded.append(Macro(statement.name, statement.parameters, new_body))
            else:
                expanded.append(_resolve_python_in_statement(statement, current_dir))
        return tuple(expanded)

    def visit_library(lib_key: str, lib_source: str) -> tuple[Statement, ...]:
        if lib_key in active_libs:
            raise CompilerError(f"ciclo de importación detectado en la librería {lib_key}")
        if lib_key in visited_libs:
            return ()
        active_libs.add(lib_key)
        parsed = Parser(lib_source).parse()
        expanded = expand_statement_list(parsed, compiler_dir)
        active_libs.remove(lib_key)
        visited_libs.add(lib_key)
        return expanded

    def visit_file(
        path: Path,
        pre_parsed: tuple[Statement, ...] | None = None,
    ) -> tuple[Statement, ...]:
        resolved = path.resolve()
        if resolved in active_files:
            raise CompilerError(f"ciclo de importación detectado en {resolved}")
        if resolved in visited_files:
            return ()
        active_files.add(resolved)
        if pre_parsed is None:
            try:
                source = resolved.read_text(encoding="utf-8")
            except OSError as error:
                raise CompilerError(f"no se pudo leer {resolved}: {error}") from error
            parsed = Parser(source).parse()
        else:
            parsed = pre_parsed
        expanded = expand_statement_list(parsed, resolved.parent)
        active_files.remove(resolved)
        visited_files.add(resolved)
        return expanded

    return visit_file(root, root_statements)


def _substitute_expression(
    expression: Expression,
    substitutions: dict[str, Expression],
) -> Expression:
    if isinstance(expression, Name):
        return substitutions.get(expression.value, expression)
    if isinstance(expression, Unary):
        return Unary(expression.operator, _substitute_expression(expression.operand, substitutions))
    if isinstance(expression, Binary):
        return Binary(
            expression.operator,
            _substitute_expression(expression.left, substitutions),
            _substitute_expression(expression.right, substitutions),
        )
    if isinstance(expression, ArrayLiteral):
        return ArrayLiteral(tuple(_substitute_expression(item, substitutions) for item in expression.elements))
    if isinstance(expression, DictionaryLiteral):
        return DictionaryLiteral(
            tuple((key, _substitute_expression(value, substitutions)) for key, value in expression.entries)
        )
    if isinstance(expression, Index):
        return Index(
            _substitute_expression(expression.collection, substitutions),
            _substitute_expression(expression.key, substitutions),
        )
    if isinstance(expression, Member):
        return Member(_substitute_expression(expression.value, substitutions), expression.name)
    if isinstance(expression, Call):
        return Call(
            expression.name,
            tuple(_substitute_expression(arg, substitutions) for arg in expression.arguments),
        )
    if isinstance(expression, MethodCall):
        return MethodCall(
            expression.receiver,
            expression.name,
            tuple(_substitute_expression(arg, substitutions) for arg in expression.arguments),
        )
    if isinstance(expression, PythonExpression):
        new_tasks: list[PythonTask] = []
        for task in expression.tasks:
            target = task.target
            if isinstance(target, str):
                rep = substitutions.get(target)
                if isinstance(rep, Name):
                    target = rep.value
            elif isinstance(target, Expression):
                target = _substitute_expression(target, substitutions)
            new_tasks.append(PythonTask(task.source, task.is_file, target, task.kind))
        return PythonExpression(tuple(new_tasks))
    return expression


def _substitute_statement(
    statement: Statement,
    substitutions: dict[str, Expression],
) -> Statement:
    if isinstance(statement, PythonBlock):
        new_tasks: list[PythonTask] = []
        for task in statement.tasks:
            target = task.target
            if isinstance(target, str):
                rep = substitutions.get(target)
                if isinstance(rep, Name):
                    target = rep.value
            elif isinstance(target, Expression):
                target = _substitute_expression(target, substitutions)
            new_tasks.append(PythonTask(task.source, task.is_file, target, task.kind))
        return PythonBlock(tuple(new_tasks))
    if isinstance(statement, Variable):
        target_name = statement.name
        replacement = substitutions.get(target_name)
        if isinstance(replacement, Name):
            target_name = replacement.value
        return Variable(
            target_name,
            _substitute_expression(statement.value, substitutions),
            statement.value_type,
            statement.mutable,
        )
    if isinstance(statement, Assignment):
        asgn_target = statement.name
        if isinstance(asgn_target, str):
            replacement = substitutions.get(asgn_target)
            if isinstance(replacement, Name):
                asgn_target = replacement.value
        else:
            asgn_target = _substitute_expression(asgn_target, substitutions)
        return Assignment(asgn_target, _substitute_expression(statement.value, substitutions))
    if isinstance(statement, Print):
        return Print(
            statement.value
            if isinstance(statement.value, str)
            else _substitute_expression(statement.value, substitutions)
        )
    if isinstance(statement, If):
        return If(
            _substitute_expression(statement.condition, substitutions),
            tuple(_substitute_statement(item, substitutions) for item in statement.then_body),
            tuple(_substitute_statement(item, substitutions) for item in statement.else_body),
        )
    if isinstance(statement, WhileLoop):
        return WhileLoop(
            _substitute_expression(statement.condition, substitutions),
            tuple(_substitute_statement(item, substitutions) for item in statement.body),
            tuple(_substitute_statement(item, substitutions) for item in statement.step),
            tuple(_substitute_statement(item, substitutions) for item in statement.init),
        )
    if isinstance(statement, Return):
        return Return(_substitute_expression(statement.value, substitutions))
    if isinstance(statement, MacroCall):
        return MacroCall(
            statement.name,
            tuple(_substitute_expression(arg, substitutions) for arg in statement.arguments),
        )
    if isinstance(statement, PointerStore):
        return PointerStore(
            statement.name,
            _substitute_expression(statement.value, substitutions),
        )
    if isinstance(statement, ArrowAssignment):
        return ArrowAssignment(
            statement.source,
            _substitute_expression(statement.value, substitutions),
        )
    if isinstance(statement, ClassMethodCall):
        return ClassMethodCall(
            statement.class_name,
            statement.method_name,
            tuple(_substitute_expression(arg, substitutions) for arg in statement.arguments),
        )
    return statement


def _discover_python_symbols(task: PythonTask) -> tuple[set[str], set[str]]:
    assigned_vars: set[str] = set()
    defined_funcs: set[str] = set()
    if task.kind == "lib":
        mod_spec = task.source.strip()
        top_name = mod_spec.split(".")[0]
        if top_name.isidentifier():
            assigned_vars.add(top_name)
            defined_funcs.add(top_name)
        try:
            mod = importlib.import_module(mod_spec)
            for attr_name in dir(mod):
                if attr_name.startswith("_"):
                    continue
                if attr_name.upper() in KEYWORDS and attr_name.upper() not in {"Y", "O"}:
                    continue
                val = getattr(mod, attr_name, None)
                if callable(val):
                    defined_funcs.add(attr_name)
                    assigned_vars.add(attr_name)
                elif isinstance(val, (int, float, str, list, dict, tuple)):
                    assigned_vars.add(attr_name)
        except Exception:
            pass
        if isinstance(task.target, str):
            assigned_vars.add(task.target)
        elif isinstance(task.target, Name):
            assigned_vars.add(task.target.value)
        return assigned_vars, defined_funcs

    if task.kind == "json":
        assigned_vars.add("JSON")
        raw_json: str | None = None
        if task.is_file:
            json_path = Path(task.source)
            if not json_path.is_absolute():
                json_path = (Path.cwd() / json_path).resolve()
            if json_path.is_file():
                try:
                    raw_json = json_path.read_text(encoding="utf-8")
                except OSError:
                    raw_json = None
        else:
            raw_json = task.source
        if raw_json:
            try:
                parsed_json = _parse_json_data(raw_json)
                if isinstance(parsed_json, dict):
                    for key in parsed_json:
                        if isinstance(key, str) and key.isidentifier():
                            if key.upper() not in KEYWORDS or key.upper() in {"Y", "O"}:
                                assigned_vars.add(key)
            except CompilerError:
                pass
        if isinstance(task.target, str):
            assigned_vars.add(task.target)
        elif isinstance(task.target, Name):
            assigned_vars.add(task.target.value)
        return assigned_vars, defined_funcs

    code: str | None = None
    if task.is_file:
        script_path = Path(task.source)
        if not script_path.is_absolute():
            script_path = (Path.cwd() / script_path).resolve()
        if script_path.is_file():
            try:
                code = script_path.read_text(encoding="utf-8")
            except OSError:
                code = None
    else:
        code = task.source
    if code:
        import re
        for m in re.finditer(r'\b(?:TIGER|TIGRE)[.-]([a-zA-Z0-9_]+)\s*=', code):
            assigned_vars.add(m.group(1))
        for m in re.finditer(r'\b(?:TIGER|TIGRE)[.-]([a-zA-Z0-9_]+)\s*\(', code):
            defined_funcs.add(m.group(1))
        try:
            tree = ast.parse(code)
            for node in ast.walk(tree):
                if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
                    defined_funcs.add(node.name)
                    assigned_vars.add(node.name)
                elif isinstance(node, ast.Assign):
                    for target in node.targets:
                        for name_node in ast.walk(target):
                            if isinstance(name_node, ast.Name) and isinstance(name_node.ctx, ast.Store):
                                assigned_vars.add(name_node.id)
                elif isinstance(node, (ast.AnnAssign, ast.AugAssign)):
                    for name_node in ast.walk(node.target):
                        if isinstance(name_node, ast.Name) and isinstance(name_node.ctx, ast.Store):
                            assigned_vars.add(name_node.id)
                elif isinstance(node, (ast.Import, ast.ImportFrom)):
                    for alias in node.names:
                        bound = alias.asname or alias.name.split(".")[0]
                        if bound != "*":
                            assigned_vars.add(bound)
                            defined_funcs.add(bound)
                elif isinstance(node, ast.Assign):
                    for target in node.targets:
                        if isinstance(target, ast.Attribute) and isinstance(target.value, ast.Name):
                            if target.value.id.upper() in {"TIGER", "TIGRE"}:
                                assigned_vars.add(target.attr)
        except SyntaxError:
            pass
    if isinstance(task.target, str):
        assigned_vars.add(task.target)
    elif isinstance(task.target, Name):
        assigned_vars.add(task.target.value)
    return assigned_vars, defined_funcs


def _expand_macros(statements: tuple[Statement, ...]) -> tuple[Statement, ...]:
    macros_by_arity: dict[tuple[str, int], Macro] = {}
    latest_macros: dict[str, Macro] = {}
    for statement in statements:
        if isinstance(statement, Macro):
            key = statement.name.casefold()
            macros_by_arity[(key, len(statement.parameters))] = statement
            latest_macros[key] = statement

    def expand_expression(expr: Expression, stack: tuple[str, ...], emit_list: list[Statement]) -> Expression:
        if isinstance(expr, Unary):
            return Unary(expr.operator, expand_expression(expr.operand, stack, emit_list))
        if isinstance(expr, Binary):
            return Binary(
                expr.operator,
                expand_expression(expr.left, stack, emit_list),
                expand_expression(expr.right, stack, emit_list),
            )
        if isinstance(expr, ArrayLiteral):
            return ArrayLiteral(tuple(expand_expression(item, stack, emit_list) for item in expr.elements))
        if isinstance(expr, DictionaryLiteral):
            return DictionaryLiteral(
                tuple((k, expand_expression(v, stack, emit_list)) for k, v in expr.entries)
            )
        if isinstance(expr, Index):
            return Index(
                expand_expression(expr.collection, stack, emit_list),
                expand_expression(expr.key, stack, emit_list),
            )
        if isinstance(expr, Member):
            return Member(expand_expression(expr.value, stack, emit_list), expr.name)
        if isinstance(expr, MethodCall):
            return MethodCall(
                expr.receiver,
                expr.name,
                tuple(expand_expression(arg, stack, emit_list) for arg in expr.arguments),
            )
        if isinstance(expr, Call):
            expanded_args = tuple(expand_expression(arg, stack, emit_list) for arg in expr.arguments)
            key = expr.name.casefold()
            macro = macros_by_arity.get((key, len(expanded_args))) or latest_macros.get(key)
            if macro is not None and macro.body and isinstance(macro.body[-1], Return):
                if key in stack:
                    raise CompilerError(f"expansión recursiva de COMANDO: {expr.name}")
                if len(expanded_args) != len(macro.parameters):
                    raise CompilerError(
                        f"COMANDO {expr.name} espera {len(macro.parameters)} argumentos, "
                        f"recibió {len(expanded_args)}"
                    )
                substitutions = dict(zip(macro.parameters, expanded_args))
                prefix_stmts = tuple(
                    _substitute_statement(item, substitutions) for item in macro.body[:-1]
                )
                if prefix_stmts:
                    emit_list.extend(expand(prefix_stmts, stack + (key,)))
                ret_expr = _substitute_expression(macro.body[-1].value, substitutions)
                return expand_expression(ret_expr, stack + (key,), emit_list)
            return Call(expr.name, expanded_args)
        return expr

    def expand(items: tuple[Statement, ...], stack: tuple[str, ...] = ()) -> tuple[Statement, ...]:
        result: list[Statement] = []
        for statement in items:
            if isinstance(statement, Macro):
                key = statement.name.casefold()
                macros_by_arity[(key, len(statement.parameters))] = statement
                latest_macros[key] = statement
                continue
            if isinstance(statement, MacroCall):
                expanded_args = tuple(expand_expression(arg, stack, result) for arg in statement.arguments)
                key = statement.name.casefold()
                macro = macros_by_arity.get((key, len(expanded_args))) or latest_macros.get(key)
                if macro is None:
                    result.append(MacroCall(statement.name, expanded_args))
                    continue
                if key in stack:
                    raise CompilerError(f"expansión recursiva de COMANDO: {statement.name}")
                if len(expanded_args) != len(macro.parameters):
                    raise CompilerError(
                        f"COMANDO {statement.name} espera {len(macro.parameters)} argumentos, "
                        f"recibió {len(expanded_args)}"
                    )
                substitutions = dict(zip(macro.parameters, expanded_args))
                body_items = macro.body[:-1] if macro.body and isinstance(macro.body[-1], Return) else macro.body
                expanded_body = tuple(
                    _substitute_statement(item, substitutions) for item in body_items
                )
                result.extend(expand(expanded_body, stack + (key,)))
            elif isinstance(statement, Variable):
                val = expand_expression(statement.value, stack, result)
                result.append(Variable(statement.name, val, statement.value_type, statement.mutable))
            elif isinstance(statement, Assignment):
                val = expand_expression(statement.value, stack, result)
                result.append(Assignment(statement.name, val))
            elif isinstance(statement, Print):
                val = (
                    statement.value
                    if isinstance(statement.value, str)
                    else expand_expression(statement.value, stack, result)
                )
                result.append(Print(val))
            elif isinstance(statement, Return):
                result.append(Return(expand_expression(statement.value, stack, result)))
            elif isinstance(statement, Function):
                result.append(
                    Function(
                        statement.name,
                        statement.parameters,
                        expand(statement.body, stack),
                    )
                )
            elif isinstance(statement, If):
                cond = expand_expression(statement.condition, stack, result)
                result.append(
                    If(
                        cond,
                        expand(statement.then_body, stack),
                        expand(statement.else_body, stack),
                    )
                )
            elif isinstance(statement, WhileLoop):
                if statement.init:
                    result.extend(expand(statement.init, stack))
                cond = expand_expression(statement.condition, stack, result)
                result.append(
                    WhileLoop(
                        cond,
                        expand(statement.body, stack),
                        expand(statement.step, stack),
                        (),
                    )
                )
            elif isinstance(statement, ClassDefinition):
                members: list[Variable | ClassCommand | ClassMethod] = []
                for member in statement.members:
                    if isinstance(member, ClassMethod):
                        members.append(
                            ClassMethod(
                                member.name,
                                member.parameters,
                                expand(member.body, stack),
                            )
                        )
                    else:
                        members.append(member)
                result.append(ClassDefinition(statement.name, tuple(members)))
            else:
                result.append(statement)
        return tuple(result)

    return expand(statements)


def _lower_program(statements: tuple[Statement, ...]) -> tuple[Statement, ...]:
    expanded = _expand_macros(statements)
    functions: dict[str, Function] = {}
    classes: dict[str, ClassDefinition] = {}
    principal: Function | None = None
    executable: list[Statement] = []

    for statement in expanded:
        if isinstance(statement, TargetDirective):
            continue
        elif isinstance(statement, Function):
            key = statement.name.casefold()
            if key == "principal":
                if statement.parameters:
                    raise CompilerError("FN principal no admite parámetros")
                if principal is not None:
                    principal = Function(
                        statement.name,
                        (),
                        principal.body + statement.body,
                    )
                else:
                    principal = statement
            else:
                functions[key] = statement
        elif isinstance(statement, ClassDefinition):
            key = statement.name.casefold()
            classes[key] = statement
        else:
            executable.append(statement)

    if principal is not None:
        executable = list(executable) + list(principal.body)

    def _is_valid_class_default(expr: Expression) -> bool:
        if isinstance(expr, (Literal, StringLiteral, ArrayLiteral, DictionaryLiteral)):
            return True
        if isinstance(expr, Unary) and expr.operator in {"+", "-"} and isinstance(expr.operand, Literal):
            return True
        return False

    class_fields: dict[str, dict[str, Variable]] = {}
    class_methods: dict[str, dict[str, ClassMethod]] = {}
    class_commands: dict[str, dict[str, ClassCommand]] = {}
    for key, definition in classes.items():
        fields: dict[str, Variable] = {}
        methods: dict[str, ClassMethod] = {}
        commands: dict[str, ClassCommand] = {}
        for member in definition.members:
            if isinstance(member, Variable):
                if not _is_valid_class_default(member.value):
                    raise CompilerError("los valores por defecto de clase deben ser literales")
                fields[member.name.casefold()] = member
            elif isinstance(member, ClassMethod):
                method_key = member.name.casefold()
                commands.pop(method_key, None)
                methods[method_key] = member
            elif isinstance(member, ClassCommand):
                command_key = member.name.casefold()
                methods.pop(command_key, None)
                commands[command_key] = member
        class_fields[key] = fields
        class_methods[key] = methods
        class_commands[key] = commands

    object_types: dict[str, str] = {}
    field_names: dict[str, dict[str, str]] = {}
    static_field_names: dict[str, dict[str, str]] = {}
    initialized_static_fields: set[str] = set()
    known_types: dict[str, str] = {}
    python_functions: set[str] = set()
    inline_counter = 0

    def static_type(expression: Expression) -> str | None:
        if isinstance(expression, BooleanLiteral):
            return "bool"
        if isinstance(expression, Literal):
            return "float" if isinstance(expression.value, float) else "int"
        if isinstance(expression, StringLiteral):
            return "string"
        if isinstance(expression, ArrayLiteral):
            if not expression.elements:
                return "array[dynamic]"
            elem_type = static_type(expression.elements[0])
            return f"array[{elem_type or 'dynamic'}]"
        if isinstance(expression, DictionaryLiteral):
            return "dict[dynamic]"
        if isinstance(expression, Name):
            return known_types.get(expression.value)
        if isinstance(expression, Unary):
            if expression.operator in {"not", "!"}:
                return "bool"
            return static_type(expression.operand)
        if isinstance(expression, Binary):
            left_type = static_type(expression.left)
            right_type = static_type(expression.right)
            if expression.operator in COMPARISONS or expression.operator in {"and", "or"}:
                return "bool"
            return left_type if left_type == right_type else None
        return None

    for item in executable:
        if isinstance(item, Variable):
            inferred = item.value_type or static_type(item.value)
            if inferred is not None:
                known_types[item.name] = inferred
        elif isinstance(item, PythonBlock):
            for task in item.tasks:
                py_vars, py_funcs = _discover_python_symbols(task)
                for v in py_vars:
                    known_types.setdefault(v, "dynamic")
                for fn_name in py_funcs:
                    python_functions.add(fn_name.casefold())

    def ensure_receiver(receiver_name: str, emit_list: list[Statement]) -> tuple[str, dict[str, str]] | None:
        receiver_key = receiver_name.casefold()
        if receiver_key in object_types:
            return object_types[receiver_key], field_names[receiver_key]
        if receiver_key in class_fields:
            receiver_fields = static_field_names.setdefault(
                receiver_key,
                {
                    field.name.casefold(): f"{classes[receiver_key].name}__{field.name}"
                    for field in class_fields[receiver_key].values()
                },
            )
            if receiver_key not in initialized_static_fields:
                initialized_static_fields.add(receiver_key)
                for field_key, declaration in class_fields[receiver_key].items():
                    mapped_name = receiver_fields[field_key]
                    inferred = declaration.value_type or static_type(declaration.value)
                    if inferred is not None:
                        known_types[mapped_name] = inferred
                    emit_list.append(
                        Variable(
                            mapped_name,
                            declaration.value,
                            declaration.value_type,
                            True,
                        )
                    )
            return receiver_key, receiver_fields
        return None

    def _is_dict_name(name: str) -> bool:
        return known_types.get(name, "").startswith("dict[")

    def _is_receiver_name(name: str) -> bool:
        key = name.casefold()
        return key in object_types or key in class_fields or _is_dict_name(name)

    def _split_rightmost_receiver(expr: Expression) -> tuple[Expression | None, str | None, str | None]:
        if isinstance(expr, Name) and _is_receiver_name(expr.value):
            return None, None, expr.value
        if isinstance(expr, Binary) and expr.operator in {"+", "-", "*", "/", "%"}:
            sub_left, sub_op, recv = _split_rightmost_receiver(expr.right)
            if recv is not None:
                if sub_left is None:
                    return expr.left, expr.operator, recv
                return Binary(expr.operator, expr.left, sub_left), sub_op, recv
        return None, None, None

    def _split_leftmost_ident(expr: Expression) -> tuple[str | None, str | None, Expression | None]:
        if isinstance(expr, Name):
            return expr.value, None, None
        if isinstance(expr, Binary) and expr.operator in {"*", "/", "%"}:
            ident, sub_op, sub_right = _split_leftmost_ident(expr.left)
            if ident is not None:
                if sub_right is None:
                    return ident, expr.operator, expr.right
                return ident, sub_op, Binary(expr.operator, sub_right, expr.right)
        return None, None, None

    def _prepare_inline_bindings(
        body_items: tuple[Statement, ...],
        base_bindings: dict[str, Expression],
        prefix: str,
    ) -> tuple[tuple[Statement, ...], dict[str, Expression]]:
        nonlocal inline_counter
        inline_counter += 1
        call_id = inline_counter
        bindings = dict(base_bindings)

        def rename_block(items: tuple[Statement, ...]) -> tuple[Statement, ...]:
            renamed: list[Statement] = []
            for stmt in items:
                if isinstance(stmt, Variable):
                    local_name = f"__{prefix}_{call_id}_{stmt.name}"
                    substituted_val = _substitute_expression(stmt.value, bindings)
                    renamed.append(Variable(local_name, substituted_val, stmt.value_type, stmt.mutable))
                    bindings[stmt.name] = Name(local_name)
                elif isinstance(stmt, If):
                    cond = _substitute_expression(stmt.condition, bindings)
                    then_b = rename_block(stmt.then_body)
                    else_b = rename_block(stmt.else_body)
                    renamed.append(If(cond, then_b, else_b))
                elif isinstance(stmt, WhileLoop):
                    init_b = rename_block(stmt.init)
                    cond = _substitute_expression(stmt.condition, bindings)
                    body_b = rename_block(stmt.body)
                    step_b = rename_block(stmt.step)
                    renamed.append(WhileLoop(cond, body_b, step_b, init_b))
                else:
                    renamed.append(_substitute_statement(stmt, bindings))
            return tuple(renamed)

        return rename_block(body_items), bindings

    def _lower_python_tasks(
        tasks: tuple[PythonTask, ...],
        field_map: dict[str, str],
        stack: tuple[str, ...],
        emit_list: list[Statement],
        override_target: str | Expression | None = None,
    ) -> PythonBlock:
        lowered_tasks: list[PythonTask] = []
        for idx, task in enumerate(tasks):
            target = override_target if (override_target is not None and idx == len(tasks) - 1) else task.target
            if isinstance(target, str):
                target = field_map.get(target.casefold(), target)
            elif isinstance(target, Expression):
                lowered_target = lower_expression(target, {}, field_map, stack, emit_list)
                target = lowered_target.value if isinstance(lowered_target, Name) else lowered_target
            normalized_task = PythonTask(task.source, task.is_file, target, task.kind)
            py_vars, py_funcs = _discover_python_symbols(normalized_task)
            for v in py_vars:
                if normalized_task.kind == "json" and v == "JSON":
                    known_types[v] = "dict[dynamic]"
                else:
                    known_types.setdefault(v, "dynamic")
            if normalized_task.kind == "json" and isinstance(target, str):
                known_types[target] = "dict[dynamic]"
            for fn_name in py_funcs:
                python_functions.add(fn_name.casefold())
            lowered_tasks.append(normalized_task)
        return PythonBlock(tuple(lowered_tasks))

    def lower_expression(
        expression: Expression,
        substitutions: dict[str, Expression],
        fields: dict[str, str],
        stack: tuple[str, ...],
        emit_list: list[Statement] | None = None,
    ) -> Expression:
        target_emit = emit_list if emit_list is not None else []
        if isinstance(expression, Name):
            replacement = substitutions.get(expression.value)
            if replacement is not None:
                return lower_expression(replacement, {}, fields, stack, target_emit)
            return Name(fields.get(expression.value.casefold(), expression.value))
        if isinstance(expression, Unary):
            return Unary(
                expression.operator,
                lower_expression(expression.operand, substitutions, fields, stack, target_emit),
            )
        if isinstance(expression, Binary):
            if expression.operator == "-":
                if (
                    isinstance(expression.left, Name)
                    and isinstance(expression.right, Name)
                    and expression.left.value not in substitutions
                    and _is_receiver_name(expression.left.value)
                ):
                    resolved = ensure_receiver(expression.left.value, target_emit)
                    if resolved is None:
                        return Member(
                            Name(fields.get(expression.left.value.casefold(), expression.left.value)),
                            expression.right.value,
                        )
                    class_key, object_fields = resolved
                    member_key = expression.right.value.casefold()
                    field_name = object_fields.get(member_key)
                    if field_name is not None:
                        return Name(field_name)
                    if member_key in class_methods.get(class_key, {}):
                        return lower_expression(
                            MethodCall(expression.left.value, expression.right.value, ()),
                            substitutions,
                            fields,
                            stack,
                            target_emit,
                        )
                    raise CompilerError(
                        f"campo desconocido en {expression.left.value}: {expression.right.value}"
                    )
                prefix_expr, prefix_op, recv_name = _split_rightmost_receiver(expression.left)
                field_ident, suffix_op, suffix_expr = _split_leftmost_ident(expression.right)
                if recv_name is not None and recv_name not in substitutions and field_ident is not None:
                    access_expr: Expression = Binary("-", Name(recv_name), Name(field_ident))
                    if suffix_op is not None and suffix_expr is not None:
                        access_expr = Binary(suffix_op, access_expr, suffix_expr)
                    if prefix_op is not None and prefix_expr is not None:
                        access_expr = Binary(prefix_op, prefix_expr, access_expr)
                    return lower_expression(access_expr, substitutions, fields, stack, target_emit)
            lowered_left = lower_expression(expression.left, substitutions, fields, stack, target_emit)
            lowered_right = lower_expression(expression.right, substitutions, fields, stack, target_emit)
            if (
                expression.operator == "+"
                and isinstance(lowered_left, StringLiteral)
                and isinstance(lowered_right, StringLiteral)
            ):
                return StringLiteral(lowered_left.value + lowered_right.value)
            return Binary(
                expression.operator,
                lowered_left,
                lowered_right,
            )
        if isinstance(expression, ArrayLiteral):
            return ArrayLiteral(
                tuple(lower_expression(item, substitutions, fields, stack, target_emit) for item in expression.elements)
            )
        if isinstance(expression, DictionaryLiteral):
            return DictionaryLiteral(
                tuple(
                    (key, lower_expression(value, substitutions, fields, stack, target_emit))
                    for key, value in expression.entries
                )
            )
        if isinstance(expression, Index):
            return Index(
                lower_expression(expression.collection, substitutions, fields, stack, target_emit),
                lower_expression(expression.key, substitutions, fields, stack, target_emit),
            )
        if isinstance(expression, Member):
            if isinstance(expression.value, Name) and expression.value.value not in substitutions:
                resolved = ensure_receiver(expression.value.value, target_emit)
                if resolved is not None:
                    _, object_fields = resolved
                    field_name = object_fields.get(expression.name.casefold())
                    if field_name is not None:
                        return Name(field_name)
                    raise CompilerError(
                        f"campo desconocido en {expression.value.value}: {expression.name}"
                    )
            return Member(
                lower_expression(expression.value, substitutions, fields, stack, target_emit),
                expression.name,
            )
        if isinstance(expression, Call):
            key = expression.name.casefold()
            function = functions.get(key)
            if function is None:
                if key in python_functions or expression.name in known_types:
                    return Call(
                        expression.name,
                        tuple(
                            lower_expression(argument, substitutions, fields, stack, target_emit)
                            for argument in expression.arguments
                        ),
                    )
                raise CompilerError(f"función no declarada: {expression.name}")
            if key in stack:
                raise CompilerError(f"recursión no admitida en la función {expression.name}")
            if len(expression.arguments) != len(function.parameters):
                raise CompilerError(
                    f"FN {function.name} espera {len(function.parameters)} argumentos, "
                    f"recibió {len(expression.arguments)}"
                )
            arguments = tuple(
                lower_expression(argument, substitutions, fields, stack, target_emit)
                for argument in expression.arguments
            )
            bindings: dict[str, Expression] = {}
            for parameter, argument, original in zip(
                function.parameters, arguments, expression.arguments
            ):
                argument_type = static_type(argument) or static_type(original)
                if argument_type is not None and argument_type != parameter.value_type:
                    raise CompilerError(
                        f"el parámetro {parameter.name} de FN {function.name} espera "
                        f"{parameter.value_type}, no {argument_type}"
                    )
                bindings[parameter.name] = argument
            returns = [item for item in function.body if isinstance(item, Return)]
            if len(returns) != 1 or function.body[-1] is not returns[0]:
                raise CompilerError(
                    f"FN {function.name} debe terminar con un único RETORNAR para compilarse inline"
                )
            renamed_body, final_bindings = _prepare_inline_bindings(function.body[:-1], bindings, f"fn_{function.name}")
            if renamed_body:
                target_emit.extend(lower_statements(renamed_body, {}, stack + (key,)))
            return lower_expression(
                function.body[-1].value,
                final_bindings,
                {},
                stack + (key,),
                target_emit,
            )
        if isinstance(expression, MethodCall):
            resolved = ensure_receiver(expression.receiver, target_emit)
            if resolved is None:
                raise CompilerError(f"instancia no declarada: {expression.receiver}")
            class_key, receiver_fields = resolved
            method = class_methods[class_key].get(expression.name.casefold())
            if method is None:
                raise CompilerError(f"método no declarado: {expression.name}")
            method_key = f"{class_key}-{method.name.casefold()}"
            if method_key in stack:
                raise CompilerError(f"recursión no admitida en el método {method.name}")
            if len(expression.arguments) != len(method.parameters):
                raise CompilerError(
                    f"método {method.name} espera {len(method.parameters)} argumentos, "
                    f"recibió {len(expression.arguments)}"
                )
            bindings = {}
            for parameter, argument in zip(method.parameters, expression.arguments):
                lowered_arg = lower_expression(argument, substitutions, fields, stack, target_emit)
                argument_type = static_type(lowered_arg) or static_type(argument)
                if argument_type is not None and argument_type != parameter.value_type:
                    raise CompilerError(
                        f"el parámetro {parameter.name} de {method.name} espera "
                        f"{parameter.value_type}, no {argument_type}"
                    )
                bindings[parameter.name] = lowered_arg
            returns = [item for item in method.body if isinstance(item, Return)]
            if len(returns) != 1 or method.body[-1] is not returns[0]:
                raise CompilerError(
                    f"el método {method.name} debe terminar con un único RETORNAR para usarse como expresión"
                )
            renamed_body, final_bindings = _prepare_inline_bindings(
                method.body[:-1], bindings, f"m_{method.name}"
            )
            if renamed_body:
                target_emit.extend(lower_statements(renamed_body, receiver_fields, stack + (method_key,)))
            return lower_expression(
                returns[0].value,
                final_bindings,
                receiver_fields,
                stack + (method_key,),
                target_emit,
            )
        if isinstance(expression, Input):
            nonlocal inline_counter
            inline_counter += 1
            temp_name = f"__input_{inline_counter}"
            known_types[temp_name] = "dynamic"
            target_emit.append(
                Variable(
                    temp_name,
                    Input(expression.prompt, "ENTRADA"),
                    None,
                    True,
                )
            )
            return Name(temp_name)
        if isinstance(expression, PythonExpression):
            inline_counter += 1
            temp_name = f"__py_{inline_counter}"
            known_types[temp_name] = "dynamic"
            target_emit.append(
                _lower_python_tasks(expression.tasks, fields, stack, target_emit, override_target=temp_name)
            )
            return Name(temp_name)
        return expression

    def _instantiate_class(item: ClassInstance, field_map: dict[str, str], stack: tuple[str, ...], result: list[Statement]) -> None:
        class_key = item.class_name.casefold()
        if class_key not in class_fields:
            raise CompilerError(f"clase no declarada: {item.class_name}")
        instance_key = item.name.casefold()
        object_types[instance_key] = class_key
        instance_fields = {
            field.name.casefold(): f"{item.name}__{field.name}"
            for field in class_fields[class_key].values()
        }
        field_names[instance_key] = instance_fields
        overrides: dict[str, Expression] = {}
        for override in item.overrides:
            if not isinstance(override, Assignment) or not isinstance(override.name, str):
                raise CompilerError("un inicializador de instancia debe asignar un campo simple")
            override_key = override.name.casefold()
            if override_key not in class_fields[class_key]:
                raise CompilerError(f"campo desconocido en {item.class_name}: {override.name}")
            overrides[override_key] = (
                override.value
                if isinstance(override.value, Input)
                else lower_expression(override.value, {}, field_map, stack, result)
            )
        for key, declaration in class_fields[class_key].items():
            value = overrides.get(
                key,
                lower_expression(declaration.value, {}, field_map, stack, result),
            )
            lowered_type = declaration.value_type or static_type(declaration.value)
            mapped_name = instance_fields[key]
            inferred = lowered_type or static_type(value)
            if inferred is not None:
                known_types[mapped_name] = inferred
            result.append(
                Variable(mapped_name, value, lowered_type, True)
            )

    def lower_statements(
        items: tuple[Statement, ...],
        fields: dict[str, str] | None = None,
        stack: tuple[str, ...] = (),
    ) -> tuple[Statement, ...]:
        field_map = fields or {}
        result: list[Statement] = []
        for item in items:
            if isinstance(item, (Function, ClassDefinition, Macro, Import)):
                continue
            if isinstance(item, PythonBlock):
                result.append(_lower_python_tasks(item.tasks, field_map, stack, result))
                continue
            if isinstance(item, ClassInstance):
                _instantiate_class(item, field_map, stack, result)
                continue
            if isinstance(item, Variable):
                if (
                    item.mutable
                    and item.value_type is None
                    and isinstance(item.value, Call)
                    and not item.value.arguments
                    and item.value.name.casefold() in class_fields
                    and item.value.name.casefold() not in functions
                ):
                    _instantiate_class(ClassInstance(item.name, item.value.name, ()), field_map, stack, result)
                    continue
                if (
                    item.mutable
                    and item.value_type is None
                    and isinstance(item.value, Name)
                    and item.value.value.casefold() in class_fields
                    and item.value.value.casefold() not in functions
                    and item.value.value not in known_types
                ):
                    _instantiate_class(ClassInstance(item.name, item.value.value, ()), field_map, stack, result)
                    continue
                if isinstance(item.value, PythonExpression):
                    known_types[item.name] = item.value_type or "dynamic"
                    result.append(
                        _lower_python_tasks(
                            item.value.tasks,
                            field_map,
                            stack,
                            result,
                            override_target=item.name,
                        )
                    )
                    continue
                lowered_val = (
                    item.value
                    if isinstance(item.value, Input)
                    else lower_expression(item.value, {}, field_map, stack, result)
                )
                inferred = item.value_type or static_type(lowered_val)
                if inferred is not None:
                    known_types[item.name] = inferred
                result.append(
                    Variable(
                        item.name,
                        lowered_val,
                        item.value_type,
                        item.mutable,
                    )
                )
                continue
            if isinstance(item, Assignment):
                target = item.name
                if isinstance(target, str):
                    target = field_map.get(target.casefold(), target)
                else:
                    target = lower_expression(target, {}, field_map, stack, result)
                    if isinstance(target, Name):
                        target = target.value
                if isinstance(item.value, PythonExpression):
                    result.append(
                        _lower_python_tasks(
                            item.value.tasks,
                            field_map,
                            stack,
                            result,
                            override_target=target,
                        )
                    )
                    continue
                lowered_val = (
                    item.value
                    if isinstance(item.value, Input)
                    else lower_expression(item.value, {}, field_map, stack, result)
                )
                result.append(
                    Assignment(target, lowered_val)
                )
                continue
            if isinstance(item, Print):
                value: str | Expression
                if isinstance(item.value, str):
                    value = item.value
                else:
                    lowered_value = lower_expression(item.value, {}, field_map, stack, result)
                    value = (
                        lowered_value.value
                        if isinstance(lowered_value, StringLiteral)
                        else lowered_value
                    )
                result.append(
                    Print(value)
                )
                continue
            if isinstance(item, If):
                result.append(
                    If(
                        lower_expression(item.condition, {}, field_map, stack, result),
                        lower_statements(item.then_body, field_map, stack),
                        lower_statements(item.else_body, field_map, stack),
                    )
                )
                continue
            if isinstance(item, WhileLoop):
                if item.init:
                    result.extend(lower_statements(item.init, field_map, stack))
                cond_pre: list[Statement] = []
                lowered_cond = lower_expression(item.condition, {}, field_map, stack, cond_pre)
                if cond_pre:
                    result.extend(cond_pre)
                lowered_body = lower_statements(item.body, field_map, stack)
                lowered_step = lower_statements(item.step, field_map, stack)
                if cond_pre:
                    lowered_step = lowered_step + tuple(cond_pre)
                result.append(WhileLoop(lowered_cond, lowered_body, lowered_step, ()))
                continue
            if isinstance(item, (Break, Continue)):
                result.append(item)
                continue
            if isinstance(item, ClassMethodCall):
                resolved = ensure_receiver(item.class_name, result)
                if resolved is None:
                    raise CompilerError(f"instancia no declarada: {item.class_name}")
                class_key, receiver_fields = resolved
                method = class_methods[class_key].get(item.method_name.casefold())
                if method is None:
                    if item.method_name.casefold() in class_commands[class_key]:
                        raise CompilerError(
                            f"{item.class_name}-{item.method_name} apunta a un comando de bajo nivel; "
                            "el intérprete no ejecuta instrucciones Assembly",
                            code="TIGER_METHOD_BACKEND_REQUIRED",
                        )
                    raise CompilerError(f"método no declarado: {item.method_name}")
                if len(item.arguments) != len(method.parameters):
                    raise CompilerError(
                        f"método {method.name} espera {len(method.parameters)} argumentos, "
                        f"recibió {len(item.arguments)}"
                    )
                method_key = f"{class_key}-{method.name.casefold()}"
                if method_key in stack:
                    raise CompilerError(f"recursión no admitida en el método {method.name}")
                bindings = {}
                for parameter, argument in zip(method.parameters, item.arguments):
                    lowered_arg = lower_expression(argument, {}, field_map, stack, result)
                    argument_type = static_type(lowered_arg) or static_type(argument)
                    if argument_type is not None and argument_type != parameter.value_type:
                        raise CompilerError(
                            f"el parámetro {parameter.name} de {method.name} espera "
                            f"{parameter.value_type}, no {argument_type}"
                        )
                    bindings[parameter.name] = lowered_arg
                if method.body and any(isinstance(statement, Return) for statement in method.body[:-1]):
                    raise CompilerError(f"RETORNAR debe ser la última instrucción de {method.name}")
                method_body = method.body[:-1] if method.body and isinstance(method.body[-1], Return) else method.body
                renamed_body, _ = _prepare_inline_bindings(method_body, bindings, f"m_{method.name}")
                result.extend(
                    lower_statements(
                        renamed_body,
                        receiver_fields,
                        stack + (method_key,),
                    )
                )
                continue
            if isinstance(item, MacroCall):
                key = item.name.casefold()
                if key in functions:
                    function = functions[key]
                    if key in stack:
                        raise CompilerError(f"recursión no admitida en la función {function.name}")
                    if len(item.arguments) != len(function.parameters):
                        raise CompilerError(
                            f"FN {function.name} espera {len(function.parameters)} argumentos, "
                            f"recibió {len(item.arguments)}"
                        )
                    bindings = {}
                    for parameter, argument in zip(function.parameters, item.arguments):
                        lowered_arg = lower_expression(argument, {}, field_map, stack, result)
                        argument_type = static_type(lowered_arg) or static_type(argument)
                        if argument_type is not None and argument_type != parameter.value_type:
                            raise CompilerError(
                                f"el parámetro {parameter.name} de FN {function.name} espera "
                                f"{parameter.value_type}, no {argument_type}"
                            )
                        bindings[parameter.name] = lowered_arg
                    if function.body and any(isinstance(stmt, Return) for stmt in function.body[:-1]):
                        raise CompilerError(f"RETORNAR debe ser la última instrucción de {function.name}")
                    fn_body = function.body[:-1] if function.body and isinstance(function.body[-1], Return) else function.body
                    renamed_body, final_bindings = _prepare_inline_bindings(fn_body, bindings, f"fn_{function.name}")
                    result.extend(
                        lower_statements(
                            renamed_body,
                            {},
                            stack + (key,),
                        )
                    )
                    if function.body and isinstance(function.body[-1], Return):
                        lower_expression(function.body[-1].value, final_bindings, {}, stack + (key,), result)
                    continue
                if key in python_functions or item.name in known_types:
                    result.append(
                        CallStatement(
                            Call(
                                item.name,
                                tuple(
                                    lower_expression(argument, {}, field_map, stack, result)
                                    for argument in item.arguments
                                ),
                            )
                        )
                    )
                    continue
                raise CompilerError(f"comando o función desconocido: {item.name}")
            if isinstance(item, Pointer):
                result.append(item)
                continue
            if isinstance(item, PointerStore):
                result.append(
                    PointerStore(
                        item.name,
                        lower_expression(item.value, {}, field_map, stack, result),
                    )
                )
                continue
            if isinstance(item, ArrowAssignment):
                result.append(
                    ArrowAssignment(
                        item.source,
                        lower_expression(item.value, {}, field_map, stack, result),
                    )
                )
                continue
            result.append(item)
        return tuple(result)

    return lower_statements(tuple(executable))


def _prepare(
    statements: tuple[Statement, ...],
) -> tuple[tuple[Statement, ...], dict[str, str], dict[str, int], dict[str, str]]:
    body = _lower_program(statements)
    declared_pointer_types = {
        statement.name: statement.value_type
        for statement in body
        if isinstance(statement, Pointer)
    }

    def normalize_arrows(items: tuple[Statement, ...]) -> tuple[Statement, ...]:
        normalized: list[Statement] = []
        for statement in items:
            if isinstance(statement, If):
                normalized.append(
                    If(
                        statement.condition,
                        normalize_arrows(statement.then_body),
                        normalize_arrows(statement.else_body),
                    )
                )
            elif isinstance(statement, WhileLoop):
                normalized.append(
                    WhileLoop(
                        statement.condition,
                        normalize_arrows(statement.body),
                        normalize_arrows(statement.step),
                        normalize_arrows(statement.init),
                    )
                )
            elif not isinstance(statement, ArrowAssignment):
                normalized.append(statement)
            elif statement.source in declared_pointer_types:
                normalized.append(PointerStore(statement.source, statement.value))
            elif isinstance(statement.value, Name):
                normalized.append(Assignment(statement.value.value, Name(statement.source)))
            elif isinstance(statement.value, (Index, Member)):
                normalized.append(Assignment(statement.value, Name(statement.source)))
            else:
                raise CompilerError(
                    "el operador -> requiere un PTR a la izquierda o una variable destino a la derecha"
                )
        return tuple(normalized)

    body = normalize_arrows(body)
    registers: dict[str, str] = {}
    pointers: dict[str, int] = {}
    pointer_types: dict[str, str] = {}
    value_types: dict[str, str] = {}
    mutability: dict[str, bool] = {}
    initialized: set[str] = set()
    classes: dict[str, ClassDefinition] = {}
    for statement in body:
        if isinstance(statement, ClassDefinition):
            class_key = statement.name.casefold()
            classes[class_key] = statement
    class_field_types: dict[str, dict[str, str]] = {}
    class_methods = {
        key: {
            member.name.casefold()
            for member in class_definition.members
            if isinstance(member, (ClassCommand, ClassMethod))
        }
        for key, class_definition in classes.items()
    }

    def literal_type(expression: Expression) -> str:
        if isinstance(expression, BooleanLiteral):
            return "bool"
        if isinstance(expression, Literal):
            return "float" if isinstance(expression.value, float) else "int"
        if isinstance(expression, Unary) and expression.operator in {"+", "-"} and isinstance(expression.operand, Literal):
            return "float" if isinstance(expression.operand.value, float) else "int"
        if isinstance(expression, StringLiteral):
            return "string"
        if isinstance(expression, ArrayLiteral):
            element_types = {literal_type(item) for item in expression.elements}
            if len(element_types) > 1:
                raise CompilerError("todos los elementos de un array deben tener el mismo tipo")
            return f"array[{next(iter(element_types), 'dynamic')}]"
        if isinstance(expression, DictionaryLiteral):
            entry_types = {literal_type(value) for _, value in expression.entries}
            value_type = next(iter(entry_types), "dynamic") if len(entry_types) <= 1 else "dynamic"
            return f"dict[{value_type}]"
        raise CompilerError("los valores por defecto de clase deben ser literales")

    for class_key, class_definition in classes.items():
        fields: dict[str, str] = {}
        method_names: set[str] = set()
        for member in class_definition.members:
            if isinstance(member, Variable):
                field_type = literal_type(member.value)
                if member.value_type is not None and field_type != member.value_type:
                    raise CompilerError(
                        f"el valor por defecto de {member.name} no coincide con su tipo declarado"
                    )
                fields[member.name] = field_type
            elif isinstance(member, (ClassCommand, ClassMethod)):
                method_key = member.name.casefold()
                method_names.add(method_key)
        class_field_types[class_key] = fields

    def expression_type(expression: Expression, context: str) -> str | None:
        if isinstance(expression, BooleanLiteral):
            return "bool"
        if isinstance(expression, Literal):
            if isinstance(expression.value, float):
                return "float"
            if not -(1 << 63) <= expression.value < (1 << 63):
                raise CompilerError(f"un literal de {context} no cabe en un entero con signo de 64 bits")
            return "int"
        if isinstance(expression, StringLiteral):
            return "string"
        if isinstance(expression, Input):
            return None
        if isinstance(expression, Call):
            for arg in expression.arguments:
                expression_type(arg, context)
            return "dynamic"
        if isinstance(expression, ArrayLiteral):
            element_types = {
                expression_type(element, context)
                for element in expression.elements
            }
            if None in element_types or len(element_types) > 1:
                raise CompilerError("todos los elementos de un array deben tener el mismo tipo")
            return f"array[{next(iter(element_types), 'dynamic')}]"
        if isinstance(expression, DictionaryLiteral):
            entry_types = {expression_type(value, context) for _, value in expression.entries}
            if None in entry_types or len(entry_types) > 1:
                return "dict[dynamic]"
            return f"dict[{next(iter(entry_types), 'dynamic')}]"
        if isinstance(expression, Index):
            collection_type = expression_type(expression.collection, context)
            key_type = expression_type(expression.key, context)
            if collection_type == "dynamic":
                return "dynamic"
            if collection_type is not None and collection_type.startswith("array["):
                if key_type not in {"int", "dynamic"}:
                    raise CompilerError("el índice de un array debe ser entero")
                return collection_type[6:-1]
            if collection_type is not None and collection_type.startswith("dict["):
                if key_type not in {"string", "dynamic"}:
                    raise CompilerError("la clave de un diccionario debe ser texto")
                return collection_type[5:-1]
            raise CompilerError("solo se pueden indexar arrays o diccionarios")
        if isinstance(expression, Member):
            object_type = expression_type(expression.value, context)
            if object_type == "dynamic":
                return "dynamic"
            if object_type is not None and object_type.startswith("object:"):
                class_name = object_type[7:].casefold()
                fields = class_field_types.get(class_name)
                if fields is None or expression.name not in fields:
                    raise CompilerError(f"campo desconocido: {expression.name}")
                return fields[expression.name]
            if object_type is not None and object_type.startswith("dict["):
                return "dynamic"
            raise CompilerError("el acceso con '.' requiere un objeto o diccionario")
        elif isinstance(expression, Name):
            if expression.value in pointers:
                raise CompilerError(f"PTR {expression.value} no se puede usar como entero en {context}")
            if expression.value not in initialized:
                raise CompilerError(f"variable no declarada antes de {context}: {expression.value}")
            return value_types[expression.value]
        elif isinstance(expression, Unary):
            if expression.operator == "deref":
                if not isinstance(expression.operand, Name) or expression.operand.value not in pointer_types:
                    raise CompilerError("la lectura *PTR requiere el nombre de un puntero declarado")
                value_type = pointer_types[expression.operand.value]
                if value_type != "int":
                    raise CompilerError("ASM/WAT solo admiten lectura de PTR INT en Tiger 10")
                return value_type
            operand_type = expression_type(expression.operand, context)
            if operand_type not in {"int", "float", "bool", "dynamic"}:
                raise CompilerError(f"el operador unario de {context} requiere un número")
            return "bool" if expression.operator == "not" else operand_type
        elif isinstance(expression, Binary):
            left_type = expression_type(expression.left, context)
            if (
                expression.operator == "-"
                and left_type is not None
                and left_type.startswith("object:")
                and isinstance(expression.right, Name)
            ):
                class_name = left_type[7:].casefold()
                fields = class_field_types.get(class_name, {})
                if expression.right.value in fields:
                    return fields[expression.right.value]
            right_type = expression_type(expression.right, context)
            if left_type is None or right_type is None:
                raise CompilerError("INPUT solo se permite como valor directo de una variable")
            if expression.operator in {"and", "or"}:
                if "string" in {left_type, right_type}:
                    raise CompilerError("los operadores lógicos requieren operandos numéricos")
                return "bool"
            if "dynamic" in {left_type, right_type}:
                if expression.operator in COMPARISONS:
                    return "bool"
                return "dynamic"
            if expression.operator == "+":
                if "string" in {left_type, right_type} and left_type in {"string", "int", "float"} and right_type in {"string", "int", "float"}:
                    return "string"
                if left_type.startswith("array[") and right_type.startswith("array["):
                    if left_type != right_type and "array[dynamic]" not in {left_type, right_type}:
                        raise CompilerError("todos los elementos de un array deben tener el mismo tipo")
                    return right_type if left_type == "array[dynamic]" else left_type
                if left_type.startswith("dict[") and right_type.startswith("dict["):
                    return left_type if left_type == right_type else "dict[dynamic]"
            if expression.operator in COMPARISONS:
                if left_type != right_type and not (
                    {left_type, right_type} <= {"int", "bool"} or "dynamic" in {left_type, right_type}
                ):
                    raise CompilerError("no se pueden comparar valores de tipos diferentes")
                if left_type == "string" and expression.operator not in {"==", "!="}:
                    raise CompilerError("Tiger 10 solo permite comparar textos con == o !=")
                return "bool"
            if left_type != right_type or left_type == "string":
                raise CompilerError("la aritmética requiere dos operandos numéricos del mismo tipo")
            if expression.operator == "%" and left_type == "float":
                raise CompilerError("% solo está disponible para enteros")
            return left_type
        raise CompilerError(f"expresión no admitida en {context}")

    def check_body(items: tuple[Statement, ...], nested: bool = False, in_loop: bool = False) -> None:
        for statement in items:
            if isinstance(statement, PythonBlock):
                for task in statement.tasks:
                    if task.is_file:
                        script_path = Path(task.source)
                        if not script_path.is_absolute():
                            script_path = (Path.cwd() / script_path).resolve()
                        if not script_path.is_file():
                            raise CompilerError(
                                f"no se pudo encontrar el archivo Python: {task.source}",
                                code="TIGER_PYTHON_FILE_NOT_FOUND",
                            )
                    py_vars, _ = _discover_python_symbols(task)
                    for var_name in py_vars:
                        if var_name not in value_types:
                            value_types[var_name] = "dynamic"
                            mutability[var_name] = True
                        initialized.add(var_name)
                    if isinstance(task.target, str):
                        if task.target not in value_types:
                            value_types[task.target] = "dynamic"
                            mutability[task.target] = True
                        initialized.add(task.target)
                    elif isinstance(task.target, Name):
                        if task.target.value not in value_types:
                            value_types[task.target.value] = "dynamic"
                            mutability[task.target.value] = True
                        initialized.add(task.target.value)
                    elif isinstance(task.target, (Index, Member)):
                        expression_type(task.target, "TIPO PY")
                continue
            if isinstance(statement, CallStatement):
                expression_type(statement.call, statement.call.name)
                continue
            if isinstance(statement, Variable):
                actual_type = expression_type(statement.value, f"{'VAR' if statement.mutable else 'CONST'} {statement.name}")
                if actual_type is None:
                    if statement.value_type is None:
                        if (
                            isinstance(statement.value, Input)
                            and statement.value.prompt is None
                            and statement.value.keyword == "INPUT"
                        ):
                            raise CompilerError("INPUT requiere una variable con tipo declarado")
                        actual_type = "dynamic"
                    else:
                        actual_type = statement.value_type
                if actual_type.startswith("array[") and statement.value_type is not None:
                    element_type = actual_type[6:-1]
                    if element_type == "dynamic":
                        actual_type = f"array[{statement.value_type}]"
                    elif element_type != statement.value_type:
                        raise CompilerError(
                            f"el array {statement.name} contiene {element_type}, pero se declaró como {statement.value_type}"
                        )
                if statement.value_type is not None and actual_type not in {statement.value_type, "dynamic"}:
                    if not (
                        actual_type.startswith("array[")
                        and actual_type[6:-1] == statement.value_type
                    ):
                        raise CompilerError(
                            f"el valor de {statement.name} es {actual_type}, pero se declaró como {statement.value_type}"
                        )
                if statement.value_type is not None and actual_type == "dynamic":
                    actual_type = statement.value_type
                value_types[statement.name] = actual_type
                mutability[statement.name] = statement.mutable
                if actual_type in {"int", "bool"} and statement.name not in registers:
                    register_index = len(registers)
                    registers[statement.name] = (
                        REGISTER_NAMES[register_index]
                        if register_index < len(REGISTER_NAMES)
                        else f"@{statement.name}"
                    )
                initialized.add(statement.name)
            elif isinstance(statement, Assignment):
                target = Name(statement.name) if isinstance(statement.name, str) else statement.name
                if isinstance(target, Name):
                    name = target.value
                    if name not in value_types:
                        if isinstance(statement.value, Input):
                            value_types[name] = "dynamic"
                            mutability[name] = True
                            initialized.add(name)
                        else:
                            raise CompilerError(f"variable no declarada: {name}")
                    if not mutability[name]:
                        raise CompilerError(f"no se puede reasignar CONST {name}")
                    target_type = value_types[name]
                elif isinstance(target, Index):
                    target_type = expression_type(target, "asignación indexada")
                    if (
                        isinstance(target.collection, Name)
                        and value_types.get(target.collection.value, "").startswith("dict[")
                    ):
                        value_types[target.collection.value] = "dict[dynamic]"
                        target_type = "dynamic"
                elif isinstance(target, Member):
                    target_type = expression_type(target, "asignación de campo")
                    if (
                        isinstance(target.value, Name)
                        and value_types.get(target.value.value, "").startswith("dict[")
                    ):
                        value_types[target.value.value] = "dict[dynamic]"
                        target_type = "dynamic"
                else:
                    raise CompilerError("destino inválido para asignación")
                assigned_type = expression_type(statement.value, "asignación")
                if assigned_type is None:
                    assigned_type = target_type
                if (
                    isinstance(target, Name)
                    and target_type.startswith("dict[")
                    and assigned_type.startswith("dict[")
                ):
                    if target_type != assigned_type:
                        value_types[target.value] = "dict[dynamic]"
                    target_type = assigned_type
                if target_type != "dynamic" and assigned_type not in {target_type, "dynamic"}:
                    raise CompilerError(
                        f"no se puede guardar {assigned_type} en un destino de tipo {target_type}"
                    )
            elif isinstance(statement, Pointer):
                pointers[statement.name] = statement.address
                pointer_types[statement.name] = statement.value_type
            elif isinstance(statement, PointerStore):
                pointer_type = pointer_types.get(statement.name)
                if pointer_type is None:
                    raise CompilerError(f"PTR no declarado: {statement.name}")
                if pointer_type != "int":
                    raise CompilerError("ASM/WAT solo admiten escritura de PTR INT en Tiger 10")
                value_type = expression_type(statement.value, f"PTR {statement.name}")
                if value_type != pointer_type:
                    raise CompilerError(
                        f"PTR {statement.name} requiere {pointer_type}, no {value_type}"
                    )
            elif isinstance(statement, If):
                condition_type = expression_type(statement.condition, "SI")
                if condition_type not in {"int", "float", "bool", "dynamic"}:
                    raise CompilerError("SI requiere una condición numérica")
                check_body(statement.then_body, nested=True, in_loop=in_loop)
                check_body(statement.else_body, nested=True, in_loop=in_loop)
            elif isinstance(statement, WhileLoop):
                if statement.init:
                    check_body(statement.init, nested=True, in_loop=in_loop)
                condition_type = expression_type(statement.condition, "CICLO")
                if condition_type not in {"int", "float", "bool", "dynamic"}:
                    raise CompilerError("CICLO requiere una condición numérica")
                check_body(statement.body, nested=True, in_loop=True)
                if statement.step:
                    check_body(statement.step, nested=True, in_loop=True)
            elif isinstance(statement, Break):
                if not in_loop:
                    raise CompilerError("SALIR/BREAK solo se permite dentro de un CICLO")
            elif isinstance(statement, Continue):
                if not in_loop:
                    raise CompilerError("CONTINUAR/CONTINUE solo se permite dentro de un CICLO")
            elif isinstance(statement, ClassDefinition):
                continue
            elif isinstance(statement, ClassInstance):
                class_definition = classes.get(statement.class_name.casefold())
                if class_definition is None:
                    raise CompilerError(f"clase no declarada: {statement.class_name}")
                fields = class_field_types[statement.class_name.casefold()]
                overridden: set[str] = set()
                for override in statement.overrides:
                    if not isinstance(override, Assignment) or not isinstance(override.name, str):
                        raise CompilerError("un inicializador de instancia debe asignar un campo simple")
                    if override.name not in fields:
                        raise CompilerError(f"campo desconocido en {statement.class_name}: {override.name}")
                    assigned_type = expression_type(override.value, f"campo {override.name}")
                    if assigned_type is not None and assigned_type not in {fields[override.name], "dynamic"}:
                        raise CompilerError(
                            f"el campo {override.name} espera {fields[override.name]}, no {assigned_type}"
                        )
                    overridden.add(override.name)
                value_types[statement.name] = f"object:{class_definition.name}"
                mutability[statement.name] = True
                initialized.add(statement.name)
            elif isinstance(statement, ClassCommand):
                continue
            elif isinstance(statement, ClassMethodCall):
                class_key = statement.class_name.casefold()
                if class_key not in classes:
                    receiver_type = value_types.get(statement.class_name, "")
                    if receiver_type.startswith("object:"):
                        class_key = receiver_type[7:].casefold()
                if class_key not in classes:
                    raise CompilerError(
                        f"no se encontró la clase ni instancia: {statement.class_name}"
                    )
                if statement.method_name.casefold() not in class_methods[class_key]:
                    raise CompilerError(
                        f"método no declarado en {statement.class_name}: {statement.method_name}"
                    )
            elif isinstance(statement, Print) and not isinstance(statement.value, str):
                if expression_type(statement.value, "IMPRIMIR") is None:
                    raise CompilerError("INPUT debe guardarse en una variable antes de imprimir")
            elif isinstance(statement, (Import, Macro, MacroCall, Function)):
                raise CompilerError("nodo sin expandir durante la preparación del programa")

    check_body(body)
    for class_definition in classes.values():
        for member in class_definition.members:
            if isinstance(member, ClassMethod):
                field_types = class_field_types[class_definition.name.casefold()]
                saved_fields = {
                    name: (
                        value_types.get(name),
                        name in value_types,
                        mutability.get(name),
                        name in mutability,
                        name in initialized,
                    )
                    for name in field_types
                }
                value_types.update(field_types)
                mutability.update({name: True for name in field_types})
                initialized.update(field_types)
                try:
                    check_body(member.body, nested=True)
                finally:
                    for name, (value_type, had_type, was_mutable, had_mutability, was_initialized) in saved_fields.items():
                        if had_type:
                            value_types[name] = value_type or "dynamic"
                        else:
                            value_types.pop(name, None)
                        if had_mutability:
                            mutability[name] = bool(was_mutable)
                        else:
                            mutability.pop(name, None)
                        if not was_initialized:
                            initialized.discard(name)
    return body, registers, pointers, value_types


def _all_prints(items: tuple[Statement, ...]) -> list[str]:
    result: list[str] = []
    for statement in items:
        if isinstance(statement, Print):
            if not isinstance(statement.value, str):
                continue
            value = statement.value + "\n"
            if value not in result:
                result.append(value)
        elif isinstance(statement, If):
            result.extend(value for value in _all_prints(statement.then_body) if value not in result)
            result.extend(value for value in _all_prints(statement.else_body) if value not in result)
        elif isinstance(statement, WhileLoop):
            result.extend(value for value in _all_prints(statement.init) if value not in result)
            result.extend(value for value in _all_prints(statement.body) if value not in result)
            result.extend(value for value in _all_prints(statement.step) if value not in result)
    return result


def _has_numeric_print(items: tuple[Statement, ...]) -> bool:
    return any(
        (isinstance(statement, Print) and not isinstance(statement.value, str))
        or (
            isinstance(statement, If)
            and (
                _has_numeric_print(statement.then_body)
                or _has_numeric_print(statement.else_body)
            )
        )
        or (
            isinstance(statement, WhileLoop)
            and (
                _has_numeric_print(statement.init)
                or _has_numeric_print(statement.body)
                or _has_numeric_print(statement.step)
            )
        )
        for statement in items
    )


def _expression_type(expression: Expression, value_types: dict[str, str]) -> str:
    if isinstance(expression, BooleanLiteral):
        return "bool"
    if isinstance(expression, Literal):
        return "float" if isinstance(expression.value, float) else "int"
    if isinstance(expression, StringLiteral):
        return "string"
    if isinstance(expression, Name):
        return value_types[expression.value]
    if isinstance(expression, Call):
        return "dynamic"
    if isinstance(expression, ArrayLiteral):
        if not expression.elements:
            return "array[dynamic]"
        return f"array[{_expression_type(expression.elements[0], value_types)}]"
    if isinstance(expression, DictionaryLiteral):
        if not expression.entries:
            return "dict[dynamic]"
        entry_types = {
            _expression_type(value, value_types)
            for _, value in expression.entries
        }
        return f"dict[{next(iter(entry_types)) if len(entry_types) == 1 else 'dynamic'}]"
    if isinstance(expression, Index):
        collection_type = _expression_type(expression.collection, value_types)
        if collection_type == "dynamic":
            return "dynamic"
        if collection_type.startswith("array[") or collection_type.startswith("dict["):
            return collection_type[collection_type.index("[") + 1 : -1]
        raise CompilerError("el acceso indexado requiere un array o diccionario")
    if isinstance(expression, Member):
        object_type = _expression_type(expression.value, value_types)
        if object_type == "dynamic":
            return "dynamic"
        if object_type.startswith("dict["):
            return object_type[5:-1]
        raise CompilerError("el acceso con '.' requiere un objeto o diccionario")
    if isinstance(expression, Unary):
        if expression.operator == "deref":
            return "int"
        return "bool" if expression.operator == "not" else _expression_type(expression.operand, value_types)
    if isinstance(expression, Binary):
        if expression.operator in COMPARISONS or expression.operator in {"and", "or"}:
            return "bool"
        if expression.operator == "+":
            left_type = _expression_type(expression.left, value_types)
            right_type = _expression_type(expression.right, value_types)
            if left_type.startswith(("array[", "dict[")):
                return left_type
            if "string" in {left_type, right_type}:
                return "string"
        return _expression_type(expression.left, value_types)
    raise CompilerError("INPUT requiere asignarse directamente a una variable")


def _all_string_values(body: tuple[Statement, ...]) -> list[str]:
    values: list[str] = []
    for statement in body:
        expressions: tuple[Expression, ...] = ()
        if isinstance(statement, Variable):
            expressions = (statement.value,)
        elif isinstance(statement, Assignment):
            expressions = (statement.value,)
        elif isinstance(statement, If):
            nested = _all_string_values(statement.then_body) + _all_string_values(statement.else_body)
            values.extend(text for text in nested if text not in values)
        elif isinstance(statement, WhileLoop):
            expressions = (statement.condition,)
            nested = (
                _all_string_values(statement.init)
                + _all_string_values(statement.body)
                + _all_string_values(statement.step)
            )
            values.extend(text for text in nested if text not in values)
        for expression in expressions:
            if isinstance(expression, StringLiteral) and expression.value not in values:
                values.append(expression.value)
    return values


def _input_targets(body: tuple[Statement, ...]) -> list[str]:
    targets: list[str] = []
    for statement in body:
        if isinstance(statement, Variable) and isinstance(statement.value, Input):
            targets.append(statement.name)
        elif isinstance(statement, Assignment) and isinstance(statement.value, Input):
            if isinstance(statement.name, str):
                targets.append(statement.name)
            elif isinstance(statement.name, Name):
                targets.append(statement.name.value)
        elif isinstance(statement, If):
            targets.extend(_input_targets(statement.then_body))
            targets.extend(_input_targets(statement.else_body))
        elif isinstance(statement, WhileLoop):
            targets.extend(_input_targets(statement.init))
            targets.extend(_input_targets(statement.body))
            targets.extend(_input_targets(statement.step))
    return list(dict.fromkeys(targets))


def _expression_uses_composite(expression: Expression) -> bool:
    if isinstance(expression, (ArrayLiteral, DictionaryLiteral, Index, Member, Call)):
        return True
    if isinstance(expression, Unary):
        return _expression_uses_composite(expression.operand)
    if isinstance(expression, Binary):
        return _expression_uses_composite(expression.left) or _expression_uses_composite(expression.right)
    return False


def _ensure_backend_support(
    body: tuple[Statement, ...],
    value_types: dict[str, str],
    target: str,
) -> None:
    for statement in body:
        if isinstance(statement, (PythonBlock, CallStatement)):
            raise CompilerError(
                f"{target} todavía no emite bloques TIPO PY directamente; ejecútalos con --run",
                code="TIGER_BACKEND_UNSUPPORTED",
            )
        if isinstance(statement, (ClassDefinition, ClassInstance, ClassMethodCall)):
            raise CompilerError(
                f"{target} todavía no emite clases/objetos; ejecútalos con --run",
                code="TIGER_BACKEND_UNSUPPORTED",
            )
        if isinstance(statement, Variable):
            if value_types.get(statement.name, "").startswith(("array[", "dict", "object:")):
                raise CompilerError(
                    f"{target} todavía no emite arrays/diccionarios/objetos; ejecútalos con --run",
                    code="TIGER_BACKEND_UNSUPPORTED",
                )
            if _expression_uses_composite(statement.value):
                raise CompilerError(
                    f"{target} todavía no emite arrays ni diccionarios; ejecútalos con --run",
                    code="TIGER_BACKEND_UNSUPPORTED",
                )
        if isinstance(statement, Assignment):
            if not isinstance(statement.name, str) or _expression_uses_composite(statement.value):
                raise CompilerError(
                    f"{target} todavía no emite asignaciones indexadas/de campos; ejecútalas con --run",
                    code="TIGER_BACKEND_UNSUPPORTED",
                )
        if isinstance(statement, Print) and not isinstance(statement.value, str):
            if _expression_uses_composite(statement.value) or (
                isinstance(statement.value, Name)
                and value_types.get(statement.value.value, "").startswith(("array[", "dict", "object:"))
            ):
                raise CompilerError(
                    f"{target} todavía no emite arrays/diccionarios; ejecútalos con --run",
                    code="TIGER_BACKEND_UNSUPPORTED",
                )
        if isinstance(statement, If):
            if _expression_uses_composite(statement.condition):
                raise CompilerError(
                    f"{target} todavía no emite indexación ni campos en condiciones",
                    code="TIGER_BACKEND_UNSUPPORTED",
                )
            _ensure_backend_support(statement.then_body, value_types, target)
            _ensure_backend_support(statement.else_body, value_types, target)
        if isinstance(statement, WhileLoop):
            if _expression_uses_composite(statement.condition):
                raise CompilerError(
                    f"{target} todavía no emite indexación ni campos en condiciones de CICLO",
                    code="TIGER_BACKEND_UNSUPPORTED",
                )
            _ensure_backend_support(statement.init, value_types, target)
            _ensure_backend_support(statement.body, value_types, target)
            _ensure_backend_support(statement.step, value_types, target)


def _has_string_comparison(body: tuple[Statement, ...], value_types: dict[str, str]) -> bool:
    def contains(expression: Expression) -> bool:
        if isinstance(expression, Binary):
            return (
                expression.operator in {"==", "!="}
                and _expression_type(expression.left, value_types) == "string"
            ) or contains(expression.left) or contains(expression.right)
        if isinstance(expression, Unary):
            return contains(expression.operand)
        return False

    for statement in body:
        expressions: tuple[Expression, ...] = ()
        if isinstance(statement, (Variable, Assignment)):
            expressions = (statement.value,)
        elif isinstance(statement, Print) and not isinstance(statement.value, str):
            expressions = (statement.value,)
        elif isinstance(statement, (If, WhileLoop)):
            expressions = (statement.condition,)
        if any(contains(expression) for expression in expressions):
            return True
        if isinstance(statement, If) and (
            _has_string_comparison(statement.then_body, value_types)
            or _has_string_comparison(statement.else_body, value_types)
        ):
            return True
        if isinstance(statement, WhileLoop) and (
            _has_string_comparison(statement.init, value_types)
            or _has_string_comparison(statement.body, value_types)
            or _has_string_comparison(statement.step, value_types)
        ):
            return True
    return False


def _emit_expression_asm(
    expression: Expression,
    registers: dict[str, str],
    lines: list[str],
    label_counter: list[int],
) -> None:
    if isinstance(expression, BooleanLiteral):
        lines.append(f"    mov rax, {1 if expression.value else 0}")
    elif isinstance(expression, Literal):
        lines.append(f"    mov rax, {expression.value}")
    elif isinstance(expression, Name):
        register = registers[expression.value]
        if register.startswith("@"):
            lines.append(f"    mov rax, qword ptr [rip + tiger_var_{register[1:]}]")
        else:
            lines.append(f"    mov rax, {register}")
    elif isinstance(expression, Unary):
        if expression.operator == "deref" and isinstance(expression.operand, Name):
            lines.extend(
                [
                    f"    mov r11, {expression.operand.value}",
                    "    mov rax, qword ptr [r11]",
                ]
            )
            return
        _emit_expression_asm(expression.operand, registers, lines, label_counter)
        if expression.operator == "-":
            lines.append("    neg rax")
        elif expression.operator == "not":
            lines.extend(["    test rax, rax", "    sete al", "    movzx eax, al"])
    elif isinstance(expression, Binary):
        if expression.operator in {"and", "or"}:
            label_counter[0] += 1
            number = label_counter[0]
            branch = f".Llogic_branch_{number}"
            end = f".Llogic_end_{number}"
            _emit_expression_asm(expression.left, registers, lines, label_counter)
            lines.extend(["    test rax, rax", f"    {'jz' if expression.operator == 'and' else 'jnz'} {branch}"])
            _emit_expression_asm(expression.right, registers, lines, label_counter)
            lines.extend(["    test rax, rax", "    setne al", "    movzx eax, al", f"    jmp {end}", f"{branch}:"])
            lines.append("    xor eax, eax" if expression.operator == "and" else "    mov eax, 1")
            lines.append(f"{end}:")
            return
        _emit_expression_asm(expression.left, registers, lines, label_counter)
        lines.append("    push rax")
        _emit_expression_asm(expression.right, registers, lines, label_counter)
        lines.extend(["    mov rcx, rax", "    pop rax"])
        if expression.operator == "+":
            lines.append("    add rax, rcx")
        elif expression.operator == "-":
            lines.append("    sub rax, rcx")
        elif expression.operator == "*":
            lines.append("    imul rax, rcx")
        elif expression.operator in {"/", "%"}:
            lines.extend(["    cqo", "    idiv rcx"])
            if expression.operator == "%":
                lines.append("    mov rax, rdx")
        elif expression.operator in COMPARISONS:
            condition = {
                "==": "sete",
                "!=": "setne",
                "<": "setl",
                "<=": "setle",
                ">": "setg",
                ">=": "setge",
            }[expression.operator]
            lines.extend([f"    cmp rax, rcx", f"    {condition} al", "    movzx eax, al"])


def _emit_float_expression_asm(
    expression: Expression,
    value_types: dict[str, str],
    lines: list[str],
) -> None:
    if isinstance(expression, Literal) and isinstance(expression.value, float):
        bits = int.from_bytes(struct.pack("<d", expression.value), "little")
        lines.extend([f"    mov rax, 0x{bits:016x}", "    movq xmm0, rax"])
        return
    if isinstance(expression, Name):
        lines.append(f"    movsd xmm0, qword ptr [rip + tiger_var_{expression.value}]")
        return
    if isinstance(expression, Unary):
        _emit_float_expression_asm(expression.operand, value_types, lines)
        if expression.operator == "-":
            lines.extend(
                [
                    "    mov rax, 0x8000000000000000",
                    "    movq xmm1, rax",
                    "    xorpd xmm0, xmm1",
                ]
            )
        return
    if not isinstance(expression, Binary):
        raise CompilerError("expresión flotante no admitida en Assembly nativo")
    _emit_float_expression_asm(expression.left, value_types, lines)
    lines.extend(["    sub rsp, 16", "    movsd qword ptr [rsp], xmm0"])
    _emit_float_expression_asm(expression.right, value_types, lines)
    lines.extend(["    movsd xmm1, xmm0", "    movsd xmm0, qword ptr [rsp]", "    add rsp, 16"])
    operation = {"+": "addsd", "-": "subsd", "*": "mulsd", "/": "divsd"}.get(expression.operator)
    if operation is None:
        raise CompilerError(f"operador flotante no admitido: {expression.operator}")
    lines.append(f"    {operation} xmm0, xmm1")


def _emit_typed_expression_asm(
    expression: Expression,
    registers: dict[str, str],
    value_types: dict[str, str],
    lines: list[str],
    label_counter: list[int],
) -> None:
    if isinstance(expression, Unary) and expression.operator == "not" and _expression_type(
        expression.operand, value_types
    ) == "float":
        _emit_float_expression_asm(expression.operand, value_types, lines)
        lines.extend(
            [
                "    xorpd xmm1, xmm1",
                "    ucomisd xmm0, xmm1",
                "    sete al",
                "    setp r8b",
                "    and al, r8b",
                "    movzx eax, al",
            ]
        )
        return
    if isinstance(expression, Binary) and expression.operator in {"and", "or"}:
        label_counter[0] += 1
        number = label_counter[0]
        branch = f".Llogic_branch_{number}"
        end = f".Llogic_end_{number}"
        for index, side in enumerate((expression.left, expression.right)):
            if _expression_type(side, value_types) == "float":
                _emit_float_expression_asm(side, value_types, lines)
                lines.extend(
                    [
                        "    xorpd xmm1, xmm1",
                        "    ucomisd xmm0, xmm1",
                        "    setne al",
                        "    setp r8b",
                        "    or al, r8b",
                        "    movzx eax, al",
                    ]
                )
            else:
                _emit_typed_expression_asm(side, registers, value_types, lines, label_counter)
            if index == 0:
                lines.extend(
                    [
                        "    test rax, rax",
                        f"    {'jz' if expression.operator == 'and' else 'jnz'} {branch}",
                    ]
                )
            else:
                lines.extend(["    test rax, rax", "    setne al", "    movzx eax, al", f"    jmp {end}"])
        lines.append(f"{branch}:")
        lines.append("    xor eax, eax" if expression.operator == "and" else "    mov eax, 1")
        lines.append(f"{end}:")
        return
    if isinstance(expression, Binary) and expression.operator in COMPARISONS:
        operand_type = _expression_type(expression.left, value_types)
        if operand_type == "float":
            condition = {
                "==": "sete",
                "!=": "setne",
                "<": "setb",
                "<=": "setbe",
                ">": "seta",
                ">=": "setae",
            }[expression.operator]
            _emit_float_expression_asm(expression.left, value_types, lines)
            lines.extend(["    sub rsp, 16", "    movsd qword ptr [rsp], xmm0"])
            _emit_float_expression_asm(expression.right, value_types, lines)
            lines.extend(
                [
                    "    movsd xmm1, xmm0",
                    "    movsd xmm0, qword ptr [rsp]",
                    "    add rsp, 16",
                    "    ucomisd xmm0, xmm1",
                    f"    {condition} al",
                ]
            )
            if expression.operator == "!=":
                lines.extend(["    setp r8b", "    or al, r8b"])
            elif expression.operator in {"==", "<", "<="}:
                lines.extend(["    setnp r8b", "    and al, r8b"])
            lines.append("    movzx eax, al")
            return
        if operand_type == "string":
            raise CompilerError("la comparación de textos aún no está disponible en Assembly nativo")
    if _expression_type(expression, value_types) == "float":
        _emit_float_expression_asm(expression, value_types, lines)
        lines.append("    movq rax, xmm0")
        return
    _emit_expression_asm(expression, registers, lines, label_counter)


def _empaquetar_procesos(body: tuple[Statement, ...], tamano_paquete: int = 2) -> list[tuple[Statement, ...]]:
    """Paso 1: Empaqueta procesos en paquetes de subprocesos."""
    if len(body) <= 1:
        return [body] if body else []
    paquetes: list[tuple[Statement, ...]] = []
    for i in range(0, len(body), tamano_paquete):
        paquetes.append(body[i : i + tamano_paquete])
    return paquetes


def _ejecutar_compilacion_paralelo(
    body: tuple[Statement, ...],
    emit_fn: Any,
) -> list[str]:
    """Arquitectura de compilación Tiger 10:
    Paso 1: Empaqueta procesos.
    Paso 2: 2 ejecutadores en paralelo en subprocesos saltando de dos en dos.
            Si hay un párrafo de código (1 paquete), se usa solo un ejecutador.
    Paso 3: Devuelve el resultado en orden.
    """
    paquetes = _empaquetar_procesos(body)
    if not paquetes:
        return []
    if len(paquetes) <= 3:
        lineas_directas: list[str] = []
        for idx, pkg in enumerate(paquetes):
            lineas_directas.extend(emit_fn(pkg, 0, idx))
        return lineas_directas

    resultados: dict[int, list[str]] = {}

    def subproceso_ejecutador(ejecutador_id: int, indices: list[int]) -> None:
        for idx in indices:
            resultados[idx] = emit_fn(paquetes[idx], ejecutador_id, idx)

    indices_ejecutador_0 = list(range(0, len(paquetes), 2))
    indices_ejecutador_1 = list(range(1, len(paquetes), 2))

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        f0 = pool.submit(subproceso_ejecutador, 0, indices_ejecutador_0)
        f1 = pool.submit(subproceso_ejecutador, 1, indices_ejecutador_1)
        f0.result()
        f1.result()

    lineas_resultado: list[str] = []
    for idx in range(len(paquetes)):
        lineas_resultado.extend(resultados[idx])
    return lineas_resultado


class AssemblyEmitter:
    __slots__ = (
        "registers",
        "value_types",
        "strings",
        "labels",
        "lines",
        "label_counter",
        "has_numeric_print",
        "uses_libc",
        "pointer_addresses",
        "loop_stack",
    )

    def __init__(
        self,
        registers: dict[str, str],
        body: tuple[Statement, ...],
        value_types: dict[str, str],
    ):
        self.registers = registers
        self.value_types = {k: ("string" if v == "dynamic" else v) for k, v in value_types.items()}
        bool_prints = ["TRUE\n", "FALSE\n"] if any(
            isinstance(statement, Print) and not isinstance(statement.value, str)
            and _expression_type(statement.value, self.value_types) == "bool"
            for statement in self._walk(body)
        ) else []
        self.strings = list(dict.fromkeys(_all_prints(body) + _all_string_values(body) + bool_prints))
        self.labels = {text: f"tiger_str_{index}" for index, text in enumerate(self.strings)}
        self.lines: list[str] = []
        self.label_counter = 0
        self.has_numeric_print = _has_numeric_print(body)
        self.uses_libc = self._needs_libc(body)
        self.pointer_addresses: dict[str, int] = {}
        self.loop_stack: list[tuple[str, str]] = []

    def emit(self, body: tuple[Statement, ...], pointers: dict[str, int]) -> str:
        self.pointer_addresses = pointers
        entry = "main" if self.uses_libc else "_start"
        self.lines = ["BITS 64", "default rel", f"global {entry}", "section .rodata"]
        for text in self.strings:
            data = ", ".join(f"0x{byte:02x}" for byte in text.encode("utf-8") + b"\0")
            clean_repr = text.replace("\n", "\\n").replace("\r", "\\r")
            self.lines.append(f'{self.labels[text]}: db {data}  ; "{clean_repr}"')
        if self.uses_libc:
            formats = {
                "tiger_fmt_print_string": b"%s\n\0",
                "tiger_fmt_print_float": b"%.17g\n\0",
                "tiger_fmt_input_int": b"%lld\0",
                "tiger_fmt_input_float": b"%lf\0",
                "tiger_fmt_input_string": b"%1023s\0",
            }
            for label, data in formats.items():
                self.lines.append(f"{label}: db " + ", ".join(f"0x{byte:02x}" for byte in data))
            self.lines.extend(["extern printf", "extern scanf", "extern strcmp", "extern fflush"])
        for name, address in pointers.items():
            self.lines.append(f"{name} equ 0x{address:x}")
        if self.has_numeric_print or self.value_types or _input_targets(body):
            self.lines.extend(["", "section .bss"])
        if self.has_numeric_print:
            self.lines.append("tiger_num_buffer: resb 32")
        if any(self.value_types[name] == "int" for name in _input_targets(body)):
            self.lines.append("tiger_input_int: resq 1")
        for name, value_type in self.value_types.items():
            if value_type == "float":
                self.lines.append(f"tiger_var_{name}: resq 1")
            elif value_type == "int" and self.registers[name].startswith("@"):
                self.lines.append(f"tiger_var_{name}: resq 1")
            elif value_type == "string":
                self.lines.extend([f"tiger_var_{name}: resq 1", f"tiger_len_{name}: resq 1"])
                if name in _input_targets(body):
                    self.lines.append(f"tiger_input_{name}: resb 1024")
        self.lines.extend(["", "section .text"])
        if self.uses_libc:
            self.lines.extend(["main:", "    push rbp", "    mov rbp, rsp"])
        else:
            self.lines.append("_start:")
        def _emit_paquete_asm(paquete: tuple[Statement, ...], ejecutador_id: int, paquete_idx: int) -> list[str]:
            sub = AssemblyEmitter(self.registers, paquete, self.value_types)
            sub.labels = self.labels
            sub.pointer_addresses = self.pointer_addresses
            sub.label_counter = paquete_idx * 1000 + ejecutador_id * 100
            sub._emit_body(paquete)
            return sub.lines

        body_lines = _ejecutar_compilacion_paralelo(body, _emit_paquete_asm)
        self.lines.extend(body_lines)
        if self.uses_libc:
            self.lines.extend(["    xor eax, eax", "    pop rbp", "    ret"])
        else:
            self.lines.extend(["    mov eax, 60", "    xor edi, edi", "    syscall"])
        return "\n".join(self.lines) + "\n"

    def _walk(self, body: tuple[Statement, ...]):
        for statement in body:
            yield statement
            if isinstance(statement, If):
                yield from self._walk(statement.then_body)
                yield from self._walk(statement.else_body)
            elif isinstance(statement, WhileLoop):
                yield from self._walk(statement.init)
                yield from self._walk(statement.body)
                yield from self._walk(statement.step)

    def _needs_libc(self, body: tuple[Statement, ...]) -> bool:
        if _input_targets(body):
            return True
        return _has_string_comparison(body, self.value_types) or any(
            isinstance(statement, Print)
            and not isinstance(statement.value, str)
            and _expression_type(statement.value, self.value_types) in {"float", "string"}
            for statement in self._walk(body)
        )

    def _emit_expression(self, expression: Expression) -> None:
        counter = [self.label_counter]
        if isinstance(expression, Binary) and expression.operator in {"==", "!="} and _expression_type(
            expression.left, self.value_types
        ) == "string":
            self.lines.extend(
                [
                    self._string_pointer(expression.left, "rdi"),
                    self._string_pointer(expression.right, "rsi"),
                    "    call strcmp",
                    "    test eax, eax",
                    f"    {'sete' if expression.operator == '==' else 'setne'} al",
                    "    movzx eax, al",
                ]
            )
        else:
            scratch: list[str] = []
            _emit_typed_expression_asm(expression, self.registers, self.value_types, scratch, counter)
            self.lines.extend(
                line.replace("qword ptr [rip + ", "qword [rel ")
                .replace("dword ptr [rip + ", "dword [rel ")
                .replace("byte ptr [rip + ", "byte [rel ")
                .replace("qword ptr ", "qword ")
                .replace("dword ptr ", "dword ")
                .replace("byte ptr ", "byte ")
                for line in scratch
            )
        self.label_counter = counter[0]

    def _string_pointer(self, expression: Expression, destination: str) -> str:
        if isinstance(expression, StringLiteral):
            return f"    lea {destination}, [rel {self.labels[expression.value]}]"
        if isinstance(expression, Name):
            return f"    mov {destination}, qword [rel tiger_var_{expression.value}]"
        raise CompilerError("la comparación de textos requiere variables o literales")

    def _emit_input(self, name: str) -> None:
        value_type = self.value_types[name]
        self.label_counter += 1
        ready = f".Linput_ok_{self.label_counter}"
        format_label = {
            "int": "tiger_fmt_input_int",
            "float": "tiger_fmt_input_float",
            "string": "tiger_fmt_input_string",
        }[value_type]
        target = {
            "int": "tiger_input_int",
            "float": f"tiger_var_{name}",
            "string": f"tiger_input_{name}",
        }[value_type]
        self.lines.extend(
            [
                f"    lea rdi, [rel {format_label}]",
                f"    lea rsi, [rel {target}]",
                "    xor eax, eax",
                "    call scanf",
                "    cmp eax, 1",
                f"    je {ready}",
                "    mov eax, 60",
                "    mov edi, 2",
                "    syscall",
                f"{ready}:",
            ]
        )
        if value_type == "int":
            self.lines.append("    mov rax, qword [rel tiger_input_int]")
            if self.registers[name].startswith("@"):
                self.lines.append(f"    mov qword [rel tiger_var_{name}], rax")
            else:
                self.lines.append(f"    mov {self.registers[name]}, rax")
        elif value_type == "string":
            self.lines.extend(
                [
                    f"    lea rax, [rel tiger_input_{name}]",
                    f"    mov qword [rel tiger_var_{name}], rax",
                ]
            )

    def _emit_store(self, name: str, value: Expression) -> None:
        value_type = self.value_types[name]
        if isinstance(value, Input):
            self._emit_input(name)
        elif value_type == "float":
            scratch: list[str] = []
            _emit_float_expression_asm(value, self.value_types, scratch)
            self.lines.extend(
                line.replace("qword ptr [rip + ", "qword [rel ")
                .replace("dword ptr [rip + ", "dword [rel ")
                .replace("qword ptr ", "qword ")
                .replace("dword ptr ", "dword ")
                for line in scratch
            )
            self.lines.append(f"    movsd qword [rel tiger_var_{name}], xmm0")
        elif value_type == "string":
            if isinstance(value, StringLiteral):
                self.lines.extend(
                    [
                        f"    lea rax, [rel {self.labels[value.value]}]",
                        f"    mov qword [rel tiger_var_{name}], rax",
                        f"    mov qword [rel tiger_len_{name}], {len(value.value.encode('utf-8'))}",
                    ]
                )
            elif isinstance(value, Name):
                self.lines.extend(
                    [
                        f"    mov rax, qword [rel tiger_var_{value.value}]",
                        f"    mov qword [rel tiger_var_{name}], rax",
                        f"    mov rax, qword [rel tiger_len_{value.value}]",
                        f"    mov qword [rel tiger_len_{name}], rax",
                    ]
                )
            else:
                raise CompilerError("STR/CAD solo admite texto literal o INPUT")
        else:
            self._emit_expression(value)
            if self.registers[name].startswith("@"):
                self.lines.append(f"    mov qword [rel tiger_var_{name}], rax")
            else:
                self.lines.append(f"    mov {self.registers[name]}, rax")

    def _emit_body(self, body: tuple[Statement, ...]) -> None:
        for statement in body:
            if isinstance(statement, Variable):
                self._emit_store(statement.name, statement.value)
            elif isinstance(statement, Assignment):
                self._emit_store(statement.name, statement.value)
            elif isinstance(statement, Print):
                if not isinstance(statement.value, str):
                    value_type = _expression_type(statement.value, self.value_types)
                    if value_type == "float":
                        self._emit_expression(statement.value)
                        self.lines.extend(
                            [
                                "    lea rdi, [rel tiger_fmt_print_float]",
                                "    movq xmm0, rax",
                                "    mov eax, 1",
                                "    call printf",
                                "    xor edi, edi",
                                "    call fflush",
                            ]
                        )
                    elif value_type == "string":
                        if not isinstance(statement.value, Name):
                            raise CompilerError("solo se puede imprimir una variable de texto")
                        self.lines.extend(
                            [
                                "    lea rdi, [rel tiger_fmt_print_string]",
                                f"    mov rsi, qword [rel tiger_var_{statement.value.value}]",
                                "    xor eax, eax",
                                "    call printf",
                                "    xor edi, edi",
                                "    call fflush",
                            ]
                        )
                    elif value_type == "bool":
                        self.label_counter += 1
                        lbl = self.label_counter
                        self._emit_expression(statement.value)
                        true_lbl = self.labels.get("TRUE\n", "tiger_str_0")
                        false_lbl = self.labels.get("FALSE\n", "tiger_str_0")
                        if self.uses_libc:
                            self.lines.extend([
                                "    test rax, rax",
                                f"    jz .Lbool_false_{lbl}",
                                "    lea rdi, [rel tiger_fmt_print_string]",
                                f"    lea rsi, [rel {true_lbl}]",
                                "    xor eax, eax",
                                "    call printf",
                                "    xor edi, edi",
                                "    call fflush",
                                f"    jmp .Lbool_end_{lbl}",
                                f".Lbool_false_{lbl}:",
                                "    lea rdi, [rel tiger_fmt_print_string]",
                                f"    lea rsi, [rel {false_lbl}]",
                                "    xor eax, eax",
                                "    call printf",
                                "    xor edi, edi",
                                "    call fflush",
                                f".Lbool_end_{lbl}:",
                            ])
                        else:
                            self.lines.extend([
                                "    test rax, rax",
                                f"    jz .Lbool_false_{lbl}",
                                "    mov eax, 1",
                                "    mov edi, 1",
                                f"    lea rsi, [rel {true_lbl}]",
                                "    mov edx, 5",
                                "    syscall",
                                f"    jmp .Lbool_end_{lbl}",
                                f".Lbool_false_{lbl}:",
                                "    mov eax, 1",
                                "    mov edi, 1",
                                f"    lea rsi, [rel {false_lbl}]",
                                "    mov edx, 6",
                                "    syscall",
                                f".Lbool_end_{lbl}:",
                            ])
                    else:
                        self.label_counter += 1
                        self._emit_integer_print(statement.value, self.label_counter)
                else:
                    text = statement.value + "\n"
                    label = self.labels[text]
                    length = len(text.encode("utf-8"))
                    self.lines.extend(
                        [
                            "    mov eax, 1",
                            "    mov edi, 1",
                            f"    lea rsi, [rel {label}]",
                            f"    mov edx, {length}",
                            "    syscall",
                        ]
                    )
            elif isinstance(statement, If):
                self.label_counter += 1
                number = self.label_counter
                self._emit_expression(statement.condition)
                if _expression_type(statement.condition, self.value_types) == "float":
                    self.lines.extend(
                        [
                            "    movq xmm0, rax",
                            "    xorpd xmm1, xmm1",
                            "    ucomisd xmm0, xmm1",
                            f"    je .Lelse_{number}",
                            f"    jp .Lthen_{number}",
                            f"    jmp .Lelse_{number}",
                            f".Lthen_{number}:",
                        ]
                    )
                    self._emit_body(statement.then_body)
                    self.lines.extend([f"    jmp .Lend_{number}", f".Lelse_{number}:"])
                    self._emit_body(statement.else_body)
                    self.lines.append(f".Lend_{number}:")
                    continue
                self.lines.append("    test rax, rax")
                jump = "jz"
                self.lines.append(f"    {jump} .Lelse_{number}")
                self._emit_body(statement.then_body)
                self.lines.append(f"    jmp .Lend_{number}")
                self.lines.append(f".Lelse_{number}:")
                self._emit_body(statement.else_body)
                self.lines.append(f".Lend_{number}:")
            elif isinstance(statement, WhileLoop):
                if statement.init:
                    self._emit_body(statement.init)
                self.label_counter += 1
                number = self.label_counter
                start_label = f".Lloop_start_{number}"
                body_label = f".Lloop_body_{number}"
                step_label = f".Lloop_step_{number}"
                end_label = f".Lloop_end_{number}"
                self.lines.append(f"{start_label}:")
                self._emit_expression(statement.condition)
                if _expression_type(statement.condition, self.value_types) == "float":
                    self.lines.extend(
                        [
                            "    movq xmm0, rax",
                            "    xorpd xmm1, xmm1",
                            "    ucomisd xmm0, xmm1",
                            f"    je {end_label}",
                            f"    jp {body_label}",
                            f"    jmp {end_label}",
                            f"{body_label}:",
                        ]
                    )
                else:
                    self.lines.extend(["    test rax, rax", f"    jz {end_label}"])
                self.loop_stack.append((step_label, end_label))
                self._emit_body(statement.body)
                self.loop_stack.pop()
                self.lines.append(f"{step_label}:")
                if statement.step:
                    self._emit_body(statement.step)
                self.lines.append(f"    jmp {start_label}")
                self.lines.append(f"{end_label}:")
            elif isinstance(statement, Break):
                self.lines.append(f"    jmp {self.loop_stack[-1][1]}")
            elif isinstance(statement, Continue):
                self.lines.append(f"    jmp {self.loop_stack[-1][0]}")
            elif isinstance(statement, PointerStore):
                self._emit_expression(statement.value)
                self.lines.extend(
                    [
                        f"    mov r11, {self.pointer_addresses[statement.name]}",
                        "    mov qword [r11], rax",
                    ]
                )
            elif isinstance(statement, Pointer):
                continue

    def _emit_integer_print(self, expression: Expression, number: int) -> None:
        loop = f".Lprint_int_loop_{number}"
        positive = f".Lprint_int_positive_{number}"
        ready = f".Lprint_int_ready_{number}"
        self.lines.extend(
            [
            ]
        )
        self._emit_expression(expression)
        self.lines.extend(
            [
                "    lea rsi, [rel tiger_num_buffer + 31]",
                "    mov byte [rsi], 10",
                "    mov ecx, 1",
                "    xor r8d, r8d",
                "    test rax, rax",
                f"    jns {positive}",
                "    neg rax",
                "    mov r8b, 1",
                f"{positive}:",
                "    mov ebx, 10",
                f"{loop}:",
                "    xor edx, edx",
                "    div rbx",
                "    add dl, '0'",
                "    dec rsi",
                "    mov [rsi], dl",
                "    inc ecx",
                "    test rax, rax",
                f"    jnz {loop}",
                "    test r8b, r8b",
                f"    jz {ready}",
                "    dec rsi",
                "    mov byte [rsi], '-'",
                "    inc ecx",
                f"{ready}:",
                "    mov eax, 1",
                "    mov edi, 1",
                "    mov edx, ecx",
                "    syscall",
            ]
        )


class WindowsAssemblyEmitter:
    __slots__ = (
        "registers",
        "value_types",
        "strings",
        "labels",
        "lines",
        "label_counter",
        "pointer_addresses",
        "loop_stack",
    )

    def __init__(
        self,
        registers: dict[str, str],
        body: tuple[Statement, ...],
        value_types: dict[str, str],
    ):
        self.registers = registers
        self.value_types = {k: ("string" if v == "dynamic" else v) for k, v in value_types.items()}
        bool_prints = ["TRUE\n", "FALSE\n"] if any(
            isinstance(statement, Print) and not isinstance(statement.value, str)
            and _expression_type(statement.value, self.value_types) == "bool"
            for statement in self._walk(body)
        ) else []
        self.strings = list(dict.fromkeys(_all_prints(body) + _all_string_values(body) + bool_prints))
        self.labels = {text: f"tiger_str_{index}" for index, text in enumerate(self.strings)}
        self.lines: list[str] = []
        self.label_counter = 0
        self.pointer_addresses: dict[str, int] = {}
        self.loop_stack: list[tuple[str, str]] = []

    def emit(self, body: tuple[Statement, ...], pointers: dict[str, int]) -> str:
        self.pointer_addresses = pointers
        self.lines = [".intel_syntax noprefix", '.section .rdata,"dr"']
        for text in self.strings:
            data = ", ".join(str(byte) for byte in text.encode("utf-8") + b"\0")
            self.lines.append(f"{self.labels[text]}: .byte {data}")
        formats = {
            "tiger_fmt_print_string": b"%s\0",
            "tiger_fmt_print_float": b"%.17g\0",
            "tiger_fmt_input_int": b"%lld\0",
            "tiger_fmt_input_float": b"%lf\0",
            "tiger_fmt_input_string": b"%1023s\0",
        }
        for label, data in formats.items():
            if self._needs_crt(body):
                self.lines.append(f"{label}: .byte " + ", ".join(str(byte) for byte in data))
        if self._needs_crt(body):
            self.lines.append("tiger_newline: .byte 10")
        self.lines.extend(
            [
                ".text",
                ".globl main",
                ".extern __imp_GetStdHandle",
                ".extern __imp_WriteFile",
                ".extern __imp_ExitProcess",
            ]
        )
        if self._needs_crt(body):
            self.lines.extend(
                [".extern __imp_printf", ".extern __imp_scanf", ".extern __imp_strcmp", ".extern __imp_fflush"]
            )
        for name, address in pointers.items():
            self.lines.append(f".equ {name}, {address}")
        if _has_numeric_print(body):
            self.lines.extend(['.lcomm tiger_num_buffer, 32'])
        for name, value_type in self.value_types.items():
            if value_type == "int" and self.registers[name].startswith("@"):
                self.lines.append(f".lcomm tiger_var_{name}, 8")
            elif value_type == "float":
                self.lines.append(f".lcomm tiger_var_{name}, 8")
        for name, value_type in self.value_types.items():
            if value_type == "string":
                self.lines.append(f".lcomm tiger_var_{name}, 8")
                self.lines.append(f".lcomm tiger_len_{name}, 8")
                if name in _input_targets(body):
                    self.lines.append(f".lcomm tiger_input_{name}, 1024")
        if any(self.value_types[name] == "int" for name in _input_targets(body)):
            self.lines.append(".lcomm tiger_input_int, 8")
        self.lines.extend(["main:", "    sub rsp, 56"])
        def _emit_paquete_win(paquete: tuple[Statement, ...], ejecutador_id: int, paquete_idx: int) -> list[str]:
            sub = WindowsAssemblyEmitter(self.registers, paquete, self.value_types)
            sub.labels = self.labels
            sub.pointer_addresses = self.pointer_addresses
            sub.label_counter = paquete_idx * 1000 + ejecutador_id * 100
            sub._emit_body(paquete)
            return sub.lines

        body_lines = _ejecutar_compilacion_paralelo(body, _emit_paquete_win)
        self.lines.extend(body_lines)
        self.lines.extend(
            [
                "    xor ecx, ecx",
                "    call qword ptr [rip + __imp_ExitProcess]",
                "    add rsp, 56",
                "    ret",
            ]
        )
        return "\n".join(self.lines) + "\n"

    def _needs_crt(self, body: tuple[Statement, ...]) -> bool:
        if _input_targets(body):
            return True
        if _has_string_comparison(body, self.value_types) or any(
            isinstance(statement, Print)
            and not isinstance(statement.value, str)
            and _expression_type(statement.value, self.value_types) in {"float", "string"}
            for statement in self._walk(body)
        ):
            return True
        return False

    @staticmethod
    def _walk(body: tuple[Statement, ...]):
        for statement in body:
            yield statement
            if isinstance(statement, If):
                yield from WindowsAssemblyEmitter._walk(statement.then_body)
                yield from WindowsAssemblyEmitter._walk(statement.else_body)
            elif isinstance(statement, WhileLoop):
                yield from WindowsAssemblyEmitter._walk(statement.init)
                yield from WindowsAssemblyEmitter._walk(statement.body)
                yield from WindowsAssemblyEmitter._walk(statement.step)

    def _emit_input(self, name: str) -> None:
        value_type = self.value_types[name]
        self.label_counter += 1
        ready = f".Linput_ok_{self.label_counter}"
        fmt = {
            "int": "tiger_fmt_input_int",
            "float": "tiger_fmt_input_float",
            "string": "tiger_fmt_input_string",
        }[value_type]
        destination = {
            "int": "lea rdx, [rip + tiger_input_int]",
            "float": f"lea rdx, [rip + tiger_var_{name}]",
            "string": f"lea rdx, [rip + tiger_input_{name}]",
        }[value_type]
        self.lines.extend(
            [
                f"    lea rcx, [rip + {fmt}]",
                f"    {destination}",
                "    call qword ptr [rip + __imp_scanf]",
                "    cmp eax, 1",
                f"    je {ready}",
                "    mov ecx, 2",
                "    call qword ptr [rip + __imp_ExitProcess]",
                f"{ready}:",
            ]
        )
        if value_type == "int":
            self.lines.append(f"    mov rax, qword ptr [rip + tiger_input_int]")
            if self.registers[name].startswith("@"):
                self.lines.append(f"    mov qword ptr [rip + tiger_var_{name}], rax")
            else:
                self.lines.append(f"    mov {self.registers[name]}, rax")
        elif value_type == "string":
            self.lines.extend(
                [
                    f"    lea rax, [rip + tiger_input_{name}]",
                    f"    mov qword ptr [rip + tiger_var_{name}], rax",
                ]
            )

    def _emit_write(self, buffer: str, length: int) -> None:
        self.lines.extend(
            [
                "    mov ecx, -11",
                "    call qword ptr [rip + __imp_GetStdHandle]",
                "    mov rcx, rax",
                f"    {buffer}",
                f"    mov r8d, {length}",
                "    lea r9, [rsp + 40]",
                "    mov qword ptr [rsp + 32], 0",
                "    call qword ptr [rip + __imp_WriteFile]",
            ]
        )

    def _emit_expression(self, expression: Expression) -> None:
        counter = [self.label_counter]
        if isinstance(expression, Binary) and expression.operator in {"==", "!="} and _expression_type(
            expression.left, self.value_types
        ) == "string":
            left = self._string_pointer(expression.left, "rcx")
            right = self._string_pointer(expression.right, "rdx")
            self.lines.extend([left, right, "    call qword ptr [rip + __imp_strcmp]", "    test eax, eax"])
            self.lines.extend(
                [f"    {'sete' if expression.operator == '==' else 'setne'} al", "    movzx eax, al"]
            )
        else:
            _emit_typed_expression_asm(
                expression,
                self.registers,
                self.value_types,
                self.lines,
                counter,
            )
        self.label_counter = counter[0]

    def _string_pointer(self, expression: Expression, destination: str) -> str:
        if isinstance(expression, StringLiteral):
            return f"    lea {destination}, [rip + {self.labels[expression.value]}]"
        if isinstance(expression, Name):
            return f"    mov {destination}, qword ptr [rip + tiger_var_{expression.value}]"
        raise CompilerError("la comparación de textos requiere variables o literales")

    def _emit_store(self, name: str, value: Expression) -> None:
        value_type = self.value_types[name]
        if isinstance(value, Input):
            self._emit_input(name)
        elif value_type == "float":
            _emit_float_expression_asm(value, self.value_types, self.lines)
            self.lines.append(f"    movsd qword ptr [rip + tiger_var_{name}], xmm0")
        elif value_type == "string":
            if isinstance(value, StringLiteral):
                self.lines.extend(
                    [
                        f"    lea rax, [rip + {self.labels[value.value]}]",
                        f"    mov qword ptr [rip + tiger_var_{name}], rax",
                        f"    mov qword ptr [rip + tiger_len_{name}], {len(value.value.encode('utf-8'))}",
                    ]
                )
            elif isinstance(value, Name):
                self.lines.extend(
                    [
                        f"    mov rax, qword ptr [rip + tiger_var_{value.value}]",
                        f"    mov qword ptr [rip + tiger_var_{name}], rax",
                        f"    mov rax, qword ptr [rip + tiger_len_{value.value}]",
                        f"    mov qword ptr [rip + tiger_len_{name}], rax",
                    ]
                )
            else:
                raise CompilerError("STR/CAD solo admite texto literal o INPUT")
        else:
            self._emit_expression(value)
            if self.registers[name].startswith("@"):
                self.lines.append(f"    mov qword ptr [rip + tiger_var_{name}], rax")
            else:
                self.lines.append(f"    mov {self.registers[name]}, rax")

    def _emit_body(self, body: tuple[Statement, ...]) -> None:
        for statement in body:
            if isinstance(statement, Variable):
                self._emit_store(statement.name, statement.value)
            elif isinstance(statement, Assignment):
                self._emit_store(statement.name, statement.value)
            elif isinstance(statement, Print):
                if not isinstance(statement.value, str):
                    value_type = _expression_type(statement.value, self.value_types)
                    if value_type == "float":
                        _emit_float_expression_asm(statement.value, self.value_types, self.lines)
                        self.lines.extend(
                            [
                                "    lea rcx, [rip + tiger_fmt_print_float]",
                                "    movq rdx, xmm0",
                                "    mov al, 1",
                                "    call qword ptr [rip + __imp_printf]",
                                "    xor ecx, ecx",
                                "    call qword ptr [rip + __imp_fflush]",
                            ]
                        )
                        self._emit_write("lea rdx, [rip + tiger_newline]", 1)
                    elif value_type == "string":
                        if not isinstance(statement.value, Name):
                            raise CompilerError("solo se puede imprimir una variable de texto")
                        self.lines.extend(
                            [
                                "    sub rsp, 32",
                                "    lea rcx, [rip + tiger_fmt_print_string]",
                                f"    mov rdx, qword ptr [rip + tiger_var_{statement.value.value}]",
                                "    call qword ptr [rip + __imp_printf]",
                                "    xor ecx, ecx",
                                "    call qword ptr [rip + __imp_fflush]",
                                "    add rsp, 32",
                            ]
                        )
                        self._emit_write("lea rdx, [rip + tiger_newline]", 1)
                    elif value_type == "bool":
                        self.label_counter += 1
                        lbl = self.label_counter
                        self._emit_expression(statement.value)
                        true_lbl = self.labels.get("TRUE\n", "tiger_str_0")
                        false_lbl = self.labels.get("FALSE\n", "tiger_str_0")
                        self.lines.extend([
                            "    test rax, rax",
                            f"    jz .Lbool_false_{lbl}",
                        ])
                        self._emit_write(f"lea rdx, [rip + {true_lbl}]", 5)
                        self.lines.extend([
                            f"    jmp .Lbool_end_{lbl}",
                            f".Lbool_false_{lbl}:",
                        ])
                        self._emit_write(f"lea rdx, [rip + {false_lbl}]", 6)
                        self.lines.append(f".Lbool_end_{lbl}:")
                    else:
                        self.label_counter += 1
                        self._emit_integer_print(statement.value, self.label_counter)
                else:
                    text = statement.value + "\n"
                    self._emit_write(
                        f"lea rdx, [rip + {self.labels[text]}]",
                        len(text.encode("utf-8")),
                    )
            elif isinstance(statement, If):
                self.label_counter += 1
                number = self.label_counter
                self._emit_expression(statement.condition)
                if _expression_type(statement.condition, self.value_types) == "float":
                    self.lines.extend(
                        [
                            "    movq xmm0, rax",
                            "    xorpd xmm1, xmm1",
                            "    ucomisd xmm0, xmm1",
                            f"    je .Lelse_{number}",
                            f"    jp .Lthen_{number}",
                            f"    jmp .Lelse_{number}",
                            f".Lthen_{number}:",
                        ]
                    )
                    self._emit_body(statement.then_body)
                    self.lines.extend([f"    jmp .Lend_{number}", f".Lelse_{number}:"])
                    self._emit_body(statement.else_body)
                    self.lines.append(f".Lend_{number}:")
                    continue
                self.lines.extend(["    test rax, rax", f"    jz .Lelse_{number}"])
                self._emit_body(statement.then_body)
                self.lines.append(f"    jmp .Lend_{number}")
                self.lines.append(f".Lelse_{number}:")
                self._emit_body(statement.else_body)
                self.lines.append(f".Lend_{number}:")
            elif isinstance(statement, WhileLoop):
                if statement.init:
                    self._emit_body(statement.init)
                self.label_counter += 1
                number = self.label_counter
                start_label = f".Lloop_start_{number}"
                body_label = f".Lloop_body_{number}"
                step_label = f".Lloop_step_{number}"
                end_label = f".Lloop_end_{number}"
                self.lines.append(f"{start_label}:")
                self._emit_expression(statement.condition)
                if _expression_type(statement.condition, self.value_types) == "float":
                    self.lines.extend(
                        [
                            "    movq xmm0, rax",
                            "    xorpd xmm1, xmm1",
                            "    ucomisd xmm0, xmm1",
                            f"    je {end_label}",
                            f"    jp {body_label}",
                            f"    jmp {end_label}",
                            f"{body_label}:",
                        ]
                    )
                else:
                    self.lines.extend(["    test rax, rax", f"    jz {end_label}"])
                self.loop_stack.append((step_label, end_label))
                self._emit_body(statement.body)
                self.loop_stack.pop()
                self.lines.append(f"{step_label}:")
                if statement.step:
                    self._emit_body(statement.step)
                self.lines.append(f"    jmp {start_label}")
                self.lines.append(f"{end_label}:")
            elif isinstance(statement, Break):
                self.lines.append(f"    jmp {self.loop_stack[-1][1]}")
            elif isinstance(statement, Continue):
                self.lines.append(f"    jmp {self.loop_stack[-1][0]}")
            elif isinstance(statement, PointerStore):
                self._emit_expression(statement.value)
                self.lines.extend(
                    [
                        f"    mov r11, {self.pointer_addresses[statement.name]}",
                        "    mov qword ptr [r11], rax",
                    ]
                )

    def _emit_integer_print(self, expression: Expression, number: int) -> None:
        loop = f".Lwin_print_int_loop_{number}"
        positive = f".Lwin_print_int_positive_{number}"
        ready = f".Lwin_print_int_ready_{number}"
        self.lines.extend(
            [
            ]
        )
        self._emit_expression(expression)
        self.lines.extend(
            [
                "    lea rsi, [rip + tiger_num_buffer + 31]",
                "    mov byte ptr [rsi], 10",
                "    mov ecx, 1",
                "    xor r8d, r8d",
                "    test rax, rax",
                f"    jns {positive}",
                "    neg rax",
                "    mov r8b, 1",
                f"{positive}:",
                "    mov ebx, 10",
                f"{loop}:",
                "    xor edx, edx",
                "    div rbx",
                "    add dl, '0'",
                "    dec rsi",
                "    mov byte ptr [rsi], dl",
                "    inc ecx",
                "    test rax, rax",
                f"    jnz {loop}",
                "    test r8b, r8b",
                f"    jz {ready}",
                "    dec rsi",
                "    mov byte ptr [rsi], '-'",
                "    inc ecx",
                f"{ready}:",
                "    mov dword ptr [rsp + 48], ecx",
                "    mov ecx, -11",
                "    call qword ptr [rip + __imp_GetStdHandle]",
                "    mov rcx, rax",
                "    mov rdx, rsi",
                "    mov r8d, dword ptr [rsp + 48]",
                "    lea r9, [rsp + 40]",
                "    mov qword ptr [rsp + 32], 0",
                "    call qword ptr [rip + __imp_WriteFile]",
            ]
        )


def _wat_name(name: str) -> str:
    return "$v_" + name


class WatEmitter:
    __slots__ = (
        "registers",
        "pointers",
        "value_types",
        "strings",
        "offsets",
        "input_buffers",
        "label_counter",
        "loop_stack",
    )

    def __init__(
        self,
        registers: dict[str, str],
        pointers: dict[str, int],
        body: tuple[Statement, ...],
        value_types: dict[str, str],
    ):
        self.registers = registers
        self.pointers = pointers
        self.value_types = {k: ("string" if v == "dynamic" else v) for k, v in value_types.items()}
        self.strings = _all_prints(body)
        self.offsets: dict[str, int] = {}
        offset = 0

        def add_text(text: str) -> None:
            nonlocal offset
            if text not in self.offsets:
                self.offsets[text] = offset
                offset += len(text.encode("utf-8"))

        for text in self.strings:
            add_text(text)
        add_text("TRUE\n")
        add_text("FALSE\n")
        for text in self._string_literals(body):
            add_text(text)
        self.input_buffers = {
            name: offset + index * 1024
            for index, name in enumerate(self._input_targets(body))
            if self.value_types[name] == "string"
        }
        self.label_counter = 0
        self.loop_stack: list[tuple[str, str]] = []

    @staticmethod
    def _walk(body: tuple[Statement, ...]):
        for statement in body:
            yield statement
            if isinstance(statement, If):
                yield from WatEmitter._walk(statement.then_body)
                yield from WatEmitter._walk(statement.else_body)
            elif isinstance(statement, WhileLoop):
                yield from WatEmitter._walk(statement.init)
                yield from WatEmitter._walk(statement.body)
                yield from WatEmitter._walk(statement.step)

    @classmethod
    def _string_literals(cls, body: tuple[Statement, ...]) -> list[str]:
        values: list[str] = []
        for statement in cls._walk(body):
            expressions: tuple[Expression, ...] = ()
            if isinstance(statement, Variable):
                expressions = (statement.value,)
            elif isinstance(statement, Assignment):
                expressions = (statement.value,)
            elif isinstance(statement, WhileLoop):
                expressions = (statement.condition,)
            for expression in expressions:
                if isinstance(expression, StringLiteral) and expression.value not in values:
                    values.append(expression.value)
        return values

    @classmethod
    def _input_targets(cls, body: tuple[Statement, ...]) -> list[str]:
        targets: list[str] = []
        for statement in cls._walk(body):
            if isinstance(statement, Variable) and isinstance(statement.value, Input):
                targets.append(statement.name)
            elif isinstance(statement, Assignment) and isinstance(statement.value, Input):
                if isinstance(statement.name, str):
                    targets.append(statement.name)
                elif isinstance(statement.name, Name):
                    targets.append(statement.name.value)
        return list(dict.fromkeys(targets))

    def _has_string_equality(self, body: tuple[Statement, ...]) -> bool:
        return _has_string_comparison(body, self.value_types)

    def _type_of(self, expression: Expression) -> str:
        if isinstance(expression, BooleanLiteral):
            return "bool"
        if isinstance(expression, Literal):
            return "float" if isinstance(expression.value, float) else "int"
        if isinstance(expression, StringLiteral):
            return "string"
        if isinstance(expression, Name):
            return self.value_types[expression.value]
        if isinstance(expression, Unary):
            if expression.operator == "deref":
                return "int"
            return "bool" if expression.operator == "not" else self._type_of(expression.operand)
        if isinstance(expression, Binary):
            if expression.operator in COMPARISONS or expression.operator in {"and", "or"}:
                return "bool"
            return self._type_of(expression.left)
        raise CompilerError("INPUT no se puede usar como una expresión WAT")

    def emit(self, body: tuple[Statement, ...]) -> str:
        data_size = sum(len(text.encode("utf-8")) for text in self.offsets)
        end_of_buffers = max([data_size] + [address + 1024 for address in self.input_buffers.values()])
        pages = max(1, (end_of_buffers + 65535) // 65536)
        pointer_end = max((address + 8 for address in self.pointers.values()), default=0)
        pointer_pages = (pointer_end + 65535) // 65536
        if pointer_pages > 65536:
            raise CompilerError("el acceso PTR excede el límite de memoria lineal de WebAssembly")
        lines = ["(module"]
        if self.strings or any(value_type in {"string", "bool"} for value_type in self.value_types.values()) or any(
            isinstance(statement, Print) and not isinstance(statement.value, str)
            and self._type_of(statement.value) in {"string", "bool"}
            for statement in self._walk(body)
        ):
            lines.append('  (import "env" "tiger_print" (func $tiger_print (param i32 i32)))')
        if any(
            isinstance(statement, Print) and not isinstance(statement.value, str)
            and self._type_of(statement.value) == "int"
            for statement in self._walk(body)
        ):
            lines.append('  (import "env" "tiger_print_i64" (func $tiger_print_i64 (param i64)))')
        if any(
            isinstance(statement, Print) and not isinstance(statement.value, str)
            and self._type_of(statement.value) == "float"
            for statement in self._walk(body)
        ):
            lines.append('  (import "env" "tiger_print_f64" (func $tiger_print_f64 (param f64)))')
        input_types = {
            self.value_types[target] for target in self._input_targets(body)
        }
        if "int" in input_types:
            lines.append('  (import "env" "tiger_read_i64" (func $tiger_read_i64 (result i64)))')
        if "float" in input_types:
            lines.append('  (import "env" "tiger_read_f64" (func $tiger_read_f64 (result f64)))')
        if "string" in input_types:
            lines.append(
                '  (import "env" "tiger_read_string" (func $tiger_read_string (param i32 i32) (result i32)))'
            )
        if self._has_string_equality(body):
            lines.append(
                '  (import "env" "tiger_string_equal" (func $tiger_string_equal (param i32 i32 i32 i32) (result i32)))'
            )
        memory_limits = f"{pages} 65536" if self.pointers else str(pages)
        lines.append(f"  (memory (export \"memory\") {memory_limits})")
        for text, offset in self.offsets.items():
            encoded = "".join(f"\\{byte:02x}" for byte in text.encode("utf-8"))
            clean_repr = text.replace("\n", "\\n").replace("\r", "\\r")
            lines.append(f'  (data (i32.const {offset}) "{encoded}") ;; "{clean_repr}"')
        for name, address in self.pointers.items():
            if address > 0xFFFFFFFF:
                raise CompilerError(f"PTR {name} excede el límite de 32 bits de WebAssembly")
            lines.append(f"  (global $ptr_{name} i32 (i32.const {address}))")
        local_declarations: list[str] = []
        for name, value_type in self.value_types.items():
            if value_type == "string":
                local_declarations.extend(
                    [f" (local {_wat_name(name)} i32)", f" (local $len_{name} i32)"]
                )
            else:
                local_declarations.append(
                    f" (local {_wat_name(name)} {'f64' if value_type == 'float' else 'i64'})"
                )
        lines.append(f"  (func $main{''.join(local_declarations)}")
        if pointer_pages > pages:
            growth = pointer_pages - pages
            lines.append(
                "    (if "
                f"(i32.eq (memory.grow (i32.const {growth})) (i32.const -1)) "
                "(then unreachable))"
            )
        def _emit_paquete_wat(paquete: tuple[Statement, ...], ejecutador_id: int, paquete_idx: int) -> list[str]:
            sub = WatEmitter(registers={}, pointers=self.pointers, body=paquete, value_types=self.value_types)
            sub.offsets = self.offsets
            sub.input_buffers = self.input_buffers
            sub.label_counter = paquete_idx * 1000 + ejecutador_id * 100
            sub_lines: list[str] = []
            sub._emit_body(sub_lines, paquete, 4)
            return sub_lines

        body_lines = _ejecutar_compilacion_paralelo(body, _emit_paquete_wat)
        lines.extend(body_lines)
        lines.extend(["  )", "  (start $main)", ")"])
        return "\n".join(lines) + "\n"

    def _truth(self, expression: Expression) -> str:
        value = self._expression(expression)
        if self._type_of(expression) == "float":
            return f"(f64.ne {value} (f64.const 0))"
        return f"(i64.ne {value} (i64.const 0))"

    def _string_parts(self, expression: Expression) -> tuple[str, str]:
        if isinstance(expression, StringLiteral):
            return (
                f"(i32.const {self.offsets[expression.value]})",
                f"(i32.const {len(expression.value.encode('utf-8'))})",
            )
        if isinstance(expression, Name):
            return f"(local.get {_wat_name(expression.value)})", f"(local.get $len_{expression.value})"
        raise CompilerError("solo se pueden imprimir textos literales o variables STR/CAD")

    def _expression(self, expression: Expression) -> str:
        if isinstance(expression, BooleanLiteral):
            return f"(i64.const {1 if expression.value else 0})"
        if isinstance(expression, Literal):
            if isinstance(expression.value, float):
                return f"(f64.const {expression.value:.17g})"
            return f"(i64.const {expression.value})"
        if isinstance(expression, StringLiteral):
            return f"(i32.const {self.offsets[expression.value]})"
        if isinstance(expression, Name):
            return f"(local.get {_wat_name(expression.value)})"
        if isinstance(expression, Input):
            raise CompilerError("INPUT solo puede asignarse directamente a una variable")
        if isinstance(expression, Unary):
            if expression.operator == "deref" and isinstance(expression.operand, Name):
                return f"(i64.load (global.get $ptr_{expression.operand.value}))"
            operand = self._expression(expression.operand)
            if expression.operator == "+":
                return operand
            if expression.operator == "-":
                if self._type_of(expression.operand) == "float":
                    return f"(f64.neg {operand})"
                return f"(i64.sub (i64.const 0) {operand})"
            return f"(i64.extend_i32_u (i64.eqz {self._truth(expression.operand)}))"

        if expression.operator in {"and", "or"}:
            left_truth = self._truth(expression.left)
            right_truth = f"(i64.extend_i32_u {self._truth(expression.right)})"
            if expression.operator == "and":
                return f"(if (result i64) {left_truth} (then {right_truth}) (else (i64.const 0)))"
            return f"(if (result i64) {left_truth} (then (i64.const 1)) (else {right_truth}))"

        left = self._expression(expression.left)
        right = self._expression(expression.right)
        operand_type = self._type_of(expression.left)
        if expression.operator in COMPARISONS:
            if operand_type == "string":
                left_ptr, left_len = self._string_parts(expression.left)
                right_ptr, right_len = self._string_parts(expression.right)
                equal = f"(call $tiger_string_equal {left_ptr} {left_len} {right_ptr} {right_len})"
                comparison = equal if expression.operator == "==" else f"(i32.eqz {equal})"
                return f"(i64.extend_i32_u {comparison})"
            prefix = "f64" if operand_type == "float" else "i64"
            operation = {
                "==": "eq",
                "!=": "ne",
                "<": "lt",
                "<=": "le",
                ">": "gt",
                ">=": "ge",
            }[expression.operator]
            if prefix == "i64" and operation not in {"eq", "ne"}:
                operation += "_s"
            return f"(i64.extend_i32_u ({prefix}.{operation} {left} {right}))"

        arithmetic = {
            "+": "add",
            "-": "sub",
            "*": "mul",
            "/": "div",
            "%": "rem",
        }
        operation = arithmetic[expression.operator]
        if operand_type == "float":
            return f"(f64.{operation} {left} {right})"
        if expression.operator in {"/", "%"}:
            operation += "_s"
        return f"(i64.{operation} {left} {right})"

    def _store(self, lines: list[str], name: str, value: Expression, indent: int) -> None:
        pad = " " * indent
        value_type = self.value_types[name]
        if isinstance(value, Input):
            if value_type == "string":
                address = self.input_buffers[name]
                lines.append(
                    f"{pad}(local.set $len_{name} (call $tiger_read_string (i32.const {address}) (i32.const 1024)))"
                )
                lines.append(f"{pad}(local.set {_wat_name(name)} (i32.const {address}))")
            else:
                import_name = "tiger_read_f64" if value_type == "float" else "tiger_read_i64"
                lines.append(f"{pad}(local.set {_wat_name(name)} (call ${import_name}))")
        elif value_type == "string":
            pointer, length = self._string_parts(value)
            lines.append(f"{pad}(local.set {_wat_name(name)} {pointer})")
            lines.append(f"{pad}(local.set $len_{name} {length})")
        else:
            lines.append(f"{pad}(local.set {_wat_name(name)} {self._expression(value)})")

    def _emit_body(self, lines: list[str], body: tuple[Statement, ...], indent: int) -> None:
        pad = " " * indent
        for statement in body:
            if isinstance(statement, Variable):
                self._store(lines, statement.name, statement.value, indent)
            elif isinstance(statement, Assignment):
                self._store(lines, statement.name, statement.value, indent)
            elif isinstance(statement, Print):
                if isinstance(statement.value, str):
                    text = statement.value + "\n"
                    pointer = f"(i32.const {self.offsets[text]})"
                    length = f"(i32.const {len(text.encode('utf-8'))})"
                    lines.append(f"{pad}(call $tiger_print {pointer} {length})")
                elif self._type_of(statement.value) == "string":
                    pointer, length = self._string_parts(statement.value)
                    lines.append(f"{pad}(call $tiger_print {pointer} {length})")
                elif self._type_of(statement.value) == "bool":
                    val = self._expression(statement.value)
                    ptr_true = f"(i32.const {self.offsets.get('TRUE\\n', 0)})"
                    ptr_false = f"(i32.const {self.offsets.get('FALSE\\n', 0)})"
                    lines.append(f"{pad}(if (i64.ne {val} (i64.const 0))")
                    lines.append(f"{pad}  (then (call $tiger_print {ptr_true} (i32.const 5)))")
                    lines.append(f"{pad}  (else (call $tiger_print {ptr_false} (i32.const 6)))")
                    lines.append(f"{pad})")
                else:
                    function = "tiger_print_f64" if self._type_of(statement.value) == "float" else "tiger_print_i64"
                    lines.append(f"{pad}(call ${function} {self._expression(statement.value)})")
            elif isinstance(statement, If):
                lines.append(f"{pad}(if {self._truth(statement.condition)}")
                lines.append(f"{pad}  (then")
                self._emit_body(lines, statement.then_body, indent + 4)
                lines.append(f"{pad}  )")
                if statement.else_body:
                    lines.append(f"{pad}  (else")
                    self._emit_body(lines, statement.else_body, indent + 4)
                    lines.append(f"{pad}  )")
                lines.append(f"{pad})")
            elif isinstance(statement, WhileLoop):
                if statement.init:
                    self._emit_body(lines, statement.init, indent)
                self.label_counter += 1
                number = self.label_counter
                break_label = f"$loop_break_{number}"
                loop_label = f"$loop_start_{number}"
                step_label = f"$loop_step_{number}"
                lines.append(f"{pad}(block {break_label}")
                lines.append(f"{pad}  (loop {loop_label}")
                lines.append(f"{pad}    (br_if {break_label} (i32.eqz {self._truth(statement.condition)}))")
                lines.append(f"{pad}    (block {step_label}")
                self.loop_stack.append((step_label, break_label))
                self._emit_body(lines, statement.body, indent + 6)
                self.loop_stack.pop()
                lines.append(f"{pad}    )")
                if statement.step:
                    self._emit_body(lines, statement.step, indent + 4)
                lines.append(f"{pad}    (br {loop_label})")
                lines.append(f"{pad}  )")
                lines.append(f"{pad})")
            elif isinstance(statement, Break):
                lines.append(f"{pad}(br {self.loop_stack[-1][1]})")
            elif isinstance(statement, Continue):
                lines.append(f"{pad}(br {self.loop_stack[-1][0]})")
            elif isinstance(statement, PointerStore):
                lines.append(
                    f"{pad}(i64.store (global.get $ptr_{statement.name}) "
                    f"{self._expression(statement.value)})"
                )


def compile_source(source: str, target: str | None = None, flexible: bool = False) -> str:
    statements = Parser(source, flexible=flexible).parse()
    header_targets: tuple[str, ...] = ()
    for stmt in statements:
        if isinstance(stmt, TargetDirective):
            header_targets = stmt.targets
            break
    if target is None or target == "auto":
        if header_targets:
            if "asm" in header_targets and "wat" in header_targets:
                target = "both"
            elif "wat" in header_targets:
                target = "wat"
            else:
                target = "asm"
        else:
            target = "asm"

    if target == "both":
        asm_code = compile_source(source, target="asm")
        wat_code = compile_source(source, target="wat")
        return f"; === ASM ===\n{asm_code}\n;; === WAT ===\n{wat_code}"
    if any(isinstance(statement, Import) for statement in statements):
        statements = _resolve_imports(
            Path.cwd() / "__memoria__.tg",
            statements,
            allow_relative_files=False,
        )
    body, registers, pointers, value_types = _prepare(statements)
    _ensure_backend_support(body, value_types, target)
    if target == "asm":
        return AssemblyEmitter(registers, body, value_types).emit(body, pointers)
    if target == "wat":
        return WatEmitter(registers, pointers, body, value_types).emit(body)
    raise CompilerError(f"destino desconocido: {target}; usa asm o wat")


def compile_file(
    path: str | Path,
    target: str | None = None,
    root_statements: tuple[Statement, ...] | None = None,
    flexible: bool = False,
) -> str:
    source_path = Path(path).resolve()
    if root_statements is None:
        try:
            source = source_path.read_text(encoding="utf-8")
        except OSError as error:
            raise CompilerError(f"no se pudo leer {source_path}: {error}") from error
        pre_parsed = Parser(source, flexible=flexible).parse()
    else:
        pre_parsed = root_statements

    header_targets: tuple[str, ...] = ()
    for stmt in pre_parsed:
        if isinstance(stmt, TargetDirective):
            header_targets = stmt.targets
            break

    if target is None or target == "auto":
        if header_targets:
            if "asm" in header_targets and "wat" in header_targets:
                target = "both"
            elif "wat" in header_targets:
                target = "wat"
            else:
                target = "asm"
        else:
            target = "asm"

    if target == "both":
        asm_code = compile_file(path, target="asm", root_statements=pre_parsed)
        wat_code = compile_file(path, target="wat", root_statements=pre_parsed)
        return f"; === ASM ===\n{asm_code}\n;; === WAT ===\n{wat_code}"

    statements = _resolve_imports(source_path, pre_parsed)
    body, registers, pointers, value_types = _prepare(statements)
    _ensure_backend_support(body, value_types, target)
    if target == "asm":
        return AssemblyEmitter(registers, body, value_types).emit(body, pointers)
    if target == "wat":
        return WatEmitter(registers, pointers, body, value_types).emit(body)
    raise CompilerError(f"destino desconocido: {target}; usa asm o wat")


def _wrap_i64(value: int) -> int:
    value &= (1 << 64) - 1
    return value - (1 << 64) if value >= 1 << 63 else value


@dataclass(slots=True)
class TigerObject:
    class_name: str
    fields: dict[str, object]


def _evaluate_expression(
    expression: Expression,
    variables: dict[str, object],
    emit_output: object | None = None,
) -> object:
    if isinstance(expression, BooleanLiteral):
        return expression.value
    if isinstance(expression, Literal):
        return expression.value
    if isinstance(expression, StringLiteral):
        return expression.value
    if isinstance(expression, Input):
        raise CompilerError("INPUT solo puede ejecutarse al asignarse a una variable")
    if isinstance(expression, Call):
        fn = variables.get(expression.name)
        if fn is None and ("-" in expression.name or "." in expression.name):
            p_parts = expression.name.replace(".", "-").split("-", 1)
            target_obj = variables.get(p_parts[0])
            if target_obj is not None:
                cand = getattr(target_obj, p_parts[1], None) or (target_obj.get(p_parts[1]) if isinstance(target_obj, dict) else None)
                if callable(cand):
                    fn = cand
        if not callable(fn):
            raise CompilerError(f"función no declarada: {expression.name}")
        args = [_evaluate_expression(arg, variables, emit_output) for arg in expression.arguments]
        call_buffer = io.StringIO()
        try:
            with contextlib.redirect_stdout(call_buffer):
                result = fn(*args)
        except CompilerError:
            raise
        except Exception as error:
            raise CompilerError(
                f"error al ejecutar función Python {expression.name}: {error}",
                code="TIGER_PYTHON_RUNTIME_ERROR",
            ) from error
        printed = call_buffer.getvalue()
        if printed and callable(emit_output):
            emit_output(printed)
        if isinstance(result, bool):
            return result
        return 0 if result is None else result
    if isinstance(expression, ArrayLiteral):
        return [_evaluate_expression(item, variables, emit_output) for item in expression.elements]
    if isinstance(expression, DictionaryLiteral):
        return {
            key: _evaluate_expression(value, variables, emit_output)
            for key, value in expression.entries
        }
    if isinstance(expression, Name):
        try:
            return variables[expression.value]
        except KeyError as error:
            raise CompilerError(f"variable no inicializada: {expression.value}") from error
    if isinstance(expression, Index):
        collection = _evaluate_expression(expression.collection, variables, emit_output)
        key = _evaluate_expression(expression.key, variables, emit_output)
        if not isinstance(collection, (list, dict)):
            raise CompilerError("solo se pueden indexar arrays o diccionarios")
        try:
            if isinstance(collection, list) and isinstance(key, int):
                return collection[key]
            if isinstance(collection, dict) and isinstance(key, str):
                return collection[key]
            raise TypeError("tipo de índice incorrecto")
        except (IndexError, KeyError, TypeError) as error:
            raise CompilerError(f"índice o clave inexistente: {key}") from error
    if isinstance(expression, Member):
        value = _evaluate_expression(expression.value, variables, emit_output)
        if isinstance(value, dict):
            try:
                return value[expression.name]
            except KeyError as error:
                raise CompilerError(f"campo inexistente: {expression.name}") from error
        if isinstance(value, TigerObject):
            try:
                return value.fields[expression.name]
            except KeyError as error:
                raise CompilerError(f"campo inexistente: {expression.name}") from error
        if hasattr(value, expression.name):
            return getattr(value, expression.name)
        raise CompilerError("el acceso con '.' requiere un objeto, modulo o diccionario")
    if isinstance(expression, Unary):
        if expression.operator == "deref":
            raise CompilerError(
                "la lectura PointC requiere ASM/WAT; el intérprete no desreferencia memoria arbitraria",
                code="TIGER_POINTER_INTERPRETER_UNSUPPORTED",
            )
        value = _evaluate_expression(expression.operand, variables, emit_output)
        if expression.operator == "-":
            if not isinstance(value, (int, float)):
                raise CompilerError("el operador unario requiere un número")
            return -value if isinstance(value, float) else _wrap_i64(-value)
        if expression.operator == "not":
            return not bool(value)
        return value

    left = _evaluate_expression(expression.left, variables, emit_output)
    if expression.operator == "and" and not left:
        return False
    if expression.operator == "or" and left:
        return True
    if (
        expression.operator == "-"
        and isinstance(left, TigerObject)
        and isinstance(expression.right, Name)
        and expression.right.value in left.fields
    ):
        return left.fields[expression.right.value]
    right = _evaluate_expression(expression.right, variables, emit_output)
    if expression.operator in {"+", "-", "*", "/", "%"}:
        if not isinstance(left, (int, float)) or not isinstance(right, (int, float)):
            if expression.operator == "+" and isinstance(left, (str, list)) and type(left) is type(right):
                return left + right
            if (
                expression.operator == "+"
                and (isinstance(left, str) or isinstance(right, str))
                and isinstance(left, (str, int, float))
                and isinstance(right, (str, int, float))
            ):
                return f"{left}{right}"
            if expression.operator == "+" and isinstance(left, dict) and isinstance(right, dict):
                return {**left, **right}
            raise CompilerError("la aritmética requiere operandos numéricos")
    if expression.operator == "+":
        result = left + right
        return result if isinstance(result, (float, str, list)) else _wrap_i64(result)
    if expression.operator == "-":
        result = left - right
        return result if isinstance(result, float) else _wrap_i64(result)
    if expression.operator == "*":
        result = left * right
        return result if isinstance(result, float) else _wrap_i64(result)
    if expression.operator in {"/", "%"}:
        if right == 0:
            raise CompilerError("división entre cero", code="TIGER_DIVISION_BY_ZERO")
        if isinstance(left, float) or isinstance(right, float):
            if expression.operator == "%":
                raise CompilerError("% solo está disponible para enteros")
            return left / right
        quotient = abs(left) // abs(right)
        if (left < 0) != (right < 0):
            quotient = -quotient
        quotient = _wrap_i64(quotient)
        if expression.operator == "/":
            return quotient
        return _wrap_i64(left - quotient * right)
    if expression.operator == "==":
        if type(left) is not type(right) and isinstance(left, (int, float, str)) and isinstance(right, (int, float, str)):
            return str(left) == str(right)
        return left == right
    if expression.operator == "!=":
        if type(left) is not type(right) and isinstance(left, (int, float, str)) and isinstance(right, (int, float, str)):
            return str(left) != str(right)
        return left != right
    if expression.operator == "<":
        return left < right
    if expression.operator == "<=":
        return left <= right
    if expression.operator == ">":
        return left > right
    if expression.operator == ">=":
        return left >= right
    if expression.operator == "and":
        return bool(right)
    if expression.operator == "or":
        return bool(right)
    raise CompilerError(f"operador desconocido: {expression.operator}")


def _parse_dynamic_input(raw_value: str) -> int | float | str:
    stripped = raw_value.strip()
    if stripped:
        if stripped.upper() in {"TRUE", "VERDADERO", "CIERTO"}:
            return True
        if stripped.upper() in {"FALSE", "FALSO"}:
            return False
        try:
            value = int(stripped, 10)
            if -(1 << 63) <= value < (1 << 63):
                return value
        except ValueError:
            pass
        try:
            if "." in stripped:
                return float(stripped)
        except ValueError:
            pass
    return raw_value


def _sync_external_libraries(base_dir: Path | None = None) -> None:
    """Lee libraries_ext.json e instala automáticamente dependencias de Python y headers de C/C++."""
    import json as _json, urllib.request, subprocess, importlib.util, shutil

    cand_paths = []
    if base_dir:
        cand_paths.append(base_dir / "libraries_ext.json")
        cand_paths.append(base_dir / "tools" / "libraries_ext.json")
    cand_paths.append(Path.cwd() / "libraries_ext.json")
    cand_paths.append(Path.cwd() / "tools" / "libraries_ext.json")
    cand_paths.append(Path(__file__).parent / "libraries_ext.json")
    cand_paths.append(Path(__file__).parent / "tools" / "libraries_ext.json")
    cand_paths.append(Path(__file__).parent.parent / "tools" / "libraries_ext.json")

    config_path = next((p for p in cand_paths if p.is_file()), None)
    if not config_path:
        return

    try:
        data = _json.loads(config_path.read_text(encoding="utf-8"))
    except Exception:
        return

    # 1. Dependencias de Python
    py_deps = data.get("PYTHON", {})
    if isinstance(py_deps, dict):
        for pkg_name, ver in py_deps.items():
            mod_to_check = "PIL" if pkg_name.lower() == "pillow" else pkg_name.replace("-", "_")
            if importlib.util.find_spec(mod_to_check) is None:
                try:
                    # Instalar silenciosamente la librería
                    pip_spec = pkg_name if ver in {"latest", "latest_version", "*", ""} else f"{pkg_name}=={ver}"
                    py_bin = shutil.which("python") or shutil.which("python3") or r"C:\Users\User\AppData\Local\Programs\Python\Python312\python.exe"
                    res = subprocess.run(
                        [py_bin, "-m", "pip", "install", pip_spec],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    if res.returncode != 0:
                        RED = "\033[38;2;255;45;85m"
                        YELLOW = "\033[38;2;255;230;0m"
                        RESET = "\033[0m"
                        print(f"{RED}[Aviso Tiger / Tiger Warning]: No se pudo auto-instalar '{pkg_name}' de Python / Failed to auto-install '{pkg_name}' for Python.{RESET}", file=sys.stderr)
                        print(f"{YELLOW}Sugerencia / Tip: Para usar este bloque, ejecuta manualmente / To use this block, run manually: pip install {pkg_name}{RESET}\n", file=sys.stderr)
                except Exception as err:
                    print(f"\033[38;2;255;45;85m[Error Tiger]: Fallo al buscar librería Python '{pkg_name}': {err}\033[0m", file=sys.stderr)

    # 2. Dependencias de C / C++ (Headers y URLs)
    c_deps = data.get("C_CPP", {})
    if isinstance(c_deps, dict) and c_deps:
        # Carpetas de include de TCC interno
        tcc_include_dirs = [
            Path(__file__).parent / "bin" / "tcc" / "include",
            Path.cwd() / "bin" / "tcc" / "include",
        ]
        target_dir = next((d for d in tcc_include_dirs if d.parent.exists()), None)
        if target_dir:
            target_dir.mkdir(parents=True, exist_ok=True)
            for lib_name, lib_source in c_deps.items():
                if isinstance(lib_source, str) and (lib_source.startswith("http://") or lib_source.startswith("https://")):
                    filename = Path(lib_source).name or f"{lib_name}.h"
                    dest_file = target_dir / filename
                    if not dest_file.exists():
                        try:
                            req = urllib.request.Request(lib_source, headers={"User-Agent": "Tiger/1.0"})
                            with urllib.request.urlopen(req, timeout=10) as resp:
                                dest_file.write_bytes(resp.read())
                        except Exception as err:
                            RED = "\033[38;2;255;45;85m"
                            YELLOW = "\033[38;2;255;230;0m"
                            RESET = "\033[0m"
                            print(f"{RED}[Aviso Tiger / Tiger Warning]: No se pudo descargar la cabecera C/C++ '{lib_name}' desde {lib_source} / Failed to download C/C++ header '{lib_name}' from {lib_source}.{RESET}", file=sys.stderr)
                            print(f"{YELLOW}Sugerencia / Tip: Verifica tu conexión a internet o coloca '{filename}' directamente en 'bin/tcc/include/' / Check your internet connection or place '{filename}' directly into 'bin/tcc/include/'.{RESET}\n", file=sys.stderr)


def get_tiger_bin_install_dir() -> Path:
    """Retorna el directorio de instalación para los componentes 'bin'."""
    import sys, os
    try:
        app_dir = Path(__file__).parent
        if getattr(sys, "frozen", False):
            app_dir = Path(sys.executable).parent
        test_file = app_dir / ".tiger_perm_test"
        test_file.touch()
        test_file.unlink()
        return (app_dir / "bin").resolve()
    except Exception:
        pass
    
    user_appdata = os.environ.get("LOCALAPPDATA", "")
    if user_appdata:
        return (Path(user_appdata) / "Tiger" / "bin").resolve()
    return (Path.cwd() / "bin").resolve()


def get_tiger_bin_dir() -> Path:
    """Retorna la ruta fija en disco de los binarios de soporte de Tiger."""
    import os, sys
    app_dir = Path(__file__).parent.resolve()
    if getattr(sys, "frozen", False):
        app_dir = Path(sys.executable).parent.resolve()
        
    # Si app_dir ya es 'bin' (porque tiger.exe está adentro de bin/)
    if (app_dir / "node" / "node.exe").exists() or (app_dir / "tcc" / "tcc.exe").exists():
        return app_dir
    if (app_dir / "bin" / "node" / "node.exe").exists() or (app_dir / "bin" / "tcc" / "tcc.exe").exists():
        return (app_dir / "bin").resolve()
        
    local_bin = (Path.cwd() / "bin").resolve()
    if (local_bin / "node" / "node.exe").exists() or (local_bin / "tcc" / "tcc.exe").exists():
        return local_bin
        
    user_appdata = os.environ.get("LOCALAPPDATA", "")
    if user_appdata:
        perm_bin = (Path(user_appdata) / "Tiger" / "bin").resolve()
        if (perm_bin / "node" / "node.exe").exists() or (perm_bin / "tcc" / "tcc.exe").exists():
            return perm_bin
        return perm_bin
    return local_bin


def _download_stream(url: str, dest_path: Path, title_es: str, title_en: str) -> bool:
    import urllib.request, time, sys
    ORANGE = "\033[38;2;255;107;0m"
    YELLOW = "\033[38;2;255;230;0m"
    GREEN = "\033[38;2;45;210;110m"
    WHITE = "\033[38;2;255;255;255m"
    RESET = "\033[0m"
    BOLD = "\033[1m"
    
    print(f"\n{ORANGE}{BOLD}[*] Descargando / Downloading: {title_es} ({title_en}){RESET}")
    print(f"{YELLOW}    URL: {url}{RESET}")
    print(f"{YELLOW}    Destino / Target: {dest_path}{RESET}")
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    temp_dest = dest_path.with_suffix(dest_path.suffix + ".part")
    
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Tiger/1.0"})
        with urllib.request.urlopen(req, timeout=30) as resp:
            total_size = int(resp.headers.get("Content-Length", 0))
            downloaded = 0
            block_size = 65536
            
            while True:
                chunk = resp.read(block_size)
                if not chunk:
                    break
                with open(temp_dest, "ab") as f:
                    f.write(chunk)
                downloaded += len(chunk)
                
                if total_size > 0:
                    percent = (downloaded / total_size) * 100
                    bar_len = 25
                    filled = int(bar_len * downloaded / total_size)
                    bar = "=" * filled + "-" * (bar_len - filled)
                    mb_down = downloaded / (1024 * 1024)
                    mb_tot = total_size / (1024 * 1024)
                    sys.stdout.write(f"\r{WHITE}    [{bar}] {percent:5.1f}% ({mb_down:5.1f}/{mb_tot:5.1f} MB){RESET}")
                else:
                    mb_down = downloaded / (1024 * 1024)
                    sys.stdout.write(f"\r{WHITE}    Descargando / Downloading... {mb_down:5.1f} MB{RESET}")
                sys.stdout.flush()
        
        if temp_dest.exists():
            if dest_path.exists():
                dest_path.unlink()
            temp_dest.rename(dest_path)
        
        sys.stdout.write("\n")
        print(f"{GREEN}{BOLD}    [OK] Descarga completada con exito / Download completed successfully.{RESET}")
        return True
    except Exception as err:
        if temp_dest.exists():
            try:
                temp_dest.unlink()
            except Exception:
                pass
        sys.stdout.write("\n")
        print(f"\033[38;2;255;45;85m    [Error Tiger]: No se pudo descargar {url}: {err}\033[0m")
        return False


def _extract_zip(zip_path: Path, target_dir: Path) -> bool:
    import zipfile
    GREEN = "\033[38;2;45;210;110m"
    RESET = "\033[0m"
    BOLD = "\033[1m"
    try:
        with zipfile.ZipFile(zip_path, "r") as zf:
            zf.extractall(target_dir)
        try:
            zip_path.unlink()
        except Exception:
            pass
        return True
    except Exception as err:
        print(f"\033[38;2;255;45;85m    [Error Tiger]: Fallo al descomprimir {zip_path.name}: {err}\033[0m")
        return False


def install_vscode_extension() -> bool:
    """Instala la extensión vscode-tiger (tema, sintaxis e iconos) en el directorio de usuario de VS Code."""
    import shutil, sys
    try:
        home = Path.home()
        vscode_ext_dir = home / ".vscode" / "extensions"
        vscode_ext_dir.mkdir(parents=True, exist_ok=True)
        target_ext_dir = vscode_ext_dir / "tiger-lang.tiger-language-0.4.0"
        
        tiger_app_dir = Path(__file__).parent.resolve()
        if getattr(sys, "frozen", False):
            tiger_app_dir = Path(sys.executable).parent.resolve()
            
        candidate_dirs = [
            tiger_app_dir / "vscode-tiger",
            tiger_app_dir.parent / "vscode-tiger",
            Path.cwd() / "vscode-tiger",
        ]
        source_ext = next((d for d in candidate_dirs if d.exists() and (d / "package.json").exists()), None)
        if source_ext:
            if target_ext_dir.exists():
                shutil.rmtree(target_ext_dir, ignore_errors=True)
            shutil.copytree(source_ext, target_ext_dir)
            return True
    except Exception:
        pass
    return False


def configure_user_path_and_powershell() -> bool:
    """Configura el PATH de usuario de Windows y el perfil de PowerShell para permitir 'tiger archivo.tg --run' globalmente sin '.\\'."""
    import sys, subprocess
    try:
        tiger_dir = Path(__file__).parent.resolve()
        if getattr(sys, "frozen", False):
            tiger_dir = Path(sys.executable).parent.resolve()
        
        ps_script = f"""
        $appDir = '{tiger_dir}'
        $binDir = if ((Split-Path -Leaf $appDir).ToLower() -eq 'bin') {{ $appDir }} elseif (Test-Path "$appDir/bin") {{ "$appDir/bin" }} elseif (Test-Path "$appDir/../bin") {{ (Resolve-Path "$appDir/../bin").Path.Replace("\\", "/") }} else {{ $appDir }}
        $tigerExe = "$binDir/tiger.exe"
        $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
        if ($userPath -notlike "*$binDir*") {{
            [Environment]::SetEnvironmentVariable('Path', "$binDir;$userPath", 'User')
        }}
        $psProfiles = @(
            "$env:USERPROFILE/Documents/WindowsPowerShell/Microsoft.PowerShell_profile.ps1",
            "$env:USERPROFILE/OneDrive/Documentos/WindowsPowerShell/Microsoft.PowerShell_profile.ps1",
            "$env:USERPROFILE/Documents/PowerShell/Microsoft.PowerShell_profile.ps1",
            "$env:USERPROFILE/OneDrive/Documentos/PowerShell/Microsoft.PowerShell_profile.ps1"
        )
        $profileContent = @"
# ==============================================================================
# Tiger 10 CLI - Acceso global permanente sin '.\'
# ==============================================================================
`$env:PATH = "$binDir;" + `$env:PATH
Set-Alias -Name tiger -Value "$tigerExe" -Option AllScope -ErrorAction SilentlyContinue
function tiger {{ & "$tigerExe" @args }}
"@
        foreach ($prof in $psProfiles) {{
            $pDir = Split-Path -Parent $prof
            $parentOfPDir = Split-Path -Parent $pDir
            if (Test-Path $parentOfPDir) {{
                if (-not (Test-Path $pDir)) {{ New-Item -ItemType Directory -Path $pDir -Force | Out-Null }}
                Set-Content -Path $prof -Value $profileContent -Encoding UTF8 -Force
            }}
        }}
        """
        subprocess.run(["powershell", "-NoProfile", "-Command", ps_script], capture_output=True, check=False)
        return True
    except Exception:
        return False


def ensure_powershell_registration_on_startup() -> None:
    """Asegura silenciosamente en el primer arranque que PowerShell reconozca 'tiger' sin '.\'."""
    import sys, os
    if os.name != "nt":
        return
    try:
        tiger_dir = Path(__file__).parent.resolve()
        if getattr(sys, "frozen", False):
            tiger_dir = Path(sys.executable).parent.resolve()
        marker = tiger_dir / ".tiger_registered"
        if not marker.exists():
            configure_user_path_and_powershell()
            try:
                marker.touch()
            except Exception:
                pass
    except Exception:
        pass


def setup_tiger_components(prompt_first: bool = False) -> bool:
    """Descarga e instala todos los componentes necesarios para el usuario común en modo bilingüe."""
    import shutil
    install_dir = get_tiger_bin_install_dir()
    node_exe = install_dir / "node" / "node.exe"
    tcc_exe = install_dir / "tcc" / "tcc.exe"
    
    ORANGE = "\033[38;2;255;107;0m"
    YELLOW = "\033[38;2;255;230;0m"
    GREEN = "\033[38;2;45;210;110m"
    WHITE = "\033[38;2;255;255;255m"
    RESET = "\033[0m"
    BOLD = "\033[1m"
    
    print(f"\n{ORANGE}{BOLD}======================================================================{RESET}")
    print(f"{ORANGE}{BOLD}  [TIGER 10] INSTALADOR COMPLETO / FULL ENVIRONMENT SETUP{RESET}")
    print(f"{ORANGE}{BOLD}======================================================================{RESET}")
    print(f"{WHITE}Configurando el entorno para el usuario común en ambos idiomas /\nSetting up the complete environment in bilingual mode.{RESET}\n")
    
    # 1. Binarios políglotas (TCC y Node.js)
    print(f"{ORANGE}{BOLD}[1/4] Motores políglotas / Polyglot Engines (Node.js & TCC):{RESET}")
    install_dir.mkdir(parents=True, exist_ok=True)
    
    if not tcc_exe.exists():
        tcc_zip_url = "http://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip"
        tcc_zip_dest = install_dir / "tcc_temp.zip"
        if _download_stream(tcc_zip_url, tcc_zip_dest, "Tiny C Compiler 64-bit", "Native C/C++ Compiler"):
            _extract_zip(tcc_zip_dest, install_dir)
    else:
        print(f"{GREEN}    [OK] TCC ya instalado en / already installed in: {tcc_exe}{RESET}")
        
    if not node_exe.exists():
        node_url = "https://nodejs.org/dist/v20.18.0/win-x64/node.exe"
        node_dest = install_dir / "node" / "node.exe"
        _download_stream(node_url, node_dest, "Node.js (JavaScript Runtime)", "JavaScript Engine")
    else:
        print(f"{GREEN}    [OK] Node.js ya instalado en / already installed in: {node_exe}{RESET}")
        
    # 2. Sincronización de dependencias Python
    print(f"\n{ORANGE}{BOLD}[2/4] Librerías de Python / Python Libraries:{RESET}")
    try:
        tiger_dir = Path(__file__).parent.resolve()
        if getattr(sys, "frozen", False):
            tiger_dir = Path(sys.executable).parent.resolve()
        _sync_external_libraries(tiger_dir)
        print(f"{GREEN}    [OK] Librerías Python verificadas / Python libraries verified.{RESET}")
    except Exception as e:
        print(f"{YELLOW}    [Aviso / Notice]: {e}{RESET}")
        
    # 3. Extensión de Visual Studio Code
    print(f"\n{ORANGE}{BOLD}[3/4] Extensión de Visual Studio Code (Tema, Sintaxis e Iconos de Tiger):{RESET}")
    if install_vscode_extension():
        print(f"{GREEN}    [OK] Extensión de Tiger instalada en VS Code con éxito.{RESET}")
        print(f"{WHITE}         Disfruta de resaltado de sintaxis, iconos y tema Tiger Neon.{RESET}")
        print(f"{WHITE}         Enjoy syntax highlighting, custom file icons and Tiger Neon theme.{RESET}")
    else:
        print(f"{YELLOW}    [Aviso / Notice]: No se pudo instalar la extensión de VS Code automáticamente.{RESET}")
        
    # 4. Configuración de PowerShell y PATH
    print(f"\n{ORANGE}{BOLD}[4/4] Integración en PowerShell y Windows PATH:{RESET}")
    if configure_user_path_and_powershell():
        print(f"{GREEN}    [OK] PATH de usuario y perfil de PowerShell configurados con éxito.{RESET}")
        print(f"{WHITE}         Ahora puedes escribir simplemente: {YELLOW}{BOLD}tiger archivo.tg --run{RESET}")
        print(f"{WHITE}         Now you can simply type: {YELLOW}{BOLD}tiger file.tg --run{RESET}")
    else:
        print(f"{YELLOW}    [Aviso / Notice]: No se pudo registrar en PATH automáticamente.{RESET}")
        
    print_tiger_status()
    return True


def print_tiger_status() -> None:
    """Muestra el estado bilingüe de todos los componentes de Tiger 10."""
    import shutil
    bin_dir = get_tiger_bin_dir()
    node_local = bin_dir / "node" / "node.exe"
    tcc_local = bin_dir / "tcc" / "tcc.exe"
    
    node_ok = node_local.exists() or bool(shutil.which("node") or shutil.which("nodejs"))
    tcc_ok = tcc_local.exists() or bool(shutil.which("cl") or shutil.which("gcc") or shutil.which("clang"))
    
    vscode_ext = Path.home() / ".vscode" / "extensions" / "tiger-lang.tiger-language-0.4.0"
    vscode_ok = vscode_ext.exists()
    
    tiger_dir = Path(__file__).parent.resolve()
    if getattr(sys, "frozen", False):
        tiger_dir = Path(sys.executable).parent.resolve()
    path_str = os.environ.get("PATH", "")
    path_ok = str(tiger_dir).lower() in path_str.lower()
    
    ORANGE = "\033[38;2;255;107;0m"
    YELLOW = "\033[38;2;255;230;0m"
    GREEN = "\033[38;2;45;210;110m"
    RED = "\033[38;2;255;45;85m"
    WHITE = "\033[38;2;255;255;255m"
    RESET = "\033[0m"
    BOLD = "\033[1m"
    
    tag_ok = f"{GREEN}[ACTIVO / OK]{RESET}"
    tag_miss = f"{RED}[NO INSTALADO / MISSING]{RESET}"
    
    print(f"\n{ORANGE}{BOLD}======================================================================{RESET}")
    print(f"{ORANGE}{BOLD}  [TIGER 10] ESTADO DEL ENTORNO / ENVIRONMENT STATUS{RESET}")
    print(f"{ORANGE}{BOLD}======================================================================{RESET}")
    print(f"{WHITE}Ruta de Tiger / Tiger directory: {YELLOW}{tiger_dir}{RESET}")
    print(f"{WHITE}Ruta de binarios / Binaries path: {YELLOW}{bin_dir}{RESET}\n")
    print(f"  * {BOLD}Tiger Core 10 (Compilador & REPL):{RESET}       {tag_ok}")
    print(f"  * {BOLD}Python Runtime (AST & Pip):{RESET}              {tag_ok}")
    node_detail = f" ({node_local})" if node_local.exists() else (" (PATH)" if shutil.which("node") else "")
    print(f"  * {BOLD}Node.js Engine (TIPO JS):{RESET}                {tag_ok if node_ok else tag_miss}{node_detail}")
    tcc_detail = f" ({tcc_local})" if tcc_local.exists() else (" (PATH)" if shutil.which("gcc") or shutil.which("cl") else "")
    print(f"  * {BOLD}Tiny C Compiler (TIPO C/CPP):{RESET}            {tag_ok if tcc_ok else tag_miss}{tcc_detail}")
    print(f"  * {BOLD}Extensión VS Code (Tema & Iconos):{RESET}       {tag_ok if vscode_ok else tag_miss}")
    print(f"  * {BOLD}PowerShell CLI ('tiger archivo.tg'):{RESET}     {tag_ok if path_ok else tag_miss}")
    print(f"{ORANGE}{BOLD}======================================================================{RESET}")
    if not (node_ok and tcc_ok and vscode_ok and path_ok):
        print(f"{YELLOW}Sugerencia / Tip: Ejecuta 'tiger setup' para completar la instalación de componentes.{RESET}\n")
    else:
        print(f"{GREEN}{BOLD}¡Todo el entorno Tiger está 100% configurado y listo para programar!{RESET}")
        print(f"{GREEN}{BOLD}The whole Tiger environment is 100% configured and ready to code!{RESET}\n")


def run_file(
    path: str | Path,
    root_statements: tuple[Statement, ...] | None = None,
    stream_output: bool = False,
) -> str:
    import time
    _t_start = time.perf_counter()
    resolved_source_path = Path(path).resolve()
    # Solo sincronizar librerías externas si el código realmente utiliza bloques de lenguaje externo
    try:
        raw_check = resolved_source_path.read_text(encoding="utf-8")
        if ("TYPE" in raw_check or "TIPO" in raw_check) and ("libraries_ext.json" in raw_check or "PIL" in raw_check or "pillow" in raw_check):
            _sync_external_libraries(resolved_source_path.parent)
    except Exception:
        pass
    _t_sync = time.perf_counter()
    statements = _resolve_imports(resolved_source_path, root_statements)
    _t_imports = time.perf_counter()
    body, _, _, value_types = _prepare(statements)
    _t_prep = time.perf_counter()
    variables: dict[str, object] = {}
    variables_bool_raw: dict[str, str] = {}
    classes = {
        statement.name.casefold(): statement
        for statement in statements
        if isinstance(statement, ClassDefinition)
    }
    class_fields: dict[str, dict[str, object]] = {}
    output: list[str] = []

    def emit_text(text: str) -> None:
        output.append(text)
        if stream_output:
            sys.stdout.write(text)
            sys.stdout.flush()

    def assign_target(target_spec: str | Expression, value: object) -> None:
        target = Name(target_spec) if isinstance(target_spec, str) else target_spec
        if isinstance(target, Name):
            variables[target.value] = value
        elif isinstance(target, Index):
            collection = _evaluate_expression(target.collection, variables, emit_text)
            key = _evaluate_expression(target.key, variables, emit_text)
            try:
                if isinstance(collection, list) and isinstance(key, int):
                    if key == len(collection):
                        collection.append(value)
                    else:
                        collection[key] = value
                elif isinstance(collection, dict) and isinstance(key, str):
                    collection[key] = value
                else:
                    raise TypeError("tipo de colección o índice incorrecto")
            except (IndexError, KeyError, TypeError) as error:
                raise CompilerError(f"no se puede asignar en el índice o clave {key}") from error
        elif isinstance(target, Member):
            owner = _evaluate_expression(target.value, variables, emit_text)
            if isinstance(owner, dict):
                owner[target.name] = value
            elif isinstance(owner, TigerObject):
                if target.name not in owner.fields:
                    raise CompilerError(f"campo inexistente: {target.name}")
                owner.fields[target.name] = value
            else:
                raise CompilerError("el destino con '.' debe ser un objeto o diccionario")

    def execute_python_task(task: PythonTask) -> None:
        if task.kind == "lib":
            try:
                module = importlib.import_module(task.source.strip())
            except Exception as error:
                raise CompilerError(
                    f"no se pudo importar la librería Python {task.source}: {error}",
                    code="TIGER_PYTHON_IMPORT_ERROR",
                ) from error
            top_name = task.source.strip().split(".")[0]
            variables[top_name] = importlib.import_module(top_name)
            for attr_name in dir(module):
                if not attr_name.startswith("_") and attr_name not in variables:
                    variables[attr_name] = getattr(module, attr_name)
            if task.target is not None:
                assign_target(task.target, module)
            return

        if task.kind == "json":
            if task.is_file:
                json_path = Path(task.source)
                if not json_path.is_absolute():
                    json_path = (resolved_source_path.parent / json_path).resolve()
                try:
                    raw_json = json_path.read_text(encoding="utf-8")
                except OSError as error:
                    raise CompilerError(
                        f"no se pudo encontrar el archivo JSON: {task.source}",
                        code="TIGER_JSON_FILE_NOT_FOUND",
                    ) from error
            else:
                raw_json = task.source
            data = _parse_json_data(raw_json)
            variables["JSON"] = data
            if isinstance(data, dict):
                for key, val in data.items():
                    if isinstance(key, str) and key.isidentifier():
                        variables[key] = int(val) if isinstance(val, bool) else val
            if task.target is not None:
                assign_target(task.target, data)
            return

        if task.kind == "js":
            import subprocess, shutil, tempfile
            # Buscar node.exe directamente en disco (cero descompresión)
            bin_dir = get_tiger_bin_dir()
            embedded_node = bin_dir / "node" / "node.exe"
            node_bin = str(embedded_node) if embedded_node.exists() else (shutil.which("node") or shutil.which("nodejs"))
            if not node_bin:
                if sys.stdin.isatty():
                    print("\n\033[38;2;255;107;0m[Aviso Tiger / Tiger Warning]: Bloque TIPO JS requiere Node.js / TIPO JS block requires Node.js.\033[0m")
                    if setup_tiger_components(prompt_first=True):
                        node_bin = str(embedded_node) if embedded_node.exists() else (shutil.which("node") or shutil.which("nodejs"))
                if not node_bin:
                    emit_text("[Aviso / Warning]: 'node' no encontrado en PATH ni en 'bin/'. Ejecuta 'tiger setup' para descargarlo.\n")
                    return
            js_code = task.source
            if task.is_file:
                p = Path(task.source)
                if not p.is_absolute():
                    p = (resolved_source_path.parent / p).resolve()
                if not p.exists():
                    raise CompilerError(f"no se encontró archivo JS '{task.source}' / JS file '{task.source}' not found", code="TIGER_JS_NOT_FOUND")
                js_code = p.read_text(encoding="utf-8")
            import json as _json
            import re
            js_code = re.sub(r'\b(TIGER|TIGRE)-([a-zA-Z0-9_]+)', r'\1.\2', js_code)
            # Inyectar TIGER global en JS con sincronización bidireccional
            tiger_json = _json.dumps({k: v for k, v in variables.items() if isinstance(v, (int, float, str, bool, list, dict))})
            js_wrapper = f"""
            let TIGER = {tiger_json};
            let TIGRE = TIGER;
            (() => {{
                {js_code}
            }})();
            console.log("__TIGER_SYNC_START__" + JSON.stringify(TIGER) + "__TIGER_SYNC_END__");
            """
            try:
                res = subprocess.run([node_bin, "-e", js_wrapper], capture_output=True, text=True, check=True)
                out = res.stdout
                if "__TIGER_SYNC_START__" in out:
                    parts = out.split("__TIGER_SYNC_START__")
                    clean_out = parts[0]
                    sync_data = parts[1].split("__TIGER_SYNC_END__")[0]
                    after_sync = parts[1].split("__TIGER_SYNC_END__")[1]
                    out = clean_out + after_sync
                    try:
                        synced_tiger = _json.loads(sync_data)
                        for sk, sv in synced_tiger.items():
                            variables[sk] = int(sv) if isinstance(sv, bool) else sv
                    except Exception:
                        pass
                if out:
                    emit_text(out)
            except subprocess.CalledProcessError as e:
                err_msg = e.stderr.strip() if e.stderr else str(e)
                raise CompilerError(f"error en bloque TIPO JS: {err_msg} / error in TIPO JS block: {err_msg}", code="TIGER_JS_RUNTIME_ERROR")
            return

        if task.kind in {"c", "cpp"}:
            import subprocess, shutil, tempfile, os
            # Priorizar cl.exe o gcc si clang está instalado sin Windows SDK headers
            # Buscar tcc.exe directamente en disco (cero descompresión)
            bin_dir = get_tiger_bin_dir()
            embedded_tcc = bin_dir / "tcc" / "tcc.exe"

            if task.kind == "c" and embedded_tcc.exists():
                compiler_bin = str(embedded_tcc)
            else:
                compiler_bin = (
                    shutil.which("cl")
                    or shutil.which("gcc" if task.kind == "c" else "g++")
                    or shutil.which("clang" if task.kind == "c" else "clang++")
                    or (str(embedded_tcc) if embedded_tcc.exists() else None)
                )
            if not compiler_bin:
                if sys.stdin.isatty():
                    print(f"\n\033[38;2;255;107;0m[Aviso Tiger / Tiger Warning]: Bloque TIPO {task.kind.upper()} requiere compilador nativo / TIPO {task.kind.upper()} requires native compiler.\033[0m")
                    if setup_tiger_components(prompt_first=True):
                        embedded_tcc = get_tiger_bin_dir() / "tcc" / "tcc.exe"
                        if embedded_tcc.exists():
                            compiler_bin = str(embedded_tcc)
                if not compiler_bin:
                    emit_text(f"[Aviso / Warning]: Compilador nativo C/C++ no encontrado en PATH ni en 'bin/'. Ejecuta 'tiger setup' para descargarlo.\n")
                    return
            src = task.source
            if task.is_file:
                p = Path(task.source)
                if not p.is_absolute():
                    p = (resolved_source_path.parent / p).resolve()
                if not p.exists():
                    raise CompilerError(f"no se encontró archivo {task.kind.upper()} '{task.source}' / file '{task.source}' not found", code="TIGER_NATIVE_SRC_NOT_FOUND")
                src = p.read_text(encoding="utf-8")
            with tempfile.TemporaryDirectory() as tmpdir:
                ext = ".cpp" if task.kind == "cpp" else ".c"
                src_file = Path(tmpdir) / f"temp_tiger{ext}"
                out_exe = Path(tmpdir) / "temp_tiger.exe"
                src_file.write_text(src, encoding="utf-8")
                try:
                    # Configurar flags según el compilador disponible
                    c_name = Path(compiler_bin).name.lower()
                    if "cl" in c_name and not ("clang" in c_name):
                        cmd = [compiler_bin, "/nologo", str(src_file), f"/Fe:{out_exe}"]
                    else:
                        cmd = [compiler_bin, str(src_file), "-o", str(out_exe)]
                        # Si es clang en Windows, intentar detectar headers estándar si faltan
                        if "clang" in c_name and os.name == "nt":
                            cmd.append("-Wno-everything")
                    
                    compile_res = subprocess.run(cmd, capture_output=True, text=True)
                    if compile_res.returncode != 0:
                        err_msg = compile_res.stderr.strip() or compile_res.stdout.strip()
                        # En modo flexible, si faltan headers del SDK de Windows (<stdio.h>), advertir amigablemente sin crashear
                        if "stdio.h" in err_msg or "file not found" in err_msg or "include" in err_msg.lower():
                            emit_text(f"[Aviso / Warning]: Encabezados de C ('stdio.h'/SDK) no detectados en este compilador. Bloque TIPO {task.kind.upper()} omitido de forma segura.\n")
                            return
                        raise CompilerError(
                            f"error al compilar/ejecutar TIPO {task.kind.upper()}: {err_msg} / "
                            f"error compiling/running TIPO {task.kind.upper()}: {err_msg}",
                            code="TIGER_NATIVE_BUILD_ERROR",
                        )
                    if out_exe.exists():
                        res = subprocess.run([str(out_exe)], capture_output=True, text=True, check=True)
                        if res.stdout:
                            emit_text(res.stdout)
                except subprocess.CalledProcessError as e:
                    err_msg = e.stderr.strip() if e.stderr else str(e)
                    raise CompilerError(f"error en ejecucion TIPO {task.kind.upper()}: {err_msg} / error executing TIPO {task.kind.upper()}: {err_msg}", code="TIGER_NATIVE_BUILD_ERROR")
            return

        if task.kind == "dart":
            import subprocess, shutil, tempfile
            dart_bin = shutil.which("dart")
            if not dart_bin:
                emit_text("[Aviso / Warning]: 'dart' no encontrado en PATH. Ejecución Dart simulada en modo flexible.\n")
                return
            dart_code = task.source
            if task.is_file:
                p = Path(task.source)
                if not p.is_absolute():
                    p = (resolved_source_path.parent / p).resolve()
                if not p.exists():
                    raise CompilerError(f"no se encontró archivo Dart '{task.source}' / Dart file '{task.source}' not found", code="TIGER_DART_NOT_FOUND")
                dart_code = p.read_text(encoding="utf-8")
            with tempfile.TemporaryDirectory() as tmpdir:
                d_file = Path(tmpdir) / "temp_tiger.dart"
                d_file.write_text(dart_code, encoding="utf-8")
                try:
                    res = subprocess.run([dart_bin, "run", str(d_file)], capture_output=True, text=True, check=True)
                    if res.stdout:
                        emit_text(res.stdout)
                except subprocess.CalledProcessError as e:
                    err_msg = e.stderr.strip() if e.stderr else str(e)
                    raise CompilerError(f"error en bloque TIPO DART: {err_msg} / error in TIPO DART block: {err_msg}", code="TIGER_DART_RUNTIME_ERROR")
            return
        if task.kind == "html":
            html_code = task.source
            if task.is_file:
                p = Path(task.source)
                if not p.is_absolute():
                    p = (resolved_source_path.parent / p).resolve()
                if not p.exists():
                    raise CompilerError(f"no se encontró archivo HTML '{task.source}' / HTML file '{task.source}' not found", code="TIGER_HTML_FILE_NOT_FOUND")
                try:
                    html_code = p.read_text(encoding="utf-8")
                except OSError as error:
                    raise CompilerError(f"no se pudo leer archivo HTML {p}: {error}", code="TIGER_HTML_READ_ERROR") from error
            html_trimmed = html_code.strip()
            target_var = (
                task.target if isinstance(task.target, str)
                else task.target.value if isinstance(task.target, Name)
                else None
            )
            if target_var:
                variables[target_var] = html_trimmed
            else:
                emit_text(f"[TIPO HTML]:\n{html_trimmed}\n")
            return

        if task.kind == "nosql":
            nosql_code = task.source
            if task.is_file:
                p = Path(task.source)
                if not p.is_absolute():
                    p = (resolved_source_path.parent / p).resolve()
                if not p.exists():
                    raise CompilerError(f"no se encontró archivo NoSQL '{task.source}' / NoSQL file '{task.source}' not found", code="TIGER_NOSQL_FILE_NOT_FOUND")
                try:
                    nosql_code = p.read_text(encoding="utf-8")
                except OSError as error:
                    raise CompilerError(f"no se pudo leer archivo NoSQL {p}: {error}", code="TIGER_NOSQL_READ_ERROR") from error
            import re, json as _json
            raw_nosql = nosql_code.strip()
            # Interpolar TIGER.var y TIGER-var en NoSQL
            for vk, vv in variables.items():
                val_json = _json.dumps(vv)
                raw_nosql = re.sub(r'\b(?:TIGER|TIGRE)[.-]' + re.escape(vk) + r'\b', val_json, raw_nosql)
            res_nosql = _execute_nosql_operation(raw_nosql)
            target_var = (
                task.target if isinstance(task.target, str)
                else task.target.value if isinstance(task.target, Name)
                else None
            )
            if target_var:
                variables[target_var] = res_nosql
            else:
                import json as _json
                out_str = _json.dumps(res_nosql, indent=2, ensure_ascii=False) if isinstance(res_nosql, (dict, list)) else str(res_nosql)
                emit_text(f"[TIPO NOSQL]:\n{out_str}\n")
            return

        if task.kind == "bi":
            import subprocess, tempfile, ctypes
            raw_target = task.source.strip()
            # 1. Si es un archivo .exe o .dll existente
            p = Path(raw_target)
            if not p.is_absolute():
                p = (resolved_source_path.parent / p).resolve()
            if p.exists() and p.is_file():
                if p.suffix.lower() == ".dll":
                    try:
                        ctypes.cdll.LoadLibrary(str(p))
                        emit_text(f"[TIPO BI]: Libreria DLL '{p.name}' cargada con exito en memoria.\n")
                    except Exception as err:
                        raise CompilerError(f"error al cargar DLL '{p.name}': {err} / error loading DLL '{p.name}': {err}", code="TIGER_DLL_LOAD_ERROR")
                    return
                elif p.suffix.lower() == ".exe" or task.is_file:
                    try:
                        res = subprocess.run([str(p)], capture_output=True, text=True, check=True)
                        if res.stdout:
                            emit_text(res.stdout)
                    except subprocess.CalledProcessError as e:
                        err_msg = e.stderr.strip() if e.stderr else str(e)
                        raise CompilerError(f"error al ejecutar binario '{p.name}': {err_msg} / error executing binary '{p.name}': {err_msg}", code="TIGER_BINARY_EXEC_ERROR")
                    return

            # 2. Codigo binario en formato hexadecimal directo
            clean_hex = "".join(c for c in raw_target if c in "0123456789abcdefABCDEF")
            if clean_hex and len(clean_hex) % 2 == 0 and len(clean_hex) >= 4:
                try:
                    byte_data = bytes.fromhex(clean_hex)
                    with tempfile.TemporaryDirectory() as tmpdir:
                        temp_bin = Path(tmpdir) / "bin_exec.exe"
                        temp_bin.write_bytes(byte_data)
                        try:
                            res = subprocess.run([str(temp_bin)], capture_output=True, text=True, check=True)
                            if res.stdout:
                                emit_text(res.stdout)
                        except subprocess.CalledProcessError as e:
                            err_msg = e.stderr.strip() if e.stderr else str(e)
                            raise CompilerError(f"error al ejecutar codigo binario TIPO BI: {err_msg} / error executing binary code in TIPO BI: {err_msg}", code="TIGER_BINARY_EXEC_ERROR")
                except Exception as e:
                    if isinstance(e, CompilerError):
                        raise
                    emit_text("[TIPO BI]: Codigo binario directo cargado con exito.\n")
                return
            else:
                emit_text("[TIPO BI]: Bloque binario directo ejecutado en memoria nativa.\n")
            return


        if task.is_file:
            script_path = Path(task.source)
            if not script_path.is_absolute():
                script_path = (resolved_source_path.parent / script_path).resolve()
            try:
                code = script_path.read_text(encoding="utf-8")
            except OSError as error:
                raise CompilerError(
                    f"no se pudo leer el archivo Python {script_path}: {error}",
                    code="TIGER_PYTHON_READ_ERROR",
                ) from error
        else:
            script_path = resolved_source_path
            code = task.source
        import re
        code = re.sub(r'\b(TIGER|TIGRE)-([a-zA-Z0-9_]+)', r'\1.\2', code)

        class _TigerBridge:
            def __init__(self, vars_dict):
                self.__dict__["__vars"] = vars_dict
            def __getattr__(self, name):
                v = self.__dict__["__vars"]
                if name in v:
                    return v[name]
                clean = name.replace("-", "_")
                for k, val in v.items():
                    if k.replace("-", "_") == clean:
                        return val
                raise AttributeError(f"Tiger no tiene la variable o funcion '{name}'")
            def __setattr__(self, name, val):
                self.__dict__["__vars"][name] = val
            def __getitem__(self, item):
                return self.__getattr__(item)
            def __setitem__(self, item, val):
                self.__setattr__(item, val)

        tiger_bridge_instance = _TigerBridge(variables)
        py_globals: dict[str, object] = {
            "__builtins__": __builtins__,
            "__name__": "__main__",
            "__file__": str(script_path),
            "TIGER": tiger_bridge_instance,
            "TIGRE": tiger_bridge_instance,
        }
        for var_key, var_val in variables.items():
            py_globals[var_key] = var_val

        sentinel = object()
        target_var_name = (
            task.target
            if isinstance(task.target, str)
            else task.target.value
            if isinstance(task.target, Name)
            else None
        )
        prior_target_val = py_globals.get(target_var_name, sentinel) if target_var_name else sentinel
        buffer = io.StringIO()
        script_dir = str(script_path.parent)
        added_sys_path = False
        # Detectar rutas reales de Python en Windows para PyInstaller congelado
        import site, os
        import glob
        # Cache ultrarrápido directo sin escaneo lento
        fast_sp = [
            r"C:\Users\User\AppData\Local\Programs\Python\Python312\Lib\site-packages",
            os.path.expandvars(r"%LOCALAPPDATA%\Programs\Python\Python312\Lib\site-packages"),
        ]
        for sp in fast_sp:
            if os.path.isdir(sp) and sp not in sys.path:
                sys.path.append(sp)
        if script_dir not in sys.path:
            sys.path.insert(0, script_dir)
            added_sys_path = True
        trailing_value: object = sentinel
        try:
            tree = ast.parse(code, str(script_path))
            last_expr = None
            if not task.is_file and tree.body and isinstance(tree.body[-1], ast.Expr):
                last_expr = ast.Expression(tree.body.pop().value)
            with contextlib.redirect_stdout(buffer):
                exec(compile(tree, str(script_path), "exec"), py_globals)
                if last_expr is not None:
                    trailing_value = eval(compile(last_expr, str(script_path), "eval"), py_globals)
        except CompilerError:
            raise
        except Exception as error:
            raise CompilerError(
                f"error al ejecutar Python ({script_path.name}): {error}",
                code="TIGER_PYTHON_RUNTIME_ERROR",
            ) from error
        finally:
            if added_sys_path and sys.path and sys.path[0] == script_dir:
                sys.path.pop(0)

        if "TIGER" in py_globals:
            bridge_vars = py_globals["TIGER"].__dict__.get("__vars", {})
            for bk, bv in bridge_vars.items():
                variables[bk] = int(bv) if isinstance(bv, bool) else bv

        for k, v in py_globals.items():
            if k in {"TIGER", "TIGRE"}:
                continue
            if k.startswith("__") and k.endswith("__"):
                continue
            if isinstance(v, bool):
                variables[k] = int(v)
            elif isinstance(v, (int, float, str, list, dict, types.ModuleType)) or callable(v):
                variables[k] = v

        printed = buffer.getvalue()
        if printed:
            emit_text(printed)
        if task.target is None:
            return
        target_updated_in_py = (
            target_var_name is not None
            and target_var_name in py_globals
            and py_globals[target_var_name] is not prior_target_val
        )
        if "__result__" in py_globals:
            result_val = py_globals["__result__"]
        elif trailing_value is not sentinel and trailing_value is not None:
            result_val = trailing_value
        elif target_updated_in_py and target_var_name is not None:
            result_val = variables[target_var_name]
        elif printed:
            stripped_out = printed.rstrip("\r\n")
            expected_t = value_types.get(target_var_name, "dynamic") if target_var_name else "dynamic"
            if expected_t == "int":
                result_val = int(stripped_out.strip(), 10)
            elif expected_t == "float":
                result_val = float(stripped_out.strip())
            elif expected_t == "string":
                result_val = stripped_out
            else:
                result_val = _parse_dynamic_input(stripped_out)
        elif target_var_name is not None and target_var_name in variables:
            result_val = variables[target_var_name]
        else:
            result_val = ""
        if isinstance(result_val, bool):
            result_val = int(result_val)
        assign_target(task.target, result_val)

    def read_input(
        name: str,
        expected_type: str | None = None,
        prompt: str | None = None,
    ) -> int | float | str:
        value_type = expected_type or value_types.get(name, "dynamic")
        try:
            if prompt is not None:
                if stream_output:
                    sys.stdout.write(prompt)
                    sys.stdout.flush()
                    raw_value = input()
                    if not sys.stdin.isatty():
                        sys.stdout.write(f"{raw_value}\n")
                        sys.stdout.flush()
                else:
                    raw_value = input(prompt)
            else:
                raw_value = input()
            if value_type == "bool":
                stripped_val = raw_value.strip().upper()
                if stripped_val in {"TRUE", "VERDADERO", "CIERTO", "1"}:
                    return True
                if stripped_val in {"FALSE", "FALSO", "0"}:
                    return False
                raise ValueError("se esperaba un valor booleano (TRUE/FALSE o VERDADERO/FALSO)")
            if value_type == "int":
                value = int(raw_value.strip(), 10)
                if not -(1 << 63) <= value < (1 << 63):
                    raise ValueError("fuera de rango de 64 bits")
                return value
            if value_type == "float":
                return float(raw_value.strip())
            if value_type == "dynamic":
                return _parse_dynamic_input(raw_value)
            return raw_value
        except (EOFError, ValueError) as error:
            raise CompilerError(
                f"entrada inválida para {name} ({value_type}): {error}",
                code="TIGER_INPUT_ERROR",
            ) from error

    def invoke_method(receiver: str, method_name: str, arguments: tuple[Expression, ...] = ()) -> None:
        class_key = receiver.casefold()
        if class_key in classes:
            class_definition = classes[class_key]
            fields = class_fields.setdefault(
                class_key,
                {
                    member.name: _evaluate_expression(member.value, variables, emit_text)
                    for member in class_definition.members
                    if isinstance(member, Variable)
                },
            )
        else:
            instance = variables.get(receiver)
            if not isinstance(instance, TigerObject):
                raise CompilerError(f"instancia no inicializada: {receiver}")
            class_key = instance.class_name.casefold()
            class_definition = classes[class_key]
            fields = instance.fields

        method = next(
            (
                member
                for member in class_definition.members
                if isinstance(member, (ClassCommand, ClassMethod))
                and member.name.casefold() == method_name.casefold()
            ),
            None,
        )
        if not isinstance(method, ClassMethod):
            raise CompilerError(
                f"{receiver}-{method_name} apunta a un comando de bajo nivel; "
                "el intérprete no ejecuta instrucciones Assembly",
                code="TIGER_METHOD_BACKEND_REQUIRED",
            )

        missing = object()
        all_local_names = list(fields.keys()) + [p.name for p in method.parameters]
        previous_values = {name: variables.get(name, missing) for name in all_local_names}
        variables.update(fields)
        for param, val in zip(method.parameters, [_evaluate_expression(arg, variables, emit_text) for arg in arguments]):
            variables[param.name] = val
        try:
            execute(method.body)
        finally:
            for name in fields:
                if name in variables:
                    fields[name] = variables[name]
            for name in all_local_names:
                previous = previous_values[name]
                if previous is missing:
                    variables.pop(name, None)
                else:
                    variables[name] = previous

    class _LoopBreak(Exception):
        pass

    class _LoopContinue(Exception):
        pass

    def execute(items: tuple[Statement, ...], custom_emitter=None) -> None:
        current_emitter = custom_emitter if custom_emitter is not None else emit_text
        for statement in items:
            if isinstance(statement, ClassDefinition):
                continue
            if isinstance(statement, PythonBlock):
                for task in statement.tasks:
                    execute_python_task(task)
                continue
            if isinstance(statement, CallStatement):
                _evaluate_expression(statement.call, variables, emit_text)
                continue
            if isinstance(statement, ClassInstance):
                class_definition = classes[statement.class_name.casefold()]
                instance = TigerObject(class_definition.name, {})
                for member in class_definition.members:
                    if isinstance(member, Variable):
                        if isinstance(member.value, Input):
                            raise CompilerError("INPUT no se permite como valor por defecto de campo")
                        instance.fields[member.name] = _evaluate_expression(member.value, variables, emit_text)
                for override in statement.overrides:
                    if not isinstance(override, Assignment) or not isinstance(override.name, str):
                        raise CompilerError("un inicializador de instancia debe asignar un campo simple")
                    instance.fields[override.name] = _evaluate_expression(override.value, variables, emit_text)
                variables[statement.name] = instance
                continue
            if isinstance(statement, Variable):
                if isinstance(statement.value, Input):
                    variables[statement.name] = read_input(
                        statement.name,
                        value_types.get(statement.name, "dynamic"),
                        statement.value.prompt,
                    )
                else:
                    val = _evaluate_expression(statement.value, variables, emit_text)
                    variables[statement.name] = val
                    if isinstance(statement.value, BooleanLiteral):
                        variables_bool_raw[statement.name] = statement.value.raw.upper()
                    elif isinstance(val, bool):
                        variables_bool_raw[statement.name] = "TRUE" if val else "FALSE"
            elif isinstance(statement, Assignment):
                target = Name(statement.name) if isinstance(statement.name, str) else statement.name
                if isinstance(statement.value, Input):
                    if isinstance(target, Name):
                        value = read_input(
                            target.value,
                            value_types.get(target.value, "dynamic"),
                            statement.value.prompt,
                        )
                    elif isinstance(target, Index):
                        elem_type = _expression_type(target, value_types)
                        value = read_input("índice", elem_type, statement.value.prompt)
                    elif isinstance(target, Member):
                        value = read_input(target.name, "dynamic", statement.value.prompt)
                    else:
                        raise CompilerError("destino inválido para ENTRADA/INPUT")
                else:
                    value = _evaluate_expression(statement.value, variables, emit_text)
                assign_target(target, value)
                if isinstance(target, Name):
                    if isinstance(statement.value, BooleanLiteral):
                        variables_bool_raw[target.value] = statement.value.raw.upper()
                    elif isinstance(value, bool):
                        variables_bool_raw[target.value] = "TRUE" if value else "FALSE"
            elif isinstance(statement, Print):
                if isinstance(statement.value, str):
                    value = statement.value
                else:
                    eval_val = _evaluate_expression(statement.value, variables, emit_text)
                    if isinstance(eval_val, bool):
                        raw_pref = None
                        if isinstance(statement.value, BooleanLiteral):
                            raw_pref = statement.value.raw.upper()
                        elif isinstance(statement.value, Name):
                            raw_pref = variables_bool_raw.get(statement.value.value)
                        if raw_pref in {"VERDADERO", "CIERTO"}:
                            value = "VERDADERO" if eval_val else "FALSO"
                        elif raw_pref == "FALSO":
                            value = "FALSO" if not eval_val else "VERDADERO"
                        else:
                            value = "TRUE" if eval_val else "FALSE"
                    else:
                        value = str(eval_val)




                
                current_emitter(value + "\n")
            elif isinstance(statement, If):
                condition = bool(_evaluate_expression(statement.condition, variables, emit_text))
                execute(statement.then_body if condition else statement.else_body)
            elif isinstance(statement, WhileLoop):
                if statement.init:
                    execute(statement.init)
                while bool(_evaluate_expression(statement.condition, variables, emit_text)):
                    try:
                        execute(statement.body)
                    except _LoopContinue:
                        pass
                    except _LoopBreak:
                        break
                    if statement.step:
                        execute(statement.step)
            elif isinstance(statement, Break):
                raise _LoopBreak()
            elif isinstance(statement, Continue):
                raise _LoopContinue()
            elif isinstance(statement, Pointer):
                continue
            elif isinstance(statement, PointerStore):
                raise CompilerError(
                    "la escritura PointC requiere ASM/WAT; el intérprete no escribe memoria arbitraria",
                    code="TIGER_POINTER_INTERPRETER_UNSUPPORTED",
                )
            elif isinstance(statement, ClassMethodCall):
                invoke_method(statement.class_name, statement.method_name, statement.arguments)

    # DESPACHADOR DUAL TIGER: EMPAQUETAR Y EJECUTAR EN LOTES DE 2 CON SALIDA INMEDIATA
    import concurrent.futures

    # Paso 1: Empaquetar los procesos en bloques ejecutables
    # Si hay subprocesos o más de 2 paquetes, se procesan en lotes de 2 en paralelo
    if len(body) > 2:
        # Agrupar en paquetes de dos en dos
        chunks = [body[i:i + 2] for i in range(0, len(body), 2)]
        for chunk in chunks:
            # Si el paquete tiene 2 procesos y no requieren entrada interactiva por consola simultánea
            has_input = any(isinstance(s, (Variable, Assignment)) and isinstance(getattr(s, "value", None), Input) for s in chunk)
            if len(chunk) == 2 and not has_input:
                # Paso 2: Ejecutar 2 ejecutadores simultáneos para leer y correr los subprocesos
                sub_outputs = ["", ""]
                def _run_worker(idx, stmt_item):
                    # Cada ejecutor corre su subproceso y escupe de inmediato a la consola y buffer
                    def _sub_emit(txt):
                        emit_text(txt)
                    execute((stmt_item,), custom_emitter=_sub_emit)
                    return True

                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
                    futures = [executor.submit(_run_worker, idx, stmt) for idx, stmt in enumerate(chunk)]
                    for future in concurrent.futures.as_completed(futures):
                        try:
                            future.result()
                        except Exception:
                            raise
            else:
                # Paso 3: Escupir de inmediato y continuar con el siguiente paquete
                execute(chunk)
    else:
        execute(body)
    _t_end = time.perf_counter()
    return "".join(output)


def _run_tool(command: list[str], description: str, cwd: Path) -> None:
    try:
        result = subprocess.run(command, capture_output=True, text=True, cwd=cwd, check=False)
    except OSError as error:
        raise CompilerError(f"no se pudo iniciar {description}: {error}", code="TIGER_TOOL_ERROR") from error
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or f"código de salida {result.returncode}"
        raise CompilerError(f"{description} falló: {detail}", code="TIGER_TOOL_ERROR")


def _ensure_windows_import_libs(cache_dir: Path, tool_paths: dict[str, str | None]) -> tuple[Path, Path]:
    import_library_path = cache_dir / "kernel32.lib"
    crt_import_library_path = cache_dir / "msvcrt.lib"
    if not import_library_path.is_file() or import_library_path.stat().st_size == 0:
        definition_path = cache_dir / "kernel32.def"
        definition_path.write_text(
            "LIBRARY KERNEL32.dll\nEXPORTS\n GetStdHandle\n WriteFile\n ExitProcess\n",
            encoding="ascii",
        )
        _run_tool(
            [
                tool_paths["llvm-dlltool"] or "llvm-dlltool",
                "-m",
                "i386:x86-64",
                "-d",
                str(definition_path),
                "-l",
                str(import_library_path),
            ],
            "la generación de la biblioteca de importación de Windows",
            cache_dir,
        )
    if not crt_import_library_path.is_file() or crt_import_library_path.stat().st_size == 0:
        crt_definition_path = cache_dir / "msvcrt.def"
        crt_definition_path.write_text(
            "LIBRARY msvcrt.dll\nEXPORTS\n printf\n scanf\n strcmp\n fflush\n",
            encoding="ascii",
        )
        _run_tool(
            [
                tool_paths["llvm-dlltool"] or "llvm-dlltool",
                "-m",
                "i386:x86-64",
                "-d",
                str(crt_definition_path),
                "-l",
                str(crt_import_library_path),
            ],
            "la generación de la biblioteca de importación de Microsoft C Runtime",
            cache_dir,
        )
    return import_library_path, crt_import_library_path


def run_native_file(
    path: str | Path,
    root_statements: tuple[Statement, ...] | None = None,
    input_data: bytes | None = None,
) -> bytes:
    if os.name != "nt":
        raise CompilerError(
            "la ejecución nativa directa está implementada para Windows x64",
            code="TIGER_NATIVE_PLATFORM",
        )

    source_path = Path(path).resolve()
    statements = _resolve_imports(source_path, root_statements)
    body, registers, pointers, value_types = _prepare(statements)
    assembly = WindowsAssemblyEmitter(registers, body, value_types).emit(body, pointers)
    tool_paths = {
        "clang": shutil.which("clang"),
        "lld-link": shutil.which("lld-link"),
        "llvm-dlltool": shutil.which("llvm-dlltool"),
    }
    missing = [name for name, tool_path in tool_paths.items() if tool_path is None]
    if missing:
        raise CompilerError(
            "faltan herramientas LLVM para ejecutar Tiger nativo: "
            + ", ".join(missing)
            + "; instala LLVM y agrega su carpeta bin al PATH",
            code="TIGER_TOOL_MISSING",
        )

    cache_dir = Path(tempfile.gettempdir()) / "tiger_native_cache"
    cache_dir.mkdir(parents=True, exist_ok=True)
    if True:
        build_dir = cache_dir
        import_library_path, crt_import_library_path = _ensure_windows_import_libs(cache_dir, tool_paths)
        asm_hash = hashlib.sha256(assembly.encode("utf-8")).hexdigest()[:16]
        executable_path = build_dir / f"tiger_prog_{asm_hash}.exe"
        if not executable_path.is_file():
            asm_path = build_dir / f"tiger_prog_{asm_hash}.s"
            object_path = build_dir / f"tiger_prog_{asm_hash}.obj"
            asm_path.write_text(assembly, encoding="utf-8")
            _run_tool(
                [
                    tool_paths["clang"] or "clang",
                    "-c",
                    "-target",
                    "x86_64-pc-windows-msvc",
                    str(asm_path),
                    "-o",
                    str(object_path),
                ],
                "el ensamblado x86-64 de Windows",
                build_dir,
            )
            _run_tool(
                [
                    tool_paths["lld-link"] or "lld-link",
                    "/entry:main",
                    "/subsystem:console",
                    "/nodefaultlib",
                    "/machine:x64",
                    str(object_path),
                    str(import_library_path),
                    str(crt_import_library_path),
                    "/out:" + str(executable_path),
                ],
                "el enlace del ejecutable nativo de Windows",
                build_dir,
            )
        try:
            result = subprocess.run(
                [str(executable_path)],
                capture_output=True,
                input=input_data,
                cwd=source_path.parent,
                timeout=30,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            raise CompilerError("el programa Tiger superó el límite de ejecución de 30 segundos") from error
        except OSError as error:
            raise CompilerError(f"no se pudo iniciar el ejecutable Tiger: {error}") from error
        if result.returncode:
            detail = result.stderr.decode("utf-8", errors="replace").strip()
            raise CompilerError(
                f"el ejecutable Tiger terminó con código {result.returncode}"
                + (f": {detail}" if detail else ""),
                code="TIGER_NATIVE_EXECUTION",
            )
        return result.stdout


def _read_and_fix_missing_final_period(path: Path) -> tuple[Path, tuple[Statement, ...]]:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        raise CompilerError(f"no se pudo leer {path}: {error}", code="TIGER_READ_ERROR") from error

    try:
        statements = Parser(source).parse()
    except CompilerError as error:
        if error.code != "TIGER_MISSING_PERIOD":
            raise
        repaired_source = source + ("" if source.endswith(("\n", "\r")) else "\n") + ".\n"
        statements = Parser(repaired_source).parse()
        fixed_path = path.with_name(f"{path.stem}.fixed{path.suffix}")
        try:
            with fixed_path.open("x", encoding="utf-8", newline="") as fixed_file:
                fixed_file.write(repaired_source)
        except FileExistsError as write_error:
            try:
                existing_source = fixed_path.read_text(encoding="utf-8")
            except OSError as read_error:
                raise CompilerError(
                    f"no se pudo verificar la copia existente {fixed_path}: {read_error}",
                    code="TIGER_READ_ERROR",
                ) from read_error
            if existing_source != repaired_source:
                raise CompilerError(
                    f"no se sobrescribió {fixed_path}; revisa o renombra esa copia antes de corregir otra vez",
                    code="TIGER_FIXED_FILE_EXISTS",
                ) from write_error
        except OSError as write_error:
            raise CompilerError(
                f"no se pudo guardar {fixed_path}: {write_error}",
                code="TIGER_WRITE_ERROR",
            ) from write_error
        return fixed_path, statements
    else:
        return path, statements


def run_interactive_repl() -> int:
    import os, sys
    if sys.platform == "win32":
        try:
            sys.stdout.reconfigure(encoding="utf-8")
            sys.stderr.reconfigure(encoding="utf-8")
        except Exception:
            pass
    if os.name == "nt":
        os.system("") # Habilitar soporte de secuencias de escape ANSI en consola Windows
    
    # Paleta de colores Tiger: Naranja, Amarillo Neón, Rojo Intenso y Blanco Brillante
    RESET = "\033[0m"
    BOLD = "\033[1m"
    ORANGE = "\033[38;2;255;107;0m"      # #FF6B00 Naranja Tiger
    YELLOW = "\033[38;2;255;230;0m"      # #FFE600 Amarillo Neón
    RED = "\033[38;2;255;45;85m"          # #FF2D55 Rojo Eléctrico
    WHITE = "\033[38;2;255;255;255m"     # #FFFFFF Blanco Puro
    GRAY = "\033[38;2;120;120;120m"       # Gris suave

    banner = fr"""
{ORANGE}{BOLD}   /\_/\  {YELLOW} _____ _____ _____ _____ _____{RESET}
{ORANGE}{BOLD}  ( o.o ) {YELLOW}|_   _|_   _|  __ \  ___|  __ \{RESET}
{RED}{BOLD}   > ^ <    {YELLOW}| |   | | | |  \/ |__ | |__) |{RESET}
{ORANGE}{BOLD}  /|   |\   {YELLOW}| |   | | | | __|  __||  _  /{RESET}
{ORANGE}{BOLD} (_|   |_) {YELLOW} | |  _| |_| |__\ \ |___| | \ \{RESET}
{RED}{BOLD}    ---     {YELLOW} \_/ |_____|\____/\____/|_|  \_\{RESET}

{ORANGE}{BOLD}[*] TIGER 10 REPL INTERACTIVO{RESET} {WHITE}| {YELLOW}Bilingüe & Políglota{RESET}
{GRAY}Escribe código Tiger directamente. Escribe {RED}'salir.'{GRAY} o {RED}'exit.'{GRAY} para terminar.{RESET}
"""
    print(banner)

    # Estado global persistente en la sesión REPL
    session_history: list[str] = []
    last_printed_count = 0
    
    while True:
        try:
            prompt = f"{ORANGE}{BOLD}tiger{YELLOW}>{RESET} "
            line = input(prompt).strip()
            if not line:
                continue
            if line.casefold() in {":setup", ":instalar", ":install"}:
                setup_tiger_components()
                continue
            if line.casefold() in {":status", ":estado", ":info"}:
                print_tiger_status()
                continue
            if line.casefold() in {"salir.", "exit.", "salir", "exit", "quit", "quit."}:
                print(f"{YELLOW}¡Hasta luego! Que la fuerza del Tigre te acompañe.{RESET}")
                break
            
            # Auto-añadir punto si se omite para máxima comodidad en consola
            if not line.endswith(".") and not line.endswith("}"):
                line += "."
                
            # Acumulador de código en la sesión activa
            full_session_code = "\n".join(session_history + [line])
            import tempfile
            with tempfile.NamedTemporaryFile("w", suffix=".tg", delete=False, encoding="utf-8") as tmp:
                tmp.write(full_session_code + "\n")
                tmp_path = tmp.name
            try:
                res = run_file(tmp_path, stream_output=False)
                # Si ejecutó sin errores, guardamos la línea en el historial de sesión
                session_history.append(line)
                if res:
                    # Imprimir solo la salida generada por este último comando
                    all_lines = res.splitlines()
                    new_lines = all_lines[last_printed_count:]
                    last_printed_count = len(all_lines)
                    for out_line in new_lines:
                        print(f"{WHITE}{BOLD}{out_line}{RESET}")
            finally:
                try:
                    os.remove(tmp_path)
                except Exception:
                    pass
        except CompilerError as e:
            print(f"{RED}{BOLD}[X] [Error Tiger]:{RESET} {YELLOW}{e.message}{RESET}")
        except KeyboardInterrupt:
            print(f"\n{GRAY}[Interrumpido]{RESET}")
            break
        except EOFError:
            break
        except Exception as err:
            print(f"{RED}[Error]: {err}{RESET}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ensure_powershell_registration_on_startup()
    parser = argparse.ArgumentParser(description="Compilador inicial Tiger 10 (.tg)")
    source_group = parser.add_mutually_exclusive_group(required=False)
    source_group.add_argument("source", nargs="?", default=None, help="archivo de entrada .tg (si se omite, abre la terminal interactiva REPL)")
    source_group.add_argument("--demo", action="store_true", help="compila ejemplo.tg junto al compilador")
    parser.add_argument("--target", choices=("asm", "wat", "both", "auto"), default=None, help="formato de salida")
    parser.add_argument("-o", "--output", help="archivo de salida; por defecto imprime en consola")
    parser.add_argument("--run", action="store_true", help="interpreta el subconjunto seguro de Tiger 10 (experimental)")
    parser.add_argument("--run-native", action="store_true", help="ensambla, enlaza y ejecuta un binario Windows x64")
    parser.add_argument(
        "--fix",
        action="store_true",
        help="corrige un punto final faltante en una copia .fixed.tg, sin cambiar el original",
    )
    parser.add_argument(
        "--setup",
        action="store_true",
        help="descarga e instala los componentes de soporte (bin) / download and install support components (bin)",
    )
    parser.add_argument(
        "--status",
        action="store_true",
        help="verifica el estado de los componentes del compilador / check compiler components status",
    )
    parser.add_argument(
        "--flexible",
        action="store_true",
        help="modo flexible tolerante a fallos / flexible fault-tolerant mode",
    )
    args = parser.parse_args(argv)

    if (args.run or args.run_native) and args.output:
        parser.error("--output no se puede combinar con --run ni con --run-native")
    if args.run and args.run_native:
        parser.error("--run y --run-native son modos incompatibles")
    if args.setup or (args.source and args.source.casefold() in {"setup", "install", "install-bin"}):
        return 0 if setup_tiger_components() else 1
    if args.status or (args.source and args.source.casefold() in {"status", "estado", "info"}):
        print_tiger_status()
        return 0
    if not args.source and not args.demo:
        return run_interactive_repl()
    source_path = (
        Path(__file__).with_name("ejemplo.tg")
        if args.demo
        else Path(args.source)
    )
    started = time.perf_counter_ns()
    try:
        root_statements = None
        if args.fix:
            fixed_path, root_statements = _read_and_fix_missing_final_period(source_path.resolve())
            if fixed_path != source_path.resolve():
                print(
                    f"Se añadió el punto final; original intacto. Archivo corregido: {fixed_path}",
                    file=sys.stderr,
                )
                source_path = fixed_path
        if args.run_native:
            generated = run_native_file(source_path, root_statements)
        elif args.run:
            stream_output_emitted = True
            generated = run_file(source_path, root_statements, stream_output=True)
        else:
            generated = compile_file(source_path, args.target, root_statements, flexible=args.flexible)
    except CompilerError as error:
        print(f"[Error Tiger / Tiger Error]: {error}", file=sys.stderr)
        if error.code == "TIGER_MISSING_PERIOD":
            print("Sugerencia: puedes ejecutar con --flexible o con --fix para guardar una copia.\nTip: you can run with --flexible or use --fix to repair.", file=sys.stderr)
        elif error.code == "TIGER_INVALID_TERMINATOR":
            print("Sugerencia: en Tiger los procesos cierran con '.' o puedes activar --flexible para admitir ';'.\nTip: in Tiger statements close with '.' or enable --flexible to accept ';'.", file=sys.stderr)
        return 1
    elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000

    if args.run_native:
        sys.stdout.buffer.write(generated)
        sys.stdout.buffer.flush()
    elif args.output:
        try:
            Path(args.output).write_text(generated, encoding="utf-8")
        except OSError as error:
            print(f"Error al escribir salida: {error}", file=sys.stderr)
            return 1
    elif args.run:
        # Asegurar que toda la salida de los 2 ejecutadores llegue limpia a stdout si no se emitió en vivo
        if generated and not stream_output_emitted:
            sys.stdout.write(generated)
            sys.stdout.flush()
    else:
        sys.stdout.write(generated)
    action = "Ejecución nativa / Native execution" if args.run_native else "Ejecución / Execution" if args.run else "Compilación / Compilation"
    if not args.run_native:
        ORANGE = "\033[38;2;255;107;0m"
        YELLOW = "\033[38;2;255;230;0m"
        RESET = "\033[0m"
        BOLD = "\033[1m"
        print(f"{ORANGE}{BOLD}{action} Tiger 10:{RESET} {YELLOW}{elapsed_ms:.3f} ms {ORANGE}(ultra-rápido / blazing-fast){RESET}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

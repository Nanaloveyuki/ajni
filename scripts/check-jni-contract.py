#!/usr/bin/env python3
"""Check existing ajni JNI contracts without loading Java or running native code.

The source parser deliberately accepts only the bridge's current literal C forms.
A source refactor outside those forms needs an explicit checker update, not a skip.
Parser/check helpers are importable for temporary, dependency-free negative smoke.
"""

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


BRIDGE_CLASS = "dev.nanaloveyuki.ajni.host.NativeBridge"
ROOT = Path(__file__).resolve().parent.parent


class ContractError(Exception):
    """A contract, bounded parser, or Android compilation failed (exit 1)."""


class PrerequisiteError(Exception):
    """A required input or tool is unavailable (exit 2)."""


@dataclass(frozen=True)
class Native:
    name: str
    descriptor: str
    target: str
    source: str


NATIVES = (
    Native("nativeInitialize", "(Landroid/content/Context;J)V", "ajni_native_initialize", "core"),
    Native("nativeShutdown", "()V", "ajni_native_shutdown", "core"),
    Native("nativeLifecycle", "(I)V", "ajni_native_lifecycle", "core"),
    Native("nativeSurfaceCreated", "(Landroid/view/Surface;II)V", "ajni_native_surface_created", "core"),
    Native("nativeSurfaceChanged", "(II)V", "ajni_native_surface_changed", "core"),
    Native("nativeSurfaceDestroyed", "()V", "ajni_native_surface_destroyed", "core"),
    Native("nativeOnUiTask", "(I)V", "ajni_native_on_ui_task", "core"),
    Native("nativeStartWorker", "()V", "ajni_native_start_worker", "core"),
    Native("nativeEcho", "(Ljava/lang/String;)Ljava/lang/String;", "ajni_native_echo", "core"),
    Native("nativeCreateIntCallback", "()Ldev/nanaloveyuki/ajni/AjniCallback;", "ajni_native_create_int_callback", "core"),
    Native("nativeHandledObserverError", "()Z", "ajni_native_handled_observer_error", "core"),
    Native("nativeWebViewEvent", "(IJLjava/lang/String;Ljava/lang/String;)V", "ajni_native_webview_event", "webview"),
)
UPCALLS = {
    "postUiCallbackForRuntime": ("(IJ)Z", "core", "ajni_enqueue_ui_callback"),
    "webViewCommandForRuntime": ("(IJLjava/lang/String;Ljava/lang/String;J)Z", "webview", "ajni_webview_command"),
    "webViewSetBoundsForRuntime": ("(JIIIIJ)Z", "webview", "ajni_webview_set_bounds"),
}
EXPECTED = {item.name: item.descriptor for item in NATIVES}
EXPECTED.update({name: spec[0] for name, spec in UPCALLS.items()})
COMPANION_RULES = {
    "dev.nanaloveyuki.ajni.AjniCallback": {
        "public <init>(long)",
        "private static native java.lang.Object dispatch(long, java.lang.Object[])",
        "private static native void close(long)",
    },
    "dev.nanaloveyuki.ajni.AjniException": {"public <init>(java.lang.String)"},
}


def require(condition, message):
    if not condition:
        raise ContractError(message)


def descriptor_types(descriptor):
    """Return (parameter JVM types, return JVM type), rejecting trailing input."""
    def one(position, allow_void=False):
        require(position < len(descriptor), "truncated descriptor: " + descriptor)
        start = position
        symbol = descriptor[position]
        if symbol == "L":
            end = descriptor.find(";", position)
            require(end != -1, "unterminated object descriptor: " + descriptor)
            name = descriptor[position + 1:end]
            require(re.fullmatch(r"[A-Za-z_$][A-Za-z0-9_$]*(?:/[A-Za-z_$][A-Za-z0-9_$]*)*", name),
                    "invalid object descriptor: " + descriptor)
            position = end + 1
        elif symbol == "[":
            _, position = one(position + 1)
        else:
            require(symbol in "ZBCSIJFD" or (allow_void and symbol == "V"),
                    "unsupported descriptor type: " + descriptor)
            position += 1
        return descriptor[start:position], position

    require(descriptor.startswith("("), "not a method descriptor: " + descriptor)
    position = 1
    parameters = []
    while position < len(descriptor) and descriptor[position] != ")":
        value, position = one(position)
        parameters.append(value)
    require(position < len(descriptor) and descriptor[position] == ")", "unclosed descriptor: " + descriptor)
    result, position = one(position + 1, True)
    require(position == len(descriptor), "trailing descriptor input: " + descriptor)
    return parameters, result


@dataclass(frozen=True)
class Method:
    name: str
    modifiers: frozenset
    descriptor: str


def parse_javap(text):
    """Pair each javap declaration block with its own descriptor."""
    methods = []
    pending = None
    in_class = False
    closed = False
    modifiers = {"public", "protected", "private", "static", "final", "native",
                 "synchronized", "abstract", "strictfp", "default"}
    for number, original in enumerate(text.splitlines(), 1):
        line = original.strip()
        if not line:
            continue
        if line.startswith("Compiled from ") and not in_class and not closed:
            continue
        if not in_class:
            require(not closed and re.fullmatch(
                r"(?:public |final |abstract )*class " + re.escape(BRIDGE_CLASS) + r" \{", line),
                f"javap:{number}: expected actual {BRIDGE_CLASS} class header")
            in_class = True
            continue
        if line == "}":
            require(pending is None, f"javap:{number}: declaration has no descriptor")
            in_class = False
            closed = True
            continue
        if line.startswith("descriptor:"):
            require(pending is not None, f"javap:{number}: orphan descriptor")
            descriptor = line[len("descriptor:"):].strip()
            declaration = pending
            pending = None
            if "(" not in declaration and declaration != "static {};":
                require(not descriptor.startswith("("), f"javap:{number}: field has method descriptor")
                continue
            descriptor_types(descriptor)
            if declaration == "static {};":
                require(descriptor == "()V", "invalid static initializer descriptor")
                continue
            match = re.fullmatch(r"(.+?)\((.*)\);", declaration)
            require(match is not None, f"javap:{number}: unsupported method declaration")
            words = match.group(1).split()
            name = words[-1]
            prefix = words[:-1]
            if name == BRIDGE_CLASS:
                require(all(word in modifiers for word in prefix) and descriptor.endswith("V"),
                        "unsupported constructor declaration")
                continue
            require(bool(prefix), f"javap:{number}: missing return type")
            method_modifiers = prefix[:-1]
            require(all(word in modifiers for word in method_modifiers) and
                    len(set(method_modifiers)) == len(method_modifiers),
                    f"javap:{number}: unsupported or duplicate modifiers")
            require(re.fullmatch(r"[A-Za-z_$][A-Za-z0-9_$]*", name), "invalid javap method name")
            methods.append(Method(name, frozenset(method_modifiers), descriptor))
            continue
        require(pending is None, f"javap:{number}: declaration has no paired descriptor")
        require(line.endswith(";"), f"javap:{number}: unconsumed output: {line}")
        pending = line
    require(closed and pending is None, "javap: incomplete or empty class output")
    return methods


def check_bytecode(text):
    """Validate all bridge natives and three private static boolean upcalls."""
    methods = parse_javap(text)
    found = {}
    for name, descriptor in EXPECTED.items():
        matches = [method for method in methods if method.name == name]
        require(len(matches) == 1, f"bytecode: {name} must occur exactly once (found {len(matches)})")
        method = matches[0]
        required = {"private", "static"}
        if name not in UPCALLS:
            required.add("native")
        require(method.modifiers in (frozenset(required), frozenset(required | {"final"})),
                f"bytecode: {name} has wrong modifiers: {sorted(method.modifiers)}")
        require(method.descriptor == descriptor,
                f"bytecode: {name}: {method.descriptor} != {descriptor}")
        found[name] = method
    extra = [method.name for method in methods if "native" in method.modifiers and method.name not in EXPECTED]
    require(not extra, "bytecode: unexpected native methods: " + ", ".join(extra))
    return found


@dataclass(frozen=True)
class Token:
    value: str
    kind: str
    line: int


def tokenize_c(text):
    """Tokenize bounded C syntax, preserving literals and preprocessor lines."""
    tokens = []
    position = 0
    line = 1
    line_start = True
    while position < len(text):
        character = text[position]
        if character.isspace():
            if character == "\n":
                line += 1
                line_start = True
            position += 1
            continue
        start = position
        start_line = line
        if text.startswith("//", position):
            end = text.find("\n", position + 2)
            position = len(text) if end < 0 else end
            continue
        if text.startswith("/*", position):
            end = text.find("*/", position + 2)
            require(end >= 0, f"C:{line}: unterminated comment")
            comment = text[position:end + 2]
            line += comment.count("\n")
            if "\n" in comment:
                line_start = True
            position = end + 2
            continue
        if character == "#" and line_start:
            while True:
                end = text.find("\n", position)
                if end < 0:
                    position = len(text)
                    break
                continued = text[position:end].rstrip("\r").endswith("\\")
                position = end + 1
                line += 1
                if not continued:
                    break
            tokens.append(Token(text[start:position], "directive", start_line))
            line_start = True
            continue
        line_start = False
        if character in ('"', "'"):
            quote = character
            position += 1
            while position < len(text) and text[position] != quote:
                require(text[position] != "\n", f"C:{start_line}: multiline literal unsupported")
                if text[position] == "\\":
                    position += 1
                position += 1
            require(position < len(text), f"C:{start_line}: unterminated literal")
            position += 1
            tokens.append(Token(text[start:position], "string" if quote == '"' else "char", start_line))
            continue
        match = re.match(r"[A-Za-z_][A-Za-z0-9_]*|(?:[0-9][A-Za-z0-9_.]*)", text[position:])
        if match:
            value = match.group()
            tokens.append(Token(value, "identifier" if value[0].isalpha() or value[0] == "_" else "number", line))
            position += len(value)
            continue
        operator = next((value for value in ("<<=", ">>=", "...", "->", "++", "--", "==", "!=", "<=", ">=",
                                            "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>")
                         if text.startswith(value, position)), None)
        if operator:
            position += len(operator)
        else:
            require(character in "{}()[];,.*&=+-/<>!?:%|^~", f"C:{line}: unsupported token {character!r}")
            operator = character
            position += 1
        tokens.append(Token(operator, "punctuation", line))
    return tokens


def values(tokens):
    return [token.value for token in tokens]


def matching(tokens, start):
    pairs = {"(": ")", "[": "]", "{": "}"}
    require(start < len(tokens) and tokens[start].value in pairs, "expected opening delimiter")
    stack = []
    for position in range(start, len(tokens)):
        token = tokens[position]
        if token.kind in ("string", "char", "directive"):
            continue
        if token.value in pairs:
            stack.append(pairs[token.value])
        elif token.value in pairs.values():
            require(stack and stack.pop() == token.value, f"C:{token.line}: mismatched delimiter")
            if not stack:
                return position
    raise ContractError(f"C:{tokens[start].line}: unclosed delimiter")


def split_arguments(tokens):
    """Split a parameter/call list at top-level commas, consuming all delimiters."""
    if not tokens:
        return []
    result = []
    start = 0
    position = 0
    while position < len(tokens):
        token = tokens[position]
        require(token.kind != "directive", f"C:{token.line}: directive inside contract expression")
        if token.kind == "punctuation" and token.value in ("(", "[", "{"):
            end = matching(tokens, position)
            require(not any(item.kind == "directive" for item in tokens[position:end + 1]),
                    "directive inside nested contract expression")
            position = end + 1
            continue
        if token.value == ",":
            require(position > start, "empty C argument")
            result.append(tokens[start:position])
            start = position + 1
        require(token.value not in (")", "]", "}"), "unexpected closing argument delimiter")
        position += 1
    require(start < len(tokens), "trailing comma in C argument list")
    result.append(tokens[start:])
    return result


@dataclass
class Function:
    name: str
    prefix: list
    parameters: list
    body: list
    start: int
    end: int


def parse_c_functions(tokens):
    """Locate actual top-level function definitions, not prototypes or calls."""
    functions = []
    position = 0
    declaration_start = 0
    while position < len(tokens):
        token = tokens[position]
        if token.kind == "directive":
            position += 1
            continue
        if token.value == "(":
            end = matching(tokens, position)
            if (position > 0 and tokens[position - 1].kind == "identifier" and
                    end + 1 < len(tokens) and tokens[end + 1].value == "{"):
                body_end = matching(tokens, end + 1)
                prefix = [item for item in tokens[declaration_start:position - 1] if item.kind != "directive"]
                parameters = tokens[position + 1:end]
                require(not any(item.kind == "directive" for item in parameters), "directive in function prototype")
                functions.append(Function(tokens[position - 1].value, prefix, parameters,
                                          tokens[end + 2:body_end], position - 1, body_end))
                position = body_end + 1
                declaration_start = position
                continue
            position = end + 1
            continue
        if token.value == "[":
            position = matching(tokens, position) + 1
            continue
        if token.value == "{":
            position = matching(tokens, position) + 1
            declaration_start = position
            continue
        if token.value == ";":
            declaration_start = position + 1
        require(token.value not in ("}", ")", "]"), f"C:{token.line}: unexpected top-level delimiter")
        position += 1
    return functions


def plain_string(token):
    require(token.kind == "string" and token.value.startswith('"') and "\\" not in token.value,
            f"C:{token.line}: expected one unescaped string literal (no macro/concatenation)")
    return token.value[1:-1]


def jni_calls(function, api):
    calls = []
    body = function.body
    for position, token in enumerate(body):
        if token.kind != "identifier" or token.value != api:
            continue
        require(position > 0 and body[position - 1].value == "->" and
                position + 1 < len(body) and body[position + 1].value == "(",
                f"{function.name}: unsupported {api} expression")
        end = matching(body, position + 1)
        arguments = split_arguments(body[position + 2:end])
        calls.append((position, end, arguments))
    return calls


def check_registration(tokens, functions, natives, owner):
    occurrences = [position for position, token in enumerate(tokens)
                   if token.kind == "identifier" and token.value == "JNINativeMethod"]
    require(len(occurrences) == 1, f"{owner}: expected one literal JNINativeMethod table")
    position = occurrences[0]
    require(position > 0 and values(tokens[position - 1:position + 6]) ==
            ["const", "JNINativeMethod", "methods", "[", "]", "=", "{"],
            f"{owner}: unsupported registration table declaration")
    start = position + 5
    end = matching(tokens, start)
    require(end + 1 < len(tokens) and tokens[end + 1].value == ";", "registration table missing semicolon")
    owners = [function for function in functions if function.start < position < end < function.end]
    require(len(owners) == 1 and owners[0].name == owner, f"registration table must be inside {owner}")
    # No qualifier/macro may precede the accepted declaration within its statement.
    preceding = position - 2
    require(preceding >= 0 and (tokens[preceding].value in (";", "{", "}") or
            tokens[preceding].kind == "directive"), "unconsumed registration declaration prefix")
    entries = {}
    cursor = start + 1
    while cursor < end:
        row = tokens[cursor:cursor + 10]
        require(len(row) == 10 and values(row)[0] == "{" and values(row)[2] == "," and
                values(row)[4:9] == [",", "(", "void", "*", ")"] and row[9].kind == "identifier",
                f"{owner}: unsupported registration entry at line {tokens[cursor].line}")
        require(cursor + 10 < end and tokens[cursor + 10].value == "}", "unconsumed registration entry")
        name = plain_string(row[1])
        descriptor = plain_string(row[3])
        require(name not in entries, f"duplicate registration: {name}")
        entries[name] = (descriptor, row[9].value)
        cursor += 11
        if cursor < end:
            require(tokens[cursor].value == ",", "registration entries must be comma separated")
            cursor += 1
    expected = {item.name: (item.descriptor, item.target) for item in natives}
    require(entries == expected, f"{owner}: registration mismatch: found {entries}, expected {expected}")
    calls = jni_calls(owners[0], "RegisterNatives")
    require(len(calls) == 1, f"{owner}: expected one RegisterNatives call")
    arguments = calls[0][2]
    require(len(arguments) == 4 and values(arguments[2]) == ["methods"] and
            values(arguments[3]) == ["sizeof", "(", "methods", ")", "/", "sizeof", "(", "methods", "[", "0", "]", ")"],
            f"{owner}: RegisterNatives must consume the exact methods table and its length")
    return entries


C_TYPES = {"I": "jint", "J": "jlong", "Z": "jboolean", "V": "void", "Ljava/lang/String;": "jstring",
           "Landroid/content/Context;": "jobject", "Landroid/view/Surface;": "jobject",
           "Ldev/nanaloveyuki/ajni/AjniCallback;": "jobject"}


def expected_c_signature(native):
    parameters, result = descriptor_types(native.descriptor)
    return C_TYPES[result], ["JNIEnv *", "jclass"] + [C_TYPES[value] for value in parameters]


def check_native_definition(function, native):
    result, expected_parameters = expected_c_signature(native)
    require(values(function.prefix) == ["static", result],
            f"{native.target}: expected actual static {result} definition")
    parameters = split_arguments(function.parameters)
    require(len(parameters) == len(expected_parameters), f"{native.target}: wrong parameter count")
    names = []
    for parameter, expected in zip(parameters, expected_parameters):
        require(parameter and parameter[-1].kind == "identifier", f"{native.target}: parameters must be named")
        name = parameter[-1].value
        require(values(parameter[:-1]) == expected.split(),
                f"{native.target}: {name} source type {values(parameter[:-1])} != {expected}")
        names.append(name)
    require(len(set(names)) == len(names), f"{native.target}: duplicate parameter names")
    require(names[0] == "env", f"{native.target}: first parameter must be the JNIEnv env")


def check_upcall(function, name, descriptor):
    require(not any(token.kind == "directive" for token in function.body),
            f"{function.name}: conditional/macro fragments inside an upcall function are unsupported")
    lookups = jni_calls(function, "GetStaticMethodID")
    calls = jni_calls(function, "CallStaticBooleanMethod")
    require(len(lookups) == 1 and len(calls) == 1, f"{function.name}: expected one lookup and one boolean upcall")
    lookup_position, lookup_end, lookup_arguments = lookups[0]
    call_position, _, call_arguments = calls[0]
    require(len(lookup_arguments) == 4 and len(lookup_arguments[2]) == 1 and len(lookup_arguments[3]) == 1,
            f"{function.name}: lookup requires four explicit arguments and literal name/descriptor")
    require(plain_string(lookup_arguments[2][0]) == name and plain_string(lookup_arguments[3][0]) == descriptor,
            f"{function.name}: wrong GetStaticMethodID name/descriptor")
    require(values(lookup_arguments[0]) in (["env"], ["lease", "->", "env"]) and
            values(lookup_arguments[1]) in (["lease", ".", "bridge_class"], ["lease", "->", "bridge_class"]),
            f"{function.name}: lookup requires the existing leased environment and bridge class")
    start = lookup_position - 1
    while start > 0 and function.body[start - 1].value not in (";", "{", "}"):
        start -= 1
    prefix = function.body[start:lookup_position]
    require(len(prefix) >= 4 and prefix[0].value == "jmethodID" and prefix[1].kind == "identifier" and
            prefix[2].value == "=", f"{function.name}: lookup must initialize one jmethodID local")
    local = prefix[1].value
    receiver = ["(", "*"] + values(lookup_arguments[0]) + [")", "->"]
    require(values(prefix[3:]) == receiver, f"{function.name}: lookup receiver/env mismatch")
    require(lookup_end + 1 < len(function.body) and function.body[lookup_end + 1].value == ";",
            f"{function.name}: unconsumed lookup initializer")
    require(lookup_end < call_position, f"{function.name}: upcall precedes its method lookup")
    parameters, _ = descriptor_types(descriptor)
    require(len(call_arguments) == len(parameters) + 3,
            f"{function.name}: boolean upcall needs {len(parameters)} explicit Java arguments")
    require(values(call_arguments[0]) == values(lookup_arguments[0]) and
            values(call_arguments[1]) == values(lookup_arguments[1]) and
            values(call_arguments[2]) == [local], f"{function.name}: boolean upcall is not associated with lookup local")
    receiver_start = call_position - len(receiver)
    require(receiver_start >= 0 and values(function.body[receiver_start:call_position]) == receiver,
            f"{function.name}: boolean upcall receiver/env mismatch")
    assignments = []
    for position, token in enumerate(function.body):
        if token.value != local or token.kind != "identifier":
            continue
        if position + 1 < len(function.body) and function.body[position + 1].value in (
                "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "++", "--"):
            assignments.append(position)
        require(position == 0 or function.body[position - 1].value not in ("++", "--", "&"),
                f"{function.name}: method local may not be mutated or escape by address")
    require(assignments == [start + 1], f"{function.name}: method local must not be reassigned")
    declarations = [position for position, token in enumerate(function.body[:-1])
                    if token.value == "jmethodID" and function.body[position + 1].value == local]
    require(declarations == [start], f"{function.name}: duplicate/shadowed method local")
    # JNI varargs are not type checked by this structural check or C compilation.
    return [(kind, " ".join(values(argument))) for kind, argument in zip(parameters, call_arguments[3:])]


def check_c_contract(core_text, webview_text):
    """Check both literal C tables, actual native prototypes, and upcall bindings."""
    parsed = {}
    for source, text in (("core", core_text), ("webview", webview_text)):
        tokens = tokenize_c(text)
        functions = parse_c_functions(tokens)
        parsed[source] = (tokens, functions)
        selected = [item for item in NATIVES if item.source == source]
        owner = "JNI_OnLoad" if source == "core" else "ajni_register_webview_natives"
        check_registration(tokens, functions, selected, owner)
    all_functions = [function for _, functions in parsed.values() for function in functions]
    for native in NATIVES:
        definitions = [function for function in all_functions if function.name == native.target]
        require(len(definitions) == 1, f"{native.target}: expected one actual definition across both stubs")
        require(definitions[0] in parsed[native.source][1], f"{native.target}: definition is in wrong stub")
        check_native_definition(definitions[0], native)
    expressions = {}
    for name, (descriptor, source, target) in UPCALLS.items():
        # Host fallback functions have the same name but contain no JNI calls.
        candidates = [function for function in parsed[source][1] if function.name == target and
                      any(token.value == "GetStaticMethodID" and token.kind == "identifier" for token in function.body)]
        require(len(candidates) == 1, f"{target}: expected one actual Android lookup function")
        expressions[name] = check_upcall(candidates[0], name, descriptor)
    expected_lookup_count = len(UPCALLS)
    lookup_count = sum(token.kind == "identifier" and token.value == "GetStaticMethodID"
                       for tokens, _ in parsed.values() for token in tokens)
    boolean_count = sum(token.kind == "identifier" and token.value == "CallStaticBooleanMethod"
                        for tokens, _ in parsed.values() for token in tokens)
    require(lookup_count == expected_lookup_count and boolean_count == expected_lookup_count,
            "unexpected/unconsumed GetStaticMethodID or CallStaticBooleanMethod expression")
    return expressions


JAVA_TYPES = {"void": "V", "boolean": "Z", "byte": "B", "char": "C", "short": "S", "int": "I",
              "long": "J", "float": "F", "double": "D"}


def java_type_descriptor(value, allow_void=False):
    value = value.strip()
    if value in JAVA_TYPES:
        require(value != "void" or allow_void, "void parameter in keep rule")
        return JAVA_TYPES[value]
    require(re.fullmatch(r"[A-Za-z_$][A-Za-z0-9_$]*(?:\.[A-Za-z_$][A-Za-z0-9_$]*)+", value),
            "unsupported/wildcard keep type: " + value)
    return "L" + value.replace(".", "/") + ";"


def parse_keep_rules(text):
    """Parse exact bridge and companion rules, rejecting unconsumed members."""
    text = re.sub(r"#[^\n]*", "", text)
    blocks = list(re.finditer(r"-keep\s+class\s+([A-Za-z_$.][A-Za-z0-9_$.]*)\s*\{([^{}]*)\}", text))
    require(not re.sub(r"-keep\s+class\s+[A-Za-z_$.][A-Za-z0-9_$.]*\s*\{[^{}]*\}", "", text).strip(),
            "keep rules: unsupported content outside exact class blocks")
    classes = {block.group(1): block.group(2) for block in blocks}
    require(len(classes) == len(blocks) and set(classes) == {BRIDGE_CLASS, *COMPANION_RULES},
            "keep rules: expected precise NativeBridge and Java companion blocks")
    for name, expected in COMPANION_RULES.items():
        entries = classes[name].split(";")
        require(not entries[-1].strip(), "keep rules: unterminated companion member")
        members = [" ".join(entry.split()) for entry in entries[:-1]]
        require(len(members) == len(expected) and set(members) == expected,
                "keep rules: companion member mismatch for " + name)
    body = classes[BRIDGE_CLASS]
    result = {}
    members = body.split(";")
    require(not members[-1].strip(), "keep rules: unterminated member")
    for member in members[:-1]:
        match = re.fullmatch(r"\s*private\s+static\s+(native\s+)?([A-Za-z_$.][A-Za-z0-9_$.]*)\s+"
                             r"([A-Za-z_$][A-Za-z0-9_$]*)\s*\(([^()]*)\)\s*", member)
        require(match is not None, "keep rules: unsupported/wildcard/empty member: " + member.strip())
        native, result_type, name, arguments = match.groups()
        require(name not in result, "keep rules: duplicate member: " + name)
        parameter_types = [] if not arguments.strip() else arguments.split(",")
        descriptor = "(" + "".join(java_type_descriptor(value) for value in parameter_types) + ")"
        descriptor += java_type_descriptor(result_type, True)
        result[name] = (descriptor, bool(native))
    return result


def check_keep_rules(text, bytecode):
    """Validate exact semantic keep members against the validated bytecode."""
    members = parse_keep_rules(text)
    require(set(members) == set(EXPECTED), "keep rules: missing or extra members: " +
            str(sorted(set(members) ^ set(EXPECTED))))
    for name, (descriptor, native) in members.items():
        require(name in bytecode and descriptor == bytecode[name].descriptor and
                native == ("native" in bytecode[name].modifiers), f"keep rules: semantic mismatch for {name}")
    return members


def input_text(path, label):
    path = Path(path).resolve()
    if not path.is_file():
        raise PrerequisiteError(f"missing {label}: {path}")
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise PrerequisiteError(f"cannot read {label}: {path}: {error}") from error


def executable(value, label):
    resolved = shutil.which(str(value))
    if not resolved:
        raise PrerequisiteError(f"missing/unusable {label}: {value}")
    # NDK target wrappers may be symlinks: their argv[0] still selects the ABI/API.
    return Path(os.path.abspath(resolved))


def run_tool(command, label):
    try:
        result = subprocess.run([str(part) for part in command], capture_output=True, text=True,
                                encoding="utf-8", errors="replace", check=False)
    except OSError as error:
        raise PrerequisiteError(f"cannot execute {label}: {error}") from error
    if result.returncode:
        raise ContractError(f"{label} failed ({result.returncode})\n"
                            f"command: {shlex_command(command)}\n{result.stdout}{result.stderr}")
    return result.stdout


def shlex_command(command):
    # Formatting only; subprocess always receives an argv list, never a shell.
    import shlex
    return shlex.join(str(part) for part in command)


def ndk_compilers(ndk):
    ndk = Path(ndk).resolve()
    host_tags = {"linux": "linux-x86_64", "darwin": "darwin-x86_64", "win32": "windows-x86_64"}
    if sys.platform not in host_tags:
        raise PrerequisiteError(f"unsupported NDK host platform: {sys.platform}")
    prebuilt = ndk / "toolchains" / "llvm" / "prebuilt" / host_tags[sys.platform]
    for header in ("jni.h", "android/log.h", "android/native_window.h", "android/native_window_jni.h"):
        if not (prebuilt / "sysroot" / "usr" / "include" / header).is_file():
            raise PrerequisiteError(f"missing real Android NDK header: {prebuilt / 'sysroot/usr/include' / header}")
    suffix = ".cmd" if sys.platform == "win32" else ""
    compilers = {}
    for abi, prefix in (("arm64-v8a", "aarch64-linux-android24"), ("x86_64", "x86_64-linux-android24")):
        compiler = prebuilt / "bin" / (prefix + "-clang" + suffix)
        compilers[abi] = executable(compiler, f"NDK API24 {abi} clang")
    return compilers


def translation_unit(stub, natives):
    """Include real definitions before checking their function-pointer types."""
    lines = ["#if !defined(__ANDROID__) || __ANDROID_API__ != 24",
             '#error "JNI contract check requires the real Android API24 target"', "#endif",
             "#if defined(AJNI_STANDALONE_JNI)", '#error "fake refcount configuration is forbidden"', "#endif",
             "#include " + json.dumps(str(Path(stub).resolve()))]
    for native in natives:
        result, parameters = expected_c_signature(native)
        pointer = result + " (*)(" + ", ".join(parameters) + ")"
        lines.append(f'_Static_assert(_Generic(&{native.target}, {pointer}: 1, default: 0), "{native.name} prototype");')
    return "\n".join(lines) + "\n"


def compile_native_contract(stubs, compilers, moon_home):
    """Syntax-check real stubs for both ABIs and export/callback configurations."""
    include = Path(moon_home).resolve() / "include"
    if not (include / "moonbit.h").is_file():
        raise PrerequisiteError(f"missing MoonBit C header: {include / 'moonbit.h'}")
    configurations = {"production": ["-DAJNI_USE_MOONBIT_EXPORTS=1", "-DAJNI_FEATURE_WEBVIEW=1"],
                      "callback (exports disabled)": ["-DAJNI_FEATURE_WEBVIEW=1"]}
    checked = []
    with tempfile.TemporaryDirectory(prefix="ajni-jni-contract-") as temporary:
        units = {}
        for source, stub in stubs.items():
            unit = Path(temporary) / (source + "-contract.c")
            unit.write_text(translation_unit(stub, [item for item in NATIVES if item.source == source]), encoding="utf-8")
            units[source] = unit
        runtime_unit = Path(temporary) / "runtime-contract.c"
        runtime_unit.write_text("#include " + json.dumps(str(ROOT / "src/runtime/ajni_runtime.c")) + "\n", encoding="utf-8")
        units["runtime"] = runtime_unit
        for abi, compiler in compilers.items():
            for configuration, defines in configurations.items():
                for source, unit in units.items():
                    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                               "-I" + str(include), "-I" + str(ROOT / "src/android"),
                               "-I" + str(ROOT / "src/runtime"), *defines, unit]
                    run_tool(command, f"Android API24 {abi} / {configuration} / {source}")
                checked.append((abi, configuration))
    return checked


def argument_parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--class-dir", type=Path, required=True, help="actual Kotlin class output directory")
    parser.add_argument("--ndk", type=Path, required=True, help="real Android NDK root")
    parser.add_argument("--moon-home", type=Path, required=True, help="MoonBit root containing include/moonbit.h")
    parser.add_argument("--core-stub", type=Path, default=ROOT / "src/android/ajni_bridge.c")
    parser.add_argument("--webview-stub", type=Path, default=ROOT / "src/webview/ajni_webview_bridge.c")
    parser.add_argument("--consumer-rules", type=Path, default=ROOT / "android/host/consumer-rules.pro")
    parser.add_argument("--javap", default="javap", help="JDK javap executable")
    parser.add_argument("--javap-output", type=Path,
                        help="additional captured-output check for mutations; never replaces real bytecode or NDK checks")
    return parser


def main(argv=None):
    arguments = argument_parser().parse_args(argv)
    try:
        stubs = {"core": arguments.core_stub.resolve(), "webview": arguments.webview_stub.resolve()}
        core_text = input_text(stubs["core"], "core C stub")
        webview_text = input_text(stubs["webview"], "WebView C stub")
        rules_text = input_text(arguments.consumer_rules, "consumer keep rules")
        class_dir = arguments.class_dir.resolve()
        class_file = class_dir / Path(BRIDGE_CLASS.replace(".", "/") + ".class")
        if not class_file.is_file():
            raise PrerequisiteError(f"missing actual Kotlin bytecode: {class_file}")
        with class_file.open("rb") as stream:
            require(stream.read(4) == b"\xca\xfe\xba\xbe", f"not a JVM class artifact: {class_file}")
        javap = executable(arguments.javap, "JDK javap")
        bytecode_text = run_tool([javap, "-private", "-s", "-classpath", class_dir, BRIDGE_CLASS], "javap actual NativeBridge bytecode")
        bytecode = check_bytecode(bytecode_text)
        if arguments.javap_output is not None:
            check_bytecode(input_text(arguments.javap_output, "additional javap capture"))
        expressions = check_c_contract(core_text, webview_text)
        check_keep_rules(rules_text, bytecode)
        compilers = ndk_compilers(arguments.ndk)
        checked = compile_native_contract(stubs, compilers, arguments.moon_home)
    except PrerequisiteError as error:
        print("JNI contract prerequisites unavailable: " + str(error), file=sys.stderr)
        return 2
    except ContractError as error:
        print("JNI contract failed: " + str(error), file=sys.stderr)
        return 1
    except OSError as error:
        print("JNI contract input/tool access failed: " + str(error), file=sys.stderr)
        return 2
    print("JNI static contract passed: 12 native methods, 3 boolean upcalls, exact bridge/companion keep members.")
    print("Bytecode: " + str(class_file))
    for abi, configuration in checked:
        print(f"Android API24 {abi}: {configuration}; real runtime/core/WebView definitions and _Generic prototypes (-Werror, syntax-only).")
    print("Manual ABI review still required for JNI varargs (the checks prove association/count, not expression types):")
    for name, arguments in expressions.items():
        print("  " + name + ": " + "; ".join(kind + " <- " + expression for kind, expression in arguments))
    print("No JNI/application initializer, native execution, refcount harness, Android/WebView runtime, or R8 lookup was run.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

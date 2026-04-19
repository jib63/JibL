# JibL

A strongly-typed, strictly procedural, AI-native programming language with trilingual keywords — English, Français, and Español.

JibL source compiles to a clean S-expression IR that a tiny C runtime evaluates. The IR is human-readable and designed to be inspected, generated, and transformed by AI tools.

```
code english

func int fibonacci(int n) returns int [:
    if n <= 1 [:
        return n
    :]
    return fibonacci(n - 1) + fibonacci(n - 2)
:]

int i = 0
while i < 10 [:
    print("fib(" + i + ") = " + fibonacci(i))
    set i = i + 1
:]
```

---

## Features

- **Trilingual keywords** — write code in English, Français, or Español
- **Strongly typed** — `int`, `float`, `string`, `bool`, `result<T,E>`, `T[]`, `json`, `ai_response`
- **Block scope** — variables are scoped to `[: :]` blocks; shadowing is a compile-time error
- **No pointers** — safe by design; no manual memory management exposed to the user
- **Explicit error handling** — `result<T,E>` type with `ok(v)` / `error(msg)` constructors
- **Contracts** — `requires` / `ensures` checked at runtime on every function call
- **AI built-ins** — `ask("prompt")` queries an LLM at runtime; `@ai "..."` generates function bodies automatically and caches them
- **S-expr IR** — `--emit` dumps the compiled program as S-expressions for AI inspection and tooling
- **Standard library** — file I/O, HTTP (GET/POST), JSON parsing

---

## Requirements

| Dependency | Notes |
|---|---|
| C11 compiler (`cc` / `gcc` / `clang`) | Standard on macOS and Linux |
| `libcurl` | For HTTP and AI API calls. macOS: `brew install curl`. Linux: `apt install libcurl4-openssl-dev` |

---

## Build

```bash
git clone https://github.com/jib63/JibL.git
cd JibL
make
```

The `jibl` binary is created in the project root.

```bash
make run          # runs examples/hello_en.jibl
make emit         # prints S-expr IR for hello_en.jibl
make clean        # removes build artifacts
make clean-cache  # clears the @ai function cache
```

---

## Usage

```bash
./jibl <file.jibl>          # compile and run
./jibl --emit <file.jibl>   # print S-expr IR, do not execute
```

---

## Language Reference

### File header (mandatory)

Every `.jibl` file must begin with a language declaration:

```
code english
code français
code español
```

### Types

| Type | Description |
|---|---|
| `int` | 64-bit signed integer |
| `float` | 64-bit double |
| `string` | UTF-8 string |
| `bool` | `true` / `false` |
| `T[]` | Homogeneous array of type T |
| `result<T, E>` | Either `ok(value)` or `error(reason)` |
| `json` | Parsed JSON object |
| `ai_response` | Response from an LLM (`.content`, `.model`, `.tokens`) |
| `void` | No value (function return only) |

### Variables and constants

```
int x = 10                      @@ mutable
const string TAG = "v1"         @@ read-only — set is a compile error
```

### Assignment

```
set x = x + 1
```

### Control flow

```
if x > 5 [:
    print("big")
:] else [:
    print("small")
:]

while x > 0 [:
    set x = x - 1
:]
```

### Functions

```
func int add(int a, int b) returns int [:
    return a + b
:]

int total = add(3, 4)       @@ expression call
call greet("World")         @@ statement call (return value discarded)
```

### Error handling with result

```
func result<int, string> safe_div(int a, int b) returns result<int, string> [:
    if b == 0 [:
        return error("division by zero")
    :]
    return ok(a / b)
:]

result<int, string> r = safe_div(10, 0)
if r.ok [:
    print("answer: " + r.value)
:] else [:
    print("error: " + r.error)
:]
```

### Contracts

```
func int divide(int a, int b) returns int
    requires b != 0
[:
    return a / b
:]
```

`requires` is checked before the function body executes. `ensures` is checked before return, with `__result` bound to the return value.

### Arrays

```
int[] scores = [10, 20, 30]
set scores = append(scores, 40)
int first = scores[0]
int size = len(scores)
```

### Comments

```
@@ single-line comment

@@@
    multi-line
    comment
@@@
```

### Multiline strings

```
string msg = """
    Hello,
    World!
"""
```

---

## AI Features

### ask() — runtime LLM query

```
ai_response r = ask("What is the boiling point of water in Celsius?")
print(r.content)
print(r.model)
print(r.tokens)
```

### @ai — AI-generated function body

```
@ai "implement bubble sort for a int array, return sorted copy"
func int[] bubble_sort(int[] arr) returns int[] [:
:]
```

On first run the AI generates a `(block ...)` S-expr body and caches it in `.jibl_cache/`. Subsequent runs load from cache — no network needed.

### Configuration

```bash
export JIBL_AI_PROVIDER=anthropic   # or: openai
export JIBL_AI_KEY=sk-ant-...
export JIBL_AI_MODEL=claude-sonnet-4-6   # optional, has defaults
```

---

## Keyword Reference

| Concept | English | Français | Español |
|---|---|---|---|
| if | `if` | `si` | `si` |
| else | `else` | `sinon` | `sino` |
| while | `while` | `tantque` | `mientras` |
| function | `func` | `fonc` | `func` |
| return | `return` | `retour` | `retorno` |
| returns (sig) | `returns` | `retourne` | `retorna` |
| assignment | `set` | `set` | `set` |
| print | `print` | `afficher` | `mostrar` |
| call (stmt) | `call` | `appel` | `llamar` |
| requires | `requires` | `necessite` | `requiere` |
| ensures | `ensures` | `garantit` | `garantiza` |
| ask (AI) | `ask` | `demander` | `preguntar` |
| append | `append` | `ajouter` | `agregar` |
| length | `len` | `longueur` | `longitud` |
| true | `true` | `vrai` | `verdadero` |
| false | `false` | `faux` | `falso` |
| and | `and` | `et` | `y` |
| or | `or` | `ou` | `o` |
| not | `not` | `non` | `no` |
| int type | `int` | `entier` | `entero` |
| float type | `float` | `decimal` | `decimal` |
| string type | `string` | `chaine` | `cadena` |
| bool type | `bool` | `booleen` | `booleano` |
| void type | `void` | `vide` | `vacio` |
| result type | `result` | `resultat` | `resultado` |
| ai_response type | `ai_response` | `reponse_ia` | `respuesta_ia` |

`code`, `const`, `set`, `[:`, `:]`, `@@`, `@@@`, `"""`, `@ai`, `ok`, `error`, and all stdlib function names are the same in every language.

---

## Examples

| File | What it demonstrates |
|---|---|
| `examples/hello_en.jibl` | Hello World in English |
| `examples/hello_fr.jibl` | Hello World in Français |
| `examples/hello_es.jibl` | Hello World in Español |
| `examples/fibonacci_en.jibl` | Recursion and while loop |
| `examples/contracts_en.jibl` | `requires` contract |
| `examples/result_type_en.jibl` | `result<T,E>` error handling |
| `examples/arrays_en.jibl` | Arrays, `append`, `len`, indexing |
| `examples/ai_ask_en.jibl` | `ask()` runtime AI query |

---

## License

MIT — see [LICENSE](LICENSE).

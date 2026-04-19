# JibL Examples

Each topic is available in three languages: **English** (`_en`), **Français** (`_fr`), **Español** (`_es`).

---

## Tour — full language walkthrough

The tour files cover every JibL feature in a single coherent program (a grade-book calculator).

| File | Language |
|---|---|
| [tour_en.jibl](tour_en.jibl) | English |
| [tour_fr.jibl](tour_fr.jibl) | Français |
| [tour_es.jibl](tour_es.jibl) | Español |

**Features demonstrated:**
- All primitive types: `int` / `float` / `string` / `bool`
- `const` declarations and multiline strings (`"""`)
- Single-line (`@@`) and multi-line (`@@@`) comments
- Arithmetic, comparison, and boolean operators (`and` / `or` / `not`)
- `if` / `else` branching
- `while` loops
- Recursive functions (`factorial`)
- `requires` preconditions and `ensures` postconditions with `__result`
- `result<T,E>` error handling with `ok` / `error` / `.ok` / `.value` / `.error`
- Arrays: literal syntax, indexing, `append`, `len`, iteration
- String auto-conversion in concatenation (`"score: " + 42`)

---

## Hello World

The simplest program: define a greeting function, call it with a constant.

| File | Language |
|---|---|
| [hello_en.jibl](hello_en.jibl) | English |
| [hello_fr.jibl](hello_fr.jibl) | Français |
| [hello_es.jibl](hello_es.jibl) | Español |

**Features demonstrated:**
- `code <language>` file header
- `func` / `returns` / `return`
- `const` string declaration
- `call` statement (discarding the return value)
- `print` / `afficher` / `mostrar`

---

## Fibonacci

Two implementations side by side, followed by a correctness check.

| File | Language |
|---|---|
| [fibonacci_en.jibl](fibonacci_en.jibl) | English |
| [fibonacci_fr.jibl](fibonacci_fr.jibl) | Français |
| [fibonacci_es.jibl](fibonacci_es.jibl) | Español |

**Features demonstrated:**
- Recursive functions with a `requires` precondition
- `while` loop for the iterative version
- Block-scoped variable declarations inside a loop body
- Modifying outer-scope variables with `set` from inside a block
- Boolean accumulator pattern (`all_match` / `tout_identique` / `todo_igual`)

---

## Contracts

Preconditions (`requires`) and postconditions (`ensures`) checked at runtime.

| File | Language |
|---|---|
| [contracts_en.jibl](contracts_en.jibl) | English |
| [contracts_fr.jibl](contracts_fr.jibl) | Français |
| [contracts_es.jibl](contracts_es.jibl) | Español |

**Features demonstrated:**
- `requires expr` — verified before the function body executes
- `ensures expr` — verified before return; `__result` is bound to the return value
- Multiple `requires` / `ensures` clauses on one function
- `clamp` as a canonical example of combined pre- and postconditions
- Commented-out line that would trigger a contract violation at runtime

---

## Result Type

Explicit error handling without exceptions using `result<T,E>`.

| File | Language |
|---|---|
| [result_type_en.jibl](result_type_en.jibl) | English |
| [result_type_fr.jibl](result_type_fr.jibl) | Français |
| [result_type_es.jibl](result_type_es.jibl) | Español |

**Features demonstrated:**
- `result<T,E>` return type declaration
- `ok(value)` and `error(msg)` constructors
- `.ok`, `.value`, `.error` field access
- Success path and error path both exercised
- Result chaining: the output of one fallible function fed into another

---

## Arrays

Declaring, indexing, growing, and iterating over homogeneous arrays.

| File | Language |
|---|---|
| [arrays_en.jibl](arrays_en.jibl) | English |
| [arrays_fr.jibl](arrays_fr.jibl) | Français |
| [arrays_es.jibl](arrays_es.jibl) | Español |

**Features demonstrated:**
- Array literal syntax: `[10, 20, 30]`
- `len` / `longueur` / `longitud` — array length
- `append` / `ajouter` / `agregar` — non-destructive growth (returns new array)
- Index access: `arr[i]`
- Last-element access: `arr[len(arr) - 1]`
- Min / max scan with `while` and `if`
- Sum and average computation using a `float` accumulator
- String arrays alongside integer arrays

---

## AI — ask()

Runtime LLM queries: send a prompt, receive an `ai_response`.

| File | Language |
|---|---|
| [ai_ask_en.jibl](ai_ask_en.jibl) | English |
| [ai_ask_fr.jibl](ai_ask_fr.jibl) | Français |
| [ai_ask_es.jibl](ai_ask_es.jibl) | Español |

**Features demonstrated:**
- `ask("prompt")` → `ai_response`
- `.content`, `.model`, `.tokens` fields on `ai_response`
- Variable interpolation in the prompt string (`"explain " + topic`)
- Asking questions in the file's own language (the LLM responds accordingly)

**Setup required:**
```bash
export JIBL_AI_PROVIDER=anthropic      # or: openai
export JIBL_AI_KEY=sk-ant-...
export JIBL_AI_MODEL=claude-sonnet-4-6 # optional — has defaults
./jibl examples/ai_ask_en.jibl
```

---

## Running any example

```bash
./jibl examples/<file>.jibl        # compile and run
./jibl --emit examples/<file>.jibl # print S-expression IR, do not execute
```

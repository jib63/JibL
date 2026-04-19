# JibL Architecture

## Pipeline

```
.jibl source file
    │
    ▼
┌─────────┐
│  Lexer  │  src/lexer.c
│         │  Reads UTF-8 source, produces TokenList.
│         │  Detects language header (code english / français / español),
│         │  selects keyword table, handles [: :] and @ disambiguation.
└────┬────┘
     │ TokenList
     ▼
┌─────────┐
│ Parser  │  src/parser.c
│         │  Recursive descent. Produces AST.
│         │  Handles result<T,E>, contracts, @ai annotation, field access.
└────┬────┘
     │ ASTNode tree
     ▼
┌─────────┐
│  Sema   │  src/sema.c
│         │  Compile-time checks: block scope, no shadowing, const mutation.
│         │  Does NOT do type inference — types are explicit.
└────┬────┘
     │ validated ASTNode tree
     ▼
┌─────────┐
│ Emitter │  src/emitter.c
│         │  AST → S-expression IR text.
│         │  --emit flag stops here and prints IR to stdout.
└────┬────┘
     │ S-expr IR string
     ▼
┌──────────────┐
│  Sexp Reader │  src/sexp.c
│              │  Parses S-expr text into a Sexp tree.
└──────┬───────┘
       │ Sexp tree
       ▼
┌──────────────────────────────────────────────────────┐
│  VM  (tree-walking evaluator)         src/vm.c        │
│                                                       │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────┐  │
│  │ stdlib_file  │  │ stdlib_http  │  │ stdlib_json│  │
│  │ file_read    │  │ http_get     │  │ json_parse │  │
│  │ file_write   │  │ http_post    │  │ json_get_* │  │
│  └──────────────┘  └──────────────┘  └────────────┘  │
│                     ┌──────────────┐                  │
│                     │  stdlib_ai   │                  │
│                     │  ask()       │                  │
│                     │  @ai cache   │                  │
│                     └──────────────┘                  │
└──────────────────────────────────────────────────────┘
```

---

## Project Structure

```
JibL/
├── Makefile
├── README.md
├── ARCHITECTURE.md
├── LICENSE
├── .jibl_cache/              ← @ai-generated bodies (git-ignored)
├── include/
│   ├── error.h               ← jibl_error() fatal error handler
│   ├── lexer.h               ← TokenType enum, Token, TokenList
│   ├── ast.h                 ← JiblType, TypeDesc, NodeType, ASTNode
│   ├── parser.h              ← parse_program()
│   ├── sema.h                ← sema_check()
│   ├── emitter.h             ← emitter_emit()
│   ├── sexp.h                ← Sexp, sexp_parse()
│   ├── vm.h                  ← Value, Env, VM, vm_run()
│   ├── stdlib_file.h
│   ├── stdlib_http.h         ← requires libcurl
│   ├── stdlib_json.h
│   └── stdlib_ai.h
├── src/
│   ├── main.c
│   ├── error.c
│   ├── lexer.c
│   ├── ast.c
│   ├── parser.c
│   ├── sema.c
│   ├── emitter.c
│   ├── sexp.c
│   ├── vm.c
│   ├── stdlib_file.c
│   ├── stdlib_http.c
│   ├── stdlib_json.c
│   └── stdlib_ai.c
└── examples/
    ├── hello_en.jibl
    ├── hello_fr.jibl
    ├── hello_es.jibl
    ├── fibonacci_en.jibl
    ├── contracts_en.jibl
    ├── result_type_en.jibl
    ├── arrays_en.jibl
    └── ai_ask_en.jibl
```

---

## Key Data Structures

### Token (lexer.h)

```c
typedef struct {
    TokenType type;
    char     *lexeme;   /* heap-allocated text */
    int       line;
} Token;
```

Notable token types: `TOK_BLOCK_OPEN` (`[:`), `TOK_BLOCK_CLOSE` (`:]`),
`TOK_LBRACKET` (`[`), `TOK_AI_ANNOT` (stores the prompt string as lexeme),
`TOK_DOT`, `TOK_TYPE_RESULT`, `TOK_TYPE_JSON`, `TOK_TYPE_AI_RESPONSE`.

### TypeDesc (ast.h)

```c
typedef struct TypeDesc {
    JiblType         base;       /* JTYPE_INT, JTYPE_RESULT, JTYPE_ARRAY, … */
    struct TypeDesc *elem_type;  /* T[] → elem_type = T */
    struct TypeDesc *ok_type;    /* result<T,E> → ok_type = T */
    struct TypeDesc *err_type;   /* result<T,E> → err_type = E */
} TypeDesc;
```

### ASTNode (ast.h)

Union-based, one struct per node kind:

```c
typedef struct ASTNode {
    NodeType kind;
    int      line;
    union {
        struct { TypeDesc type; char *name; struct ASTNode *init; int is_const; } var_decl;
        struct { char *name; struct ASTNode *value; }                             assign;
        struct { struct ASTNode *cond; NodeList then_body; NodeList else_body; }  if_stmt;
        struct { struct ASTNode *cond; NodeList body; }                           while_stmt;
        struct { char *name; Param *params; int param_count;
                 TypeDesc return_type;
                 NodeList requires; NodeList ensures;
                 NodeList body; char *ai_prompt; }                                func_decl;
        struct { struct ASTNode *expr; }                                          return_stmt;
        struct { NodeList args; }                                                 print_stmt;
        struct { char *name; NodeList args; }                                     call_expr;
        struct { struct ASTNode *obj; char *field; }                              field_access;
        struct { struct ASTNode *inner; }                                         ok_expr;
        struct { struct ASTNode *inner; }                                         error_expr;
        /* … literals, array, index, binary, unary, ai_ask … */
    };
} ASTNode;
```

### Sexp (sexp.h)

```c
typedef enum { SEXP_ATOM, SEXP_LIST } SexpType;

typedef struct Sexp {
    SexpType type;
    char    *atom;       /* SEXP_ATOM */
    struct Sexp **elems; /* SEXP_LIST */
    int      count;
    int      cap;
} Sexp;
```

### Value (vm.h)

```c
typedef struct Value_s Value;   /* forward declaration enables self-reference */

typedef struct { Value *data; int len; int cap; } ArrayVal;

struct Value_s {
    ValType type;   /* VAL_INT, VAL_FLOAT, VAL_STRING, VAL_BOOL,
                       VAL_ARRAY, VAL_RESULT, VAL_JSON, VAL_AI_RESPONSE, VAL_VOID */
    union {
        long   i;
        double f;
        char  *s;
        int    b;
        ArrayVal             *arr;
        struct { int ok; Value *payload; } result;
        struct JsonNode      *json;
        struct { char *content; char *model; int tokens; } ai;
    };
};
```

`val_copy()` performs a deep copy (strings, arrays, result payloads, ai responses).

### Env (vm.h)

```c
typedef struct Env {
    EnvEntry  *buckets[ENV_BUCKETS];
    struct Env *parent;
} Env;
```

`env_get()` / `env_set()` walk the parent chain. `env_set_const()` marks an
entry read-only. Scopes are created by `exec_block()` and freed on exit.

### ExecResult (vm.h)

```c
typedef struct { int did_return; Value return_value; } ExecResult;
```

Propagates `return` statements through nested `exec_block()` calls without
using `setjmp/longjmp`. Every `exec()` call returns an `ExecResult`; callers
check `did_return` to short-circuit.

---

## Trilingual Keyword Dispatch

```
detect_language()
    reads "code english" / "code français" / "code español" from line 1
    sets active_table = KW_EN | KW_FR | KW_ES

lex_ident()
    looks up lexeme in KW_COMMON[] first (const, set, ok, error, stdlib names…)
    then looks up in active_table[]
    returns keyword TokenType, or TOK_IDENT if not found
```

The three keyword tables (`KW_EN`, `KW_FR`, `KW_ES`) map identifier strings to
`TokenType` values. Because all tables map to the same `TokenType` enum, the
rest of the pipeline (parser, sema, emitter, VM) is language-agnostic.

---

## S-Expression IR — Node Reference

```lisp
(program stmts…)

;; Declarations
(decl  type name expr)          ;; mutable variable
(const type name expr)          ;; read-only variable
(set   name expr)               ;; assignment

;; Functions
(func name (params (type name)…) (returns type)
           (requires expr…)
           (ensures  expr…)
           body)

;; Control flow
(if cond then-block else-block?)
(while cond body)
(return expr?)
(block stmts…)

;; I/O
(print args…)
(call name args…)               ;; statement call

;; result<T,E> constructors and field access
(ok    expr)
(error expr)
(field-get expr "ok")
(field-get expr "value")
(field-get expr "error")

;; Arithmetic / comparison / logical
(+  a b)(- a b)(* a b)(/ a b)(% a b)
(== a b)(!= a b)(< a b)(<= a b)(> a b)(>= a b)
(and a b)(or a b)(not a)(neg a)

;; Literals and variables
(int    42)
(float  3.14)
(str    "hello")
(bool   true)
(var    name)

;; Arrays
(array  elems…)
(index  arr idx)
(call append arr val)
(call len    arr)

;; Stdlib
(call file_read          path)
(call file_write         path content)
(call http_get           url)
(call http_post          url body)
(call json_parse         str)
(call json_get_string    obj key)
(call json_get_int       obj key)
(call json_get_bool      obj key)

;; AI
(ai-ask   prompt)               ;; ask() — runtime LLM call, returns ai_response
(ai-func  hash sig prompt)      ;; @ai — cache key + deferred body generation
```

---

## AI Integration

### ask() — runtime query

```
ask("prompt")
    │
    ▼
stdlib_ai_ask()
    reads JIBL_AI_PROVIDER, JIBL_AI_KEY, JIBL_AI_MODEL
    builds JSON request body
    POST to Anthropic messages API  OR  OpenAI chat/completions
    parses response: extracts content, model, token count
    returns VAL_AI_RESPONSE { content, model, tokens }
```

### @ai — AI-generated function body with caching

```
@ai "sort an int array, return sorted copy"
func int[] sort(int[] arr) returns int[] [:
:]

On first run:
    cache_key = SHA-256( func_name + "|" + ai_prompt )
    check .jibl_cache/<key>.sexp
    if miss:
        build prompt: function signature as S-expr + user annotation
        POST to AI API requesting only a (block ...) S-expr node
        write response to .jibl_cache/<key>.sexp
    inject cached (block ...) as function body in VM func table

On subsequent runs:
    cache hit → load from disk, no network call
```

The AI prompt explicitly instructs the model to return only a valid
`(block ...)` S-expr with no markdown fences or explanation, so the
response can be fed directly into `sexp_parse()`.

---

## Build

```
CC    = cc
FLAGS = -std=c11 -Wall -Wextra -Wpedantic -g
LIBS  = -lcurl
```

C11 is required for anonymous unions in `Value`. `libcurl` is required for
`stdlib_http` and `stdlib_ai`. The build produces a single `jibl` binary.

#ifndef JIBL_VM_H
#define JIBL_VM_H

#include "sexp.h"

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
    VAL_VOID,
    VAL_ARRAY,
    VAL_RESULT,
    VAL_JSON,
    VAL_AI_RESPONSE
} ValType;

/* Forward declaration so Value* can be used inside Value itself */
typedef struct Value_s Value;

struct JsonNode;

typedef struct ArrayVal {
    Value   *elems;
    int      count;
    int      capacity;
    ValType  elem_type;
} ArrayVal;

struct Value_s {
    ValType type;
    union {
        long   i;
        double f;
        char  *s;   /* heap-allocated, owned */
        int    b;
        ArrayVal        *arr;
        struct {
            int    ok;
            Value *payload;  /* heap-allocated */
        } result;
        struct JsonNode *json;
        struct {
            char *content;  /* heap-allocated */
            char *model;    /* heap-allocated */
            int   tokens;
        } ai;
    };
};

/* ---- Environment ---- */

#define ENV_BUCKETS 64

typedef struct EnvEntry {
    char            *name;
    int              is_const;
    Value            value;
    struct EnvEntry *next;
} EnvEntry;

typedef struct Env {
    EnvEntry   *buckets[ENV_BUCKETS];
    struct Env *parent;
} Env;

/* ---- Function table ---- */

typedef struct {
    char  *name;
    Sexp  *decl;        /* points into sexp tree */
    char  *ai_prompt;   /* NULL unless @ai annotated */
    char  *ai_sig;      /* serialized signature for cache key */
} FuncEntry;

/* ---- VM ---- */

typedef struct {
    FuncEntry *funcs;
    int        func_count;
    int        func_cap;
    Env       *globals;
} VM;

/* ---- ExecResult ---- */

typedef struct {
    int   did_return;
    Value return_value;
} ExecResult;

/* Public API */
VM       *vm_new(void);
void      vm_free(VM *vm);
void      vm_run(VM *vm, Sexp *program);

/* Value helpers */
Value     val_int(long i);
Value     val_float(double f);
Value     val_string(const char *s);
Value     val_bool(int b);
Value     val_void(void);
Value     val_copy(Value v);
void      val_free(Value v);
void      val_print(Value v);
Value     val_result_ok(Value payload);
Value     val_result_err(Value payload);

/* Env helpers */
Env      *env_new(Env *parent);
void      env_free(Env *e);
void      env_define(Env *e, const char *name, Value v, int is_const);
Value     env_get(Env *e, const char *name, int line);
void      env_set(Env *e, const char *name, Value v, int line);

#endif

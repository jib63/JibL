#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm.h"
#include "error.h"
#include "stdlib_file.h"
#include "stdlib_json.h"
#include "stdlib_http.h"
#include "stdlib_ai.h"

/* ---- Value helpers ---- */

Value val_int(long i)          { Value v; v.type = VAL_INT;   v.i = i;          return v; }
Value val_float(double f)      { Value v; v.type = VAL_FLOAT; v.f = f;          return v; }
Value val_bool(int b)          { Value v; v.type = VAL_BOOL;  v.b = b;          return v; }
Value val_void(void)           { Value v; v.type = VAL_VOID;  v.i = 0;          return v; }

Value val_string(const char *s) {
    Value v; v.type = VAL_STRING;
    v.s = strdup(s ? s : "");
    return v;
}

Value val_result_ok(Value payload) {
    Value v; v.type = VAL_RESULT; v.result.ok = 1;
    v.result.payload = malloc(sizeof(Value));
    *v.result.payload = val_copy(payload);
    return v;
}

Value val_result_err(Value payload) {
    Value v; v.type = VAL_RESULT; v.result.ok = 0;
    v.result.payload = malloc(sizeof(Value));
    *v.result.payload = val_copy(payload);
    return v;
}

Value val_copy(Value v) {
    switch (v.type) {
        case VAL_STRING: return val_string(v.s);
        case VAL_RESULT: {
            if (v.result.ok)
                return val_result_ok(val_copy(*v.result.payload));
            else
                return val_result_err(val_copy(*v.result.payload));
        }
        case VAL_ARRAY: {
            ArrayVal *src = v.arr;
            ArrayVal *dst = malloc(sizeof(ArrayVal));
            dst->count    = src->count;
            dst->capacity = src->count ? src->count : 1;
            dst->elem_type = src->elem_type;
            dst->elems    = malloc(sizeof(Value) * (size_t)dst->capacity);
            for (int i = 0; i < src->count; i++)
                dst->elems[i] = val_copy(src->elems[i]);
            Value a; a.type = VAL_ARRAY; a.arr = dst;
            return a;
        }
        case VAL_AI_RESPONSE: {
            Value r; r.type = VAL_AI_RESPONSE;
            r.ai.content = strdup(v.ai.content ? v.ai.content : "");
            r.ai.model   = strdup(v.ai.model   ? v.ai.model   : "");
            r.ai.tokens  = v.ai.tokens;
            return r;
        }
        default: return v;
    }
}

void val_free(Value v) {
    switch (v.type) {
        case VAL_STRING: free(v.s); break;
        case VAL_RESULT:
            if (v.result.payload) { val_free(*v.result.payload); free(v.result.payload); }
            break;
        case VAL_ARRAY:
            if (v.arr) {
                for (int i = 0; i < v.arr->count; i++) val_free(v.arr->elems[i]);
                free(v.arr->elems);
                free(v.arr);
            }
            break;
        case VAL_AI_RESPONSE:
            free(v.ai.content); free(v.ai.model);
            break;
        default: break;
    }
}

void val_print(Value v) {
    switch (v.type) {
        case VAL_INT:    printf("%ld", v.i);   break;
        case VAL_FLOAT:  printf("%g",  v.f);   break;
        case VAL_STRING: printf("%s",  v.s);   break;
        case VAL_BOOL:   printf("%s",  v.b ? "true" : "false"); break;
        case VAL_VOID:   break;
        case VAL_RESULT:
            if (v.result.ok) { printf("ok("); val_print(*v.result.payload); printf(")"); }
            else              { printf("error("); val_print(*v.result.payload); printf(")"); }
            break;
        case VAL_ARRAY:
            printf("[");
            for (int i = 0; i < v.arr->count; i++) {
                if (i > 0) printf(", ");
                val_print(v.arr->elems[i]);
            }
            printf("]");
            break;
        case VAL_AI_RESPONSE:
            printf("%s", v.ai.content ? v.ai.content : "");
            break;
        case VAL_JSON:
            printf("<json>");
            break;
    }
}

/* ---- Environment ---- */

static unsigned int env_hash(const char *name) {
    unsigned int h = 5381;
    while (*name) h = h * 33 + (unsigned char)*name++;
    return h % ENV_BUCKETS;
}

Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env));
    e->parent = parent;
    return e;
}

void env_free(Env *e) {
    if (!e) return;
    for (int i = 0; i < ENV_BUCKETS; i++) {
        EnvEntry *en = e->buckets[i];
        while (en) {
            EnvEntry *next = en->next;
            free(en->name);
            val_free(en->value);
            free(en);
            en = next;
        }
    }
    free(e);
}

void env_define(Env *e, const char *name, Value v, int is_const) {
    unsigned int h = env_hash(name);
    EnvEntry *en = malloc(sizeof(EnvEntry));
    en->name     = strdup(name);
    en->value    = val_copy(v);
    en->is_const = is_const;
    en->next     = e->buckets[h];
    e->buckets[h] = en;
}

Value env_get(Env *e, const char *name, int line) {
    for (Env *cur = e; cur; cur = cur->parent) {
        unsigned int h = env_hash(name);
        for (EnvEntry *en = cur->buckets[h]; en; en = en->next)
            if (strcmp(en->name, name) == 0) return val_copy(en->value);
    }
    jibl_error(line, "undefined variable '%s'", name);
    return val_void();
}

void env_set(Env *e, const char *name, Value v, int line) {
    for (Env *cur = e; cur; cur = cur->parent) {
        unsigned int h = env_hash(name);
        for (EnvEntry *en = cur->buckets[h]; en; en = en->next) {
            if (strcmp(en->name, name) == 0) {
                if (en->is_const) jibl_error(line, "cannot assign to const '%s'", name);
                val_free(en->value);
                en->value = val_copy(v);
                return;
            }
        }
    }
    jibl_error(line, "undefined variable '%s' (use type declaration to introduce it)", name);
}

/* ---- VM ---- */

VM *vm_new(void) {
    VM *vm = calloc(1, sizeof(VM));
    vm->globals  = env_new(NULL);
    vm->func_cap = 16;
    vm->funcs    = malloc(sizeof(FuncEntry) * (size_t)vm->func_cap);
    return vm;
}

void vm_free(VM *vm) {
    env_free(vm->globals);
    for (int i = 0; i < vm->func_count; i++) {
        free(vm->funcs[i].name);
        free(vm->funcs[i].ai_prompt);
        free(vm->funcs[i].ai_sig);
    }
    free(vm->funcs);
    free(vm);
}

static void vm_register_func(VM *vm, Sexp *decl) {
    if (vm->func_count >= vm->func_cap) {
        vm->func_cap *= 2;
        vm->funcs = realloc(vm->funcs, sizeof(FuncEntry) * (size_t)vm->func_cap);
    }
    FuncEntry *fe = &vm->funcs[vm->func_count++];
    fe->decl      = decl;
    fe->ai_prompt = NULL;
    fe->ai_sig    = NULL;
    /* name is 2nd element of (func name ...) */
    Sexp *name_s  = sexp_nth(decl, 1);
    fe->name      = strdup(sexp_atom(name_s) ? sexp_atom(name_s) : "?");
}

static FuncEntry *vm_find_func(VM *vm, const char *name) {
    for (int i = 0; i < vm->func_count; i++)
        if (strcmp(vm->funcs[i].name, name) == 0) return &vm->funcs[i];
    return NULL;
}

/* Forward declarations */
static Value     eval(VM *vm, Sexp *s, Env *env);
static ExecResult exec(VM *vm, Sexp *s, Env *env);
static ExecResult exec_block(VM *vm, Sexp *block, Env *parent_env);

/* ---- Arithmetic / comparison helpers ---- */

static Value do_arith(const char *op, Value a, Value b, int line) {
    /* Promote int to float if mixed */
    if ((a.type == VAL_INT || a.type == VAL_FLOAT) &&
        (b.type == VAL_INT || b.type == VAL_FLOAT)) {
        if (strcmp(op, "+") == 0) {
            if (a.type == VAL_INT && b.type == VAL_INT) return val_int(a.i + b.i);
            double fa = a.type == VAL_INT ? (double)a.i : a.f;
            double fb = b.type == VAL_INT ? (double)b.i : b.f;
            return val_float(fa + fb);
        }
        if (strcmp(op, "-") == 0) {
            if (a.type == VAL_INT && b.type == VAL_INT) return val_int(a.i - b.i);
            double fa = a.type == VAL_INT ? (double)a.i : a.f;
            double fb = b.type == VAL_INT ? (double)b.i : b.f;
            return val_float(fa - fb);
        }
        if (strcmp(op, "*") == 0) {
            if (a.type == VAL_INT && b.type == VAL_INT) return val_int(a.i * b.i);
            double fa = a.type == VAL_INT ? (double)a.i : a.f;
            double fb = b.type == VAL_INT ? (double)b.i : b.f;
            return val_float(fa * fb);
        }
        if (strcmp(op, "/") == 0) {
            if (b.type == VAL_INT && b.i == 0) jibl_error(line, "division by zero");
            if (b.type == VAL_FLOAT && b.f == 0.0) jibl_error(line, "division by zero");
            if (a.type == VAL_INT && b.type == VAL_INT) return val_int(a.i / b.i);
            double fa = a.type == VAL_INT ? (double)a.i : a.f;
            double fb = b.type == VAL_INT ? (double)b.i : b.f;
            return val_float(fa / fb);
        }
        if (strcmp(op, "%") == 0) {
            if (a.type != VAL_INT || b.type != VAL_INT)
                jibl_error(line, "%% requires integer operands");
            if (b.i == 0) jibl_error(line, "modulo by zero");
            return val_int(a.i % b.i);
        }
    }
    /* String concatenation with + (auto-converts any type to string) */
    if (strcmp(op, "+") == 0 &&
        (a.type == VAL_STRING || b.type == VAL_STRING)) {
        /* Convert both sides to string */
        char bufa[64], bufb[64];
        const char *sa, *sb;
        if (a.type == VAL_STRING) { sa = a.s; }
        else if (a.type == VAL_INT)   { snprintf(bufa, sizeof(bufa), "%ld", a.i); sa = bufa; }
        else if (a.type == VAL_FLOAT) { snprintf(bufa, sizeof(bufa), "%g",  a.f); sa = bufa; }
        else if (a.type == VAL_BOOL)  { sa = a.b ? "true" : "false"; }
        else { jibl_error(line, "cannot convert value to string for concatenation"); sa = ""; }
        if (b.type == VAL_STRING) { sb = b.s; }
        else if (b.type == VAL_INT)   { snprintf(bufb, sizeof(bufb), "%ld", b.i); sb = bufb; }
        else if (b.type == VAL_FLOAT) { snprintf(bufb, sizeof(bufb), "%g",  b.f); sb = bufb; }
        else if (b.type == VAL_BOOL)  { sb = b.b ? "true" : "false"; }
        else { jibl_error(line, "cannot convert value to string for concatenation"); sb = ""; }
        size_t la = strlen(sa), lb = strlen(sb);
        char *res = malloc(la + lb + 1);
        memcpy(res, sa, la); memcpy(res + la, sb, lb); res[la + lb] = '\0';
        Value r = val_string(res);
        free(res);
        return r;
    }
    jibl_error(line, "type error in operator '%s'", op);
    return val_void();
}

static Value do_compare(const char *op, Value a, Value b, int line) {
    int result = 0;
    if ((a.type == VAL_INT || a.type == VAL_FLOAT) &&
        (b.type == VAL_INT || b.type == VAL_FLOAT)) {
        double fa = a.type == VAL_INT ? (double)a.i : a.f;
        double fb = b.type == VAL_INT ? (double)b.i : b.f;
        if      (strcmp(op, "==") == 0) result = fa == fb;
        else if (strcmp(op, "!=") == 0) result = fa != fb;
        else if (strcmp(op, "<")  == 0) result = fa <  fb;
        else if (strcmp(op, "<=") == 0) result = fa <= fb;
        else if (strcmp(op, ">")  == 0) result = fa >  fb;
        else if (strcmp(op, ">=") == 0) result = fa >= fb;
    } else if (a.type == VAL_STRING && b.type == VAL_STRING) {
        int cmp = strcmp(a.s, b.s);
        if      (strcmp(op, "==") == 0) result = cmp == 0;
        else if (strcmp(op, "!=") == 0) result = cmp != 0;
        else if (strcmp(op, "<")  == 0) result = cmp <  0;
        else if (strcmp(op, "<=") == 0) result = cmp <= 0;
        else if (strcmp(op, ">")  == 0) result = cmp >  0;
        else if (strcmp(op, ">=") == 0) result = cmp >= 0;
    } else if (a.type == VAL_BOOL && b.type == VAL_BOOL) {
        if      (strcmp(op, "==") == 0) result = a.b == b.b;
        else if (strcmp(op, "!=") == 0) result = a.b != b.b;
        else jibl_error(line, "cannot use '%s' with bool values", op);
    } else {
        jibl_error(line, "type mismatch in comparison '%s'", op);
    }
    return val_bool(result);
}

/* ---- Evaluator ---- */

static Value eval(VM *vm, Sexp *s, Env *env) {
    if (!s) return val_void();

    /* Atom: number, string, bool, or identifier — should not appear at top level of eval */
    if (s->type == SEXP_ATOM) {
        jibl_error(0, "unexpected atom in eval: %s", s->atom);
        return val_void();
    }

    int len = sexp_list_len(s);
    if (len == 0) return val_void();

    const char *car = sexp_atom(sexp_nth(s, 0));
    if (!car) { jibl_error(0, "eval: car is not an atom"); return val_void(); }

    /* Literals */
    if (strcmp(car, "int") == 0) {
        const char *v = sexp_atom(sexp_nth(s, 1));
        return val_int(v ? atol(v) : 0);
    }
    if (strcmp(car, "float") == 0) {
        const char *v = sexp_atom(sexp_nth(s, 1));
        return val_float(v ? atof(v) : 0.0);
    }
    if (strcmp(car, "str") == 0) {
        const char *v = sexp_atom(sexp_nth(s, 1));
        return val_string(v ? v : "");
    }
    if (strcmp(car, "bool") == 0) {
        const char *v = sexp_atom(sexp_nth(s, 1));
        return val_bool(v && strcmp(v, "true") == 0);
    }
    if (strcmp(car, "void") == 0) return val_void();

    /* Variable reference */
    if (strcmp(car, "var") == 0) {
        const char *name = sexp_atom(sexp_nth(s, 1));
        if (!name) jibl_error(0, "var: missing name");
        return env_get(env, name, 0);
    }

    /* Arithmetic / string concat */
    if (strcmp(car, "+") == 0 || strcmp(car, "-") == 0 ||
        strcmp(car, "*") == 0 || strcmp(car, "/") == 0 ||
        strcmp(car, "%") == 0) {
        Value a = eval(vm, sexp_nth(s, 1), env);
        Value b = eval(vm, sexp_nth(s, 2), env);
        Value r = do_arith(car, a, b, 0);
        val_free(a); val_free(b);
        return r;
    }

    /* Unary */
    if (strcmp(car, "neg") == 0) {
        Value a = eval(vm, sexp_nth(s, 1), env);
        Value r = val_void();
        if (a.type == VAL_INT)   r = val_int(-a.i);
        else if (a.type == VAL_FLOAT) r = val_float(-a.f);
        else jibl_error(0, "neg: expected numeric value");
        val_free(a); return r;
    }
    if (strcmp(car, "not") == 0) {
        Value a = eval(vm, sexp_nth(s, 1), env);
        if (a.type != VAL_BOOL) jibl_error(0, "not: expected bool");
        Value r = val_bool(!a.b);
        val_free(a); return r;
    }

    /* Comparison */
    if (strcmp(car, "==") == 0 || strcmp(car, "!=") == 0 ||
        strcmp(car, "<")  == 0 || strcmp(car, "<=") == 0 ||
        strcmp(car, ">")  == 0 || strcmp(car, ">=") == 0) {
        Value a = eval(vm, sexp_nth(s, 1), env);
        Value b = eval(vm, sexp_nth(s, 2), env);
        Value r = do_compare(car, a, b, 0);
        val_free(a); val_free(b);
        return r;
    }

    /* Logical */
    if (strcmp(car, "and") == 0) {
        Value a = eval(vm, sexp_nth(s, 1), env);
        if (a.type != VAL_BOOL) jibl_error(0, "and: expected bool");
        if (!a.b) { val_free(a); return val_bool(0); }
        val_free(a);
        Value b = eval(vm, sexp_nth(s, 2), env);
        if (b.type != VAL_BOOL) jibl_error(0, "and: expected bool");
        Value r = val_bool(b.b);
        val_free(b); return r;
    }
    if (strcmp(car, "or") == 0) {
        Value a = eval(vm, sexp_nth(s, 1), env);
        if (a.type != VAL_BOOL) jibl_error(0, "or: expected bool");
        if (a.b) { val_free(a); return val_bool(1); }
        val_free(a);
        Value b = eval(vm, sexp_nth(s, 2), env);
        if (b.type != VAL_BOOL) jibl_error(0, "or: expected bool");
        Value r = val_bool(b.b);
        val_free(b); return r;
    }

    /* result constructors */
    if (strcmp(car, "ok") == 0) {
        Value payload = eval(vm, sexp_nth(s, 1), env);
        Value r = val_result_ok(payload);
        val_free(payload); return r;
    }
    if (strcmp(car, "error") == 0) {
        Value payload = eval(vm, sexp_nth(s, 1), env);
        Value r = val_result_err(payload);
        val_free(payload); return r;
    }

    /* Field access: (field-get expr field) */
    if (strcmp(car, "field-get") == 0) {
        Value obj = eval(vm, sexp_nth(s, 1), env);
        const char *field = sexp_atom(sexp_nth(s, 2));
        if (!field) jibl_error(0, "field-get: missing field name");
        Value r = val_void();
        if (obj.type == VAL_RESULT) {
            if (strcmp(field, "ok") == 0) { r = val_bool(obj.result.ok); }
            else if (strcmp(field, "value") == 0) {
                if (!obj.result.ok) jibl_error(0, "accessed .value on an error result");
                r = val_copy(*obj.result.payload);
            } else if (strcmp(field, "error") == 0) {
                if (obj.result.ok) jibl_error(0, "accessed .error on an ok result");
                r = val_copy(*obj.result.payload);
            } else jibl_error(0, "unknown result field '%s'", field);
        } else if (obj.type == VAL_AI_RESPONSE) {
            if      (strcmp(field, "content") == 0) r = val_string(obj.ai.content);
            else if (strcmp(field, "model")   == 0) r = val_string(obj.ai.model);
            else if (strcmp(field, "tokens")  == 0) r = val_int(obj.ai.tokens);
            else jibl_error(0, "unknown ai_response field '%s'", field);
        } else {
            jibl_error(0, "field-get: value has no field '%s'", field);
        }
        val_free(obj); return r;
    }

    /* Array literal: (array e1 e2 ...) */
    if (strcmp(car, "array") == 0) {
        ArrayVal *arr = malloc(sizeof(ArrayVal));
        arr->count    = len - 1;
        arr->capacity = arr->count ? arr->count : 1;
        arr->elems    = malloc(sizeof(Value) * (size_t)arr->capacity);
        arr->elem_type = VAL_INT; /* inferred at runtime */
        for (int i = 1; i < len; i++)
            arr->elems[i - 1] = eval(vm, sexp_nth(s, i), env);
        Value v; v.type = VAL_ARRAY; v.arr = arr;
        return v;
    }

    /* Index: (index arr idx) */
    if (strcmp(car, "index") == 0) {
        Value arr = eval(vm, sexp_nth(s, 1), env);
        Value idx = eval(vm, sexp_nth(s, 2), env);
        if (arr.type != VAL_ARRAY) jibl_error(0, "index: not an array");
        if (idx.type != VAL_INT)   jibl_error(0, "index: index must be int");
        if (idx.i < 0 || idx.i >= arr.arr->count)
            jibl_error(0, "index %ld out of bounds (len=%d)", idx.i, arr.arr->count);
        Value r = val_copy(arr.arr->elems[idx.i]);
        val_free(arr); val_free(idx);
        return r;
    }

    /* Function call or stdlib call: (call name args...) */
    if (strcmp(car, "call") == 0) {
        const char *fname = sexp_atom(sexp_nth(s, 1));
        if (!fname) jibl_error(0, "call: missing function name");

        /* collect args */
        int argc = len - 2;
        Value *args = argc > 0 ? malloc(sizeof(Value) * (size_t)argc) : NULL;
        for (int i = 0; i < argc; i++)
            args[i] = eval(vm, sexp_nth(s, 2 + i), env);

        Value result = val_void();

        /* stdlib dispatch */
        if (strcmp(fname, "append") == 0 && argc == 2) {
            if (args[0].type != VAL_ARRAY) jibl_error(0, "append: first arg must be array");
            result = val_copy(args[0]);
            ArrayVal *arr = result.arr;
            if (arr->count >= arr->capacity) {
                arr->capacity = arr->capacity * 2;
                arr->elems = realloc(arr->elems, sizeof(Value) * (size_t)arr->capacity);
            }
            arr->elems[arr->count++] = val_copy(args[1]);
        } else if (strcmp(fname, "len") == 0 && argc == 1) {
            if (args[0].type == VAL_ARRAY)  result = val_int(args[0].arr->count);
            else if (args[0].type == VAL_STRING) result = val_int((long)strlen(args[0].s));
            else jibl_error(0, "len: expected array or string");
        } else if (strcmp(fname, "file_read") == 0 && argc == 1) {
            if (args[0].type != VAL_STRING) jibl_error(0, "file_read: expected string path");
            result = stdlib_file_read(args[0].s);
        } else if (strcmp(fname, "file_write") == 0 && argc == 2) {
            if (args[0].type != VAL_STRING) jibl_error(0, "file_write: expected string path");
            if (args[1].type != VAL_STRING) jibl_error(0, "file_write: expected string content");
            result = stdlib_file_write(args[0].s, args[1].s);
        } else if (strcmp(fname, "http_get") == 0 && argc == 1) {
            if (args[0].type != VAL_STRING) jibl_error(0, "http_get: expected string url");
            result = stdlib_http_get(args[0].s);
        } else if (strcmp(fname, "http_post") == 0 && argc == 2) {
            if (args[0].type != VAL_STRING) jibl_error(0, "http_post: expected string url");
            if (args[1].type != VAL_STRING) jibl_error(0, "http_post: expected string body");
            result = stdlib_http_post(args[0].s, args[1].s);
        } else if (strcmp(fname, "json_parse") == 0 && argc == 1) {
            if (args[0].type != VAL_STRING) jibl_error(0, "json_parse: expected string");
            result = stdlib_json_parse(args[0].s);
        } else if (strcmp(fname, "json_get_string") == 0 && argc == 2) {
            if (args[1].type != VAL_STRING) jibl_error(0, "json_get_string: expected string key");
            result = stdlib_json_get_string(args[0], args[1].s);
        } else if (strcmp(fname, "json_get_int") == 0 && argc == 2) {
            if (args[1].type != VAL_STRING) jibl_error(0, "json_get_int: expected string key");
            result = stdlib_json_get_int(args[0], args[1].s);
        } else if (strcmp(fname, "json_get_bool") == 0 && argc == 2) {
            if (args[1].type != VAL_STRING) jibl_error(0, "json_get_bool: expected string key");
            result = stdlib_json_get_bool(args[0], args[1].s);
        } else {
            /* User-defined function */
            FuncEntry *fe = vm_find_func(vm, fname);
            if (!fe) jibl_error(0, "undefined function '%s'", fname);

            Sexp *decl = fe->decl;
            /* (func name (params ...) (returns ...) ... (block ...)) */
            /* Find params and body */
            Sexp *body_s = NULL;
            Sexp *params_s = NULL;
            for (int i = 2; i < sexp_list_len(decl); i++) {
                Sexp *item = sexp_nth(decl, i);
                if (sexp_is(item, "params"))  params_s = item;
                else if (sexp_is(item, "block")) body_s = item;
            }

            /* Bind parameters */
            Env *fn_env = env_new(vm->globals);
            if (params_s) {
                int pc = sexp_list_len(params_s) - 1;
                if (pc != argc) jibl_error(0, "function '%s' expects %d args, got %d", fname, pc, argc);
                for (int i = 0; i < pc; i++) {
                    Sexp *param = sexp_nth(params_s, i + 1);
                    const char *pname = sexp_atom(sexp_nth(param, 1));
                    if (!pname) jibl_error(0, "func param missing name");
                    env_define(fn_env, pname, args[i], 0);
                }
            }

            /* Check requires contracts */
            for (int i = 2; i < sexp_list_len(decl); i++) {
                Sexp *item = sexp_nth(decl, i);
                if (sexp_is(item, "requires")) {
                    Value cond = eval(vm, sexp_nth(item, 1), fn_env);
                    if (cond.type != VAL_BOOL || !cond.b)
                        jibl_error(0, "precondition violated for '%s'", fname);
                    val_free(cond);
                }
            }

            ExecResult er = {0, val_void()};
            if (body_s) er = exec_block(vm, body_s, fn_env);

            /* Check ensures contracts */
            if (er.did_return) {
                env_define(fn_env, "__result", er.return_value, 1);
                for (int i = 2; i < sexp_list_len(decl); i++) {
                    Sexp *item = sexp_nth(decl, i);
                    if (sexp_is(item, "ensures")) {
                        Value cond = eval(vm, sexp_nth(item, 1), fn_env);
                        if (cond.type != VAL_BOOL || !cond.b)
                            jibl_error(0, "postcondition violated for '%s'", fname);
                        val_free(cond);
                    }
                }
                result = val_copy(er.return_value);
            }
            val_free(er.return_value);
            env_free(fn_env);
        }

        for (int i = 0; i < argc; i++) val_free(args[i]);
        free(args);
        return result;
    }

    /* AI ask */
    if (strcmp(car, "ai-ask") == 0) {
        Value prompt = eval(vm, sexp_nth(s, 1), env);
        if (prompt.type != VAL_STRING) jibl_error(0, "ask: expected string prompt");
        Value r = stdlib_ai_ask(prompt.s);
        val_free(prompt);
        return r;
    }

    jibl_error(0, "eval: unknown form '%s'", car);
    return val_void();
}

/* ---- Statement executor ---- */

static ExecResult exec(VM *vm, Sexp *s, Env *env) {
    ExecResult er; er.did_return = 0; er.return_value = val_void();
    if (!s || s->type != SEXP_LIST) return er;

    const char *car = sexp_atom(sexp_nth(s, 0));
    if (!car) return er;

    /* (decl type name expr) */
    if (strcmp(car, "decl") == 0) {
        const char *name = sexp_atom(sexp_nth(s, 2));
        if (!name) jibl_error(0, "decl: missing name");
        Value v = eval(vm, sexp_nth(s, 3), env);
        env_define(env, name, v, 0);
        val_free(v);
        return er;
    }

    /* (const type name expr) */
    if (strcmp(car, "const") == 0) {
        const char *name = sexp_atom(sexp_nth(s, 2));
        if (!name) jibl_error(0, "const: missing name");
        Value v = eval(vm, sexp_nth(s, 3), env);
        env_define(env, name, v, 1);
        val_free(v);
        return er;
    }

    /* (set name expr) */
    if (strcmp(car, "set") == 0) {
        const char *name = sexp_atom(sexp_nth(s, 1));
        if (!name) jibl_error(0, "set: missing name");
        Value v = eval(vm, sexp_nth(s, 2), env);
        env_set(env, name, v, 0);
        val_free(v);
        return er;
    }

    /* (print args...) */
    if (strcmp(car, "print") == 0) {
        for (int i = 1; i < sexp_list_len(s); i++) {
            Value v = eval(vm, sexp_nth(s, i), env);
            val_print(v);
            val_free(v);
        }
        printf("\n");
        return er;
    }

    /* (return) or (return expr) */
    if (strcmp(car, "return") == 0) {
        er.did_return = 1;
        if (sexp_list_len(s) > 1)
            er.return_value = eval(vm, sexp_nth(s, 1), env);
        return er;
    }

    /* (if cond then else?) */
    if (strcmp(car, "if") == 0) {
        Value cond = eval(vm, sexp_nth(s, 1), env);
        if (cond.type != VAL_BOOL) jibl_error(0, "if: condition must be bool");
        int branch = cond.b;
        val_free(cond);
        if (branch)
            return exec_block(vm, sexp_nth(s, 2), env);
        else if (sexp_list_len(s) > 3)
            return exec_block(vm, sexp_nth(s, 3), env);
        return er;
    }

    /* (while cond body) */
    if (strcmp(car, "while") == 0) {
        while (1) {
            Value cond = eval(vm, sexp_nth(s, 1), env);
            if (cond.type != VAL_BOOL) jibl_error(0, "while: condition must be bool");
            int go = cond.b;
            val_free(cond);
            if (!go) break;
            er = exec_block(vm, sexp_nth(s, 2), env);
            if (er.did_return) return er;
            val_free(er.return_value);
            er.return_value = val_void();
        }
        return er;
    }

    /* Function call as statement: (call fname args...) */
    if (strcmp(car, "call") == 0) {
        Value r = eval(vm, s, env);
        val_free(r);
        return er;
    }

    /* (func ...) — register function */
    if (strcmp(car, "func") == 0) {
        vm_register_func(vm, s);
        return er;
    }

    /* (ai-func name prompt (func ...)) — register AI function */
    if (strcmp(car, "ai-func") == 0) {
        const char *fname   = sexp_atom(sexp_nth(s, 1));
        const char *prompt  = sexp_atom(sexp_nth(s, 2));
        Sexp       *fn_decl = sexp_nth(s, 3);
        if (!fname || !fn_decl) jibl_error(0, "ai-func: malformed");

        /* Try to load cached body */
        char *cached = stdlib_ai_cache_lookup(fname, prompt);
        if (cached) {
            /* Inject cached sexp body into func decl — find block, replace */
            /* For simplicity, create a synthetic func sexp with the cached block */
            Sexp *cached_sexp = sexp_parse(cached);
            /* Replace the last (block ...) in fn_decl with cached_sexp */
            for (int i = sexp_list_len(fn_decl) - 1; i >= 0; i--) {
                if (sexp_is(sexp_nth(fn_decl, i), "block")) {
                    /* swap */
                    sexp_free(fn_decl->list.elems[i]);
                    fn_decl->list.elems[i] = cached_sexp;
                    break;
                }
            }
            free(cached);
        } else {
            /* Generate body via AI */
            /* Build signature string */
            char sig_buf[512] = {0};
            snprintf(sig_buf, sizeof(sig_buf), "(func %s)", fname);

            char *body_sexp = stdlib_ai_generate_func(fname, prompt, sig_buf);
            if (body_sexp) {
                stdlib_ai_cache_store(fname, prompt, body_sexp);
                Sexp *generated = sexp_parse(body_sexp);
                for (int i = sexp_list_len(fn_decl) - 1; i >= 0; i--) {
                    if (sexp_is(sexp_nth(fn_decl, i), "block")) {
                        sexp_free(fn_decl->list.elems[i]);
                        fn_decl->list.elems[i] = generated;
                        break;
                    }
                }
                free(body_sexp);
            }
        }
        vm_register_func(vm, fn_decl);
        return er;
    }

    /* Fallthrough: try as expression statement */
    Value r = eval(vm, s, env);
    val_free(r);
    return er;
}

static ExecResult exec_block(VM *vm, Sexp *block, Env *parent_env) {
    ExecResult er; er.did_return = 0; er.return_value = val_void();
    if (!block) return er;

    Env *env = env_new(parent_env);
    int len = sexp_list_len(block);
    /* skip car "block" */
    for (int i = 1; i < len; i++) {
        er = exec(vm, sexp_nth(block, i), env);
        if (er.did_return) break;
        val_free(er.return_value);
        er.return_value = val_void();
    }
    env_free(env);
    return er;
}

void vm_run(VM *vm, Sexp *program) {
    /* program = (program stmts...) */
    if (!sexp_is(program, "program"))
        jibl_error(0, "vm_run: expected (program ...)");

    /* First pass: register all top-level functions */
    int len = sexp_list_len(program);
    for (int i = 1; i < len; i++) {
        Sexp *s = sexp_nth(program, i);
        if (sexp_is(s, "func")) vm_register_func(vm, s);
        if (sexp_is(s, "ai-func")) {
            /* register during execution pass */
        }
    }

    /* Second pass: execute statements */
    for (int i = 1; i < len; i++) {
        Sexp *s = sexp_nth(program, i);
        if (sexp_is(s, "func")) continue; /* already registered */
        ExecResult er = exec(vm, s, vm->globals);
        val_free(er.return_value);
        if (er.did_return) break;
    }
}

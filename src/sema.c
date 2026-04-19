#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "sema.h"
#include "error.h"

/* ---- Simple scope stack using linked-list of name buckets ---- */

#define SCOPE_BUCKETS 32

typedef struct ScopeEntry {
    char             *name;
    int               is_const;
    struct ScopeEntry *next;
} ScopeEntry;

typedef struct Scope {
    ScopeEntry   *buckets[SCOPE_BUCKETS];
    struct Scope *parent;
} Scope;

static unsigned int hash_name(const char *name) {
    unsigned int h = 5381;
    while (*name) h = h * 33 + (unsigned char)*name++;
    return h % SCOPE_BUCKETS;
}

static Scope *scope_push(Scope *parent) {
    Scope *s = calloc(1, sizeof(Scope));
    s->parent = parent;
    return s;
}

static void scope_pop(Scope *s) {
    for (int i = 0; i < SCOPE_BUCKETS; i++) {
        ScopeEntry *e = s->buckets[i];
        while (e) {
            ScopeEntry *next = e->next;
            free(e->name);
            free(e);
            e = next;
        }
    }
    free(s);
}

/* Returns 1 if name exists anywhere in scope chain */
static int scope_exists(Scope *s, const char *name) {
    while (s) {
        unsigned int h = hash_name(name);
        for (ScopeEntry *e = s->buckets[h]; e; e = e->next)
            if (strcmp(e->name, name) == 0) return 1;
        s = s->parent;
    }
    return 0;
}

/* Returns 1 if name is const in current scope chain */
static int scope_is_const(Scope *s, const char *name) {
    while (s) {
        unsigned int h = hash_name(name);
        for (ScopeEntry *e = s->buckets[h]; e; e = e->next)
            if (strcmp(e->name, name) == 0) return e->is_const;
        s = s->parent;
    }
    return 0;
}

static void scope_define(Scope *s, const char *name, int is_const, int line) {
    if (scope_exists(s, name))
        jibl_error(line, "'%s' shadows a variable in an enclosing scope", name);
    unsigned int h = hash_name(name);
    ScopeEntry *e = malloc(sizeof(ScopeEntry));
    e->name     = strdup(name);
    e->is_const = is_const;
    e->next     = s->buckets[h];
    s->buckets[h] = e;
}

/* ---- AST walkers ---- */

static void check_expr(ASTNode *n, Scope *scope);
static void check_stmts(NodeList *stmts, Scope *scope);

static void check_expr(ASTNode *n, Scope *scope) {
    if (!n) return;
    switch (n->kind) {
        case NODE_IDENT:
            /* Not checking use-before-define here (runtime will catch) */
            break;
        case NODE_BINARY:
            check_expr(n->binary.left, scope);
            check_expr(n->binary.right, scope);
            break;
        case NODE_UNARY:
            check_expr(n->unary.operand, scope);
            break;
        case NODE_CALL_EXPR:
            for (int i = 0; i < n->call.args.count; i++)
                check_expr(n->call.args.nodes[i], scope);
            break;
        case NODE_INDEX:
            check_expr(n->index.array, scope);
            check_expr(n->index.index, scope);
            break;
        case NODE_FIELD:
            check_expr(n->field.object, scope);
            break;
        case NODE_ARRAY_LIT:
            for (int i = 0; i < n->array_lit.elems.count; i++)
                check_expr(n->array_lit.elems.nodes[i], scope);
            break;
        case NODE_OK_EXPR:
        case NODE_ERROR_EXPR:
        case NODE_ASK_EXPR:
            check_expr(n->wrap.expr, scope);
            break;
        default:
            break;
    }
}

static void check_stmts(NodeList *stmts, Scope *scope) {
    for (int i = 0; i < stmts->count; i++) {
        ASTNode *n = stmts->nodes[i];
        if (!n) continue;
        switch (n->kind) {
            case NODE_VAR_DECL:
                check_expr(n->var_decl.init, scope);
                scope_define(scope, n->var_decl.name, 0, n->line);
                break;
            case NODE_CONST_DECL:
                check_expr(n->var_decl.init, scope);
                scope_define(scope, n->var_decl.name, 1, n->line);
                break;
            case NODE_ASSIGN:
                if (scope_is_const(scope, n->assign.name))
                    jibl_error(n->line, "cannot assign to const '%s'", n->assign.name);
                check_expr(n->assign.value, scope);
                break;
            case NODE_IF: {
                check_expr(n->if_stmt.cond, scope);
                Scope *then_scope = scope_push(scope);
                check_stmts(&n->if_stmt.then_body, then_scope);
                scope_pop(then_scope);
                if (n->if_stmt.else_body.count > 0) {
                    Scope *else_scope = scope_push(scope);
                    check_stmts(&n->if_stmt.else_body, else_scope);
                    scope_pop(else_scope);
                }
                break;
            }
            case NODE_WHILE: {
                check_expr(n->while_stmt.cond, scope);
                Scope *w_scope = scope_push(scope);
                check_stmts(&n->while_stmt.body, w_scope);
                scope_pop(w_scope);
                break;
            }
            case NODE_RETURN:
                check_expr(n->ret.value, scope);
                break;
            case NODE_PRINT:
                for (int j = 0; j < n->print.args.count; j++)
                    check_expr(n->print.args.nodes[j], scope);
                break;
            case NODE_CALL_STMT:
                for (int j = 0; j < n->call.args.count; j++)
                    check_expr(n->call.args.nodes[j], scope);
                break;
            case NODE_FUNC_DECL: {
                /* Function params go into a new scope */
                Scope *fn_scope = scope_push(scope);
                for (int j = 0; j < n->func_decl.param_count; j++)
                    scope_define(fn_scope, n->func_decl.params[j].name, 0, n->line);
                for (int j = 0; j < n->func_decl.requires.count; j++)
                    check_expr(n->func_decl.requires.nodes[j], fn_scope);
                for (int j = 0; j < n->func_decl.ensures.count; j++)
                    check_expr(n->func_decl.ensures.nodes[j], fn_scope);
                check_stmts(&n->func_decl.body, fn_scope);
                scope_pop(fn_scope);
                break;
            }
            default:
                break;
        }
    }
}

void sema_check(ASTNode *program) {
    Scope *global = scope_push(NULL);
    check_stmts(&program->program.body, global);
    scope_pop(global);
}

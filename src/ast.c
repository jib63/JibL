#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ast.h"

/* ---- TypeDesc ---- */

TypeDesc *type_make(JiblType base) {
    TypeDesc *t = calloc(1, sizeof(TypeDesc));
    t->base = base;
    return t;
}

TypeDesc *type_make_array(TypeDesc *elem) {
    TypeDesc *t = calloc(1, sizeof(TypeDesc));
    t->base      = JTYPE_ARRAY;
    t->elem_type = elem;
    return t;
}

TypeDesc *type_make_result(TypeDesc *ok, TypeDesc *err) {
    TypeDesc *t = calloc(1, sizeof(TypeDesc));
    t->base     = JTYPE_RESULT;
    t->ok_type  = ok;
    t->err_type = err;
    return t;
}

void type_free(TypeDesc *t) {
    if (!t) return;
    type_free(t->elem_type);
    type_free(t->ok_type);
    type_free(t->err_type);
    free(t);
}

char *type_to_str(const TypeDesc *t) {
    if (!t) return strdup("unknown");
    switch (t->base) {
        case JTYPE_INT:         return strdup("int");
        case JTYPE_FLOAT:       return strdup("float");
        case JTYPE_STRING:      return strdup("string");
        case JTYPE_BOOL:        return strdup("bool");
        case JTYPE_VOID:        return strdup("void");
        case JTYPE_JSON:        return strdup("json");
        case JTYPE_AI_RESPONSE: return strdup("ai_response");
        case JTYPE_UNKNOWN:     return strdup("unknown");
        case JTYPE_ARRAY: {
            char *elem = type_to_str(t->elem_type);
            int len = (int)strlen(elem) + 3;
            char *buf = malloc((size_t)len);
            snprintf(buf, (size_t)len, "%s[]", elem);
            free(elem);
            return buf;
        }
        case JTYPE_RESULT: {
            char *ok  = type_to_str(t->ok_type);
            char *err = type_to_str(t->err_type);
            int len = (int)strlen(ok) + (int)strlen(err) + 12;
            char *buf = malloc((size_t)len);
            snprintf(buf, (size_t)len, "result<%s, %s>", ok, err);
            free(ok); free(err);
            return buf;
        }
        default: return strdup("?");
    }
}

/* ---- NodeList ---- */

void nl_init(NodeList *nl) {
    nl->nodes    = NULL;
    nl->count    = 0;
    nl->capacity = 0;
}

void nl_push(NodeList *nl, ASTNode *node) {
    if (nl->count >= nl->capacity) {
        nl->capacity = nl->capacity ? nl->capacity * 2 : 8;
        nl->nodes = realloc(nl->nodes, sizeof(ASTNode *) * (size_t)nl->capacity);
        if (!nl->nodes) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    nl->nodes[nl->count++] = node;
}

void nl_free(NodeList *nl) {
    for (int i = 0; i < nl->count; i++) ast_free(nl->nodes[i]);
    free(nl->nodes);
    nl->nodes = NULL; nl->count = 0; nl->capacity = 0;
}

/* ---- ASTNode ---- */

ASTNode *ast_new(NodeType kind, int line) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    if (!n) { fprintf(stderr, "out of memory\n"); exit(1); }
    n->kind = kind;
    n->line = line;
    return n;
}

void ast_free(ASTNode *n) {
    if (!n) return;
    switch (n->kind) {
        case NODE_PROGRAM:
            nl_free(&n->program.body);
            break;
        case NODE_FUNC_DECL:
            free(n->func_decl.name);
            for (int i = 0; i < n->func_decl.param_count; i++) {
                type_free(n->func_decl.params[i].type);
                free(n->func_decl.params[i].name);
            }
            free(n->func_decl.params);
            type_free(n->func_decl.return_type);
            nl_free(&n->func_decl.requires);
            nl_free(&n->func_decl.ensures);
            nl_free(&n->func_decl.body);
            free(n->func_decl.ai_prompt);
            break;
        case NODE_VAR_DECL:
        case NODE_CONST_DECL:
            type_free(n->var_decl.type);
            free(n->var_decl.name);
            ast_free(n->var_decl.init);
            break;
        case NODE_ASSIGN:
            free(n->assign.name);
            ast_free(n->assign.value);
            break;
        case NODE_IF:
            ast_free(n->if_stmt.cond);
            nl_free(&n->if_stmt.then_body);
            nl_free(&n->if_stmt.else_body);
            break;
        case NODE_WHILE:
            ast_free(n->while_stmt.cond);
            nl_free(&n->while_stmt.body);
            break;
        case NODE_RETURN:
            ast_free(n->ret.value);
            break;
        case NODE_PRINT:
            nl_free(&n->print.args);
            break;
        case NODE_CALL_STMT:
        case NODE_CALL_EXPR:
            free(n->call.name);
            nl_free(&n->call.args);
            break;
        case NODE_BINARY:
            free(n->binary.op);
            ast_free(n->binary.left);
            ast_free(n->binary.right);
            break;
        case NODE_UNARY:
            free(n->unary.op);
            ast_free(n->unary.operand);
            break;
        case NODE_IDENT:
            free(n->ident.name);
            break;
        case NODE_STRING_LIT:
            free(n->string_lit.value);
            break;
        case NODE_ARRAY_LIT:
            nl_free(&n->array_lit.elems);
            break;
        case NODE_INDEX:
            ast_free(n->index.array);
            ast_free(n->index.index);
            break;
        case NODE_FIELD:
            ast_free(n->field.object);
            free(n->field.field);
            break;
        case NODE_OK_EXPR:
        case NODE_ERROR_EXPR:
        case NODE_ASK_EXPR:
            ast_free(n->wrap.expr);
            break;
        default:
            break;
    }
    free(n);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "emitter.h"

/* ---- Dynamic string buffer ---- */
typedef struct {
    char *buf;
    int   len;
    int   cap;
} SBuf;

static void sb_init(SBuf *sb) { sb->buf = NULL; sb->len = 0; sb->cap = 0; }

static void sb_ensure(SBuf *sb, int extra) {
    if (sb->len + extra + 1 >= sb->cap) {
        sb->cap = sb->cap ? sb->cap * 2 : 512;
        if (sb->len + extra + 1 >= sb->cap) sb->cap = sb->len + extra + 256;
        sb->buf = realloc(sb->buf, (size_t)sb->cap);
        if (!sb->buf) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
}

static void sb_cat(SBuf *sb, const char *s) {
    int slen = (int)strlen(s);
    sb_ensure(sb, slen);
    memcpy(sb->buf + sb->len, s, (size_t)slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
}

static void sb_catf(SBuf *sb, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (n < (int)sizeof(tmp)) { sb_cat(sb, tmp); }
        else {
            char *big = malloc((size_t)(n + 1));
            va_start(ap, fmt);
            vsnprintf(big, (size_t)(n + 1), fmt, ap);
            va_end(ap);
            sb_cat(sb, big);
            free(big);
        }
    }
}

/* Emit a string with escape sequences */
static void emit_string(SBuf *sb, const char *s) {
    sb_cat(sb, "\"");
    for (; *s; s++) {
        if      (*s == '"')  sb_cat(sb, "\\\"");
        else if (*s == '\\') sb_cat(sb, "\\\\");
        else if (*s == '\n') sb_cat(sb, "\\n");
        else if (*s == '\t') sb_cat(sb, "\\t");
        else { char tmp[2] = {*s, '\0'}; sb_cat(sb, tmp); }
    }
    sb_cat(sb, "\"");
}

static void emit_type(SBuf *sb, const TypeDesc *t) {
    if (!t) { sb_cat(sb, "void"); return; }
    switch (t->base) {
        case JTYPE_INT:         sb_cat(sb, "int");         break;
        case JTYPE_FLOAT:       sb_cat(sb, "float");       break;
        case JTYPE_STRING:      sb_cat(sb, "string");      break;
        case JTYPE_BOOL:        sb_cat(sb, "bool");        break;
        case JTYPE_VOID:        sb_cat(sb, "void");        break;
        case JTYPE_JSON:        sb_cat(sb, "json");        break;
        case JTYPE_AI_RESPONSE: sb_cat(sb, "ai_response"); break;
        case JTYPE_ARRAY:
            sb_cat(sb, "(array ");
            emit_type(sb, t->elem_type);
            sb_cat(sb, ")");
            break;
        case JTYPE_RESULT:
            sb_cat(sb, "(result ");
            emit_type(sb, t->ok_type);
            sb_cat(sb, " ");
            emit_type(sb, t->err_type);
            sb_cat(sb, ")");
            break;
        default: sb_cat(sb, "unknown"); break;
    }
}

static void emit_expr(SBuf *sb, ASTNode *n);
static void emit_stmts(SBuf *sb, NodeList *stmts, int indent);
static void emit_stmt(SBuf *sb, ASTNode *n, int indent);

static void emit_indent(SBuf *sb, int indent) {
    for (int i = 0; i < indent * 2; i++) sb_cat(sb, " ");
}

static void emit_expr(SBuf *sb, ASTNode *n) {
    if (!n) { sb_cat(sb, "(void)"); return; }
    switch (n->kind) {
        case NODE_INT_LIT:
            sb_catf(sb, "(int %ld)", n->int_lit.value);
            break;
        case NODE_FLOAT_LIT:
            sb_catf(sb, "(float %g)", n->float_lit.value);
            break;
        case NODE_STRING_LIT:
            sb_cat(sb, "(str ");
            emit_string(sb, n->string_lit.value);
            sb_cat(sb, ")");
            break;
        case NODE_BOOL_LIT:
            sb_catf(sb, "(bool %s)", n->bool_lit.value ? "true" : "false");
            break;
        case NODE_IDENT:
            sb_catf(sb, "(var %s)", n->ident.name);
            break;
        case NODE_BINARY:
            sb_catf(sb, "(%s ", n->binary.op);
            emit_expr(sb, n->binary.left);
            sb_cat(sb, " ");
            emit_expr(sb, n->binary.right);
            sb_cat(sb, ")");
            break;
        case NODE_UNARY:
            sb_catf(sb, "(%s ", n->unary.op);
            emit_expr(sb, n->unary.operand);
            sb_cat(sb, ")");
            break;
        case NODE_CALL_EXPR:
            sb_catf(sb, "(call %s", n->call.name);
            for (int i = 0; i < n->call.args.count; i++) {
                sb_cat(sb, " ");
                emit_expr(sb, n->call.args.nodes[i]);
            }
            sb_cat(sb, ")");
            break;
        case NODE_ARRAY_LIT:
            sb_cat(sb, "(array");
            for (int i = 0; i < n->array_lit.elems.count; i++) {
                sb_cat(sb, " ");
                emit_expr(sb, n->array_lit.elems.nodes[i]);
            }
            sb_cat(sb, ")");
            break;
        case NODE_INDEX:
            sb_cat(sb, "(index ");
            emit_expr(sb, n->index.array);
            sb_cat(sb, " ");
            emit_expr(sb, n->index.index);
            sb_cat(sb, ")");
            break;
        case NODE_FIELD:
            sb_cat(sb, "(field-get ");
            emit_expr(sb, n->field.object);
            sb_catf(sb, " %s)", n->field.field);
            break;
        case NODE_OK_EXPR:
            sb_cat(sb, "(ok ");
            emit_expr(sb, n->wrap.expr);
            sb_cat(sb, ")");
            break;
        case NODE_ERROR_EXPR:
            sb_cat(sb, "(error ");
            emit_expr(sb, n->wrap.expr);
            sb_cat(sb, ")");
            break;
        case NODE_ASK_EXPR:
            sb_cat(sb, "(ai-ask ");
            emit_expr(sb, n->wrap.expr);
            sb_cat(sb, ")");
            break;
        default:
            sb_cat(sb, "(?)");
            break;
    }
}

static void emit_stmt(SBuf *sb, ASTNode *n, int indent) {
    if (!n) return;
    emit_indent(sb, indent);
    switch (n->kind) {
        case NODE_VAR_DECL:
            sb_cat(sb, "(decl ");
            emit_type(sb, n->var_decl.type);
            sb_catf(sb, " %s ", n->var_decl.name);
            emit_expr(sb, n->var_decl.init);
            sb_cat(sb, ")\n");
            break;
        case NODE_CONST_DECL:
            sb_cat(sb, "(const ");
            emit_type(sb, n->var_decl.type);
            sb_catf(sb, " %s ", n->var_decl.name);
            emit_expr(sb, n->var_decl.init);
            sb_cat(sb, ")\n");
            break;
        case NODE_ASSIGN:
            sb_catf(sb, "(set %s ", n->assign.name);
            emit_expr(sb, n->assign.value);
            sb_cat(sb, ")\n");
            break;
        case NODE_IF:
            sb_cat(sb, "(if ");
            emit_expr(sb, n->if_stmt.cond);
            sb_cat(sb, "\n");
            emit_indent(sb, indent + 1);
            sb_cat(sb, "(block\n");
            emit_stmts(sb, &n->if_stmt.then_body, indent + 2);
            emit_indent(sb, indent + 1);
            sb_cat(sb, ")");
            if (n->if_stmt.else_body.count > 0) {
                sb_cat(sb, "\n");
                emit_indent(sb, indent + 1);
                sb_cat(sb, "(block\n");
                emit_stmts(sb, &n->if_stmt.else_body, indent + 2);
                emit_indent(sb, indent + 1);
                sb_cat(sb, ")");
            }
            sb_cat(sb, ")\n");
            break;
        case NODE_WHILE:
            sb_cat(sb, "(while ");
            emit_expr(sb, n->while_stmt.cond);
            sb_cat(sb, "\n");
            emit_indent(sb, indent + 1);
            sb_cat(sb, "(block\n");
            emit_stmts(sb, &n->while_stmt.body, indent + 2);
            emit_indent(sb, indent + 1);
            sb_cat(sb, "))\n");
            break;
        case NODE_RETURN:
            if (n->ret.value) {
                sb_cat(sb, "(return ");
                emit_expr(sb, n->ret.value);
                sb_cat(sb, ")\n");
            } else {
                sb_cat(sb, "(return)\n");
            }
            break;
        case NODE_PRINT:
            sb_cat(sb, "(print");
            for (int i = 0; i < n->print.args.count; i++) {
                sb_cat(sb, " ");
                emit_expr(sb, n->print.args.nodes[i]);
            }
            sb_cat(sb, ")\n");
            break;
        case NODE_CALL_STMT:
            sb_catf(sb, "(call %s", n->call.name);
            for (int i = 0; i < n->call.args.count; i++) {
                sb_cat(sb, " ");
                emit_expr(sb, n->call.args.nodes[i]);
            }
            sb_cat(sb, ")\n");
            break;
        case NODE_FUNC_DECL: {
            if (n->func_decl.ai_prompt) {
                /* Emit ai-func marker */
                sb_cat(sb, "(ai-func ");
                emit_string(sb, n->func_decl.name);
                sb_cat(sb, " ");
                emit_string(sb, n->func_decl.ai_prompt);
                sb_cat(sb, "\n");
                emit_indent(sb, indent + 1);
            }
            sb_catf(sb, "(func %s\n", n->func_decl.name);
            emit_indent(sb, indent + 1);
            sb_cat(sb, "(params");
            for (int i = 0; i < n->func_decl.param_count; i++) {
                sb_cat(sb, " (");
                emit_type(sb, n->func_decl.params[i].type);
                sb_catf(sb, " %s)", n->func_decl.params[i].name);
            }
            sb_cat(sb, ")\n");
            emit_indent(sb, indent + 1);
            sb_cat(sb, "(returns ");
            emit_type(sb, n->func_decl.return_type);
            sb_cat(sb, ")\n");
            for (int i = 0; i < n->func_decl.requires.count; i++) {
                emit_indent(sb, indent + 1);
                sb_cat(sb, "(requires ");
                emit_expr(sb, n->func_decl.requires.nodes[i]);
                sb_cat(sb, ")\n");
            }
            for (int i = 0; i < n->func_decl.ensures.count; i++) {
                emit_indent(sb, indent + 1);
                sb_cat(sb, "(ensures ");
                emit_expr(sb, n->func_decl.ensures.nodes[i]);
                sb_cat(sb, ")\n");
            }
            emit_indent(sb, indent + 1);
            sb_cat(sb, "(block\n");
            emit_stmts(sb, &n->func_decl.body, indent + 2);
            emit_indent(sb, indent + 1);
            sb_cat(sb, "))\n");
            if (n->func_decl.ai_prompt) {
                emit_indent(sb, indent);
                sb_cat(sb, ")\n");
            }
            break;
        }
        default:
            sb_cat(sb, "(? unknown-stmt)\n");
            break;
    }
}

static void emit_stmts(SBuf *sb, NodeList *stmts, int indent) {
    for (int i = 0; i < stmts->count; i++)
        emit_stmt(sb, stmts->nodes[i], indent);
}

char *emitter_emit(ASTNode *program) {
    SBuf sb; sb_init(&sb);
    sb_cat(&sb, "(program\n");
    emit_stmts(&sb, &program->program.body, 1);
    sb_cat(&sb, ")\n");
    return sb.buf;
}

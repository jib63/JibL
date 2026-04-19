#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "parser.h"
#include "error.h"

typedef struct {
    TokenList *tl;
    int        pos;
    char      *pending_ai_prompt;  /* set when @ai is seen before func */
} Parser;

static Token *peek_tok(Parser *p) {
    return &p->tl->tokens[p->pos];
}

static Token *peek2_tok(Parser *p) {
    if (p->pos + 1 < p->tl->count)
        return &p->tl->tokens[p->pos + 1];
    return &p->tl->tokens[p->tl->count - 1]; /* EOF */
}

static Token *advance_tok(Parser *p) {
    Token *t = &p->tl->tokens[p->pos];
    if (t->type != TOK_EOF) p->pos++;
    return t;
}

static Token *expect(Parser *p, TokenType type) {
    Token *t = peek_tok(p);
    if (t->type != type)
        jibl_error(t->line, "expected '%s', got '%s'",
                   token_type_name(type), token_type_name(t->type));
    return advance_tok(p);
}

static int check(Parser *p, TokenType type) {
    return peek_tok(p)->type == type;
}

static int match(Parser *p, TokenType type) {
    if (check(p, type)) { advance_tok(p); return 1; }
    return 0;
}

/* ---- Type parsing ---- */

static int is_type_token(TokenType t) {
    return t == TOK_TYPE_INT    || t == TOK_TYPE_FLOAT  ||
           t == TOK_TYPE_STRING || t == TOK_TYPE_BOOL   ||
           t == TOK_TYPE_VOID   || t == TOK_TYPE_RESULT ||
           t == TOK_TYPE_JSON   || t == TOK_TYPE_AI_RESPONSE;
}

static TypeDesc *parse_type(Parser *p) {
    Token *t = peek_tok(p);
    if (!is_type_token(t->type))
        jibl_error(t->line, "expected a type, got '%s'", token_type_name(t->type));
    advance_tok(p);

    JiblType base;
    switch (t->type) {
        case TOK_TYPE_INT:         base = JTYPE_INT;         break;
        case TOK_TYPE_FLOAT:       base = JTYPE_FLOAT;       break;
        case TOK_TYPE_STRING:      base = JTYPE_STRING;      break;
        case TOK_TYPE_BOOL:        base = JTYPE_BOOL;        break;
        case TOK_TYPE_VOID:        base = JTYPE_VOID;        break;
        case TOK_TYPE_JSON:        base = JTYPE_JSON;        break;
        case TOK_TYPE_AI_RESPONSE: base = JTYPE_AI_RESPONSE; break;
        case TOK_TYPE_RESULT: {
            expect(p, TOK_LT);
            TypeDesc *ok  = parse_type(p);
            expect(p, TOK_COMMA);
            TypeDesc *err = parse_type(p);
            expect(p, TOK_GT);
            return type_make_result(ok, err);
        }
        default: base = JTYPE_UNKNOWN; break;
    }

    TypeDesc *td = type_make(base);

    /* Check for [] suffix → array */
    if (check(p, TOK_LBRACKET) && peek2_tok(p)->type == TOK_RBRACKET) {
        advance_tok(p); /* [ */
        advance_tok(p); /* ] */
        return type_make_array(td);
    }

    return td;
}

/* ---- Forward declarations ---- */
static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_stmt(Parser *p);
static void     parse_body(Parser *p, NodeList *body);

/* ---- Expressions ---- */

static NodeList parse_arg_list(Parser *p) {
    NodeList args; nl_init(&args);
    if (!check(p, TOK_RPAREN)) {
        nl_push(&args, parse_expr(p));
        while (match(p, TOK_COMMA))
            nl_push(&args, parse_expr(p));
    }
    return args;
}

static ASTNode *parse_primary(Parser *p) {
    Token *t = peek_tok(p);
    int line = t->line;

    /* Integer literal */
    if (t->type == TOK_INT_LIT) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_INT_LIT, line);
        n->int_lit.value = atol(t->lexeme);
        return n;
    }

    /* Float literal */
    if (t->type == TOK_FLOAT_LIT) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_FLOAT_LIT, line);
        n->float_lit.value = atof(t->lexeme);
        return n;
    }

    /* String literal */
    if (t->type == TOK_STRING_LIT) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_STRING_LIT, line);
        n->string_lit.value = strdup(t->lexeme);
        return n;
    }

    /* Boolean literals */
    if (t->type == TOK_TRUE) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_BOOL_LIT, line);
        n->bool_lit.value = 1;
        return n;
    }
    if (t->type == TOK_FALSE) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_BOOL_LIT, line);
        n->bool_lit.value = 0;
        return n;
    }

    /* Array literal [e1, e2, ...] */
    if (t->type == TOK_LBRACKET) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_ARRAY_LIT, line);
        nl_init(&n->array_lit.elems);
        if (!check(p, TOK_RBRACKET)) {
            nl_push(&n->array_lit.elems, parse_expr(p));
            while (match(p, TOK_COMMA))
                nl_push(&n->array_lit.elems, parse_expr(p));
        }
        expect(p, TOK_RBRACKET);
        return n;
    }

    /* Grouped expression */
    if (t->type == TOK_LPAREN) {
        advance_tok(p);
        ASTNode *inner = parse_expr(p);
        expect(p, TOK_RPAREN);
        return inner;
    }

    /* ok(expr) */
    if (t->type == TOK_OK) {
        advance_tok(p);
        expect(p, TOK_LPAREN);
        ASTNode *n = ast_new(NODE_OK_EXPR, line);
        n->wrap.expr = parse_expr(p);
        expect(p, TOK_RPAREN);
        return n;
    }

    /* error(expr) */
    if (t->type == TOK_ERROR_KW) {
        advance_tok(p);
        expect(p, TOK_LPAREN);
        ASTNode *n = ast_new(NODE_ERROR_EXPR, line);
        n->wrap.expr = parse_expr(p);
        expect(p, TOK_RPAREN);
        return n;
    }

    /* ask(expr) */
    if (t->type == TOK_ASK) {
        advance_tok(p);
        expect(p, TOK_LPAREN);
        ASTNode *n = ast_new(NODE_ASK_EXPR, line);
        n->wrap.expr = parse_expr(p);
        expect(p, TOK_RPAREN);
        return n;
    }

    /* Identifier — may be followed by ( for call, or [ for index */
    if (t->type == TOK_IDENT || t->type == TOK_APPEND || t->type == TOK_LEN ||
        t->type == TOK_FILE_READ   || t->type == TOK_FILE_WRITE ||
        t->type == TOK_HTTP_GET    || t->type == TOK_HTTP_POST   ||
        t->type == TOK_JSON_PARSE  || t->type == TOK_JSON_GET_STRING ||
        t->type == TOK_JSON_GET_INT|| t->type == TOK_JSON_GET_BOOL) {
        advance_tok(p);
        const char *name_str = t->lexeme ? t->lexeme : token_type_name(t->type);
        char *name = strdup(name_str);

        if (check(p, TOK_LPAREN)) {
            advance_tok(p);
            ASTNode *n = ast_new(NODE_CALL_EXPR, line);
            n->call.name = name;
            n->call.args = parse_arg_list(p);
            expect(p, TOK_RPAREN);
            return n;
        }

        ASTNode *n = ast_new(NODE_IDENT, line);
        n->ident.name = name;

        /* Array index */
        while (check(p, TOK_LBRACKET)) {
            advance_tok(p);
            ASTNode *idx_node = ast_new(NODE_INDEX, line);
            idx_node->index.array = n;
            idx_node->index.index = parse_expr(p);
            expect(p, TOK_RBRACKET);
            n = idx_node;
        }

        return n;
    }

    jibl_error(line, "unexpected token '%s' in expression", token_type_name(t->type));
    return NULL;
}

/* Left-recursive postfix: field access and array index after any primary */
static ASTNode *parse_postfix(Parser *p) {
    ASTNode *n = parse_primary(p);
    while (1) {
        if (check(p, TOK_DOT)) {
            int line = peek_tok(p)->line;
            advance_tok(p);
            Token *field_tok = advance_tok(p);
            /* Field name can be an identifier OR a keyword (ok, error, value, model, etc.) */
            const char *fname_str = field_tok->lexeme
                                  ? field_tok->lexeme
                                  : token_type_name(field_tok->type);
            if (field_tok->type == TOK_EOF)
                jibl_error(line, "expected field name after '.'");
            ASTNode *fn = ast_new(NODE_FIELD, line);
            fn->field.object = n;
            fn->field.field  = strdup(fname_str);
            n = fn;
        } else if (check(p, TOK_LBRACKET)) {
            int line = peek_tok(p)->line;
            advance_tok(p);
            ASTNode *idx_node = ast_new(NODE_INDEX, line);
            idx_node->index.array = n;
            idx_node->index.index = parse_expr(p);
            expect(p, TOK_RBRACKET);
            n = idx_node;
        } else {
            break;
        }
    }
    return n;
}

static ASTNode *parse_unary(Parser *p) {
    Token *t = peek_tok(p);
    if (t->type == TOK_MINUS) {
        int line = t->line; advance_tok(p);
        ASTNode *n = ast_new(NODE_UNARY, line);
        n->unary.op      = strdup("neg");
        n->unary.operand = parse_unary(p);
        return n;
    }
    if (t->type == TOK_NOT) {
        int line = t->line; advance_tok(p);
        ASTNode *n = ast_new(NODE_UNARY, line);
        n->unary.op      = strdup("not");
        n->unary.operand = parse_unary(p);
        return n;
    }
    return parse_postfix(p);
}

static ASTNode *parse_mul(Parser *p) {
    ASTNode *left = parse_unary(p);
    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT)) {
        Token *op = advance_tok(p);
        int line = op->line;
        const char *op_str = op->type == TOK_STAR ? "*" :
                             op->type == TOK_SLASH ? "/" : "%";
        ASTNode *n = ast_new(NODE_BINARY, line);
        n->binary.op    = strdup(op_str);
        n->binary.left  = left;
        n->binary.right = parse_unary(p);
        left = n;
    }
    return left;
}

static ASTNode *parse_add(Parser *p) {
    ASTNode *left = parse_mul(p);
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        Token *op = advance_tok(p);
        int line = op->line;
        const char *op_str = op->type == TOK_PLUS ? "+" : "-";
        ASTNode *n = ast_new(NODE_BINARY, line);
        n->binary.op    = strdup(op_str);
        n->binary.left  = left;
        n->binary.right = parse_mul(p);
        left = n;
    }
    return left;
}

static ASTNode *parse_compare(Parser *p) {
    ASTNode *left = parse_add(p);
    while (check(p, TOK_LT) || check(p, TOK_LTE) || check(p, TOK_GT) ||
           check(p, TOK_GTE) || check(p, TOK_EQ) || check(p, TOK_NEQ)) {
        Token *op = advance_tok(p);
        int line = op->line;
        const char *op_str;
        switch (op->type) {
            case TOK_LT:  op_str = "<";  break;
            case TOK_LTE: op_str = "<="; break;
            case TOK_GT:  op_str = ">";  break;
            case TOK_GTE: op_str = ">="; break;
            case TOK_EQ:  op_str = "=="; break;
            case TOK_NEQ: op_str = "!="; break;
            default:      op_str = "?";  break;
        }
        ASTNode *n = ast_new(NODE_BINARY, line);
        n->binary.op    = strdup(op_str);
        n->binary.left  = left;
        n->binary.right = parse_add(p);
        left = n;
    }
    return left;
}

static ASTNode *parse_and(Parser *p) {
    ASTNode *left = parse_compare(p);
    while (check(p, TOK_AND)) {
        int line = peek_tok(p)->line; advance_tok(p);
        ASTNode *n = ast_new(NODE_BINARY, line);
        n->binary.op    = strdup("and");
        n->binary.left  = left;
        n->binary.right = parse_compare(p);
        left = n;
    }
    return left;
}

static ASTNode *parse_expr(Parser *p) {
    ASTNode *left = parse_and(p);
    while (check(p, TOK_OR)) {
        int line = peek_tok(p)->line; advance_tok(p);
        ASTNode *n = ast_new(NODE_BINARY, line);
        n->binary.op    = strdup("or");
        n->binary.left  = left;
        n->binary.right = parse_and(p);
        left = n;
    }
    return left;
}

/* ---- Statements ---- */

static void parse_body(Parser *p, NodeList *body) {
    nl_init(body);
    while (!check(p, TOK_BLOCK_CLOSE) && !check(p, TOK_EOF)) {
        ASTNode *s = parse_stmt(p);
        if (s) nl_push(body, s);
    }
    expect(p, TOK_BLOCK_CLOSE);
}

static ASTNode *parse_func_decl(Parser *p, int line, char *ai_prompt) {
    ASTNode *n = ast_new(NODE_FUNC_DECL, line);
    nl_init(&n->func_decl.requires);
    nl_init(&n->func_decl.ensures);
    nl_init(&n->func_decl.body);
    n->func_decl.ai_prompt = ai_prompt;

    /* return type */
    n->func_decl.return_type = parse_type(p);

    /* name */
    Token *name_tok = expect(p, TOK_IDENT);
    n->func_decl.name = strdup(name_tok->lexeme);

    /* parameters */
    expect(p, TOK_LPAREN);
    int cap = 4;
    n->func_decl.params      = malloc(sizeof(Param) * (size_t)cap);
    n->func_decl.param_count = 0;
    if (!check(p, TOK_RPAREN)) {
        do {
            if (n->func_decl.param_count >= cap) {
                cap *= 2;
                n->func_decl.params = realloc(n->func_decl.params, sizeof(Param) * (size_t)cap);
            }
            Param *pr = &n->func_decl.params[n->func_decl.param_count++];
            pr->type = parse_type(p);
            Token *pname = expect(p, TOK_IDENT);
            pr->name = strdup(pname->lexeme);
        } while (match(p, TOK_COMMA));
    }
    expect(p, TOK_RPAREN);

    /* returns keyword */
    expect(p, TOK_RETURNS);
    /* consume return type again (matches the signature return type already parsed above) */
    type_free(parse_type(p));

    /* optional requires / ensures */
    while (check(p, TOK_REQUIRES) || check(p, TOK_ENSURES)) {
        if (match(p, TOK_REQUIRES)) {
            nl_push(&n->func_decl.requires, parse_expr(p));
        } else {
            advance_tok(p); /* ensures */
            nl_push(&n->func_decl.ensures, parse_expr(p));
        }
    }

    /* body */
    expect(p, TOK_BLOCK_OPEN);
    parse_body(p, &n->func_decl.body);

    return n;
}

static ASTNode *parse_stmt(Parser *p) {
    Token *t = peek_tok(p);
    int line = t->line;

    /* @ai annotation — store prompt, then expect func */
    if (t->type == TOK_AI_ANNOT) {
        advance_tok(p);
        p->pending_ai_prompt = strdup(t->lexeme);
        return NULL; /* will be consumed by next func_decl */
    }

    /* func declaration */
    if (t->type == TOK_FUNC) {
        advance_tok(p);
        char *prompt = p->pending_ai_prompt;
        p->pending_ai_prompt = NULL;
        return parse_func_decl(p, line, prompt);
    }

    /* const declaration: const <type> <name> = <expr> */
    if (t->type == TOK_CONST) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_CONST_DECL, line);
        n->var_decl.type = parse_type(p);
        Token *name = expect(p, TOK_IDENT);
        n->var_decl.name = strdup(name->lexeme);
        expect(p, TOK_ASSIGN);
        n->var_decl.init = parse_expr(p);
        return n;
    }

    /* var declaration: <type> <name> = <expr>
       Detect: type token followed by identifier followed by = */
    if (is_type_token(t->type) ||
        (t->type == TOK_TYPE_RESULT)) {
        /* Lookahead: after parsing type, is next token IDENT? */
        int saved_pos = p->pos;
        TypeDesc *td = parse_type(p);
        if (check(p, TOK_IDENT)) {
            Token *name_tok = advance_tok(p);
            if (check(p, TOK_ASSIGN)) {
                advance_tok(p);
                ASTNode *n = ast_new(NODE_VAR_DECL, line);
                n->var_decl.type = td;
                n->var_decl.name = strdup(name_tok->lexeme);
                n->var_decl.init = parse_expr(p);
                return n;
            }
            /* Not a declaration — restore and fall through */
            p->pos = saved_pos;
            type_free(td);
        } else {
            p->pos = saved_pos;
            type_free(td);
        }
    }

    /* set assignment */
    if (t->type == TOK_SET) {
        advance_tok(p);
        Token *name_tok = expect(p, TOK_IDENT);
        expect(p, TOK_ASSIGN);
        ASTNode *n = ast_new(NODE_ASSIGN, line);
        n->assign.name  = strdup(name_tok->lexeme);
        n->assign.value = parse_expr(p);
        return n;
    }

    /* if statement */
    if (t->type == TOK_IF) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_IF, line);
        n->if_stmt.cond = parse_expr(p);
        expect(p, TOK_BLOCK_OPEN);
        parse_body(p, &n->if_stmt.then_body);
        nl_init(&n->if_stmt.else_body);
        if (match(p, TOK_ELSE)) {
            expect(p, TOK_BLOCK_OPEN);
            parse_body(p, &n->if_stmt.else_body);
        }
        return n;
    }

    /* while statement */
    if (t->type == TOK_WHILE) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_WHILE, line);
        n->while_stmt.cond = parse_expr(p);
        expect(p, TOK_BLOCK_OPEN);
        parse_body(p, &n->while_stmt.body);
        return n;
    }

    /* return */
    if (t->type == TOK_RETURN) {
        advance_tok(p);
        ASTNode *n = ast_new(NODE_RETURN, line);
        /* no value if next is :] or EOF */
        if (check(p, TOK_BLOCK_CLOSE) || check(p, TOK_EOF))
            n->ret.value = NULL;
        else
            n->ret.value = parse_expr(p);
        return n;
    }

    /* print */
    if (t->type == TOK_PRINT) {
        advance_tok(p);
        expect(p, TOK_LPAREN);
        ASTNode *n = ast_new(NODE_PRINT, line);
        n->print.args = parse_arg_list(p);
        expect(p, TOK_RPAREN);
        return n;
    }

    /* call statement */
    if (t->type == TOK_CALL) {
        advance_tok(p);
        Token *name_tok = expect(p, TOK_IDENT);
        expect(p, TOK_LPAREN);
        ASTNode *n = ast_new(NODE_CALL_STMT, line);
        n->call.name = strdup(name_tok->lexeme);
        n->call.args = parse_arg_list(p);
        expect(p, TOK_RPAREN);
        return n;
    }

    jibl_error(line, "unexpected token '%s' at start of statement",
               token_type_name(t->type));
    return NULL;
}

ASTNode *parser_parse(TokenList *tl) {
    Parser p;
    p.tl                = tl;
    p.pos               = 0;
    p.pending_ai_prompt = NULL;

    ASTNode *program = ast_new(NODE_PROGRAM, 1);
    nl_init(&program->program.body);

    while (!check(&p, TOK_EOF)) {
        ASTNode *s = parse_stmt(&p);
        if (s) nl_push(&program->program.body, s);
    }

    free(p.pending_ai_prompt);
    return program;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"
#include "error.h"

/* ---------- keyword tables ---------- */

typedef struct { const char *word; TokenType type; } KWEntry;

#define KW_END {NULL, TOK_EOF}

static const KWEntry KW_COMMON[] = {
    /* These are the same in all languages */
    {"const",              TOK_CONST},
    {"set",                TOK_SET},
    {"ok",                 TOK_OK},
    {"error",              TOK_ERROR_KW},
    {"file_read",          TOK_FILE_READ},
    {"file_write",         TOK_FILE_WRITE},
    {"http_get",           TOK_HTTP_GET},
    {"http_post",          TOK_HTTP_POST},
    {"json_parse",         TOK_JSON_PARSE},
    {"json_get_string",    TOK_JSON_GET_STRING},
    {"json_get_int",       TOK_JSON_GET_INT},
    {"json_get_bool",      TOK_JSON_GET_BOOL},
    KW_END
};

static const KWEntry KW_EN[] = {
    {"if",           TOK_IF},
    {"else",         TOK_ELSE},
    {"while",        TOK_WHILE},
    {"func",         TOK_FUNC},
    {"returns",      TOK_RETURNS},
    {"return",       TOK_RETURN},
    {"call",         TOK_CALL},
    {"print",        TOK_PRINT},
    {"requires",     TOK_REQUIRES},
    {"ensures",      TOK_ENSURES},
    {"ask",          TOK_ASK},
    {"append",       TOK_APPEND},
    {"len",          TOK_LEN},
    {"true",         TOK_TRUE},
    {"false",        TOK_FALSE},
    {"and",          TOK_AND},
    {"or",           TOK_OR},
    {"not",          TOK_NOT},
    {"int",          TOK_TYPE_INT},
    {"float",        TOK_TYPE_FLOAT},
    {"string",       TOK_TYPE_STRING},
    {"bool",         TOK_TYPE_BOOL},
    {"void",         TOK_TYPE_VOID},
    {"result",       TOK_TYPE_RESULT},
    {"json",         TOK_TYPE_JSON},
    {"ai_response",  TOK_TYPE_AI_RESPONSE},
    KW_END
};

static const KWEntry KW_FR[] = {
    {"si",           TOK_IF},
    {"sinon",        TOK_ELSE},
    {"tantque",      TOK_WHILE},
    {"fonc",         TOK_FUNC},
    {"retourne",     TOK_RETURNS},
    {"retour",       TOK_RETURN},
    {"appel",        TOK_CALL},
    {"afficher",     TOK_PRINT},
    {"necessite",    TOK_REQUIRES},
    {"garantit",     TOK_ENSURES},
    {"demander",     TOK_ASK},
    {"ajouter",      TOK_APPEND},
    {"longueur",     TOK_LEN},
    {"vrai",         TOK_TRUE},
    {"faux",         TOK_FALSE},
    {"et",           TOK_AND},
    {"ou",           TOK_OR},
    {"non",          TOK_NOT},
    {"entier",       TOK_TYPE_INT},
    {"decimal",      TOK_TYPE_FLOAT},
    {"chaine",       TOK_TYPE_STRING},
    {"booleen",      TOK_TYPE_BOOL},
    {"vide",         TOK_TYPE_VOID},
    {"resultat",     TOK_TYPE_RESULT},
    {"json",         TOK_TYPE_JSON},
    {"reponse_ia",   TOK_TYPE_AI_RESPONSE},
    KW_END
};

static const KWEntry KW_ES[] = {
    {"si",           TOK_IF},
    {"sino",         TOK_ELSE},
    {"mientras",     TOK_WHILE},
    {"func",         TOK_FUNC},
    {"retorna",      TOK_RETURNS},
    {"retorno",      TOK_RETURN},
    {"llamar",       TOK_CALL},
    {"mostrar",      TOK_PRINT},
    {"requiere",     TOK_REQUIRES},
    {"garantiza",    TOK_ENSURES},
    {"preguntar",    TOK_ASK},
    {"agregar",      TOK_APPEND},
    {"longitud",     TOK_LEN},
    {"verdadero",    TOK_TRUE},
    {"falso",        TOK_FALSE},
    {"y",            TOK_AND},
    {"o",            TOK_OR},
    {"no",           TOK_NOT},
    {"entero",       TOK_TYPE_INT},
    {"decimal",      TOK_TYPE_FLOAT},
    {"cadena",       TOK_TYPE_STRING},
    {"booleano",     TOK_TYPE_BOOL},
    {"vacio",        TOK_TYPE_VOID},
    {"resultado",    TOK_TYPE_RESULT},
    {"json",         TOK_TYPE_JSON},
    {"respuesta_ia", TOK_TYPE_AI_RESPONSE},
    KW_END
};

/* ---------- lexer state ---------- */

typedef struct {
    const char       *src;
    int               pos;
    int               line;
    const char       *filename;
    const KWEntry    *lang_kw;  /* selected language keyword table */
    TokenList         tl;
} Lexer;

static void tl_push(TokenList *tl, Token tok) {
    if (tl->count >= tl->capacity) {
        tl->capacity = tl->capacity ? tl->capacity * 2 : 64;
        tl->tokens = realloc(tl->tokens, sizeof(Token) * (size_t)tl->capacity);
        if (!tl->tokens) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    tl->tokens[tl->count++] = tok;
}

static Token make_tok(TokenType type, char *lexeme, int line) {
    Token t; t.type = type; t.lexeme = lexeme; t.line = line;
    return t;
}

static char peek(Lexer *lx) {
    return lx->src[lx->pos];
}

static char peek2(Lexer *lx) {
    if (lx->src[lx->pos] == '\0') return '\0';
    return lx->src[lx->pos + 1];
}

static char advance(Lexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') lx->line++;
    return c;
}

static void skip_line_comment(Lexer *lx) {
    while (peek(lx) && peek(lx) != '\n') advance(lx);
}

static void skip_block_comment(Lexer *lx, int start_line) {
    /* consume chars until we find @@@ */
    while (peek(lx)) {
        if (peek(lx) == '@' && peek2(lx) == '@') {
            advance(lx); advance(lx);
            if (peek(lx) == '@') { advance(lx); return; }
        } else {
            advance(lx);
        }
    }
    jibl_error(start_line, "unterminated @@@ comment");
}

/* Read a quoted string (opening " already consumed). Handles \n \t \\ \" */
static char *read_string(Lexer *lx) {
    int start = lx->pos;
    int len = 0;
    /* first pass: count length */
    int i = start;
    while (lx->src[i] && lx->src[i] != '"') {
        if (lx->src[i] == '\\') i++;
        i++; len++;
    }
    if (!lx->src[i]) jibl_error(lx->line, "unterminated string literal");
    char *buf = malloc((size_t)(len + 1));
    if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
    int j = 0;
    while (peek(lx) && peek(lx) != '"') {
        char c = advance(lx);
        if (c == '\\') {
            char e = advance(lx);
            switch (e) {
                case 'n':  buf[j++] = '\n'; break;
                case 't':  buf[j++] = '\t'; break;
                case '\\': buf[j++] = '\\'; break;
                case '"':  buf[j++] = '"';  break;
                default:   buf[j++] = e;    break;
            }
        } else {
            buf[j++] = c;
        }
    }
    advance(lx); /* closing " */
    buf[j] = '\0';
    return buf;
}

/* Read a multiline string (opening """ already consumed) */
static char *read_multiline_string(Lexer *lx, int start_line) {
    int cap = 256, len = 0;
    char *buf = malloc((size_t)cap);
    if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
    while (peek(lx)) {
        if (peek(lx) == '"' && peek2(lx) == '"') {
            /* peek third */
            if (lx->src[lx->pos + 2] == '"') {
                advance(lx); advance(lx); advance(lx);
                buf[len] = '\0';
                return buf;
            }
        }
        char c = advance(lx);
        if (len + 2 >= cap) {
            cap *= 2;
            buf = realloc(buf, (size_t)cap);
            if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
        }
        buf[len++] = c;
    }
    jibl_error(start_line, "unterminated multiline string");
    return NULL;
}

static char *read_ident_fc(Lexer *lx, char first) {
    int cap = 32, len = 1;
    char *buf = malloc((size_t)cap);
    if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
    buf[0] = first;
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') {
        if (len + 2 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
        buf[len++] = advance(lx);
    }
    buf[len] = '\0';
    return buf;
}

static TokenType lookup_kw(const KWEntry *table, const char *word) {
    for (int i = 0; table[i].word; i++)
        if (strcmp(table[i].word, word) == 0) return table[i].type;
    return TOK_EOF; /* not found */
}

/* Read the @ai annotation: @ai "prompt string" */
static char *read_ai_prompt(Lexer *lx) {
    /* skip whitespace */
    while (peek(lx) == ' ' || peek(lx) == '\t') advance(lx);
    if (peek(lx) != '"') jibl_error(lx->line, "@ai must be followed by a string literal");
    advance(lx); /* consume opening " */
    return read_string(lx);
}

/* Detect language header "code english/français/español" at start of file.
   Returns pointer to appropriate keyword table, advances past the header line. */
static const KWEntry *detect_language(Lexer *lx) {
    /* Skip whitespace and newlines at very beginning */
    while (peek(lx) == ' ' || peek(lx) == '\t' || peek(lx) == '\n' || peek(lx) == '\r')
        advance(lx);

    /* Expect "code " */
    const char *code_kw = "code";
    for (int i = 0; code_kw[i]; i++) {
        if (peek(lx) != code_kw[i])
            jibl_error(lx->line, "file must begin with: code english | code français | code español");
        advance(lx);
    }
    if (peek(lx) != ' ' && peek(lx) != '\t')
        jibl_error(lx->line, "expected language name after 'code'");
    while (peek(lx) == ' ' || peek(lx) == '\t') advance(lx);

    /* Read language name (may contain non-ASCII UTF-8 for é, ñ etc.) */
    char lang[32]; int li = 0;
    while (peek(lx) && peek(lx) != '\n' && peek(lx) != '\r' && li < 31) {
        lang[li++] = advance(lx);
    }
    /* Trim trailing spaces */
    while (li > 0 && (lang[li-1] == ' ' || lang[li-1] == '\t')) li--;
    lang[li] = '\0';

    if (strcmp(lang, "english") == 0) return KW_EN;
    /* français: UTF-8 for ç is C3 A7, for é is C3 A9 */
    /* français: UTF-8 bytes for ç = 0xC3 0xA7 */
    if (lang[0]=='f' && lang[1]=='r' && lang[2]=='a' && lang[3]=='n' &&
        (unsigned char)lang[4]==0xC3 && (unsigned char)lang[5]==0xA7 &&
        strcmp(lang+6,"ais")==0) return KW_FR;
    /* español: UTF-8 bytes for ñ = 0xC3 0xB1 */
    if (lang[0]=='e' && lang[1]=='s' && lang[2]=='p' && lang[3]=='a' &&
        (unsigned char)lang[4]==0xC3 && (unsigned char)lang[5]==0xB1 &&
        strcmp(lang+6,"ol")==0) return KW_ES;
    /* ASCII fallbacks */
    if (strcmp(lang, "francais") == 0) return KW_FR;
    if (strcmp(lang, "espanol")  == 0) return KW_ES;

    jibl_error(lx->line, "unknown language '%s'; use: english, français, español", lang);
    return NULL;
}

/* ---------- main tokenizer ---------- */

TokenList lexer_tokenize(const char *source, const char *filename) {
    Lexer lx;
    lx.src      = source;
    lx.pos      = 0;
    lx.line     = 1;
    lx.filename = filename;
    lx.tl.tokens   = NULL;
    lx.tl.count    = 0;
    lx.tl.capacity = 0;

    jibl_error_init(filename);

    lx.lang_kw = detect_language(&lx);

    while (peek(&lx)) {
        /* skip whitespace */
        if (peek(&lx) == ' ' || peek(&lx) == '\t' ||
            peek(&lx) == '\r' || peek(&lx) == '\n') {
            advance(&lx);
            continue;
        }

        int line = lx.line;
        char c = advance(&lx);

        /* Comments and @ai annotation */
        if (c == '@') {
            if (peek(&lx) == '@') {
                advance(&lx);
                if (peek(&lx) == '@') {
                    advance(&lx);
                    skip_block_comment(&lx, line);
                } else {
                    skip_line_comment(&lx);
                }
                continue;
            }
            /* @ai annotation */
            if (peek(&lx) == 'a' && lx.src[lx.pos+1] == 'i' &&
                !isalnum((unsigned char)lx.src[lx.pos+2]) && lx.src[lx.pos+2] != '_') {
                advance(&lx); advance(&lx); /* consume 'ai' */
                char *prompt = read_ai_prompt(&lx);
                tl_push(&lx.tl, make_tok(TOK_AI_ANNOT, prompt, line));
                continue;
            }
            jibl_error(line, "unexpected '@'");
        }

        /* Strings */
        if (c == '"') {
            if (peek(&lx) == '"' && peek2(&lx) == '"') {
                advance(&lx); advance(&lx); /* consume remaining "" */
                char *s = read_multiline_string(&lx, line);
                tl_push(&lx.tl, make_tok(TOK_STRING_LIT, s, line));
            } else {
                char *s = read_string(&lx);
                tl_push(&lx.tl, make_tok(TOK_STRING_LIT, s, line));
            }
            continue;
        }

        /* Numbers */
        if (isdigit((unsigned char)c)) {
            int cap = 32, len = 1;
            char *buf = malloc((size_t)cap);
            if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
            buf[0] = c;
            int is_float = 0;
            while (isdigit((unsigned char)peek(&lx)) ||
                   (peek(&lx) == '.' && !is_float)) {
                if (peek(&lx) == '.') is_float = 1;
                if (len + 2 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
                buf[len++] = advance(&lx);
            }
            buf[len] = '\0';
            tl_push(&lx.tl, make_tok(is_float ? TOK_FLOAT_LIT : TOK_INT_LIT, buf, line));
            continue;
        }

        /* Identifiers and keywords */
        if (isalpha((unsigned char)c) || c == '_') {
            char *word = read_ident_fc(&lx, c);
            /* Check common keywords first */
            TokenType t = lookup_kw(KW_COMMON, word);
            if (t == TOK_EOF) t = lookup_kw(lx.lang_kw, word);
            if (t != TOK_EOF) {
                free(word);
                tl_push(&lx.tl, make_tok(t, NULL, line));
            } else {
                tl_push(&lx.tl, make_tok(TOK_IDENT, word, line));
            }
            continue;
        }

        /* Block open [: and array [ */
        if (c == '[') {
            if (peek(&lx) == ':') {
                advance(&lx);
                tl_push(&lx.tl, make_tok(TOK_BLOCK_OPEN, NULL, line));
            } else {
                tl_push(&lx.tl, make_tok(TOK_LBRACKET, NULL, line));
            }
            continue;
        }

        /* Block close :] */
        if (c == ':') {
            if (peek(&lx) == ']') {
                advance(&lx);
                tl_push(&lx.tl, make_tok(TOK_BLOCK_CLOSE, NULL, line));
            } else {
                jibl_error(line, "unexpected ':' (did you mean ':]'?)");
            }
            continue;
        }

        /* Operators */
        switch (c) {
            case '+': tl_push(&lx.tl, make_tok(TOK_PLUS,    NULL, line)); break;
            case '-': tl_push(&lx.tl, make_tok(TOK_MINUS,   NULL, line)); break;
            case '*': tl_push(&lx.tl, make_tok(TOK_STAR,    NULL, line)); break;
            case '/': tl_push(&lx.tl, make_tok(TOK_SLASH,   NULL, line)); break;
            case '%': tl_push(&lx.tl, make_tok(TOK_PERCENT, NULL, line)); break;
            case '.': tl_push(&lx.tl, make_tok(TOK_DOT,     NULL, line)); break;
            case '(': tl_push(&lx.tl, make_tok(TOK_LPAREN,  NULL, line)); break;
            case ')': tl_push(&lx.tl, make_tok(TOK_RPAREN,  NULL, line)); break;
            case ',': tl_push(&lx.tl, make_tok(TOK_COMMA,   NULL, line)); break;
            case ']': tl_push(&lx.tl, make_tok(TOK_RBRACKET,NULL, line)); break;
            case '=':
                if (peek(&lx) == '=') { advance(&lx); tl_push(&lx.tl, make_tok(TOK_EQ,  NULL, line)); }
                else                  {                tl_push(&lx.tl, make_tok(TOK_ASSIGN, NULL, line)); }
                break;
            case '!':
                if (peek(&lx) == '=') { advance(&lx); tl_push(&lx.tl, make_tok(TOK_NEQ, NULL, line)); }
                else jibl_error(line, "expected '!='");
                break;
            case '<':
                if (peek(&lx) == '=') { advance(&lx); tl_push(&lx.tl, make_tok(TOK_LTE, NULL, line)); }
                else                  {                tl_push(&lx.tl, make_tok(TOK_LT,  NULL, line)); }
                break;
            case '>':
                if (peek(&lx) == '=') { advance(&lx); tl_push(&lx.tl, make_tok(TOK_GTE, NULL, line)); }
                else                  {                tl_push(&lx.tl, make_tok(TOK_GT,  NULL, line)); }
                break;
            default:
                jibl_error(line, "unexpected character '%c' (0x%02x)", c, (unsigned char)c);
        }
    }

    tl_push(&lx.tl, make_tok(TOK_EOF, NULL, lx.line));
    return lx.tl;
}

void lexer_free(TokenList *tl) {
    for (int i = 0; i < tl->count; i++)
        free(tl->tokens[i].lexeme);
    free(tl->tokens);
    tl->tokens = NULL;
    tl->count  = 0;
    tl->capacity = 0;
}

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOK_EOF:              return "EOF";
        case TOK_INT_LIT:          return "INT_LIT";
        case TOK_FLOAT_LIT:        return "FLOAT_LIT";
        case TOK_STRING_LIT:       return "STRING_LIT";
        case TOK_TRUE:             return "true";
        case TOK_FALSE:            return "false";
        case TOK_IF:               return "if";
        case TOK_ELSE:             return "else";
        case TOK_WHILE:            return "while";
        case TOK_FUNC:             return "func";
        case TOK_RETURNS:          return "returns";
        case TOK_RETURN:           return "return";
        case TOK_SET:              return "set";
        case TOK_CONST:            return "const";
        case TOK_CALL:             return "call";
        case TOK_PRINT:            return "print";
        case TOK_REQUIRES:         return "requires";
        case TOK_ENSURES:          return "ensures";
        case TOK_OK:               return "ok";
        case TOK_ERROR_KW:         return "error";
        case TOK_ASK:              return "ask";
        case TOK_AI_ANNOT:         return "@ai";
        case TOK_APPEND:           return "append";
        case TOK_LEN:              return "len";
        case TOK_FILE_READ:        return "file_read";
        case TOK_FILE_WRITE:       return "file_write";
        case TOK_HTTP_GET:         return "http_get";
        case TOK_HTTP_POST:        return "http_post";
        case TOK_JSON_PARSE:       return "json_parse";
        case TOK_JSON_GET_STRING:  return "json_get_string";
        case TOK_JSON_GET_INT:     return "json_get_int";
        case TOK_JSON_GET_BOOL:    return "json_get_bool";
        case TOK_AND:              return "and";
        case TOK_OR:               return "or";
        case TOK_NOT:              return "not";
        case TOK_TYPE_INT:         return "int";
        case TOK_TYPE_FLOAT:       return "float";
        case TOK_TYPE_STRING:      return "string";
        case TOK_TYPE_BOOL:        return "bool";
        case TOK_TYPE_VOID:        return "void";
        case TOK_TYPE_RESULT:      return "result";
        case TOK_TYPE_JSON:        return "json";
        case TOK_TYPE_AI_RESPONSE: return "ai_response";
        case TOK_IDENT:            return "IDENT";
        case TOK_PLUS:             return "+";
        case TOK_MINUS:            return "-";
        case TOK_STAR:             return "*";
        case TOK_SLASH:            return "/";
        case TOK_PERCENT:          return "%";
        case TOK_EQ:               return "==";
        case TOK_NEQ:              return "!=";
        case TOK_LT:               return "<";
        case TOK_LTE:              return "<=";
        case TOK_GT:               return ">";
        case TOK_GTE:              return ">=";
        case TOK_ASSIGN:           return "=";
        case TOK_DOT:              return ".";
        case TOK_LPAREN:           return "(";
        case TOK_RPAREN:           return ")";
        case TOK_COMMA:            return ",";
        case TOK_LBRACKET:         return "[";
        case TOK_RBRACKET:         return "]";
        case TOK_BLOCK_OPEN:       return "[:";
        case TOK_BLOCK_CLOSE:      return ":]";
        default:                   return "?";
    }
}

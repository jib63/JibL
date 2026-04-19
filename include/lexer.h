#ifndef JIBL_LEXER_H
#define JIBL_LEXER_H

typedef enum {
    TOK_EOF,

    /* Literals */
    TOK_INT_LIT,
    TOK_FLOAT_LIT,
    TOK_STRING_LIT,

    /* Boolean literals */
    TOK_TRUE,
    TOK_FALSE,

    /* Control flow */
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,

    /* Functions */
    TOK_FUNC,
    TOK_RETURNS,
    TOK_RETURN,

    /* Variables / assignment */
    TOK_SET,
    TOK_CONST,

    /* Calls / IO */
    TOK_CALL,
    TOK_PRINT,

    /* Contracts */
    TOK_REQUIRES,
    TOK_ENSURES,

    /* Result type constructors */
    TOK_OK,
    TOK_ERROR_KW,

    /* AI */
    TOK_ASK,
    TOK_AI_ANNOT,   /* @ai "prompt string" — lexeme holds prompt */

    /* Array builtins */
    TOK_APPEND,
    TOK_LEN,

    /* Stdlib builtins (language-neutral names) */
    TOK_FILE_READ,
    TOK_FILE_WRITE,
    TOK_HTTP_GET,
    TOK_HTTP_POST,
    TOK_JSON_PARSE,
    TOK_JSON_GET_STRING,
    TOK_JSON_GET_INT,
    TOK_JSON_GET_BOOL,

    /* Logical operators (keyword form) */
    TOK_AND,
    TOK_OR,
    TOK_NOT,

    /* Types */
    TOK_TYPE_INT,
    TOK_TYPE_FLOAT,
    TOK_TYPE_STRING,
    TOK_TYPE_BOOL,
    TOK_TYPE_VOID,
    TOK_TYPE_RESULT,
    TOK_TYPE_JSON,
    TOK_TYPE_AI_RESPONSE,

    /* Identifier */
    TOK_IDENT,

    /* Arithmetic operators */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,

    /* Comparison operators */
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LTE,
    TOK_GT,
    TOK_GTE,

    /* Assignment */
    TOK_ASSIGN,

    /* Member access */
    TOK_DOT,

    /* Punctuation */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_LBRACKET,    /* [  */
    TOK_RBRACKET,    /* ]  */
    TOK_BLOCK_OPEN,  /* [: */
    TOK_BLOCK_CLOSE, /* :] */
} TokenType;

typedef struct {
    TokenType  type;
    char      *lexeme;  /* heap-allocated, may be NULL */
    int        line;
} Token;

typedef struct {
    Token *tokens;
    int    count;
    int    capacity;
} TokenList;

TokenList   lexer_tokenize(const char *source, const char *filename);
void        lexer_free(TokenList *tl);
const char *token_type_name(TokenType t);

#endif

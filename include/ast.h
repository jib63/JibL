#ifndef JIBL_AST_H
#define JIBL_AST_H

/* ---- Type system ---- */

typedef enum {
    JTYPE_INT,
    JTYPE_FLOAT,
    JTYPE_STRING,
    JTYPE_BOOL,
    JTYPE_VOID,
    JTYPE_RESULT,       /* result<T, E> — carries ok_type and err_type */
    JTYPE_JSON,
    JTYPE_AI_RESPONSE,
    JTYPE_ARRAY,        /* T[] — carries elem_type */
    JTYPE_UNKNOWN       /* unresolved / not yet typed */
} JiblType;

typedef struct TypeDesc TypeDesc;
struct TypeDesc {
    JiblType   base;
    TypeDesc  *elem_type;   /* for JTYPE_ARRAY */
    TypeDesc  *ok_type;     /* for JTYPE_RESULT */
    TypeDesc  *err_type;    /* for JTYPE_RESULT */
};

TypeDesc *type_make(JiblType base);
TypeDesc *type_make_array(TypeDesc *elem);
TypeDesc *type_make_result(TypeDesc *ok, TypeDesc *err);
void      type_free(TypeDesc *t);
char     *type_to_str(const TypeDesc *t);  /* caller must free */

/* ---- Node types ---- */

typedef enum {
    /* Top-level */
    NODE_PROGRAM,

    /* Declarations */
    NODE_VAR_DECL,    /* type name = expr */
    NODE_CONST_DECL,  /* const type name = expr */
    NODE_FUNC_DECL,   /* func ... */

    /* Statements */
    NODE_ASSIGN,      /* set name = expr */
    NODE_IF,
    NODE_WHILE,
    NODE_RETURN,
    NODE_PRINT,
    NODE_CALL_STMT,   /* call name(args) */

    /* Expressions */
    NODE_BINARY,
    NODE_UNARY,
    NODE_CALL_EXPR,   /* name(args) in expression position */
    NODE_IDENT,
    NODE_INT_LIT,
    NODE_FLOAT_LIT,
    NODE_STRING_LIT,
    NODE_BOOL_LIT,
    NODE_ARRAY_LIT,   /* [e1, e2, ...] */
    NODE_INDEX,       /* arr[idx] */
    NODE_FIELD,       /* expr.name  (r.ok, r.value, r.error) */
    NODE_OK_EXPR,     /* ok(expr) */
    NODE_ERROR_EXPR,  /* error(expr) */
    NODE_ASK_EXPR,    /* ask(expr) */
} NodeType;

/* ---- Dynamic node list ---- */

typedef struct ASTNode ASTNode;

typedef struct {
    ASTNode **nodes;
    int       count;
    int       capacity;
} NodeList;

void nl_init(NodeList *nl);
void nl_push(NodeList *nl, ASTNode *node);
void nl_free(NodeList *nl);

/* ---- Function parameter ---- */

typedef struct {
    TypeDesc *type;
    char     *name;
} Param;

/* ---- AST Node ---- */

struct ASTNode {
    NodeType kind;
    int      line;

    union {
        /* NODE_PROGRAM */
        struct { NodeList body; } program;

        /* NODE_FUNC_DECL */
        struct {
            char     *name;
            Param    *params;
            int       param_count;
            TypeDesc *return_type;
            NodeList  requires;   /* precondition exprs */
            NodeList  ensures;    /* postcondition exprs */
            NodeList  body;
            char     *ai_prompt;  /* NULL unless @ai annotated */
        } func_decl;

        /* NODE_VAR_DECL / NODE_CONST_DECL */
        struct {
            TypeDesc *type;
            char     *name;
            ASTNode  *init;
        } var_decl;

        /* NODE_ASSIGN */
        struct {
            char    *name;
            ASTNode *value;
        } assign;

        /* NODE_IF */
        struct {
            ASTNode *cond;
            NodeList then_body;
            NodeList else_body;
        } if_stmt;

        /* NODE_WHILE */
        struct {
            ASTNode *cond;
            NodeList body;
        } while_stmt;

        /* NODE_RETURN */
        struct { ASTNode *value; } ret;

        /* NODE_PRINT */
        struct { NodeList args; } print;

        /* NODE_CALL_STMT / NODE_CALL_EXPR */
        struct {
            char    *name;
            NodeList args;
        } call;

        /* NODE_BINARY */
        struct {
            char    *op;
            ASTNode *left;
            ASTNode *right;
        } binary;

        /* NODE_UNARY */
        struct {
            char    *op;
            ASTNode *operand;
        } unary;

        /* NODE_IDENT */
        struct { char *name; } ident;

        /* NODE_INT_LIT */
        struct { long value; } int_lit;

        /* NODE_FLOAT_LIT */
        struct { double value; } float_lit;

        /* NODE_STRING_LIT */
        struct { char *value; } string_lit;

        /* NODE_BOOL_LIT */
        struct { int value; } bool_lit;

        /* NODE_ARRAY_LIT */
        struct { NodeList elems; } array_lit;

        /* NODE_INDEX */
        struct {
            ASTNode *array;
            ASTNode *index;
        } index;

        /* NODE_FIELD */
        struct {
            ASTNode *object;
            char    *field;
        } field;

        /* NODE_OK_EXPR / NODE_ERROR_EXPR / NODE_ASK_EXPR */
        struct { ASTNode *expr; } wrap;
    };
};

ASTNode *ast_new(NodeType kind, int line);
void     ast_free(ASTNode *node);

#endif

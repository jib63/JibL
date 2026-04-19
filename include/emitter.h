#ifndef JIBL_EMITTER_H
#define JIBL_EMITTER_H

#include "ast.h"

/* Emits the program AST as S-expression IR text.
   Returns a heap-allocated string; caller must free. */
char *emitter_emit(ASTNode *program);

#endif

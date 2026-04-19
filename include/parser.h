#ifndef JIBL_PARSER_H
#define JIBL_PARSER_H

#include "lexer.h"
#include "ast.h"

ASTNode *parser_parse(TokenList *tl);

#endif

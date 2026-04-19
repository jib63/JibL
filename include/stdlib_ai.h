#ifndef JIBL_STDLIB_AI_H
#define JIBL_STDLIB_AI_H

#include "vm.h"

Value stdlib_ai_ask(const char *prompt);
char *stdlib_ai_generate_func(const char *fname, const char *prompt, const char *sig_sexp);
char *stdlib_ai_cache_lookup(const char *fname, const char *prompt);
void  stdlib_ai_cache_store(const char *fname, const char *prompt, const char *sexp_body);

#endif

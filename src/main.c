#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "emitter.h"
#include "sexp.h"
#include "vm.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "jibl: cannot open '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f); rewind(f);
    char *buf = malloc((size_t)(size + 1));
    if (!buf) { fprintf(stderr, "out of memory\n"); exit(1); }
    fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[size] = '\0';
    return buf;
}

static void usage(void) {
    fprintf(stderr,
        "Usage: jibl [--emit] <file.jibl>\n"
        "       --emit   Print S-expression IR and exit (no execution)\n");
    exit(1);
}

int main(int argc, char **argv) {
    int emit_only = 0;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--emit") == 0) emit_only = 1;
        else if (!filename)                 filename = argv[i];
        else                                usage();
    }
    if (!filename) usage();

    /* 1. Read source */
    char *source = read_file(filename);

    /* 2. Lex */
    TokenList tl = lexer_tokenize(source, filename);
    free(source);

    /* 3. Parse */
    ASTNode *program = parser_parse(&tl);
    lexer_free(&tl);

    /* 4. Semantic checks */
    sema_check(program);

    /* 5. Emit S-expr IR */
    char *ir = emitter_emit(program);
    ast_free(program);

    if (emit_only) {
        printf("%s", ir);
        free(ir);
        return 0;
    }

    /* 6. Parse S-expr IR */
    Sexp *sexp = sexp_parse(ir);
    free(ir);

    /* 7. Execute */
    VM *vm = vm_new();
    vm_run(vm, sexp);
    vm_free(vm);
    sexp_free(sexp);

    return 0;
}

#ifndef JIBL_SEXP_H
#define JIBL_SEXP_H

typedef enum {
    SEXP_ATOM,
    SEXP_LIST
} SexpType;

typedef struct Sexp Sexp;
struct Sexp {
    SexpType type;
    union {
        char  *atom;                /* SEXP_ATOM: heap-allocated string */
        struct {
            Sexp **elems;
            int    count;
            int    capacity;
        } list;                     /* SEXP_LIST */
    };
};

Sexp *sexp_parse(const char *src);
void  sexp_free(Sexp *s);

/* Helpers */
const char *sexp_atom(const Sexp *s);        /* NULL if not SEXP_ATOM */
int         sexp_list_len(const Sexp *s);    /* 0 if not SEXP_LIST */
Sexp       *sexp_nth(const Sexp *s, int i);  /* NULL if out of range */
int         sexp_is(const Sexp *s, const char *car);  /* car matches */

#endif

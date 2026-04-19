#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "sexp.h"

typedef struct { const char *src; int pos; } SexpParser;

static void sp_skip_ws(SexpParser *sp) {
    while (sp->src[sp->pos] && isspace((unsigned char)sp->src[sp->pos]))
        sp->pos++;
    /* skip ; comments (S-expr convention) */
    while (sp->src[sp->pos] == ';') {
        while (sp->src[sp->pos] && sp->src[sp->pos] != '\n') sp->pos++;
        while (sp->src[sp->pos] && isspace((unsigned char)sp->src[sp->pos])) sp->pos++;
    }
}

static Sexp *new_sexp(SexpType type) {
    Sexp *s = calloc(1, sizeof(Sexp));
    s->type = type;
    return s;
}

static void list_push(Sexp *s, Sexp *elem) {
    if (s->list.count >= s->list.capacity) {
        s->list.capacity = s->list.capacity ? s->list.capacity * 2 : 8;
        s->list.elems = realloc(s->list.elems, sizeof(Sexp *) * (size_t)s->list.capacity);
    }
    s->list.elems[s->list.count++] = elem;
}

static Sexp *sp_parse_one(SexpParser *sp);

static Sexp *sp_parse_string(SexpParser *sp) {
    /* opening " already consumed */
    int cap = 64, len = 0;
    char *buf = malloc((size_t)cap);
    while (sp->src[sp->pos] && sp->src[sp->pos] != '"') {
        if (sp->src[sp->pos] == '\\') {
            sp->pos++;
            char e = sp->src[sp->pos++];
            switch (e) {
                case 'n':  buf[len++] = '\n'; break;
                case 't':  buf[len++] = '\t'; break;
                case '\\': buf[len++] = '\\'; break;
                case '"':  buf[len++] = '"';  break;
                default:   buf[len++] = e;    break;
            }
        } else {
            buf[len++] = sp->src[sp->pos++];
        }
        if (len + 2 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
    }
    if (sp->src[sp->pos] == '"') sp->pos++;
    buf[len] = '\0';
    Sexp *s = new_sexp(SEXP_ATOM);
    s->atom = buf;
    return s;
}

static Sexp *sp_parse_atom(SexpParser *sp) {
    int start = sp->pos;
    while (sp->src[sp->pos] &&
           !isspace((unsigned char)sp->src[sp->pos]) &&
           sp->src[sp->pos] != '(' &&
           sp->src[sp->pos] != ')') {
        sp->pos++;
    }
    int len = sp->pos - start;
    Sexp *s = new_sexp(SEXP_ATOM);
    s->atom = malloc((size_t)(len + 1));
    memcpy(s->atom, sp->src + start, (size_t)len);
    s->atom[len] = '\0';
    return s;
}

static Sexp *sp_parse_one(SexpParser *sp) {
    sp_skip_ws(sp);
    if (!sp->src[sp->pos]) return NULL;

    if (sp->src[sp->pos] == '(') {
        sp->pos++;
        Sexp *list = new_sexp(SEXP_LIST);
        while (1) {
            sp_skip_ws(sp);
            if (!sp->src[sp->pos] || sp->src[sp->pos] == ')') break;
            Sexp *elem = sp_parse_one(sp);
            if (elem) list_push(list, elem);
        }
        if (sp->src[sp->pos] == ')') sp->pos++;
        return list;
    }

    if (sp->src[sp->pos] == '"') {
        sp->pos++;
        return sp_parse_string(sp);
    }

    return sp_parse_atom(sp);
}

Sexp *sexp_parse(const char *src) {
    SexpParser sp; sp.src = src; sp.pos = 0;
    return sp_parse_one(&sp);
}

void sexp_free(Sexp *s) {
    if (!s) return;
    if (s->type == SEXP_ATOM) {
        free(s->atom);
    } else {
        for (int i = 0; i < s->list.count; i++) sexp_free(s->list.elems[i]);
        free(s->list.elems);
    }
    free(s);
}

const char *sexp_atom(const Sexp *s) {
    return (s && s->type == SEXP_ATOM) ? s->atom : NULL;
}

int sexp_list_len(const Sexp *s) {
    return (s && s->type == SEXP_LIST) ? s->list.count : 0;
}

Sexp *sexp_nth(const Sexp *s, int i) {
    if (!s || s->type != SEXP_LIST || i < 0 || i >= s->list.count) return NULL;
    return s->list.elems[i];
}

int sexp_is(const Sexp *s, const char *car) {
    if (!s || s->type != SEXP_LIST || s->list.count == 0) return 0;
    const char *a = sexp_atom(s->list.elems[0]);
    return a && strcmp(a, car) == 0;
}

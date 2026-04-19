#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "stdlib_json.h"
#include "error.h"

/* ---- Minimal JSON parser ---- */

typedef enum {
    JNODE_OBJECT, JNODE_ARRAY, JNODE_STRING,
    JNODE_NUMBER, JNODE_BOOL, JNODE_NULL
} JNodeType;

typedef struct JsonNode JsonNode;
typedef struct JsonPair { char *key; JsonNode *value; } JsonPair;

struct JsonNode {
    JNodeType type;
    union {
        struct { JsonPair *pairs; int count; int cap; } obj;
        struct { JsonNode **elems; int count; int cap; } arr;
        char  *str;
        double num;
        int    bool_val;
    };
};

static JsonNode *jnode_new(JNodeType t) {
    JsonNode *n = calloc(1, sizeof(JsonNode));
    n->type = t;
    return n;
}


typedef struct { const char *src; int pos; } JP;

static void jp_skip(JP *p) {
    while (isspace((unsigned char)p->src[p->pos])) p->pos++;
}

static char jp_peek(JP *p) { return p->src[p->pos]; }
static char jp_adv(JP *p) { return p->src[p->pos++]; }

static char *jp_read_string(JP *p) {
    jp_adv(p); /* opening " */
    int cap = 64, len = 0;
    char *buf = malloc((size_t)cap);
    while (p->src[p->pos] && p->src[p->pos] != '"') {
        if (p->src[p->pos] == '\\') { p->pos++; }
        if (len + 2 >= cap) { cap *= 2; buf = realloc(buf, (size_t)cap); }
        buf[len++] = jp_adv(p);
    }
    if (p->src[p->pos] == '"') jp_adv(p);
    buf[len] = '\0';
    return buf;
}

static JsonNode *jp_parse(JP *p);

static JsonNode *jp_parse_object(JP *p) {
    jp_adv(p); /* { */
    JsonNode *n = jnode_new(JNODE_OBJECT);
    jp_skip(p);
    while (jp_peek(p) && jp_peek(p) != '}') {
        jp_skip(p);
        if (jp_peek(p) != '"') break;
        char *key = jp_read_string(p);
        jp_skip(p);
        if (jp_peek(p) == ':') jp_adv(p);
        jp_skip(p);
        JsonNode *val = jp_parse(p);
        /* store pair */
        if (n->obj.count >= n->obj.cap) {
            n->obj.cap = n->obj.cap ? n->obj.cap * 2 : 8;
            n->obj.pairs = realloc(n->obj.pairs, sizeof(JsonPair) * (size_t)n->obj.cap);
        }
        n->obj.pairs[n->obj.count].key   = key;
        n->obj.pairs[n->obj.count].value = val;
        n->obj.count++;
        jp_skip(p);
        if (jp_peek(p) == ',') jp_adv(p);
        jp_skip(p);
    }
    if (jp_peek(p) == '}') jp_adv(p);
    return n;
}

static JsonNode *jp_parse_array(JP *p) {
    jp_adv(p); /* [ */
    JsonNode *n = jnode_new(JNODE_ARRAY);
    jp_skip(p);
    while (jp_peek(p) && jp_peek(p) != ']') {
        JsonNode *elem = jp_parse(p);
        if (n->arr.count >= n->arr.cap) {
            n->arr.cap = n->arr.cap ? n->arr.cap * 2 : 8;
            n->arr.elems = realloc(n->arr.elems, sizeof(JsonNode*) * (size_t)n->arr.cap);
        }
        n->arr.elems[n->arr.count++] = elem;
        jp_skip(p);
        if (jp_peek(p) == ',') jp_adv(p);
        jp_skip(p);
    }
    if (jp_peek(p) == ']') jp_adv(p);
    return n;
}

static JsonNode *jp_parse(JP *p) {
    jp_skip(p);
    char c = jp_peek(p);
    if (c == '{') return jp_parse_object(p);
    if (c == '[') return jp_parse_array(p);
    if (c == '"') {
        JsonNode *n = jnode_new(JNODE_STRING);
        n->str = jp_read_string(p);
        return n;
    }
    if (c == 't') { p->pos += 4; JsonNode *n = jnode_new(JNODE_BOOL); n->bool_val = 1; return n; }
    if (c == 'f') { p->pos += 5; JsonNode *n = jnode_new(JNODE_BOOL); n->bool_val = 0; return n; }
    if (c == 'n') { p->pos += 4; return jnode_new(JNODE_NULL); }
    if (c == '-' || isdigit((unsigned char)c)) {
        char *end;
        double v = strtod(p->src + p->pos, &end);
        p->pos = (int)(end - p->src);
        JsonNode *n = jnode_new(JNODE_NUMBER);
        n->num = v;
        return n;
    }
    return jnode_new(JNODE_NULL);
}

static JsonNode *find_key(JsonNode *obj, const char *key) {
    if (!obj || obj->type != JNODE_OBJECT) return NULL;
    for (int i = 0; i < obj->obj.count; i++)
        if (strcmp(obj->obj.pairs[i].key, key) == 0)
            return obj->obj.pairs[i].value;
    return NULL;
}

/* ---- Public API ---- */

Value stdlib_json_parse(const char *src) {
    if (!src) return val_result_err(val_string("null input"));
    JP p; p.src = src; p.pos = 0;
    JsonNode *root = jp_parse(&p);
    if (!root) return val_result_err(val_string("parse error"));
    Value v; v.type = VAL_JSON; v.json = root;
    return val_result_ok(v);
}

Value stdlib_json_get_string(Value json_val, const char *key) {
    if (json_val.type != VAL_JSON) return val_string("");
    JsonNode *node = find_key((JsonNode *)json_val.json, key);
    if (!node || node->type != JNODE_STRING) return val_string("");
    return val_string(node->str);
}

Value stdlib_json_get_int(Value json_val, const char *key) {
    if (json_val.type != VAL_JSON) return val_int(0);
    JsonNode *node = find_key((JsonNode *)json_val.json, key);
    if (!node || node->type != JNODE_NUMBER) return val_int(0);
    return val_int((long)node->num);
}

Value stdlib_json_get_bool(Value json_val, const char *key) {
    if (json_val.type != VAL_JSON) return val_bool(0);
    JsonNode *node = find_key((JsonNode *)json_val.json, key);
    if (!node || node->type != JNODE_BOOL) return val_bool(0);
    return val_bool(node->bool_val);
}

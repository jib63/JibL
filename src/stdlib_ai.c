#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include "stdlib_ai.h"
#include "error.h"

/* ---- Simple SHA-256 for cache keys ---- */

#define RR32(x,n) (((x)>>(n))|((x)<<(32-(n))))

static const unsigned int K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256(const unsigned char *msg, size_t len, unsigned char out[32]) {
    unsigned int h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    /* Append padding */
    size_t total = ((len + 8) / 64 + 1) * 64;
    unsigned char *buf = calloc(1, total);
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    unsigned long long bitlen = (unsigned long long)len * 8;
    for (int i = 0; i < 8; i++)
        buf[total - 8 + i] = (unsigned char)(bitlen >> (56 - i * 8));

    for (size_t chunk = 0; chunk < total; chunk += 64) {
        unsigned int w[64];
        for (int i = 0; i < 16; i++) {
            unsigned char *b = buf + chunk + i * 4;
            w[i] = ((unsigned int)b[0]<<24)|((unsigned int)b[1]<<16)|
                   ((unsigned int)b[2]<<8)|(unsigned int)b[3];
        }
        for (int i = 16; i < 64; i++) {
            unsigned int s0 = RR32(w[i-15],7)^RR32(w[i-15],18)^(w[i-15]>>3);
            unsigned int s1 = RR32(w[i-2],17)^RR32(w[i-2],19)^(w[i-2]>>10);
            w[i] = w[i-16]+s0+w[i-7]+s1;
        }
        unsigned int a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            unsigned int S1  = RR32(e,6)^RR32(e,11)^RR32(e,25);
            unsigned int ch  = (e&f)^((~e)&g);
            unsigned int tmp1= hh+S1+ch+K256[i]+w[i];
            unsigned int S0  = RR32(a,2)^RR32(a,13)^RR32(a,22);
            unsigned int maj = (a&b)^(a&c)^(b&c);
            unsigned int tmp2= S0+maj;
            hh=g; g=f; f=e; e=d+tmp1; d=c; c=b; b=a; a=tmp1+tmp2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(buf);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (unsigned char)(h[i]>>24);
        out[i*4+1] = (unsigned char)(h[i]>>16);
        out[i*4+2] = (unsigned char)(h[i]>>8);
        out[i*4+3] = (unsigned char)(h[i]);
    }
}

static void cache_key(const char *fname, const char *prompt, char out[65]) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s|%s", fname, prompt);
    unsigned char hash[32];
    sha256((const unsigned char *)buf, strlen(buf), hash);
    for (int i = 0; i < 32; i++) sprintf(out + i*2, "%02x", hash[i]);
    out[64] = '\0';
}

/* ---- HTTP helpers for AI API calls ---- */

typedef struct { char *data; size_t size; } Buf;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    Buf *b = (Buf *)ud;
    size_t total = size * nmemb;
    b->data = realloc(b->data, b->size + total + 1);
    memcpy(b->data + b->size, ptr, total);
    b->size += total;
    b->data[b->size] = '\0';
    return total;
}

static char *ai_post(const char *url, const char *api_key,
                     const char *model, const char *json_body) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    Buf buf; buf.data = malloc(1); buf.data[0] = '\0'; buf.size = 0;

    char auth_hdr[256];
    snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s", api_key);

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, auth_hdr);
    if (model) {
        /* Anthropic requires x-api-key + anthropic-version */
        hdrs = curl_slist_append(hdrs, "anthropic-version: 2023-06-01");
        char ak_hdr[256];
        snprintf(ak_hdr, sizeof(ak_hdr), "x-api-key: %s", api_key);
        hdrs = curl_slist_append(hdrs, ak_hdr);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(hdrs);

    return buf.data;
}

/* Very naive JSON string extraction: find "content":"..." or "text":"..." */
static char *extract_text(const char *json) {
    if (!json) return NULL;
    /* Try Anthropic format: "text":"..." inside content array */
    const char *p = strstr(json, "\"text\":");
    if (!p) p = strstr(json, "\"content\":");
    if (!p) return strdup("");
    p = strchr(p, '"'); if (!p) return strdup("");
    p++; /* skip opening field quote */
    p = strchr(p, '"'); if (!p) return strdup("");
    p++; /* opening value quote */
    const char *end = p;
    int cap = 256, len = 0;
    char *out = malloc((size_t)cap);
    while (*end && *end != '"') {
        if (*end == '\\' && *(end+1)) { end++; }
        if (len + 2 >= cap) { cap *= 2; out = realloc(out, (size_t)cap); }
        out[len++] = *end++;
    }
    out[len] = '\0';
    return out;
}

static char *extract_model(const char *json) {
    if (!json) return strdup("unknown");
    const char *p = strstr(json, "\"model\":");
    if (!p) return strdup("unknown");
    p = strchr(p + 8, '"'); if (!p) return strdup("unknown");
    p++;
    const char *end = strchr(p, '"');
    if (!end) return strdup("unknown");
    int len = (int)(end - p);
    char *m = malloc((size_t)(len + 1));
    memcpy(m, p, (size_t)len);
    m[len] = '\0';
    return m;
}

static int extract_tokens(const char *json) {
    if (!json) return 0;
    const char *p = strstr(json, "\"input_tokens\":");
    if (!p) p = strstr(json, "\"total_tokens\":");
    if (!p) p = strstr(json, "\"prompt_tokens\":");
    if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    return atoi(p + 1);
}

/* ---- Public API ---- */

Value stdlib_ai_ask(const char *prompt) {
    const char *provider = getenv("JIBL_AI_PROVIDER");
    const char *api_key  = getenv("JIBL_AI_KEY");
    const char *model_env= getenv("JIBL_AI_MODEL");

    if (!api_key || !*api_key) {
        Value err = val_string("JIBL_AI_KEY not set");
        return val_result_err(err);
    }

    int is_anthropic = !provider || strcmp(provider, "anthropic") == 0;
    const char *default_model = is_anthropic ? "claude-sonnet-4-6" : "gpt-4o";
    const char *model = model_env && *model_env ? model_env : default_model;

    char json_body[4096];
    char *response = NULL;

    /* Escape prompt for JSON */
    char escaped[2048]; int ei = 0;
    for (const char *c = prompt; *c && ei < 2040; c++) {
        if (*c == '"')       { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
        else if (*c == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
        else if (*c == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
        else escaped[ei++] = *c;
    }
    escaped[ei] = '\0';

    if (is_anthropic) {
        snprintf(json_body, sizeof(json_body),
            "{\"model\":\"%s\",\"max_tokens\":1024,"
            "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
            model, escaped);
        response = ai_post("https://api.anthropic.com/v1/messages",
                           api_key, model, json_body);
    } else {
        snprintf(json_body, sizeof(json_body),
            "{\"model\":\"%s\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
            model, escaped);
        response = ai_post("https://api.openai.com/v1/chat/completions",
                           api_key, NULL, json_body);
    }

    if (!response) {
        Value r; r.type = VAL_AI_RESPONSE;
        r.ai.content = strdup("(no response)");
        r.ai.model   = strdup(model);
        r.ai.tokens  = 0;
        return r;
    }

    Value r; r.type = VAL_AI_RESPONSE;
    r.ai.content = extract_text(response);
    r.ai.model   = extract_model(response);
    r.ai.tokens  = extract_tokens(response);
    free(response);
    return r;
}

char *stdlib_ai_generate_func(const char *fname, const char *prompt, const char *sig_sexp) {
    const char *provider = getenv("JIBL_AI_PROVIDER");
    const char *api_key  = getenv("JIBL_AI_KEY");
    const char *model_env= getenv("JIBL_AI_MODEL");

    if (!api_key || !*api_key) {
        fprintf(stderr, "warning: JIBL_AI_KEY not set, @ai annotation skipped for '%s'\n", fname);
        return NULL;
    }

    int is_anthropic = !provider || strcmp(provider, "anthropic") == 0;
    const char *default_model = is_anthropic ? "claude-sonnet-4-6" : "gpt-4o";
    const char *model = model_env && *model_env ? model_env : default_model;

    char sys_prompt[2048];
    snprintf(sys_prompt, sizeof(sys_prompt),
        "You are a JibL language code generator. "
        "Generate ONLY a valid S-expression block: (block stmt...) "
        "for the following function. "
        "Function signature: %s. "
        "Task: %s. "
        "Return ONLY the (block ...) S-expression, no explanation, no markdown.",
        sig_sexp, prompt);

    /* Escape */
    char escaped[3072]; int ei = 0;
    for (const char *c = sys_prompt; *c && ei < 3060; c++) {
        if (*c == '"')       { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
        else if (*c == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
        else if (*c == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
        else escaped[ei++] = *c;
    }
    escaped[ei] = '\0';

    char json_body[4096]; char *response = NULL;
    if (is_anthropic) {
        snprintf(json_body, sizeof(json_body),
            "{\"model\":\"%s\",\"max_tokens\":2048,"
            "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
            model, escaped);
        response = ai_post("https://api.anthropic.com/v1/messages",
                           api_key, model, json_body);
    } else {
        snprintf(json_body, sizeof(json_body),
            "{\"model\":\"%s\","
            "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
            model, escaped);
        response = ai_post("https://api.openai.com/v1/chat/completions",
                           api_key, NULL, json_body);
    }

    char *text = extract_text(response);
    free(response);

    /* Strip markdown code fences if present */
    if (text) {
        char *start = strstr(text, "(block");
        if (!start) start = text;
        char *result = strdup(start);
        free(text);
        return result;
    }
    return NULL;
}

char *stdlib_ai_cache_lookup(const char *fname, const char *prompt) {
    char key[65]; cache_key(fname, prompt, key);
    char path[256];
    snprintf(path, sizeof(path), ".jibl_cache/%s.sexp", key);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f); rewind(f);
    char *buf = malloc((size_t)(size + 1));
    fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[size] = '\0';
    return buf;
}

void stdlib_ai_cache_store(const char *fname, const char *prompt, const char *sexp_body) {
    char key[65]; cache_key(fname, prompt, key);
#ifdef _WIN32
    _mkdir(".jibl_cache");
#else
    mkdir(".jibl_cache", 0755);
#endif
    char path[256];
    snprintf(path, sizeof(path), ".jibl_cache/%s.sexp", key);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "warning: cannot write cache file %s\n", path); return; }
    fwrite(sexp_body, 1, strlen(sexp_body), f);
    fclose(f);
}

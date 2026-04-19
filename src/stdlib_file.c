#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdlib_file.h"

Value stdlib_file_read(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "cannot open file '%s'", path);
        return val_result_err(val_string(msg));
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)(size + 1));
    if (!buf) { fclose(f); return val_result_err(val_string("out of memory")); }
    long read = (long)fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    Value content = val_string(buf);
    free(buf);
    return val_result_ok(content);
}

Value stdlib_file_write(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        char msg[256];
        snprintf(msg, sizeof(msg), "cannot write file '%s'", path);
        return val_result_err(val_string(msg));
    }
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    if (written != len)
        return val_result_err(val_string("write error"));
    return val_result_ok(val_void());
}

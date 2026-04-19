#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "stdlib_http.h"

typedef struct {
    char *data;
    size_t size;
} CurlBuf;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    CurlBuf *buf = (CurlBuf *)userdata;
    size_t total = size * nmemb;
    buf->data = realloc(buf->data, buf->size + total + 1);
    if (!buf->data) return 0;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static Value do_request(const char *url, const char *post_body) {
    CURL *curl = curl_easy_init();
    if (!curl) return val_result_err(val_string("curl init failed"));

    CurlBuf buf; buf.data = malloc(1); buf.data[0] = '\0'; buf.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    struct curl_slist *headers = NULL;
    if (post_body) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "HTTP error: %s", curl_easy_strerror(res));
        free(buf.data);
        return val_result_err(val_string(msg));
    }

    Value content = val_string(buf.data);
    free(buf.data);
    return val_result_ok(content);
}

Value stdlib_http_get(const char *url) {
    return do_request(url, NULL);
}

Value stdlib_http_post(const char *url, const char *body) {
    return do_request(url, body);
}

#ifndef JIBL_STDLIB_HTTP_H
#define JIBL_STDLIB_HTTP_H

#include "vm.h"

Value stdlib_http_get(const char *url);
Value stdlib_http_post(const char *url, const char *body);

#endif

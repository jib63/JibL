#ifndef JIBL_STDLIB_JSON_H
#define JIBL_STDLIB_JSON_H

#include "vm.h"

Value stdlib_json_parse(const char *src);
Value stdlib_json_get_string(Value json_val, const char *key);
Value stdlib_json_get_int(Value json_val, const char *key);
Value stdlib_json_get_bool(Value json_val, const char *key);

#endif

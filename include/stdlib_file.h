#ifndef JIBL_STDLIB_FILE_H
#define JIBL_STDLIB_FILE_H

#include "vm.h"

Value stdlib_file_read(const char *path);
Value stdlib_file_write(const char *path, const char *content);

#endif

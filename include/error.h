#ifndef JIBL_ERROR_H
#define JIBL_ERROR_H

void jibl_error_init(const char *filename);
void jibl_error(int line, const char *fmt, ...);

#endif

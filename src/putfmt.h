/* Number formatting for console output. Each function writes at p and
   returns the end. No NUL is written. */
#pragma once
#include <stdint.h>

char *put_str(char *p, const char *s);
char *put_hex(char *p, uint32_t v);
char *put_dec(char *p, uint32_t v);
char *put_int(char *p, int32_t v);

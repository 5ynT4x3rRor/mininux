#ifndef TINYC_COMPILER_H
#define TINYC_COMPILER_H

unsigned int tinyc_compile_source(const char *source,
                                  unsigned char *output,
                                  unsigned int capacity);

#endif
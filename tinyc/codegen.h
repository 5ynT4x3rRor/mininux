#ifndef TINYC_CODEGEN_H
#define TINYC_CODEGEN_H

#include "parser.h"

unsigned int tinyc_codegen_return(const struct tinyc_program *program,
                                  unsigned char *output,
                                  unsigned int capacity);

#endif
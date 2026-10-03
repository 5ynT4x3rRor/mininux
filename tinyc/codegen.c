#include "codegen.h"

unsigned int tinyc_codegen_return(const struct tinyc_program *program,
                                  unsigned char *output,
                                  unsigned int capacity)
{
    unsigned int value = program->return_value;

    if (capacity < 6) {
        return 0;
    }

    output[0] = 0xb8;
    output[1] = (unsigned char)(value & 0xff);
    output[2] = (unsigned char)((value >> 8) & 0xff);
    output[3] = (unsigned char)((value >> 16) & 0xff);
    output[4] = (unsigned char)((value >> 24) & 0xff);
    output[5] = 0xc3;
    return 6;
}
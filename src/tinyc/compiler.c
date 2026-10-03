#include "codegen.h"
#include "compiler.h"
#include "parser.h"

unsigned int tinyc_compile_source(const char *source,
                                  unsigned char *output,
                                  unsigned int capacity)
{
    struct tinyc_program program;

    if (tinyc_parse_program(source, &program) != 0) {
        return 0;
    }
    return tinyc_codegen_return(&program, output, capacity);
}
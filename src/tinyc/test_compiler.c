#include "compiler.h"

int main(void)
{
    const char source[] = "int main() { return 42; }";
    unsigned char output[6];
    unsigned int size = tinyc_compile_source(source, output, sizeof(output));

    if (size != 6 || output[0] != 0xb8 || output[1] != 42 || output[2] != 0x00 ||
        output[3] != 0x00 || output[4] != 0x00 || output[5] != 0xc3) {
        return 1;
    }
    return 0;
}
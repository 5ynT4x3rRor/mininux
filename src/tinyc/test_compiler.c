#include "compiler.h"

static int check(const char *source, int expected)
{
    unsigned char code[256];
    int result = 0;
    unsigned int length = tinyc_compile_source(source, code, sizeof(code));

    return length != 0 && tinyc_execute(code, length, &result) == 0 &&
           result == expected;
}

int main(void)
{
    unsigned char code[256];
    int result;
    unsigned int length;

    if (!check("int main() { return 42; }", 42) ||
        !check("int main() { return 2 + 3 * 4; }", 14) ||
        !check("int main() { return (2 + 3) * 4; }", 20) ||
        !check("int main() { return (2 + 3) * 4 - 7 / 2; }", 17) ||
        !check("int main() { int x = 40; int y = 2; return x + y; }", 42) ||
        !check("int main() { int n = 7; return n * n; }", 49) ||
        !check("int main() { return -7 + 2; }", -5) ||
        !check("int main() { return 20 / 4 - 3; }", 2)) {
        return 1;
    }
    if (tinyc_compile_source("int main() { return 1 + ; }", code, sizeof(code)) != 0 ||
        tinyc_compile_source("int main() { return 1; } trailing", code, sizeof(code)) != 0 ||
        tinyc_compile_source("int not_main() { return 1; }", code, sizeof(code)) != 0 ||
        tinyc_compile_source("int main() { return missing; }", code, sizeof(code)) != 0 ||
        tinyc_compile_source("int main() { int x = x; return x; }", code, sizeof(code)) != 0 ||
        tinyc_compile_source("int main() { return 1; }@", code, sizeof(code)) != 0) {
        return 2;
    }
    length = tinyc_compile_source("int main() { return 1 / 0; }", code, sizeof(code));
    if (length == 0 || tinyc_execute(code, length, &result) != -2) {
        return 3;
    }
    return 0;
}

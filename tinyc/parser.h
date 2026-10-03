#ifndef TINYC_PARSER_H
#define TINYC_PARSER_H

struct tinyc_program {
    const char *function_name;
    unsigned int function_name_length;
    unsigned int return_value;
};

int tinyc_parse_program(const char *source, struct tinyc_program *program);

#endif
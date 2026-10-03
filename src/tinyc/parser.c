#include "lexer.h"
#include "parser.h"

static int token_is(struct tinyc_token token, enum tinyc_token_kind kind)
{
    return token.kind == kind;
}

int tinyc_parse_program(const char *source, struct tinyc_program *program)
{
    struct tinyc_lexer lexer;
    struct tinyc_token token;

    tinyc_lexer_init(&lexer, source);
    token = tinyc_next_token(&lexer);
    if (!token_is(token, TINYC_INT)) {
        return -1;
    }

    token = tinyc_next_token(&lexer);
    if (!token_is(token, TINYC_IDENTIFIER)) {
        return -1;
    }
    program->function_name = token.start;
    program->function_name_length = token.length;

    if (!token_is(tinyc_next_token(&lexer), TINYC_LPAREN) ||
        !token_is(tinyc_next_token(&lexer), TINYC_RPAREN) ||
        !token_is(tinyc_next_token(&lexer), TINYC_LBRACE) ||
        !token_is(tinyc_next_token(&lexer), TINYC_RETURN)) {
        return -1;
    }

    token = tinyc_next_token(&lexer);
    if (!token_is(token, TINYC_NUMBER)) {
        return -1;
    }
    program->return_value = token.number;

    if (!token_is(tinyc_next_token(&lexer), TINYC_SEMICOLON) ||
        !token_is(tinyc_next_token(&lexer), TINYC_RBRACE) ||
        !token_is(tinyc_next_token(&lexer), TINYC_EOF)) {
        return -1;
    }
    return 0;
}
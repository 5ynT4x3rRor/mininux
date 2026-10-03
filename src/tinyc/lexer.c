#include "lexer.h"

static int is_space(char character)
{
    return character == ' ' || character == '\n' || character == '\r' || character == '\t';
}

static int is_alpha(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') || character == '_';
}

static int is_digit(char character)
{
    return character >= '0' && character <= '9';
}

static int matches(const char *start, unsigned int length, const char *word)
{
    unsigned int index = 0;

    while (word[index] != '\0' && index < length && start[index] == word[index]) {
        index++;
    }
    return index == length && word[index] == '\0';
}

void tinyc_lexer_init(struct tinyc_lexer *lexer, const char *source)
{
    lexer->source = source;
    lexer->position = 0;
}

struct tinyc_token tinyc_next_token(struct tinyc_lexer *lexer)
{
    struct tinyc_token token;
    const char *source = lexer->source;
    unsigned int start;

    while (is_space(source[lexer->position])) {
        lexer->position++;
    }

    start = lexer->position;
    token.start = &source[start];
    token.length = 1;
    token.number = 0;

    if (source[start] == '\0') {
        token.kind = TINYC_EOF;
        token.length = 0;
        return token;
    }

    if (is_alpha(source[start])) {
        lexer->position++;
        while (is_alpha(source[lexer->position]) || is_digit(source[lexer->position])) {
            lexer->position++;
        }
        token.length = lexer->position - start;
        token.kind = matches(token.start, token.length, "int") ? TINYC_INT :
                     matches(token.start, token.length, "return") ? TINYC_RETURN :
                     TINYC_IDENTIFIER;
        return token;
    }

    if (is_digit(source[start])) {
        token.kind = TINYC_NUMBER;
        while (is_digit(source[lexer->position])) {
            token.number = token.number * 10 + (unsigned int)(source[lexer->position] - '0');
            lexer->position++;
        }
        token.length = lexer->position - start;
        return token;
    }

    lexer->position++;
    token.kind = source[start] == '+' ? TINYC_PLUS :
                 source[start] == '-' ? TINYC_MINUS :
                 source[start] == '*' ? TINYC_STAR :
                 source[start] == '/' ? TINYC_SLASH :
                 source[start] == '=' ? TINYC_ASSIGN :
                 source[start] == ';' ? TINYC_SEMICOLON :
                 source[start] == '(' ? TINYC_LPAREN :
                 source[start] == ')' ? TINYC_RPAREN :
                 source[start] == '{' ? TINYC_LBRACE :
                 source[start] == '}' ? TINYC_RBRACE : TINYC_EOF;
    return token;
}
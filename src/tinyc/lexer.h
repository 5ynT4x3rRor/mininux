#ifndef TINYC_LEXER_H
#define TINYC_LEXER_H

enum tinyc_token_kind {
    TINYC_EOF,
    TINYC_INVALID,
    TINYC_IDENTIFIER,
    TINYC_NUMBER,
    TINYC_INT,
    TINYC_RETURN,
    TINYC_PLUS,
    TINYC_MINUS,
    TINYC_STAR,
    TINYC_SLASH,
    TINYC_ASSIGN,
    TINYC_SEMICOLON,
    TINYC_LPAREN,
    TINYC_RPAREN,
    TINYC_LBRACE,
    TINYC_RBRACE
};

struct tinyc_token {
    enum tinyc_token_kind kind;
    const char *start;
    unsigned int length;
    unsigned int number;
};

struct tinyc_lexer {
    const char *source;
    unsigned int position;
};

void tinyc_lexer_init(struct tinyc_lexer *lexer, const char *source);
struct tinyc_token tinyc_next_token(struct tinyc_lexer *lexer);

#endif
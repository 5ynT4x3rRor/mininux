#include "compiler.h"
#include "lexer.h"

enum {
    BC_PUSH = 1,
    BC_ADD,
    BC_SUB,
    BC_MUL,
    BC_DIV,
    BC_NEG,
    BC_RETURN,
    BC_LOAD,
    BC_STORE,
    MAX_DEPTH = 32,
    MAX_VARIABLES = 8
};

struct variable {
    const char *name;
    unsigned int length;
};

struct compiler {
    struct tinyc_lexer lexer;
    struct tinyc_token token;
    unsigned char *output;
    unsigned int capacity;
    unsigned int length;
    unsigned int depth;
    unsigned int variable_count;
    struct variable variables[MAX_VARIABLES];
    unsigned char failed;
};

static void next(struct compiler *compiler)
{
    compiler->token = tinyc_next_token(&compiler->lexer);
}

static unsigned char emit(struct compiler *compiler, unsigned char value)
{
    if (compiler->length >= compiler->capacity) {
        compiler->failed = 1;
        return 0;
    }
    compiler->output[compiler->length++] = value;
    return 1;
}

static unsigned char emit_integer(struct compiler *compiler, unsigned int value)
{
    if (compiler->capacity - compiler->length < 5) {
        compiler->failed = 1;
        return 0;
    }
    emit(compiler, BC_PUSH);
    emit(compiler, (unsigned char)value);
    emit(compiler, (unsigned char)(value >> 8));
    emit(compiler, (unsigned char)(value >> 16));
    emit(compiler, (unsigned char)(value >> 24));
    return 1;
}

static int variable_find(const struct compiler *compiler, struct tinyc_token token)
{
    for (unsigned int index = 0; index < compiler->variable_count; index++) {
        const struct variable *variable = &compiler->variables[index];

        if (variable->length != token.length) {
            continue;
        }
        unsigned int offset = 0;
        while (offset < token.length && variable->name[offset] == token.start[offset]) {
            offset++;
        }
        if (offset == token.length) {
            return (int)index;
        }
    }
    return -1;
}

static void expression(struct compiler *compiler);

static void primary(struct compiler *compiler)
{
    struct tinyc_token token = compiler->token;

    if (++compiler->depth > MAX_DEPTH) {
        compiler->failed = 1;
        return;
    }
    if (token.kind == TINYC_NUMBER) {
        emit_integer(compiler, token.number);
        next(compiler);
    } else if (token.kind == TINYC_IDENTIFIER) {
        int index = variable_find(compiler, token);

        if (index < 0) {
            compiler->failed = 1;
        } else {
            emit(compiler, BC_LOAD);
            emit(compiler, (unsigned char)index);
        }
        next(compiler);
    } else if (token.kind == TINYC_MINUS) {
        next(compiler);
        primary(compiler);
        emit(compiler, BC_NEG);
    } else if (token.kind == TINYC_LPAREN) {
        next(compiler);
        expression(compiler);
        if (compiler->token.kind != TINYC_RPAREN) {
            compiler->failed = 1;
        } else {
            next(compiler);
        }
    } else {
        compiler->failed = 1;
    }
    compiler->depth--;
}

static void term(struct compiler *compiler)
{
    primary(compiler);
    while (!compiler->failed &&
           (compiler->token.kind == TINYC_STAR || compiler->token.kind == TINYC_SLASH)) {
        enum tinyc_token_kind operation = compiler->token.kind;

        next(compiler);
        primary(compiler);
        emit(compiler, operation == TINYC_STAR ? BC_MUL : BC_DIV);
    }
}

static void expression(struct compiler *compiler)
{
    term(compiler);
    while (!compiler->failed &&
           (compiler->token.kind == TINYC_PLUS || compiler->token.kind == TINYC_MINUS)) {
        enum tinyc_token_kind operation = compiler->token.kind;

        next(compiler);
        term(compiler);
        emit(compiler, operation == TINYC_PLUS ? BC_ADD : BC_SUB);
    }
}

unsigned int tinyc_compile_source(const char *source,
                                  unsigned char *output,
                                  unsigned int capacity)
{
    struct compiler compiler;

    if (source == 0 || output == 0 || capacity == 0) {
        return 0;
    }
    tinyc_lexer_init(&compiler.lexer, source);
    compiler.output = output;
    compiler.capacity = capacity;
    compiler.length = 0;
    compiler.depth = 0;
    compiler.variable_count = 0;
    compiler.failed = 0;
    for (unsigned int index = 0; index < MAX_VARIABLES; index++) {
        compiler.variables[index].name = 0;
        compiler.variables[index].length = 0;
    }
    next(&compiler);
    if (compiler.token.kind != TINYC_INT) {
        return 0;
    }
    next(&compiler);
    if (compiler.token.kind != TINYC_IDENTIFIER ||
        compiler.token.length != 4 ||
        compiler.token.start[0] != 'm' || compiler.token.start[1] != 'a' ||
        compiler.token.start[2] != 'i' || compiler.token.start[3] != 'n') {
        return 0;
    }
    next(&compiler);
    if (compiler.token.kind != TINYC_LPAREN) {
        return 0;
    }
    next(&compiler);
    if (compiler.token.kind != TINYC_RPAREN) {
        return 0;
    }
    next(&compiler);
    if (compiler.token.kind != TINYC_LBRACE) {
        return 0;
    }
    next(&compiler);
    while (compiler.token.kind == TINYC_INT && !compiler.failed) {
        struct tinyc_token name;

        next(&compiler);
        name = compiler.token;
        if (name.kind != TINYC_IDENTIFIER ||
            compiler.variable_count >= MAX_VARIABLES ||
            variable_find(&compiler, name) >= 0) {
            return 0;
        }
        next(&compiler);
        if (compiler.token.kind != TINYC_ASSIGN) {
            return 0;
        }
        next(&compiler);
        expression(&compiler);
        if (compiler.failed || compiler.token.kind != TINYC_SEMICOLON) {
            return 0;
        }
        compiler.variables[compiler.variable_count].name = name.start;
        compiler.variables[compiler.variable_count].length = name.length;
        emit(&compiler, BC_STORE);
        emit(&compiler, (unsigned char)compiler.variable_count);
        compiler.variable_count++;
        next(&compiler);
    }
    if (compiler.token.kind != TINYC_RETURN) {
        return 0;
    }
    next(&compiler);
    expression(&compiler);
    if (compiler.failed || compiler.token.kind != TINYC_SEMICOLON) {
        return 0;
    }
    next(&compiler);
    if (compiler.token.kind != TINYC_RBRACE) {
        return 0;
    }
    next(&compiler);
    if (compiler.token.kind != TINYC_EOF || compiler.failed ||
        !emit(&compiler, BC_RETURN)) {
        return 0;
    }
    return compiler.length;
}

int tinyc_execute(const unsigned char *code, unsigned int length, int *result)
{
    unsigned int stack[MAX_DEPTH];
    unsigned int variables[MAX_VARIABLES] = {0};
    unsigned int depth = 0;
    unsigned int position = 0;

    if (code == 0 || result == 0 || length == 0) {
        return -1;
    }
    while (position < length) {
        unsigned char instruction = code[position++];

        if (instruction == BC_PUSH) {
            unsigned int value;

            if (depth >= MAX_DEPTH || length - position < 4) {
                return -1;
            }
            value = (unsigned int)code[position] |
                    ((unsigned int)code[position + 1] << 8) |
                    ((unsigned int)code[position + 2] << 16) |
                    ((unsigned int)code[position + 3] << 24);
            position += 4;
            stack[depth++] = value;
        } else if (instruction == BC_NEG) {
            if (depth < 1) {
                return -1;
            }
            stack[depth - 1] = 0U - stack[depth - 1];
        } else if (instruction == BC_LOAD) {
            unsigned int index;

            if (position >= length || depth >= MAX_DEPTH) {
                return -1;
            }
            index = code[position++];
            if (index >= MAX_VARIABLES) {
                return -1;
            }
            stack[depth++] = variables[index];
        } else if (instruction == BC_STORE) {
            unsigned int index;

            if (position >= length || depth < 1) {
                return -1;
            }
            index = code[position++];
            if (index >= MAX_VARIABLES) {
                return -1;
            }
            variables[index] = stack[--depth];
        } else if (instruction == BC_RETURN) {
            if (depth != 1 || position != length) {
                return -1;
            }
            *result = (int)stack[0];
            return 0;
        } else {
            unsigned int right;
            unsigned int left;

            if (depth < 2) {
                return -1;
            }
            right = stack[--depth];
            left = stack[depth - 1];
            switch (instruction) {
            case BC_ADD: stack[depth - 1] = left + right; break;
            case BC_SUB: stack[depth - 1] = left - right; break;
            case BC_MUL: stack[depth - 1] = left * right; break;
            case BC_DIV:
                if (right == 0) {
                    return -2;
                }
                if (left == 0x80000000U && right == 0xffffffffU) {
                    stack[depth - 1] = left;
                } else {
                    stack[depth - 1] = (unsigned int)((int)left / (int)right);
                }
                break;
            default: return -1;
            }
        }
    }
    return -1;
}

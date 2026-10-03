#include "crypto.h"

struct sha256_context {
    unsigned int state[8];
    unsigned char block[64];
    unsigned int block_length;
    unsigned int total_length;
};

static const unsigned int sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static unsigned int rotate_right(unsigned int value, unsigned int count)
{
    return (value >> count) | (value << (32 - count));
}

void mn_copy(void *destination, const void *source, unsigned int length)
{
    unsigned char *to = destination;
    const unsigned char *from = source;

    while (length-- != 0) {
        *to++ = *from++;
    }
}

void mn_zero(void *destination, unsigned int length)
{
    volatile unsigned char *to = destination;

    while (length-- != 0) {
        *to++ = 0;
    }
}

/* Comparaison a temps constant (pas de sortie anticipee). */
unsigned char mn_equal(const void *left, const void *right, unsigned int length)
{
    const unsigned char *a = left;
    const unsigned char *b = right;
    unsigned char difference = 0;

    while (length-- != 0) {
        difference |= (unsigned char)(*a++ ^ *b++);
    }
    return difference == 0;
}

static void sha256_compress(struct sha256_context *context, const unsigned char *block)
{
    unsigned int w[64];
    unsigned int a, b, c, d, e, f, g, h;

    for (unsigned int i = 0; i < 16; i++) {
        w[i] = ((unsigned int)block[i * 4] << 24) | ((unsigned int)block[i * 4 + 1] << 16) |
               ((unsigned int)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }
    for (unsigned int i = 16; i < 64; i++) {
        unsigned int s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned int s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = context->state[0]; b = context->state[1]; c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5]; g = context->state[6]; h = context->state[7];
    for (unsigned int i = 0; i < 64; i++) {
        unsigned int s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        unsigned int choose = (e & f) ^ (~e & g);
        unsigned int t1 = h + s1 + choose + sha256_k[i] + w[i];
        unsigned int s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        unsigned int majority = (a & b) ^ (a & c) ^ (b & c);
        unsigned int t2 = s0 + majority;

        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    context->state[0] += a; context->state[1] += b; context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f; context->state[6] += g; context->state[7] += h;
    mn_zero(w, sizeof(w));
}

static void sha256_start(struct sha256_context *context)
{
    context->state[0] = 0x6a09e667; context->state[1] = 0xbb67ae85;
    context->state[2] = 0x3c6ef372; context->state[3] = 0xa54ff53a;
    context->state[4] = 0x510e527f; context->state[5] = 0x9b05688c;
    context->state[6] = 0x1f83d9ab; context->state[7] = 0x5be0cd19;
    context->block_length = 0;
    context->total_length = 0;
}

static void sha256_update(struct sha256_context *context, const unsigned char *data,
                          unsigned int length)
{
    context->total_length += length;
    while (length != 0) {
        context->block[context->block_length++] = *data++;
        length--;
        if (context->block_length == 64) {
            sha256_compress(context, context->block);
            context->block_length = 0;
        }
    }
}

static void sha256_finish(struct sha256_context *context, unsigned char out[32])
{
    unsigned int bits_high = context->total_length >> 29;
    unsigned int bits_low = context->total_length << 3;
    unsigned char padding = 0x80;
    unsigned char zero = 0;
    unsigned char tail[8];

    sha256_update(context, &padding, 1);
    while (context->block_length != 56) {
        sha256_update(context, &zero, 1);
    }
    for (unsigned int i = 0; i < 4; i++) {
        tail[i] = (unsigned char)(bits_high >> (24 - i * 8));
        tail[4 + i] = (unsigned char)(bits_low >> (24 - i * 8));
    }
    sha256_update(context, tail, 8);
    for (unsigned int i = 0; i < 8; i++) {
        out[i * 4] = (unsigned char)(context->state[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(context->state[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(context->state[i] >> 8);
        out[i * 4 + 3] = (unsigned char)context->state[i];
    }
    mn_zero(context, sizeof(*context));
}

void sha256(const unsigned char *data, unsigned int length, unsigned char out[32])
{
    struct sha256_context context;

    sha256_start(&context);
    sha256_update(&context, data, length);
    sha256_finish(&context, out);
}

static void hmac_finish(const struct sha256_context *inner_start,
                        const struct sha256_context *outer_start,
                        const unsigned char *message, unsigned int message_length,
                        const unsigned char *message2, unsigned int message2_length,
                        unsigned char out[32])
{
    struct sha256_context context;
    unsigned char inner[32];

    mn_copy(&context, inner_start, sizeof(context));
    sha256_update(&context, message, message_length);
    if (message2_length != 0) {
        sha256_update(&context, message2, message2_length);
    }
    sha256_finish(&context, inner);
    mn_copy(&context, outer_start, sizeof(context));
    sha256_update(&context, inner, 32);
    sha256_finish(&context, out);
    mn_zero(inner, sizeof(inner));
}

void pbkdf2_sha256(const unsigned char *password, unsigned int password_length,
                   const unsigned char *salt, unsigned int salt_length,
                   unsigned int iterations, unsigned char out[32])
{
    struct sha256_context inner_start;
    struct sha256_context outer_start;
    unsigned char key[64];
    unsigned char pad[64];
    unsigned char counter[4] = {0, 0, 0, 1};
    unsigned char u[32];
    unsigned char t[32];

    mn_zero(key, sizeof(key));
    if (password_length > 64) {
        sha256(password, password_length, key);
    } else {
        mn_copy(key, password, password_length);
    }
    for (unsigned int i = 0; i < 64; i++) {
        pad[i] = key[i] ^ 0x36;
    }
    sha256_start(&inner_start);
    sha256_update(&inner_start, pad, 64);
    for (unsigned int i = 0; i < 64; i++) {
        pad[i] = key[i] ^ 0x5c;
    }
    sha256_start(&outer_start);
    sha256_update(&outer_start, pad, 64);

    /* Premier bloc: HMAC(mot de passe, sel || compteur=1). */
    {
        struct sha256_context context;
        unsigned char inner[32];

        mn_copy(&context, &inner_start, sizeof(context));
        sha256_update(&context, salt, salt_length);
        sha256_update(&context, counter, 4);
        sha256_finish(&context, inner);
        mn_copy(&context, &outer_start, sizeof(context));
        sha256_update(&context, inner, 32);
        sha256_finish(&context, u);
        mn_zero(inner, sizeof(inner));
    }
    mn_copy(t, u, 32);
    for (unsigned int round = 1; round < iterations; round++) {
        hmac_finish(&inner_start, &outer_start, u, 32, 0, 0, u);
        for (unsigned int i = 0; i < 32; i++) {
            t[i] ^= u[i];
        }
    }
    mn_copy(out, t, 32);
    mn_zero(&inner_start, sizeof(inner_start));
    mn_zero(&outer_start, sizeof(outer_start));
    mn_zero(key, sizeof(key));
    mn_zero(pad, sizeof(pad));
    mn_zero(u, sizeof(u));
    mn_zero(t, sizeof(t));
}

/* Reserve d'entropie: RDRAND si present, sinon horodatages des touches. */
static unsigned char entropy_pool[32];
static unsigned int entropy_position;

void entropy_add(unsigned int value)
{
    entropy_pool[entropy_position % 32] ^= (unsigned char)value;
    entropy_pool[(entropy_position + 7) % 32] += (unsigned char)(value >> 8);
    entropy_pool[(entropy_position + 13) % 32] ^= (unsigned char)(value >> 16);
    entropy_pool[(entropy_position + 19) % 32] += (unsigned char)(value >> 24);
    entropy_position++;
    sha256(entropy_pool, sizeof(entropy_pool), entropy_pool);
}

void entropy_add_timestamp(void)
{
    unsigned int low;
    unsigned int high;

    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    entropy_add(low ^ (high << 7));
}

static unsigned char rdrand_available(void)
{
    unsigned int eax = 1;
    unsigned int ebx;
    unsigned int ecx = 0;
    unsigned int edx;

    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (ecx >> 30) & 1U;
}

static unsigned char rdrand_value(unsigned int *value)
{
    unsigned char ok;

    for (unsigned int attempt = 0; attempt < 10; attempt++) {
        __asm__ volatile ("rdrand %0; setc %1" : "=r"(*value), "=qm"(ok) : : "cc");
        if (ok) {
            return 1;
        }
    }
    return 0;
}

void random_bytes(unsigned char *out, unsigned int length)
{
    unsigned char block[64];
    unsigned char digest[32];
    unsigned int counter = 0;
    unsigned char hardware = rdrand_available();

    while (length != 0) {
        unsigned int amount = length < 32 ? length : 32;
        unsigned int value;

        entropy_add_timestamp();
        mn_copy(block, entropy_pool, 32);
        for (unsigned int i = 0; i < 4; i++) {
            value = 0;
            if (hardware) {
                rdrand_value(&value);
            }
            block[32 + i * 4] = (unsigned char)value;
            block[33 + i * 4] = (unsigned char)(value >> 8);
            block[34 + i * 4] = (unsigned char)(value >> 16);
            block[35 + i * 4] = (unsigned char)(value >> 24);
        }
        block[48] = (unsigned char)counter;
        block[49] = (unsigned char)(counter >> 8);
        counter++;
        sha256(block, 50, digest);
        mn_copy(out, digest, amount);
        out += amount;
        length -= amount;
    }
    mn_zero(block, sizeof(block));
    mn_zero(digest, sizeof(digest));
}

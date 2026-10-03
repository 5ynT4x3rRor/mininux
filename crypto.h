#ifndef MININUX_CRYPTO_H
#define MININUX_CRYPTO_H

void mn_copy(void *destination, const void *source, unsigned int length);
void mn_zero(void *destination, unsigned int length);
unsigned char mn_equal(const void *left, const void *right, unsigned int length);

void sha256(const unsigned char *data, unsigned int length, unsigned char out[32]);
void pbkdf2_sha256(const unsigned char *password, unsigned int password_length,
                   const unsigned char *salt, unsigned int salt_length,
                   unsigned int iterations, unsigned char out[32]);

void entropy_add(unsigned int value);
void entropy_add_timestamp(void);
void random_bytes(unsigned char *out, unsigned int length);

#endif

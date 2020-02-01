#ifndef LIBWALLY_MNEMONIC_H
#define LIBWALLY_MNEMONIC_H

#include <stddef.h>

#define BIP39_ENTROPY_LEN_128 16
#define BIP39_ENTROPY_LEN_160 20
#define BIP39_ENTROPY_LEN_192 24
#define BIP39_ENTROPY_LEN_224 28
#define BIP39_ENTROPY_LEN_256 32
#define BIP39_ENTROPY_LEN_288 36
#define BIP39_ENTROPY_LEN_320 40

struct words;

/**
 * Return a mnemonic representation of a block of bytes.
 *
 * @w: List of words.
 * @bytes: Bytes to convert to a mnemonic sentence.
 * @len: The length of @bytes in bytes.
 *
 * @bytes must be an even multiple of the number of bits in the wordlist used.
 */
char *mnemonic_from_bytes(
    const struct words *w,
    const unsigned char *bytes,
    size_t len);

/**
 * Convert a mnemonic representation into a block of bytes.
 *
 * @w: List of words.
 * @mnemonic: Mnemonic sentence to store.
 * @bytes_out: Where to store the converted representation.
 * @len: The length of @bytes_out in bytes.
 * @written: Destination for the number of bytes written.
 */
int mnemonic_to_bytes(
    const struct words *w,
    const char *mnemonic,
    unsigned char *bytes_out,
    size_t len,
    size_t *written);

#endif /* LIBWALLY_MNEMONIC_H */

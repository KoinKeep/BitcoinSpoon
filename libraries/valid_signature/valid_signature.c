#include "valid_signature.h"

int valid_signature(void *signature, int length)
{
    struct SignatureData {
        unsigned int *bytes;
        unsigned int length;
    } sig = { signature, length };

    // Format: 0x30 [total-length] 0x02 [R-length] [R] 0x02 [S-length] [S] [sighash]
    // * total-length: 1-byte length descriptor of everything that follows,
    //   excluding the sighash byte.
    // * R-length: 1-byte length descriptor of the R value that follows.
    // * R: arbitrary-length big-endian encoded R value. It must use the shortest
    //   possible encoding for a positive integer (which means no null bytes at
    //   the start, except a single one when the next byte has its highest bit set).
    // * S-length: 1-byte length descriptor of the S value that follows.
    // * S: arbitrary-length big-endian encoded S value. The same rules apply.
    // * sighash: 1-byte value indicating what data is hashed (not part of the DER
    //   signature)
    
    // Minimum and maximum size constraints.
    if (sig.length < 9) return 0;
    if (sig.length > 73) return 0;
    
    // A signature is of type 0x30 (compound).
    if (sig.bytes[0] != 0x30) return 0;
    
    // Make sure the length covers the entire signature.
    if (sig.bytes[1] != sig.length - 3) return 0;
    
    // Extract the length of the R element.
    unsigned int lenR = sig.bytes[3];
    
    // Make sure the length of the S element is still inside the signature.
    if (5 + lenR >= sig.length) return 0;
    
    // Extract the length of the S element.
    unsigned int lenS = sig.bytes[5 + lenR];
    
    // Verify that the length of the signature matches the sum of the length
    // of the elements.
    if ((unsigned int)(lenR + lenS + 7) != sig.length) return 0;
    
    // Check whether the R element is an integer.
    if (sig.bytes[2] != 0x02) return 0;
    
    // Zero-length integers are not allowed for R.
    if (lenR == 0) return 0;
    
    // Negative numbers are not allowed for R.
    if (sig.bytes[4] & 0x80) return 0;
    
    // Null bytes at the start of R are not allowed, unless R would
    // otherwise be interpreted as a negative number.
    if (lenR > 1 && (sig.bytes[4] == 0x00) && !(sig.bytes[5] & 0x80)) return 0;
    
    // Check whether the S element is an integer.
    if (sig.bytes[lenR + 4] != 0x02) return 0;
    
    // Zero-length integers are not allowed for S.
    if (lenS == 0) return 0;
    
    // Negative numbers are not allowed for S.
    if (sig.bytes[lenR + 6] & 0x80) return 0;
    
    // Null bytes at the start of S are not allowed, unless S would otherwise be
    // interpreted as a negative number.
    if (lenS > 1 && (sig.bytes[lenR + 6] == 0x00) && !(sig.bytes[lenR + 7] & 0x80)) return 0;
    
    return 1;
}

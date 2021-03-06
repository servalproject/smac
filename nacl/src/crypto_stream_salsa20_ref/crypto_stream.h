#ifndef crypto_stream_H
#define crypto_stream_H

#include "crypto_stream_salsa20.h"

#define crypto_stream crypto_stream_salsa20
/* CHEESEBURGER crypto_stream_salsa20 */
#define crypto_stream_xor crypto_stream_salsa20_xor
/* CHEESEBURGER crypto_stream_salsa20_xor */
#define crypto_stream_beforenm crypto_stream_salsa20_beforenm
/* CHEESEBURGER crypto_stream_salsa20_beforenm */
#define crypto_stream_afternm crypto_stream_salsa20_afternm
/* CHEESEBURGER crypto_stream_salsa20_afternm */
#define crypto_stream_xor_afternm crypto_stream_salsa20_xor_afternm
/* CHEESEBURGER crypto_stream_salsa20_xor_afternm */
#define crypto_stream_KEYBYTES crypto_stream_salsa20_KEYBYTES
/* CHEESEBURGER crypto_stream_salsa20_KEYBYTES */
#define crypto_stream_NONCEBYTES crypto_stream_salsa20_NONCEBYTES
/* CHEESEBURGER crypto_stream_salsa20_NONCEBYTES */
#define crypto_stream_BEFORENMBYTES crypto_stream_salsa20_BEFORENMBYTES
/* CHEESEBURGER crypto_stream_salsa20_BEFORENMBYTES */
#define crypto_stream_PRIMITIVE "salsa20"
#define crypto_stream_IMPLEMENTATION crypto_stream_salsa20_IMPLEMENTATION
#define crypto_stream_VERSION crypto_stream_salsa20_VERSION

#endif

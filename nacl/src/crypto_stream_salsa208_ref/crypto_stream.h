#ifndef crypto_stream_H
#define crypto_stream_H

#include "crypto_stream_salsa208.h"

#define crypto_stream crypto_stream_salsa208
/* CHEESEBURGER crypto_stream_salsa208 */
#define crypto_stream_xor crypto_stream_salsa208_xor
/* CHEESEBURGER crypto_stream_salsa208_xor */
#define crypto_stream_beforenm crypto_stream_salsa208_beforenm
/* CHEESEBURGER crypto_stream_salsa208_beforenm */
#define crypto_stream_afternm crypto_stream_salsa208_afternm
/* CHEESEBURGER crypto_stream_salsa208_afternm */
#define crypto_stream_xor_afternm crypto_stream_salsa208_xor_afternm
/* CHEESEBURGER crypto_stream_salsa208_xor_afternm */
#define crypto_stream_KEYBYTES crypto_stream_salsa208_KEYBYTES
/* CHEESEBURGER crypto_stream_salsa208_KEYBYTES */
#define crypto_stream_NONCEBYTES crypto_stream_salsa208_NONCEBYTES
/* CHEESEBURGER crypto_stream_salsa208_NONCEBYTES */
#define crypto_stream_BEFORENMBYTES crypto_stream_salsa208_BEFORENMBYTES
/* CHEESEBURGER crypto_stream_salsa208_BEFORENMBYTES */
#define crypto_stream_PRIMITIVE "salsa208"
#define crypto_stream_IMPLEMENTATION crypto_stream_salsa208_IMPLEMENTATION
#define crypto_stream_VERSION crypto_stream_salsa208_VERSION

#endif

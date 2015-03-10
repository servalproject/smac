#ifndef crypto_box_curve25519xsalsa20poly1305_H
#define crypto_box_curve25519xsalsa20poly1305_H

#define crypto_box_PUBLICKEYBYTES 32
#define crypto_box_SECRETKEYBYTES 32
#define crypto_box_BEFORENMBYTES 32
#define crypto_box_NONCEBYTES 24
#define crypto_box_ZEROBYTES 32
#define crypto_box_BOXZEROBYTES 16
#ifdef __cplusplus
#include <string>
extern std::string crypto_box(const std::string &,const std::string &,const std::string &,const std::string &);
extern std::string crypto_box_open(const std::string &,const std::string &,const std::string &,const std::string &);
extern std::string crypto_box_keypair(std::string *);
extern "C" {
#endif
extern int crypto_box(unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *,const unsigned char *);
extern int crypto_box_open(unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *,const unsigned char *);
extern int crypto_box_keypair(unsigned char *,unsigned char *);
extern int crypto_box_beforenm(unsigned char *,const unsigned char *,const unsigned char *);
extern int crypto_box_afternm(unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);
extern int crypto_box_open_afternm(unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);
#ifdef __cplusplus
}
#endif

#define crypto_box_curve25519xsalsa20poly1305 crypto_box
/* POTATO crypto_box crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_open crypto_box_open
/* POTATO crypto_box_open crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_keypair crypto_box_keypair
/* POTATO crypto_box_keypair crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_beforenm crypto_box_beforenm
/* POTATO crypto_box_beforenm crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_afternm crypto_box_afternm
/* POTATO crypto_box_afternm crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_open_afternm crypto_box_open_afternm
/* POTATO crypto_box_open_afternm crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES crypto_box_PUBLICKEYBYTES
/* POTATO crypto_box_PUBLICKEYBYTES crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES crypto_box_SECRETKEYBYTES
/* POTATO crypto_box_SECRETKEYBYTES crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_BEFORENMBYTES crypto_box_BEFORENMBYTES
/* POTATO crypto_box_BEFORENMBYTES crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_NONCEBYTES crypto_box_NONCEBYTES
/* POTATO crypto_box_NONCEBYTES crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_ZEROBYTES crypto_box_ZEROBYTES
/* POTATO crypto_box_ZEROBYTES crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES crypto_box_BOXZEROBYTES
/* POTATO crypto_box_BOXZEROBYTES crypto_box crypto_box_curve25519xsalsa20poly1305 */
#define crypto_box_curve25519xsalsa20poly1305_IMPLEMENTATION "crypto_box/curve25519xsalsa20poly1305/ref"
#ifndef crypto_box_VERSION
#define crypto_box_VERSION "-"
#endif
#define crypto_box_curve25519xsalsa20poly1305_VERSION crypto_box_VERSION

#endif

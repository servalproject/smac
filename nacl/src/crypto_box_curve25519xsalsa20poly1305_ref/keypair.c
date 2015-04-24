#include "crypto_scalarmult_curve25519.h"
#include "crypto_box.h"
#include "randombytes.h"

int crypto_box_keypair(
  unsigned char *pk,
  unsigned char *sk
)
{
  randombytes(sk,32);
  return crypto_scalarmult_curve25519_base(pk,sk);
}

int crypto_box_public_from_private(
				   unsigned char *pk,
				   unsigned char *sk)
{
  return crypto_scalarmult_curve25519_base(pk,sk);
}

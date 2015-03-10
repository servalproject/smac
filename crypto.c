#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "md5.h"
#include "crypto_box.h"

/* Decrypt a message */
int decryptMessage(unsigned char *secret_key,unsigned char *nonce_in,int nonce_len,
		   unsigned char *in, int in_len,

		   unsigned char *out,int *out_len)
{
  // Prepare nonce
  unsigned char nonce[crypto_box_NONCEBYTES];  
  int o=0;
  for(int i=0;i<crypto_box_NONCEBYTES;i++) {
    if (o>=nonce_len) o=0;
    nonce[i]=nonce[o++];
  }

  // compute public key from secret key
  // unsigned char pk[crypto_box_PUBLICKEYBYTES];
  // crypto_scalarmult_curve25519_base(pk,secret_key);
  
  if (crypto_box_open(out,in,in_len-crypto_box_PUBLICKEYBYTES,
		      nonce,&in[in_len-crypto_box_PUBLICKEYBYTES],secret_key))
    return -1;

  return 0;
}

/* Encrypt a message */
int encryptMessage(unsigned char *public_key,unsigned char *in, int in_len,
		   unsigned char *out,int *out_len, unsigned char *nonce, int nonce_len)
{
  // Generate temporary keypair for use when encrypting this message.
  // This key is then discarded after so that only the recipient can decrypt it once
  // it has been encrypted

  unsigned char pk[crypto_box_PUBLICKEYBYTES];
  unsigned char sk[crypto_box_SECRETKEYBYTES];
  crypto_box_keypair(pk,sk);

  /* Output message will consist of encrypted version of original preceded by 
     crypto_box_ZEROBYTES which will hold the authentication information.

     We need to supply a nonce for the encryption.  To save space, we will use a
     short nonce repeated several times, which will be prefixed to each fragment
     of the encoded message in base-64.  Thus we need to return the nonce to the
     caller.
  */

  // Get short nonce, and repeat to fill the full nonce length
  randombytes(nonce,nonce_len);
  int o=0;
  for(int i=nonce_len;i<crypto_box_NONCEBYTES;i++) {
    if (o>=nonce_len) o=0;
    nonce[i]=nonce[o++];
  }

  // Prepare message with space for authentication code and public key
  unsigned long long template_len
    = in_len + crypto_box_ZEROBYTES
    + crypto_box_PUBLICKEYBYTES;
  unsigned char template[template_len];
  bzero(&template[0],crypto_box_ZEROBYTES);
  bcopy(in,&template[crypto_box_ZEROBYTES],in_len);
  bzero(out,template_len);
  bcopy(pk,&template[crypto_box_ZEROBYTES+in_len],crypto_box_PUBLICKEYBYTES);
  
  if (crypto_box(out,template,template_len,nonce,pk,sk)) return -1;
  
  (*out_len)=in_len+crypto_box_ZEROBYTES+crypto_box_PUBLICKEYBYTES;

  return 0;

}

char *private_key_from_passphrase_buffer[crypto_box_SECRETKEYBYTES];
unsigned char *private_key_from_passphrase(char *passphrase)
{
  MD5_CTX md5;
  MD5_Init(&md5);
  MD5_Update(&md5,"spleen",6);
  MD5_Update(&md5,passphrase,strlen(passphrase));
  MD5_Update(&md5,"rock melon",10);
  MD5_Final(&private_key_from_passphrase_buffer[0],&md5);
  MD5_Init(&md5);
  MD5_Update(&md5,"dropbear",8);
  MD5_Update(&md5,passphrase,strlen(passphrase));
  MD5_Update(&md5,"silvester",9);
  MD5_Final(&private_key_from_passphrase_buffer[16],&md5);
  return private_key_from_passphrase_buffer;
}

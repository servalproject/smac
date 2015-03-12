#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include "md5.h"
#include "crypto_box.h"

int crypto_scalarmult_curve25519_ref_base(unsigned char *q,const unsigned char *n);

void randombytes(unsigned char *buf,int len)
{
  static int urandomfd = -1;
  int tries = 0;
  if (urandomfd == -1) {
    for (tries = 0; tries < 4; ++tries) {
      urandomfd = open("/dev/urandom",O_RDONLY);
      if (urandomfd != -1) break;
      sleep(1);
    }
    if (urandomfd == -1) {
      exit(-1);
    }
  }
  tries = 0;
  while (len > 0) {
    ssize_t i = read(urandomfd, buf, (len < 1048576) ? len : 1048576);
    if (i == -1) {
      if (++tries > 4) {
	exit(-1);
      }
    } else {
      tries = 0;
      buf += i;
      len -= i;
    }
  }
  return;
}

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
  
  if (crypto_box_open(out,in,in_len-crypto_box_PUBLICKEYBYTES,
		      nonce,&in[in_len-crypto_box_PUBLICKEYBYTES],secret_key))
    return -1;

  return 0;
}

int dump_bytes(char *m,unsigned char *b,int n)
{
  printf("%s: ",m);
  for(int i=0;i<n;i++) {
    if (!(i&7)) printf(" ");
    printf("%02x",b[i]);
  }printf("\n");
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
    = in_len + crypto_box_ZEROBYTES;
  unsigned char template[template_len];
  bzero(&template[0],crypto_box_ZEROBYTES);
  bcopy(in,&template[crypto_box_ZEROBYTES],in_len);
  bzero(out,template_len);
  
  if (crypto_box(out,template,template_len,nonce,pk,sk)) {
    fprintf(stderr,"crypto_box failed\n");
    exit(-1);
  }

  // This leaves crypto_box_ZEROBYTES of zeroes at the start of the message.
  // This is a waste.  We will stuff half of our public key in there, and then the
  // other half at the end.
  bcopy(&pk[0],&out[0],crypto_box_BOXZEROBYTES);
  bcopy(&pk[crypto_box_BOXZEROBYTES],&out[crypto_box_ZEROBYTES+in_len],
	crypto_box_PUBLICKEYBYTES-crypto_box_BOXZEROBYTES);
  (*out_len)=in_len+crypto_box_PUBLICKEYBYTES
    +(crypto_box_ZEROBYTES-crypto_box_BOXZEROBYTES);
  
  return 0;
}

unsigned char private_key_from_passphrase_buffer[crypto_box_SECRETKEYBYTES];
unsigned char *private_key_from_passphrase(char *passphrase)
{
  MD5_CTX md5;
  MD5_Init(&md5);
  MD5_Update(&md5,(unsigned char *)"spleen",6);
  MD5_Update(&md5,(unsigned char *)passphrase,strlen(passphrase));
  MD5_Update(&md5,(unsigned char *)"rock melon",10);
  MD5_Final(&private_key_from_passphrase_buffer[0],&md5);
  MD5_Init(&md5);
  MD5_Update(&md5,(unsigned char *)"dropbear",8);
  MD5_Update(&md5,(unsigned char *)passphrase,strlen(passphrase));
  MD5_Update(&md5,(unsigned char *)"silvester",9);
  MD5_Final(&private_key_from_passphrase_buffer[16],&md5);
  
  return private_key_from_passphrase_buffer;
}

int num_to_char(int n)
{
  assert(n>=0); assert(n<64);
  if (n<10) return '0'+n;
  if (n<36) return 'a'+(n-10);
  if (n<62) return 'A'+(n-36);
  switch(n) {
  case 62: return '.'; 
  case 63: return '+';
  default: return -1;
  }
}

int char_to_num(int c)
{
  if ((c>='0')&&(c<='9')) return c-'0';
  if ((c>='a')&&(c<='z')) return c-'a'+10;
  if ((c>='A')&&(c<='Z')) return c-'A'+36;
  if (c=='.') return 62;
  if (c=='+') return 63;
  return -1;
}

int base64_append(char *out,int *out_offset,unsigned char *bytes,int count)
{
  int i;
  for(i=0;i<count;i+=3) {
    int n=4;
    unsigned int b[30];
    b[0]=bytes[i];
    if ((i+2)>=count) { b[2]=0; n=3; } else b[2]=bytes[i+2];
    if ((i+1)>=count) { b[1]=0; n=2; } else b[1]=bytes[i+1];
    out[(*out_offset)++] = num_to_char(b[0]&0x3f);
    out[(*out_offset)++] = num_to_char( ((b[0]&0xc0)>>6) | ((b[1]&0x0f)<<2) );
    if (n==2) return 0;
    out[(*out_offset)++] = num_to_char( ((b[1]&0xf0)>>4) | ((b[2]&0x03)<<4) );
    if (n==3) return 0;
    out[(*out_offset)++] = num_to_char((b[2]&0xfc)>>2);
  }
  return 0;
}

int encryptAndFragment(char *filename,int mtu,char *outputdir, char *publickeyhex)
{
  /* Read a file, encrypt it, break it into fragments and write them into the output
     directory. */

  unsigned char in_buffer[16384];
  FILE *f=fopen(filename,"r");
  assert(f);
  int r=fread(in_buffer,1,16384,f);
  assert(r>0);
  if (r>=16384) {
    fprintf(stderr,"File must be <16KB\n");
  }
  fclose(f);
  printf("Read %d bytes\n",r);
  in_buffer[r]=0;

  unsigned char nonce[crypto_box_NONCEBYTES];
  int nonce_len=6;
  randombytes(nonce,6);

  unsigned char pk[crypto_box_PUBLICKEYBYTES];
  char hex[3];
  hex[2]=0;
  for(int i=0;i<crypto_box_PUBLICKEYBYTES;i++)
    {
      hex[0]=publickeyhex[i*2+0];
      hex[1]=publickeyhex[i*2+1];
      pk[i]=strtoll(hex,NULL,16);
    }

  unsigned char *out_buffer=alloca(32768);
  int out_len=0;
  
  encryptMessage(pk,in_buffer,r,
		 out_buffer,&out_len,nonce,nonce_len);
  
  /* Work out how many bytes per fragment:

     Assumes that: 
     1. body gets base64 encoded.
     2. Nonce is 6 bytes (48 bits), and so takes 8 characters to encode.
     3. Fragment number is expressed using two leading characters: 0-9a-zA-Z = 
        current fragment number, followed by 2nd character which indicates the max
	fragment number.  Thus we can have 62 fragments.
  */
  int overhead=2+(48/6);
  int bytes_per_fragment=(mtu-overhead)*6/8;
  assert(bytes_per_fragment>0);
  int frag_count=out_len/bytes_per_fragment;
  if (out_len%bytes_per_fragment) frag_count++;
  assert(frag_count<=62);

  char prefix[16];
  int frag_number=0;
  for(int i=0;i<out_len;i+=bytes_per_fragment)
    {
      int bytes=bytes_per_fragment;
      if (bytes>(out_len-i)) bytes=out_len-i;

      char fragment[mtu+1];
      int offset=0;

      fragment[offset++]=num_to_char(frag_number);
      fragment[offset++]=num_to_char(frag_count-1);
      base64_append(fragment,&offset,nonce,6);
      base64_append(fragment,&offset,&out_buffer[i],bytes);

      fragment[offset]=0;

      char filename[1024];
      for(int i=0;i<10;i++) prefix[i]=fragment[i]; prefix[10]=0;
      snprintf(filename,1024,"%s/%s",outputdir,prefix);
      FILE *f=fopen(filename,"w");
      if (f) {
	fprintf(f,"%s",fragment);
	fclose(f);
      }
      
      frag_number++;
    }
  printf("Wrote %d fragments.  Message prefix is '%s'\n",
	 frag_number,frag_number?&prefix[2]:"N/A");
  
  return -1;
}

int defragmentAndDecrypt(char *inputdir,char *outputdir,char *privatekeypassphrase)
{
  unsigned char *sk = private_key_from_passphrase(privatekeypassphrase);
  unsigned char pk[crypto_box_PUBLICKEYBYTES];
  crypto_scalarmult_curve25519_ref_base(pk,sk);
  fprintf(stderr,"Public key for passphrase: ");
  for(int i=0;i<crypto_box_PUBLICKEYBYTES;i++) fprintf(stderr,"%02x",pk[i]);
  fprintf(stderr,"\n"); 
  
  return -1;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <alloca.h>
#include <dirent.h>
#include "md5.h"
#include "crypto_box.h"
#include "randombytes.h"

#ifdef ANDROID
#include <android/log.h>
 
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "libsmac", __VA_ARGS__))
#else
#define LOGI(...)
#endif
#define CHECKPOINT() LOGI("checkpoint: %s:%d",__FILE__,__LINE__);

#define MAX_FRAGMENTS 64
struct fragment_set {
  int frag_count;
  char *prefix;
  char *pieces[MAX_FRAGMENTS];
};

int crypto_scalarmult_curve25519_ref_base(unsigned char *q,const unsigned char *n);

void randombytes(unsigned char *buf,unsigned long long len)
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
		   unsigned char *out,int *out_len, unsigned char *nonce,
		   int nonce_len, int debug)
{
  // Generate temporary keypair for use when encrypting this message.
  // This key is then discarded after so that only the recipient can decrypt it once
  // it has been encrypted

  unsigned char pk[crypto_box_PUBLICKEYBYTES];
  unsigned char sk[crypto_box_SECRETKEYBYTES];
  if (!debug)
    crypto_box_keypair(pk,sk);
  else {
    // In debug mode, use a secret key of all zeroes
    bzero(sk,crypto_box_SECRETKEYBYTES);
    crypto_box_public_from_private(pk, sk);
  }
  

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
  
  if (crypto_box(out,template,template_len,nonce,public_key,sk)) {
    fprintf(stderr,"crypto_box failed\n");
    exit(-1);
  }

  {
    unsigned char temp[32768];
    int clen=in_len+crypto_box_ZEROBYTES;
    int result = crypto_box_open(temp,out,clen,
				 nonce,pk,sk);
    printf("open result = %d, clen=%d\n",result,clen);    
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
  if (passphrase[0]=='@') {
    // Pass phrase comes from a file
    FILE *f=fopen(&passphrase[1],"r");
    if (!f) return NULL;
    passphrase=alloca(1024);
    int r=fread(passphrase,1,1020,f);
    if (r<1) return NULL;
    passphrase[r]=0;
    while (passphrase[0]&&(passphrase[strlen(passphrase)-1]<' '))
      passphrase[strlen(passphrase)-1]=0;
    fclose(f);
  }
  
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
  case 62: return '='; 
  case 63: return '+';
  default: return -1;
  }
}

int char_to_num(int c)
{
  if ((c>='0')&&(c<='9')) return c-'0';
  if ((c>='a')&&(c<='z')) return c-'a'+10;
  if ((c>='A')&&(c<='Z')) return c-'A'+36;
  if (c=='=') return 62;
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
  
int encryptAndFragmentBuffer(unsigned char *in_buffer,int in_len,
			     char *fragments[MAX_FRAGMENTS],int *fragment_count,
			     int mtu,char *publickeyhex,int debug)
{
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
  
  encryptMessage(pk,in_buffer,in_len,
		 out_buffer,&out_len,nonce,nonce_len, debug);

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

  int frag_number=0;
  for(int i=0;i<out_len;i+=bytes_per_fragment)
    {
      if ((*fragment_count)>=MAX_FRAGMENTS) return -1;
      
      int bytes=bytes_per_fragment;
      if (bytes>(out_len-i)) bytes=out_len-i;

      char fragment[mtu+1];
      int offset=0;

      fragment[offset++]=num_to_char(*fragment_count);
      fragment[offset++]=num_to_char(frag_count-1);
      base64_append(fragment,&offset,nonce,6);
      base64_append(fragment,&offset,&out_buffer[i],bytes);

      fragment[offset]=0;
      
      fragments[(*fragment_count)++]=strdup(fragment);
    }
  
  return 0;
}

int encryptAndFragment(char *filename,int mtu,char *outputdir,char *publickeyhex,
		       int debug)
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
  in_buffer[r]=0;

  char *fragments[MAX_FRAGMENTS];
  int fragment_count=0;
  
  encryptAndFragmentBuffer(in_buffer,r,fragments,&fragment_count,
			   mtu,publickeyhex, debug);

  if (fragment_count<1) return 0;
  
  // Now write fragments out to files
  for(int j=0;j<fragment_count;j++) {
    char filename[1024], prefix[11];
    for(int i=0;i<10;i++) prefix[i]=fragments[j][i]; prefix[10]=0;
    snprintf(filename,1024,"%s/%s",outputdir,prefix);
    FILE *f=fopen(filename,"w");
    if (f) {
      fprintf(f,"%s",fragments[j]);
      fclose(f);
    }
  }
  return fragment_count;
}

int base64_extract(char *in,unsigned char *out,int *out_len)
{
  int v[4];
  
  for(int i=0;i<strlen(in);i+=4) {
    int c=0;
    v[0]=char_to_num(in[i]);
    if (!in[i+1]) {
      // invalid
    } else if (!in[i+2]) {
      // one byte
      v[1]=char_to_num(in[i+1]); c=1;
    } else if (!in[i+3]) {
      // two byte
      v[1]=char_to_num(in[i+1]);
      v[2]=char_to_num(in[i+2]); c=2;
    } else {
      // three bytes
      v[1]=char_to_num(in[i+1]);
      v[2]=char_to_num(in[i+2]);
      v[3]=char_to_num(in[i+3]); c=3;
    }
    if (c>0) out[(*out_len)++]=v[0]|((v[1]&3)<<6);
    if (c>1) out[(*out_len)++]=((v[1]&0x3c)>>2)|((v[2]&0xf)<<4);
    if (c>2) out[(*out_len)++]=((v[2]&0x30)>>4)|(v[3]<<2);
  }
  return 0;
}

int reassembleAndDecrypt(struct fragment_set *f,char *outputdir,
			 unsigned char *sk, unsigned char *pk)
{
  unsigned char buffer[32768];
  bzero(buffer,32768);
  int offset=0;
  
  printf("Reassembling %s\n",f->prefix);
  for(int i=0;i<=f->frag_count;i++) {
    base64_extract(&f->pieces[i][10],buffer,&offset);
    printf("  fragment '%s'\n",f->pieces[i]);
  }

  unsigned char nonce[crypto_box_NONCEBYTES];
  int nonce_len=0;
  base64_extract(f->prefix,nonce,&nonce_len);
  int o=0;
  for(int i=nonce_len;i<crypto_box_NONCEBYTES;i++) {
    if (o>=nonce_len) o=0;
    nonce[i]=nonce[o++];
  }

  printf("Preparing to decrypt %d bytes.\n",offset);
  if (offset<=(crypto_box_ZEROBYTES-crypto_box_BOXZEROBYTES))
    {
      printf("Message too short -- ignoring.\n");
      return -1;
    }
  
  unsigned char msg_pk[crypto_box_PUBLICKEYBYTES];

  /*
    Encrypted message consists of:
    1. crypto_box_BOXZEROBYTES of message public key.
    2. the message.
    3. crypto_box_ZEROBYTES-crypto_box_BOXZEROBYTES of message public key.

    It needs to end up as:

    1. crypto_box_BOXZEROBYTES of zeroes.
    2. the message

    So we copy the bytes of the public key out, then zero the first 
    crypto_box_BOXZEROBYTES of the message, and trim its length by
    crypto_box_ZEROBYTES-crypto_box_BOXZEROBYTES.
  */
  bcopy(&buffer[0],&msg_pk[0],crypto_box_BOXZEROBYTES);
  bzero(&buffer[0],crypto_box_BOXZEROBYTES);
  offset-=crypto_box_ZEROBYTES-crypto_box_BOXZEROBYTES;
  bcopy(&buffer[offset],
	&msg_pk[crypto_box_BOXZEROBYTES],crypto_box_ZEROBYTES-crypto_box_BOXZEROBYTES);
  
  unsigned char enclaire[32768];
  bzero(enclaire,32768);  
  int result = crypto_box_open(enclaire,buffer,
			       offset,
			       nonce,msg_pk,sk);
  if (result) {
    printf("decryption of %s failed (crypto box returned = %d)\n",f->prefix,result);
    return -1;
  }
  printf("Decrypted %s\n",f->prefix);

  char filename[1024];
  snprintf(filename,1024,"%s/%s.out",outputdir,f->prefix);
  FILE *of=fopen(filename,"w");
  if (!of) {
    printf("failed to open %s for writing\n",filename);
    return -1;
  }
  enclaire[offset]=0;
  int n=fwrite(&enclaire[crypto_box_ZEROBYTES],offset-crypto_box_ZEROBYTES,1,of);
  fclose(of);
  if (n!=1) {
    printf("failed to write data into %s\n",filename);
  }
  
  return 0;
}


#define MAX_FRAGSETS 65536
struct fragment_set *fragments[MAX_FRAGSETS];
int fragset_count=0;

int defragmentAndDecrypt(char *inputdir,char *outputdir,char *privatekeypassphrase)
{
  unsigned char *sk = private_key_from_passphrase(privatekeypassphrase);
  if (!sk) { fprintf(stderr,"Failed to read passphrase\n"); exit(-1); }
  unsigned char pk[crypto_box_PUBLICKEYBYTES];
  crypto_scalarmult_curve25519_ref_base(pk,sk);  
  fprintf(stderr,"Public key for passphrase: ");
  for(int i=0;i<crypto_box_PUBLICKEYBYTES;i++) fprintf(stderr,"%02x",pk[i]);
  fprintf(stderr,"\n"); 

  // Iterate through the input directory, building lists of message fragments, and
  // then reassembling and decrypting them when we find the whole set.  It's really
  // a bit like collecting Paddle-Pop(tm) Lick-a-prize(tm) sticks.
  DIR *d=opendir(inputdir);
  if (!d) return -1;
  struct dirent *de=NULL;
  while ((de=readdir(d))!=NULL) {
    char this_prefix[16];
    if (strlen(de->d_name)>=10) {
      for(int i=0;i<8;i++) this_prefix[i]=de->d_name[2+i];
      int frag_num=char_to_num(de->d_name[0]);
      int frag_count=char_to_num(de->d_name[1]);

      char message[32768];
      char filename[1024];
      snprintf(filename,1024,"%s/%s",inputdir,de->d_name);
      FILE *f=fopen(filename,"r");
      if (!f) continue;
      int r=fread(message,1,32768,f);
      fclose(f);
      if (r<0) r=0; if (r>32767) r=32767;
      message[r]=0;
      
      printf("Found fragment #%d/%d of %s\n",frag_num,frag_count,this_prefix);
      int i;
      for(i=0;i<fragset_count;i++) {
	if (!strcmp(fragments[i]->prefix,this_prefix)) break;
      }
      if (i==fragset_count) {
	// Need a new fragset.

	// No space, so ignore
	if (fragset_count==MAX_FRAGSETS) continue;

	fragments[i]=calloc(sizeof(struct fragment_set),1);
	assert(fragments[i]);

	fragments[i]->prefix=strdup(this_prefix);

	fragset_count++;
      }

      fragments[i]->frag_count=frag_count;
      if (fragments[i]->pieces[frag_num]) free(fragments[i]->pieces[frag_num]);
      fragments[i]->pieces[frag_num]=strdup(message);
      assert(fragments[i]->pieces[frag_num]);

      int j;
      for(j=0;j<=frag_count;j++) if (!fragments[i]->pieces[j]) break;
      if (j==frag_count+1) {
	// Have whole message -- reassemble, decrypt and delete
	reassembleAndDecrypt(fragments[i],outputdir,sk,pk);

	// free fragment and adjust list
	for(int j=0;j<fragments[i]->frag_count;j++) {
	  if (fragments[i]->pieces[j]) free(fragments[i]->pieces[j]);
	}
	free(fragments[i]);
	fragments[i]=fragments[fragset_count-1];
	fragments[fragset_count-1]=NULL;
	fragset_count--;
      }
	
    }
    
  }

  closedir(d);
  
  return -1;
}

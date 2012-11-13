/*
Copyright (C) 2012 Paul Gardner-Stephen
 
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
 
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
 
You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>

#include "arithmetic.h"
#include "method_stats3.h"

int processFile(FILE *f);

int lines=0;
double worstPercent=0,bestPercent=100;
long long total_compressed_bits=0;
long long total_uncompressed_bits=0;
long long total_alpha_bits=0;
long long total_nonalpha_bits=0;
long long total_case_bits=0;
long long total_model_bits=0;
long long total_length_bits=0;
long long total_finalisation_bits=0;

int main(int argc,char *argv[])
{
  if (!argv[1]) {
    fprintf(stderr,"Must provide message to compress.\n");
    exit(-1);
  }
  
  FILE *f;

  int argn=1;

  for(argn=1;argn<argc;argn++) {
    if (strcmp(argv[1],"-")) f=fopen(argv[argn],"r"); else f=stdin;
    if (!f) {
      fprintf(stderr,"Failed to open `%s' for input.\n",argv[1]);
      exit(-1);
    } else {
      processFile(f);
      fclose(f);
    }
  }

  printf("Summary:\n");
  printf("         compressed size: %f%%\n",
	 total_compressed_bits*100.0/total_uncompressed_bits);
  printf("       uncompressed bits: %lld\n",total_uncompressed_bits);
  printf("         compressed bits: %lld\n",total_compressed_bits);
  printf("    length-encoding bits: %lld\n",total_length_bits);
  printf("     model-encoding bits: %lld\n",total_model_bits);
  printf("      case-encoding bits: %lld\n",total_case_bits);
  printf("     alpha-encoding bits: %lld\n",total_alpha_bits);
  printf("  nonalpha-encoding bits: %lld\n",total_nonalpha_bits);

  return 0;
}

int processFile(FILE *f)
{
  char m[1024]; // raw message, no pre-processing

  m[0]=0; fgets(m,1024,f);
  
  while(m[0]) {    
    /* chop newline */
    m[strlen(m)-1]=0;

    range_coder *c=range_new_coder(1024);
    stats3_compress(c,(unsigned char *)m);

    total_compressed_bits+=c->bits_used;
    total_uncompressed_bits+=strlen(m)*8;

    double percent=c->bits_used*100.0/(strlen(m)*8);
    if (percent<bestPercent) bestPercent=percent;
    if (percent>worstPercent) worstPercent=percent;

    /* Verify that compression worked */
    {
      int lenout=0;
      char mout[1025];
      range_coder *d=range_coder_dup(c);
      d->bit_stream_length=d->bits_used;
      d->bits_used=0;
      d->low=0; d->high=0xffffffff;
      
      range_decode_prefetch(d);

      stats3_decompress(d,mout,&lenout);

      if (lenout!=strlen(m)) {	
	printf("Verify error: length mismatch: decoded = %d, original = %d\n",lenout,(int)strlen(m));
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
      	exit(-1);
      } else if (strcasecmp(m,mout)) {
	printf("Verify error: even ignoring case, the messages do not match.\n");
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
	int i;
	for(i=0;i<strlen(m);i++)
	  printf("<%02x>",m[i]);
	printf("\n");
	exit(-1);
      } else if (strcmp(m,mout)) {
	printf("Verify error: messages differ in case only.\n");
	printf("   Input: [%s]\n  Output: [%s]\n",m,mout);
	exit(-1);
      }

      range_coder_free(d);
    }

    range_coder_free(c);

    lines++;
    m[0]=0; fgets(m,1024,f);
  }
  return 0;
}

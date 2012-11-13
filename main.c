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

int main(int argc,char *argv[])
{
  if (!argv[1]) {
    fprintf(stderr,"Must provide message to compress.\n");
    exit(-1);
  }

  char m[1024]; // raw message, no pre-processing
  
  FILE *f;

  if (strcmp(argv[1],"-")) f=fopen(argv[1],"r"); else f=stdin;
  if (!f) {
    fprintf(stderr,"Failed to open `%s' for input.\n",argv[1]);
    exit(-1);
  }

  m[0]=0; fgets(m,1024,f);
  
  int lines=0;
  double runningPercent=0;
  double worstPercent=0,bestPercent=100;

  while(m[0]) {    
    /* chop newline */
    m[strlen(m)-1]=0;
    if (1) printf(">>> %s\n",m);

    range_coder *c=range_new_coder(1024);
    stats3_compress(c,(unsigned char *)m);

    double percent=c->bits_used*100.0/(strlen(m)*8);
    if (percent<bestPercent) bestPercent=percent;
    if (percent>worstPercent) worstPercent=percent;
    runningPercent+=percent;

    printf("Total encoded length = %d bits = %.2f%% (best:avg:worst %.2f%%:%.2f%%:%.2f%%)\n",
	   c->bits_used,percent,bestPercent,runningPercent/(lines+1),worstPercent);
    {
      int lenout=0;
      char mout[1025];
      range_coder *d=range_coder_dup(c);
      d->bit_stream_length=d->bits_used;
      d->bits_used=0;
      d->low=0; d->high=0xffffffff;
      range_decode_prefetch(d);

      stats3_decompress(d,mout,&lenout);

      printf("<<< %s\n",mout);
      if (lenout!=strlen(m)) {	
	printf("Verify error: length mismatch: decoded = %d, original = %d\n",lenout,(int)strlen(m));
	printf("m='%s'\n",m);
	exit(-1);
      } else printf("decoded length matches (%d)\n",lenout);

      range_coder_free(d);
    }

    range_coder_free(c);

    lines++;
    m[0]=0; fgets(m,1024,f);
  }
  fclose(f);
  return 0;
}

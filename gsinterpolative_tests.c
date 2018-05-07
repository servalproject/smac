#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include "arithmetic.h"


/*      -------------------------------------------------
		  Test Bed
	------------------------------------------------- */

int log2_ceil(int v)
{
  int b;
  for(b=0;b<31;b++)
    if ((1<<b)>=v) return b;
  return 31;
}


void swap(int *a,int *b)
{
  int t=*a;
  *a=*b;
  *b=t;
}

#ifndef __sun__

#include <sys/time.h>

typedef long long hrtime_t;

hrtime_t gethrvtime()
{
  struct timeval tv;

  gettimeofday(&tv,NULL);

  return tv.tv_sec*1000000000+tv.tv_usec*1000;
}
#endif

/* Size of demo list for compression / decompression */
#define DEMO_SIZE (40*1024)
#define LIST_DENSITY 0.05
#define REPEAT_COUNT 1

int main(int argc,char **argv)
{
  int list2[DEMO_SIZE];
  int list2_out[DEMO_SIZE];

  hrtime_t last_time,this_time;
  long long mean;

  int i,b;

  int fraction=0xffff*LIST_DENSITY;

  /* Generate a series of gaps corresponding to a geometric series */
  srandom(0);
  b=0;
  for(i=0;i<DEMO_SIZE;i++)
    {
      b++;
      while((random()&0xffff)>fraction) b++;
      list2[i]=b;
    }
  printf("Using %d integers ranging from %d to %d inclusive,"
	 " requiring %d bits\n",
	 DEMO_SIZE,list2[0],list2[DEMO_SIZE-1],
	 log2_ceil(list2[DEMO_SIZE-1]-list2[0]));

  if (DEMO_SIZE<20)
    {
      printf("  ");
      for(i=0;i<DEMO_SIZE;i++) printf("%d ",list2[i]);
      printf("\n");
    }

  if (1)
    {
      printf("\n\nRecursive implementation:\n");
      printf("-------------------------\n\n");

      range_coder *c=range_new_coder(65536);

      bzero(list2_out,sizeof(int)*DEMO_SIZE);

      /* Encode */
      printf("Encoding ...\n"); fflush(stdout);

      last_time=gethrvtime();
      ic_encode_recursive(list2,DEMO_SIZE,b,c);
      this_time=gethrvtime();

      range_conclude(c);

      printf("%d bits written in %lldns per pointer,"
	     " %d pointers in range [%d,%d]\n",
	     c->bits_used,(this_time-last_time)/DEMO_SIZE,
	     DEMO_SIZE,list2[0],b);

      c->bit_stream_length=c->bits_used;
      c->bits_used=0;
      c->value=0x80000000;
      c->low=0; c->high=0xffffffff;
      range_decode_prefetch(c);

      mean=0;
      printf("Decoding ...\n"); fflush(stdout);

      last_time=gethrvtime();
      ic_decode_recursive(list2_out,DEMO_SIZE,b,c);
      this_time=gethrvtime();


      mean+=(this_time-last_time)/DEMO_SIZE;

      printf(" Used %d of %d bits during decoding at %lldns per pointer.\n",
	     c->bits_used,c->bit_stream_length,mean/REPEAT_COUNT);


      /* Verify */
      printf("Verifying ...\n");
      for(i=0;i<DEMO_SIZE;i++)
	{
	  if (list2[i]!=list2_out[i])
	    {
	      printf("Verify error at entry %d (found %d instead of %d)\n",
		     i,list2_out[i],list2[i]);
	      exit(-1);
	    }
	}
    }

  printf("  -- passed\n");
  return 0;
}

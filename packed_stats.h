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

struct probability_vector {
  unsigned int v[69];
};

typedef struct vector_cache {
  char *string;
  struct vector_cache *left,*right;

  struct probability_vector v;
} vector_cache;

typedef struct node {
  long long count;
  unsigned int counts[69];
  struct node *children[69];

  /* Only populated when in caching mode */
  unsigned int *childAddresses;
  unsigned int *vector;
} node;

typedef struct compressed_stats_handle {
  FILE *file;
  unsigned char *mmap;
  int fileLength;
  int dummyOffset;
  unsigned char *buffer;
  unsigned char *bufferBitmap;  

  unsigned int rootNodeAddress;
  unsigned int totalCount;
  unsigned int maximumOrder;

  int use_cache;
  struct vector_cache *cache;
  /* Full extracted tree */
  struct node *tree;

  /* Used when not caching vectors for returning vector values */
  struct probability_vector vector;
} stats_handle;

void node_free(struct node *n);
struct node *extractNode(char *string,int len,stats_handle *h);
struct node *extractNodeAt(char *s,int len,unsigned int nodeAddress,int count,
			   stats_handle *h,int extractAllP,int debugP);
struct probability_vector *extractVector(char *string,int len,stats_handle *h);
double entropyOfSymbol(struct probability_vector *v,int s);
int vectorReportShort(char *name,struct probability_vector *v,int s);
int vectorReport(char *name,struct probability_vector *v,int s);
int dumpNode(struct node *n);

void stats_handle_free(stats_handle *h);
stats_handle *stats_new_handle(char *file);
unsigned char *getCompressedBytes(stats_handle *h,int start,int count);

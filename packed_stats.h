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
  unsigned int v[CHARCOUNT];
};

typedef struct vector_cache {
  char *string;
  struct vector_cache *left,*right;

  struct probability_vector v;
} vector_cache;

typedef struct node {
  long long count;
  unsigned int counts[CHARCOUNT];
  struct node *children[CHARCOUNT];

} node;

struct unicode_page_statistics {
  // Counts of each of the 128 characters
  // plus counts of transitions to the 512 possible code pages
  // plus count of transitions back to the previously used code page
  int counts[128+512+1];
};

typedef struct compressed_stats_handle {
  FILE *file;
  unsigned char *mmap;
  int fileLength;
  int dummyOffset;
  unsigned char *buffer;
  unsigned char *bufferBitmap;  

  unsigned int rootNodeAddress;
  unsigned int totalCount;
  unsigned int unicodeAddress;
  unsigned int maximumOrder;

  /* Basic model statistics that are required.
     These are preloaded when a stats_handle is created.
     Total size of these is about 5KB, so no big problem,
     even on an embedded system. */
  unsigned int casestartofmessage[1][1];
  unsigned int casestartofword2[2][1];
  unsigned int casestartofword3[2][2][1];
  unsigned int caseposn1[80][1];
  unsigned int caseposn2[2][80][1];
  int messagelengths[1024];

  /* Full extracted tree */
  struct node *tree;

  /* Unicode statistics */
  struct unicode_page_statistics *unicode_pages[512];
  int *unicode_page_addresses;

  /* Used when not caching vectors for returning vector values */
  struct probability_vector vector;
} stats_handle;

void node_free(struct node *n);
struct node *extractNode(unsigned short *string,int len,stats_handle *h);
struct node *extractNodeAt(unsigned short *s,int len,unsigned int nodeAddress,
			   int count,
			   stats_handle *h,int extractAllP,int debugP);
struct probability_vector *extractVector(unsigned short *string,int len,
					 stats_handle *h);
double entropyOfSymbol(struct probability_vector *v,int s);
int vectorReportShort(char *name,struct probability_vector *v,int s);
int vectorReport(char *name,struct probability_vector *v,int s);
int dumpNode(struct node *n);

void stats_handle_free(stats_handle *h);
stats_handle *stats_new_handle(char *file);
int stats_load_tree(stats_handle *h);
unsigned char *getCompressedBytes(stats_handle *h,int start,int count);
int *getUnicodeStatistics(stats_handle *h,int codePage);
int unicodeVectorReport(char *name,int *counts,int previousCodePage,
			int codePage,unsigned short s);

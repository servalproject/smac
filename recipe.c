/*
  Compress a key value pair file according to a recipe.
  The recipe indicates the type of each field.
  For certain field types the precision or range of allowed
  values can be specified to further aid compression.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "charset.h"
#include "visualise.h"
#include "arithmetic.h"
#include "packed_stats.h"
#include "smac.h"

#define FIELDTYPE_INTEGER 0
#define FIELDTYPE_FLOAT 1
#define FIELDTYPE_FIXEDPOINT 2
#define FIELDTYPE_BOOLEAN 3
#define FIELDTYPE_TIMEOFDAY 4
#define FIELDTYPE_DATE 5
#define FIELDTYPE_LATLONG 6
#define FIELDTYPE_TEXT 7

int recipe_parse_fieldtype(char *name)
{
  if (!strcasecmp(name,"integer")) return FIELDTYPE_INTEGER;
  if (!strcasecmp(name,"int")) return FIELDTYPE_INTEGER;
  if (!strcasecmp(name,"float")) return FIELDTYPE_FLOAT;
  if (!strcasecmp(name,"fixedpoint")) return FIELDTYPE_FIXEDPOINT;
  if (!strcasecmp(name,"boolean")) return FIELDTYPE_BOOLEAN;
  if (!strcasecmp(name,"bool")) return FIELDTYPE_BOOLEAN;
  if (!strcasecmp(name,"timeofday")) return FIELDTYPE_TIMEOFDAY;
  if (!strcasecmp(name,"date")) return FIELDTYPE_DATE;
  if (!strcasecmp(name,"latlong")) return FIELDTYPE_LATLONG;
  if (!strcasecmp(name,"text")) return FIELDTYPE_TEXT;
  
  return -1;
}

struct field {
  char *name;
  int type;
  int minimum;
  int maximum;
  int precision; // meaning differs based on field type
};

struct recipe {
  struct field fields[1024];
  int field_count;
};

char recipe_error[1024]="No error.\n";

void recipe_free(struct recipe *recipe)
{
  int i;
  for(i=0;i<recipe->field_count;i++) {
    if (recipe->fields[i].name) free(recipe->fields[i].name);
    recipe->fields[i].name=NULL;
  }
  free(recipe);
}

struct recipe *recipe_read(char *buffer,int buffer_size)
{
  if (buffer_size<1||buffer_size>1048576) {
    snprintf(recipe_error,1024,"Recipe file empty or too large (>1MB).\n");
    return NULL;
  }

  struct recipe *recipe=calloc(sizeof(struct recipe),1);
  if (!recipe) {
    snprintf(recipe_error,1024,"Allocation of recipe structure failed.\n");
    return NULL;
  }

  int i;
  int l=0;
  int line_number=1;
  char line[1024];
  char name[1024],type[1024];
  int min,max,precision;

  for(i=0;i<=buffer_size;i++) {
    if (l>1000) { 
      snprintf(recipe_error,1024,"line:%d:Line too long.\n",line_number);
      recipe_free(recipe); return NULL; }
    if (i==buffer_size||buffer[i]=='\n'||buffer[i]=='\r') {
      if (recipe->field_count>1000) {
	snprintf(recipe_error,1024,"line:%d:Too many field definitions (must be <=1000).\n",line_number);
	recipe_free(recipe); return NULL;
      }
      // Process recipe line
      line[l]=0; l=0;
      if ((l>0)&&(line[0]!='#')) {
	if (sscanf(line,"%[^:]:%[^:]:%d:%d:%d",
		   name,type,&min,&max,&precision)==5) {
	  int fieldtype=recipe_parse_fieldtype(type);
	  if (fieldtype==-1) {
	    snprintf(recipe_error,1024,"line:%d:Unknown or misspelled field type '%s'.\n",line_number,type);
	    recipe_free(recipe); return NULL;
	  } else {
	    // Store parsed field
	    recipe->fields[recipe->field_count].name=strdup(name);
	    recipe->fields[recipe->field_count].type=fieldtype;
	    recipe->fields[recipe->field_count].minimum=min;
	    recipe->fields[recipe->field_count].maximum=max;
	    recipe->fields[recipe->field_count].precision=precision;
	    recipe->field_count++;
	  }
	} else {
	  snprintf(recipe_error,1024,"line:%d:Malformed field definition.\n",line_number);
	  recipe_free(recipe); return NULL;
	}
      }
      line_number++;
    } else {
      line[l++]=buffer[i];
    }
  }
  return recipe;
}

struct recipe *recipe_read_from_file(char *filename)
{
  struct recipe *recipe=NULL;

  unsigned char *buffer;

  int fd=open(filename,O_RDONLY);
  if (fd==-1) {
    snprintf(recipe_error,1024,"Could not open recipe file '%s'\n",filename);
    return NULL;
  }

  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    snprintf(recipe_error,1024,"Could not stat recipe file '%s'\n",filename);
    close(fd); return NULL;
  }

  buffer=mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer==MAP_FAILED) {
    snprintf(recipe_error,1024,"Could not memory map recipe file '%s'\n",filename);
    close(fd); return NULL; 
  }

  recipe=recipe_read((char *)buffer,stat.st_size);

  munmap(buffer,stat.st_size);
  close(fd);
  return recipe;
}

int recipe_decompress(struct recipe *recipe,unsigned char *in,int in_len, char *out, int out_size)
{
  return -1;
}

int recipe_compress(struct recipe *recipe,char *in,int in_len, unsigned char *out, int out_size)
{
  return -1;
}

int recipe_main(int argc,char *argv[], stats_handle *h)
{
  if (argc<=2) {
    fprintf(stderr,"'smac recipe' command requires further arguments.\n");
    exit(-1);
  }

  if (!strcasecmp(argv[2],"parse")) {
    if (argc<=3) {
      fprintf(stderr,"'smac recipe parse' requires name of recipe to load.\n");
      exit(-1);
    }
    struct recipe *recipe = recipe_read_from_file(argv[3]);
    if (!recipe) {
      fprintf(stderr,"%s",recipe_error);
      exit(-1);
    } 
  }
}

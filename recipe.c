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
#include <math.h>
#include <time.h>

#include "charset.h"
#include "visualise.h"
#include "arithmetic.h"
#include "packed_stats.h"
#include "smac.h"

// min,max set inclusive bound
#define FIELDTYPE_INTEGER 0
// precision specifies bits of precision. currently only 32 is supported.
#define FIELDTYPE_FLOAT 1
// precision sets number of decimal places.
// gets encoded by multiplying value,min and max by 10^precision, and then encoding as an integer.
#define FIELDTYPE_FIXEDPOINT 2
#define FIELDTYPE_BOOLEAN 3
// precision is bits of time of day encoded.  17 = 1 sec granularity
#define FIELDTYPE_TIMEOFDAY 4
// precision is bits of date encoded.  32 gets full UNIX Julian seconds.
// 16 gets resolution of ~ 1 day.
// with min set appropriately, 25 gets 1 second granularity within a year.
#define FIELDTYPE_DATE 5
// precision is bits of precision in coordinates
#define FIELDTYPE_LATLONG 6
// min,max refer to size limits of text field.
// precision refers to minimum number of characters to encode if we run short of space.
#define FIELDTYPE_TEXT 7
// precision is the number of bits of the UUID
// we just pull bits from the left of the UUID
#define FIELDTYPE_UUID 8
// Like time of day, but takes a particular string format of date
#define FIELDTYPE_TIMEDATE 9
#define FIELDTYPE_ENUM 10

int recipe_parse_fieldtype(char *name)
{
  if (!strcasecmp(name,"integer")) return FIELDTYPE_INTEGER;
  if (!strcasecmp(name,"int")) return FIELDTYPE_INTEGER;
  if (!strcasecmp(name,"float")) return FIELDTYPE_FLOAT;
  if (!strcasecmp(name,"decimal")) return FIELDTYPE_FIXEDPOINT;
  if (!strcasecmp(name,"fixedpoint")) return FIELDTYPE_FIXEDPOINT;
  if (!strcasecmp(name,"boolean")) return FIELDTYPE_BOOLEAN;
  if (!strcasecmp(name,"bool")) return FIELDTYPE_BOOLEAN;
  if (!strcasecmp(name,"timeofday")) return FIELDTYPE_TIMEOFDAY;
  if (!strcasecmp(name,"timestamp")) return FIELDTYPE_TIMEDATE;
  if (!strcasecmp(name,"datetime")) return FIELDTYPE_TIMEDATE;
  if (!strcasecmp(name,"date")) return FIELDTYPE_DATE;
  if (!strcasecmp(name,"latlong")) return FIELDTYPE_LATLONG;
  if (!strcasecmp(name,"geopoint")) return FIELDTYPE_LATLONG;
  if (!strcasecmp(name,"text")) return FIELDTYPE_TEXT;
  if (!strcasecmp(name,"uuid")) return FIELDTYPE_UUID;
  if (!strcasecmp(name,"enum")) return FIELDTYPE_ENUM;
  
  return -1;
}

char *recipe_field_type_name(int f)
{
  switch(f) {
  case FIELDTYPE_INTEGER: return    "integer";
  case FIELDTYPE_FLOAT: return    "float";
  case FIELDTYPE_FIXEDPOINT: return    "fixedpoint";
  case FIELDTYPE_BOOLEAN: return    "boolean";
  case FIELDTYPE_TIMEOFDAY: return    "timeofday";
  case FIELDTYPE_TIMEDATE: return    "timestamp";
  case FIELDTYPE_DATE: return    "date";
  case FIELDTYPE_LATLONG: return    "latlong";
  case FIELDTYPE_TEXT: return    "text";
  case FIELDTYPE_UUID: return    "uuid";
  default: return "unknown";
  }
}


struct field {
  char *name;
  int type;
  int minimum;
  int maximum;
  int precision; // meaning differs based on field type
  char *enum_values[32];
  int enum_count;
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
    int e;
    for(e=0;e<recipe->fields[i].enum_count;e++) {
      if (recipe->fields[i].enum_values[e]) {
	free(recipe->fields[i].enum_values[e]);
	recipe->fields[i].enum_values[e]=NULL;
      }
    }
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
  char enumvalues[1024];

  for(i=0;i<=buffer_size;i++) {
    if (l>1000) { 
      snprintf(recipe_error,1024,"line:%d:Line too long.\n",line_number);
      recipe_free(recipe); return NULL; }
    if ((i==buffer_size)||(buffer[i]=='\n')||(buffer[i]=='\r')) {
      if (recipe->field_count>1000) {
	snprintf(recipe_error,1024,"line:%d:Too many field definitions (must be <=1000).\n",line_number);
	recipe_free(recipe); return NULL;
      }
      // Process recipe line
      line[l]=0; 
      if ((l>0)&&(line[0]!='#')) {
	enumvalues[0]=0;
	if (sscanf(line,"%[^:]:%[^:]:%d:%d:%d:%[^\n]",
		   name,type,&min,&max,&precision,enumvalues)>=5) {
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

	    if (fieldtype==FIELDTYPE_ENUM) {
	      char enum_value[1024];
	      int e=0;
	      int en=0;
	      int i;
	      for(i=0;i<=strlen(enumvalues);i++) {
		if ((enumvalues[i]==',')||(enumvalues[i]==0)) {
		  // End of field
		  enum_value[e]=0;
		  if (en>=32) {
		    snprintf(recipe_error,1024,"line:%d:enum has too many values (max=32)\n",line_number);
		    recipe_free(recipe);
		    return NULL;
		  }
		  recipe->fields[recipe->field_count].enum_values[en]
		    =strdup(enum_value);
		  en++;
		} else {
		  // next character of field
		  enum_value[e++]=enumvalues[i];
		}
	      }
	    }

	    recipe->field_count++;
	  }
	} else {
	  snprintf(recipe_error,1024,"line:%d:Malformed field definition.\n",line_number);
	  recipe_free(recipe); return NULL;
	}
      }
      line_number++; l=0;
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
  
  if (recipe&&recipe->field_count==0) {
    recipe_free(recipe);
    snprintf(recipe_error,1024,"Recipe contains no field definitions\n");
    return NULL;
  }

  return recipe;
}

int recipe_parse_boolean(char *b)
{
  if (!b) return 0;
  switch(b[0]) {
  case 'y': case 'Y': case 't': case 'T': case '1':
    return 1;
  default:
    return 0;
  }
}

int recipe_decode_field(struct recipe *recipe,stats_handle *stats, range_coder *c,
			int fieldnumber,char *value,int value_size)
{
  int normalised_value;
  int minimum;
  int maximum;
  int precision;
  int h,m,s,d,y;
  float lat,lon;
  int ilat,ilon;

  int r;
  
  precision=recipe->fields[fieldnumber].precision;

  switch (recipe->fields[fieldnumber].type) {
  case FIELDTYPE_INTEGER:
    minimum=recipe->fields[fieldnumber].minimum;
    maximum=recipe->fields[fieldnumber].maximum;
    normalised_value=range_decode_equiprobable(c,maximum-minimum+1);
    sprintf(value,"%d",normalised_value+minimum);
    break;
  case FIELDTYPE_BOOLEAN:
    normalised_value=range_decode_equiprobable(c,2);
    sprintf(value,"%d",normalised_value);
    break;
  case FIELDTYPE_TEXT:
    r=stats3_decompress_bits(c,(unsigned char *)value,&value_size,stats,NULL);
    return 0;
  case FIELDTYPE_TIMEDATE:
    // time is 32-bit seconds since 1970.
    // Format as yyyy-mm-ddThh:mm:ss+hh:mm
    {
      time_t t=range_decode_equiprobable(c,0x7fffffff);
      struct tm tm;
      gmtime_r(&t,&tm);
      sprintf(value,"%04d-%02d-%02dT%02d:%02d:%02d+00:00",
	      tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,
	      tm.tm_hour,tm.tm_hour,tm.tm_min);
      return 0;
    }
  case FIELDTYPE_UUID:
    {
      int i,j=5;      
      sprintf(value,"uuid:");
      for(i=0;i<16;i++)
	{
	  int b=0;
	  if (i<recipe->fields[fieldnumber].precision)
	    b=range_decode_equiprobable(c,256);
	  switch(i) {
	  case 4: case 6: case 8: case 10:
	    value[j++]='-';
	  }
	  sprintf(&value[j],"%02x",b); j+=2;
	  value[j]=0;
	}
      return 0;
    }
  case FIELDTYPE_LATLONG:
    {
      int ilat,ilon;
      double lat,lon;
      switch(recipe->fields[fieldnumber].precision) {
      case 0: case 34:
	ilat=range_decode_equiprobable(c,182*112000); ilat-=90*112000;
	ilon=range_decode_equiprobable(c,361*112000); ilon-=180*112000;
	lat=ilat/112000.0; lon=ilon/112000.0;
	break;
      case 16:
	ilat=range_decode_equiprobable(c,182); ilat-=90;
	ilon=range_decode_equiprobable(c,361); ilon-=180;
	lat=ilat; lon=ilon;
	break;
      default:
	sprintf(recipe_error,"Illegal LATLONG precision of %d bits.  Should be 16 or 34.\n",recipe->fields[fieldnumber].precision);
	return -1;
      }
      sprintf(value,"%.5f %.5f",lat,lon);
      return 0;
    }
  default:
    snprintf(recipe_error,1024,"Attempting decompression of unsupported field type of '%s'.\n",recipe_field_type_name(recipe->fields[fieldnumber].type));
    return -1;
  }

  return 0;
}

int parseHexDigit(int c)
{
  if (c>='0'&&c<='9') return c-'0';
  if (c>='A'&&c<='F') return c-'A'+10;
  if (c>='a'&&c<='f') return c-'a'+10;
  return 0;
}

int parseHexByte(char *hex)
{
  return (parseHexDigit(hex[0])<<4)|parseHexDigit(hex[1]);
}

int recipe_encode_field(struct recipe *recipe,stats_handle *stats, range_coder *c,
			int fieldnumber,char *value)
{
  int normalised_value;
  int minimum;
  int maximum;
  int precision;
  int h,m,s,d,y;
  float lat,lon;
  int ilat,ilon;

  precision=recipe->fields[fieldnumber].precision;

  switch (recipe->fields[fieldnumber].type) {
  case FIELDTYPE_INTEGER:
    normalised_value=atoi(value)-recipe->fields[fieldnumber].minimum;
    minimum=recipe->fields[fieldnumber].minimum;
    maximum=recipe->fields[fieldnumber].maximum;
    return range_encode_equiprobable(c,maximum-minimum+1,normalised_value);
  case FIELDTYPE_FLOAT:
  case FIELDTYPE_FIXEDPOINT:
  case FIELDTYPE_BOOLEAN:
    normalised_value=recipe_parse_boolean(value);
    minimum=0;
    maximum=1;
    return range_encode_equiprobable(c,maximum-minimum+1,normalised_value);
  case FIELDTYPE_TIMEOFDAY:
    if (sscanf(value,"%d:%d.%d",&h,&m,&s)<2) return -1;
    // XXX - We don't support leap seconds
    if (h<0||h>23||m<0||m>59||s<0||s>59) return -1;
    normalised_value=h*3600+m*60+s;
    minimum=0;
    maximum=24*60*60;
    if (precision==0) precision=17; // 2^16 < 24*60*60 < 2^17
    if (precision<17) {
      normalised_value=normalised_value >> (17 - precision);
      minimum=minimum >> (17 - precision);
      maximum=maximum >> (17 - precision);
      maximum+=1; // make sure that normalised_value cannot = maximum
    }
    return range_encode_equiprobable(c,maximum-minimum+1,normalised_value);
  case FIELDTYPE_TIMEDATE:
    {
      struct tm tm;
      int tzh=0,tzm=0;
      int r;
      bzero(&tm,sizeof(tm));
      if ((r=sscanf(value,"%d-%d-%dT%d:%d:%d.%*d+%d:%d",
		 &tm.tm_year,&tm.tm_mon,&tm.tm_mday,
		 &tm.tm_hour,&tm.tm_min,&tm.tm_sec,
		    &tzh,&tzm))<6) {
	printf("r=%d\n",r);
	return -1;
      }
      tm.tm_gmtoff=tzm*60+tzh*3600;
      tm.tm_year-=1900;
      tm.tm_mon-=1;
      time_t t = mktime(&tm);
      minimum=1;
      maximum=0x7fffffff;
      normalised_value=t;
      return range_encode_equiprobable(c,maximum-minimum+1,normalised_value);
    }

  case FIELDTYPE_DATE:
    if (sscanf(value,"%d/%d/%d",&y,&m,&d)!=3) return -1;
		 
    // XXX Not as efficient as it could be (assumes all months have 31 days)
    if (y<1||y>9999||m<1||m>12||d<1||d>31) return -1;
    normalised_value=y*372+(m-1)*31+(d-1);
    minimum=0;
    maximum=10000*372;
    if (precision==0) precision=22; // 2^21 < maximum < 2^22
    if (precision<22) {
      normalised_value=normalised_value >> (22 - precision);
      minimum=minimum >> (22 - precision);
      maximum=maximum >> (22 - precision);
      maximum+=1; // make sure that normalised_value cannot = maximum
    }
    return range_encode_equiprobable(c,maximum-minimum+1,normalised_value);
  case FIELDTYPE_LATLONG:
    if (sscanf(value,"%f %f",&lat,&lon)!=2) return -1;
    if (lat<-90||lat>90||lon<-180||lon>180) return -1;
    ilat=lroundf(lat);
    ilon=lroundf(lon);
    ilat+=90; // range now 0..181 (for -90 to +90, inclusive)
    ilon+=180; // range now 0..360 (for -180 to +180, inclusive)
    if (precision==16) {
      // gradicule resolution
      range_encode_equiprobable(c,182,ilat);
      return range_encode_equiprobable(c,361,ilon);
    } else if (precision==0||precision==34) {
      // ~1m resolution
    ilat=lroundf(lat*112000);
    ilon=lroundf(lon*112000);
    ilat+=90*112000; // range now 0..181 (for -90 to +90, inclusive)
    ilon+=180*112000; // range now 0..359 (for -179 to +180, inclusive)
    // encode latitude
    range_encode_equiprobable(c,182*112000,ilat);
    return range_encode_equiprobable(c,361*112000,ilon);
    } else
      return -1;
  case FIELDTYPE_TEXT:
    {
      int before=c->bits_used;
      if (strlen(value)>recipe->fields[fieldnumber].precision)
	value[recipe->fields[fieldnumber].precision]=0;
      int r=stats3_compress_append(c,(unsigned char *)value,strlen(value),stats,
				   NULL);
      printf("'%s' encoded in %d bits\n",value,c->bits_used-before);
      if (r) return -1;
      return 0;
    }
  case FIELDTYPE_UUID:
    {
      // Parse out the 128 bits (=16 bytes) of UUID, and encode as much as we have been asked.
      // XXX Will accept all kinds of rubbish
      int i,j=0;
      unsigned char uuid[16];
      i=0;
      if (!strncasecmp(value,"uuid:",5)) i=5;
      for(;value[i];i++) {
	if (j==16) {j=17; break; }
	if (value[i]!='-') {
	  uuid[j++]=parseHexByte(&value[i]);
	  i++;
	}
      }
      if (j!=16) {
	sprintf(recipe_error,"Malformed UUID field.\n");
	return -1;
      }
      // write appropriate number of bytes
      for(i=0;i<recipe->fields[fieldnumber].precision;i++) {
	range_encode_equiprobable(c,256,uuid[i]);
      }
      return 0;
    }
  }

  return -1;
}

int recipe_decompress(stats_handle *h, struct recipe *recipe,
		      unsigned char *in,int in_len, char *out, int out_size)
{
  if (!recipe) {
    snprintf(recipe_error,1024,"No recipe provided.\n");
    return -1;
  }
  if (!in) {
    snprintf(recipe_error,1024,"No input provided.\n");
    return -1;
  }
  if (!out) {
    snprintf(recipe_error,1024,"No output buffer provided.\n");
    return -1;
  }
  if (in_len>=1024) {
    snprintf(recipe_error,1024,"Input must be <1KB.\n");
    return -1;
  }

  // Make new range coder with 1KB of space
  range_coder *c=range_new_coder(1024);

  // Point range coder bit stream to input buffer
  bcopy(in,c->bit_stream,in_len);
  c->bit_stream_length=in_len*8;
  range_decode_prefetch(c);

  int written=0;

  int field;
  for(field=0;field<recipe->field_count;field++)
    {
      int field_present=range_decode_equiprobable(c,2);
      if (field_present) {
	// printf("Decompressing value for '%s'\n",recipe->fields[field].name);
	char value[1024];
	int r=recipe_decode_field(recipe,h,c,field,value,1024);
	if (r) {
	  range_coder_free(c);
	  return -1;
	}
	int r2=snprintf(&out[written],out_size-written,"%s=%s\n",
			recipe->fields[field].name,value);
	if (r2>0) written+=r2;
      }
    }
  
  range_coder_free(c);

  return written;
}

int recipe_compress(stats_handle *h,struct recipe *recipe,
		    char *in,int in_len, unsigned char *out, int out_size)
{
  /*
    Eventually we want to support full skip logic, repeatable sections and so on.
    For now we will allow skip sections by indicating missing fields.
    This approach lets us specify fields implictly by their order in the recipe
    (NOT in the completed form).
    This entails parsing the completed form, and then iterating through the RECIPE
    and considering each field in turn.  A single bit per field will be used to
    indicate whether it is present.  This can be optimised later.
  */

  if (!recipe) {
    snprintf(recipe_error,1024,"No recipe provided.\n");
    return -1;
  }
  if (!in) {
    snprintf(recipe_error,1024,"No input provided.\n");
    return -1;
  }
  if (!out) {
    snprintf(recipe_error,1024,"No output buffer provided.\n");
    return -1;
  }

  // Make new range coder with 1KB of space
  range_coder *c=range_new_coder(1024);
  if (!c) {
    snprintf(recipe_error,1024,"Could not instantiate range coder.\n");
    return -1;
  }

  char *keys[1024];
  char *values[1024];
  int value_count=0;

    int i;
  int l=0;
  int line_number=1;
  char line[1024];
  char key[1024],value[1024];

  for(i=0;i<=in_len;i++) {
    if (l>1000) { 
      snprintf(recipe_error,1024,"line:%d:Data line too long.\n",line_number);
      return -1; }
    if ((i==in_len)||(in[i]=='\n')||(in[i]=='\r')) {
      if (value_count>1000) {
	snprintf(recipe_error,1024,"line:%d:Too many data lines (must be <=1000).\n",line_number);
	return -1;
      }
      // Process key=value line
      line[l]=0; 
      if ((l>0)&&(line[0]!='#')) {
	if (sscanf(line,"%[^=]=%[^\n]",key,value)==2) {
	  keys[value_count]=strdup(key);
	  values[value_count]=strdup(value);
	  value_count++;
	} else {
	  snprintf(recipe_error,1024,"line:%d:Malformed data line.\n",line_number);
	  return -1;
	}
      }
      line_number++; l=0;
    } else {
      line[l++]=in[i];
    }
  }
  printf("Read %d data lines.\n",value_count);

  int field;

  for(field=0;field<recipe->field_count;field++) {
    // look for this field in keys[] 
    for (i=0;i<value_count;i++) {
      if (!strcasecmp(keys[i],recipe->fields[field].name)) break;
    }
    if (i<value_count) {
      // Field present
      printf("Found field #%d ('%s')\n",field,recipe->fields[field].name);
      // Record that the field is present.
      range_encode_equiprobable(c,2,1);
      // Now, based on type of field, encode it.
      if (recipe_encode_field(recipe,h,c,field,values[i]))
	{
	  range_coder_free(c);
	  snprintf(recipe_error,1024,"Could not record value '%s' for field '%s'\n",
		   values[i],recipe->fields[field].name);
	  return -1;
	}
    } else {
      // Field missing: record this fact and nothing else.
      range_encode_equiprobable(c,2,0);
    }
  }

  // Get result and store it, unless it is too big for the output buffer
  range_conclude(c);
  int bytes=(c->bits_used/8)+((c->bits_used&7)?1:0);
  if (bytes>out_size) {
    range_coder_free(c);
    snprintf(recipe_error,1024,"Compressed data too big for output buffer\n");
    return -1;
  }
  
  bcopy(c->bit_stream,out,bytes);
  range_coder_free(c);

  printf("Used %d bits (%d bytes).\n",c->bits_used,bytes);

  return bytes;
}

int recipe_compress_file(stats_handle *h,char *recipe_file,char *input_file,char *output_file)
{
  struct recipe *recipe=recipe_read_from_file(recipe_file);
  if (!recipe) return -1;

  unsigned char *buffer;

  int fd=open(input_file,O_RDONLY);
  if (fd==-1) {
    snprintf(recipe_error,1024,"Could not open uncompressed file '%s'\n",input_file);
    return -1;
  }

  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    snprintf(recipe_error,1024,"Could not stat uncompressed file '%s'\n",input_file);
    close(fd); return -1;
  }

  buffer=mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer==MAP_FAILED) {
    snprintf(recipe_error,1024,"Could not memory map uncompressed file '%s'\n",input_file);
    close(fd); return -1; 
  }

  unsigned char out_buffer[1024];
  int r=recipe_compress(h,recipe,(char *)buffer,stat.st_size,out_buffer,1024);

  munmap(buffer,stat.st_size); close(fd);

  if (r<0) return -1;
  
  FILE *f=fopen(output_file,"w");
  if (!f) {
    snprintf(recipe_error,1024,"Could not write succinct data compressed file '%s'\n",output_file);
    return -1;
  }
  int wrote=fwrite(out_buffer,r,1,f);
  fclose(f);
  if (wrote!=1) {
    snprintf(recipe_error,1024,"Could not write %d bytes of compressed succinct data into '%s'\n",r,output_file);
    return -1;
  }

  return r;
}

int recipe_decompress_file(stats_handle *h,char *recipe_file,char *input_file,char *output_file)
{
  struct recipe *recipe=recipe_read_from_file(recipe_file);
  if (!recipe) return -1;

  unsigned char *buffer;

  int fd=open(input_file,O_RDONLY);
  if (fd==-1) {
    snprintf(recipe_error,1024,"Could not open succinct data file '%s'\n",input_file);
    return -1;
  }

  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    snprintf(recipe_error,1024,"Could not stat succinct data file '%s'\n",input_file);
    close(fd); return -1;
  }

  buffer=mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer==MAP_FAILED) {
    snprintf(recipe_error,1024,"Could not memory map succinct data file '%s'\n",input_file);
    close(fd); return -1; 
  }

  char out_buffer[1024];
  int r=recipe_decompress(h,recipe,buffer,stat.st_size,out_buffer,1024);

  munmap(buffer,stat.st_size); close(fd);

  if (r<0) return -1;
  
  FILE *f=fopen(output_file,"w");
  if (!f) {
    snprintf(recipe_error,1024,"Could not write decompressed file '%s'\n",output_file);
    return -1;
  }
  int wrote=fwrite(out_buffer,r,1,f);
  fclose(f);
  if (wrote!=1) {
    snprintf(recipe_error,1024,"Could not write %d bytes of decompressed data into '%s'\n",r,output_file);
    return -1;
  }

  return r;
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
    printf("recipe=%p\n",recipe);
    printf("recipe->field_count=%d\n",recipe->field_count);
  } else if (!strcasecmp(argv[2],"compress")) {
    if (argc<=5) {
      fprintf(stderr,"'smac recipe compress' requires recipe, input and output files.\n");
      exit(-1);
    }
    if (recipe_compress_file(h,argv[3],argv[4],argv[5])==-1) {
      fprintf(stderr,"%s",recipe_error);
      exit(-1);
    }
    else return 0;
  } else if (!strcasecmp(argv[2],"decompress")) {
    if (argc<=5) {
      fprintf(stderr,"'smac recipe decompress' requires recipe, input and output files.\n");
      exit(-1);
    }
    if (recipe_decompress_file(h,argv[3],argv[4],argv[5])==-1) {
      fprintf(stderr,"%s",recipe_error);
      exit(-1);
    }
    else return 0;
  } else {
    fprintf(stderr,"unknown 'smac recipe' sub-command '%s'.\n",argv[2]);
      exit(-1);
  }

  return 0;
}

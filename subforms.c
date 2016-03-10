/*
Copyright (C) 2016 Paul Gardner-Stephen, Flinders University.
 
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
#include<sys/types.h>
#include<sys/time.h>

#include "charset.h"
#include "visualise.h"
#include "arithmetic.h"
#include "packed_stats.h"
#include "smac.h"
#include "recipe.h"
#include "subforms.h"

#ifdef ANDROID
#include <jni.h>
#include <android/log.h>
 
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "libsmac", __VA_ARGS__))
#define C LOGI("%s:%d: checkpoint",__FILE__,__LINE__);
#else
#define LOGI(...)
#define C
#endif


int record_free(struct record *r)
{
  if (!r) return -1;
  for(int i=0;i<r->field_count;i++)
    {
      if (r->fields[i].key) free(r->fields[i].key); r->fields[i].key=NULL;
      if (r->fields[i].value) free(r->fields[i].value); r->fields[i].value=NULL;
      if (r->fields[i].subrecord)
	record_free(r->fields[i].subrecord);
      r->fields[i].subrecord=NULL;
    }
  free(r);
  return 0;
}

struct record *parse_stripped_with_subforms(char *in,int in_len)
{
  struct record *record=calloc(sizeof(record),1);
  struct record *current_record=record;
  int i;
  char line[1024];
  char key[1024],value[1024];
  int line_number=1;
  int l=0;
  
  for(i=0;i<=in_len;i++) {
    if ((i==in_len)||(in[i]=='\n')||(in[i]=='\r')) {
      // Process key=value line
      if (l>=1000) l=0; // ignore long lines
      line[l]=0;
      if (line[0]=='{') {
	// Start of sub-form
	// Move current record down into a new sub-record at this point

	if (current_record->field_count>=MAX_FIELDS) {
	  snprintf(recipe_error,1024,"line:%d:Too many data lines (must be <=%d, or increase MAX_FIELDS).\n",
		   line_number,MAX_FIELDS);
	  record_free(record);
	  return NULL;
	}
	
	current_record->fields[record->field_count].subrecord=calloc(sizeof(struct record),1);
	current_record->fields[record->field_count].subrecord->parent=current_record;
	current_record->field_count++;
	current_record=current_record->fields[record->field_count-1].subrecord;
      } else if (line[0]=='}') {
	// End of sub-form
	if (!current_record->fields[record->field_count].subrecord->parent) {
	    snprintf(recipe_error,1024,"line:%d:} without matching {.\n",
		     line_number);
	  record_free(record);
	  return NULL;
	}
	current_record->fields[record->field_count].subrecord
	  =current_record->fields[record->field_count].subrecord->parent;
      } else if ((l>0)&&(line[0]!='#')) {
	if (sscanf(line,"%[^=]=%[^\n]",key,value)==2) {
	  if (current_record->field_count>=MAX_FIELDS) {
	    snprintf(recipe_error,1024,"line:%d:Too many data lines (must be <=%d, or increase MAX_FIELDS).\n",
		     line_number,MAX_FIELDS);
	  record_free(record);
	  return NULL;
	  }	
	  current_record->fields[current_record->field_count].key=strdup(key);
	  current_record->fields[current_record->field_count].key=strdup(value);
	  current_record->field_count++;
	} else {
	  snprintf(recipe_error,1024,"line:%d:Malformed data line (%s:%d): '%s'\n",
		   line_number,__FILE__,__LINE__,line);	  
	  record_free(record);
	  return NULL;
	}
      }
      line_number++; 
      l=0;
    } else {
      if (l<1000) { line[l++]=in[i]; }
      else {
	if (l==1000) {
	  fprintf(stderr,"line:%d:Line too long -- ignoring (must be < 1000 characters).\n",line_number);	  
	  LOGI("line:%d:Line too long -- ignoring (must be < 1000 characters).\n",line_number);
	}
	l++;
      }
    } 
  }

  if (current_record->fields[record->field_count].subrecord->parent) {
    snprintf(recipe_error,1024,"line:%d:End of input, but } expected.\n",
	     line_number);
    record_free(record);
    return NULL;
  }

  
  printf("Read %d data lines, %d values.\n",line_number,record->field_count);
  LOGI("Read %d data lines, %d values.\n",line_number,record->field_count);

  return record;
}

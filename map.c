/*
  Generate HTML map interface to show collected data for each form.

  (C) Copyright Paul Gardner-Stephen, 2014.

*/

#include <stdio.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "charset.h"
#include "visualise.h"
#include "arithmetic.h"
#include "packed_stats.h"
#include "smac.h"
#include "recipe.h"

char *htmlTop=""
"<!DOCTYPE HTML>\n"
"<html>\n"
"\n"
"<head>\n"
"<title>Leaf Map Example</title>\n"
"<link rel=\"stylesheet\" href=\"http://cdn.leafletjs.com/leaflet-0.7.3/leaflet.css\" />\n"
"<script src=\"http://cdn.leafletjs.com/leaflet-0.7.3/leaflet.js\"></script>\n"
"<meta charset=\"utf-8\" />\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<link rel=\"stylesheet\" href=\"../dist/leaflet.css\" />\n"
"\n"
"</head>\n"
"\n"
"<body>\n"
"\n"
" <div id=\"map\" style=\"width: 600px; height: 400px\"></div>\n"
" <script src=\"../dist/leaflet.js\"></script>\n"
" \n"
" <script>\n"
" var viewportwidth;\n"
" var viewportheight;\n"
"\n"
" // the more standards compliant browsers (mozilla/netscape/opera/IE7) use window.innerWidth and window.innerHeight\n"
"\n"
" if (typeof window.innerWidth != 'undefined')\n"
" {                      \n"
"      viewportwidth = window.innerWidth,\n"
"      viewportheight = window.innerHeight\n"
" }                              \n"
"                        \n"
"// IE6 in standards compliant mode (i.e. with a valid doctype as the first line in the document)\n"
"  else if (typeof document.documentElement != 'undefined'     && typeof document.documentElement.clientWidth !=\n"
"     'undefined' && document.documentElement.clientWidth != 0) {       viewportwidth = document.documentElement.clientWidth,       viewportheight = document.documentElement.clientHeight\n"
" }   // older versions of IE\n"
"   else {       viewportwidth = document.getElementsByTagName('body')[0].clientWidth,\n"
"       viewportheight = document.getElementsByTagName('body')[0].clientHeight }\n"
" document.getElementById(\"map\").setAttribute(\"style\",\"width:\"+String(viewportwidth*0.9)+\"; height:\"+String(viewportheight*0.9)+\"px\");\n"
"\n"
" var map = L.map('map').setView([-35.029613, 138.573177], 4);\n"
"L.tileLayer('https://{s}.tiles.mapbox.com/v3/{id}/{z}/{x}/{y}.png', {\n"
"			maxZoom: 18,\n"
"			attribution: 'Map data &copy; <a href=\"http://openstreetmap.org\">OpenStreetMap</a> contributors, ' +\n"
"				'<a href=\"http://creativecommons.org/licenses/by-sa/2.0/\">CC-BY-SA</a>, ' +\n"
"				'Imagery © <a href=\"http://mapbox.com\">Mapbox</a>',\n"
"			id: 'examples.map-i86knfo3'\n"
"		}).addTo(map);\n"
  ;
char *htmlBottom=
" </script>\n"
" \n"
"</body>\n"
"\n"
"</html>\n";

struct stripped {
  char *keys[1024];
  char *values[1024];
  int value_count;
};

char sanitiseOut[8192];
char *sanitise(char *in)
{
  // Sanitise output for inserting in HTML: double-quotes and < & > should be 
  // escaped.
  int i=0;
  int outLen=0;

  for(i=0;in[i];i++) {
    sanitiseOut[outLen]=0;
    if (i>1024) return sanitiseOut;

    switch(in[i]) {
    case '&': 
      sanitiseOut[outLen++]='&';
      sanitiseOut[outLen++]='a';
      sanitiseOut[outLen++]='m';
      sanitiseOut[outLen++]='p';
      sanitiseOut[outLen++]=';';
      break;
    case '<': 
      sanitiseOut[outLen++]='&';
      sanitiseOut[outLen++]='l';
      sanitiseOut[outLen++]='t';
      sanitiseOut[outLen++]=';';
      break;
    case '>': 
      sanitiseOut[outLen++]='&';
      sanitiseOut[outLen++]='g';
      sanitiseOut[outLen++]='t';
      sanitiseOut[outLen++]=';';
      break;
    case '"': 
      sanitiseOut[outLen++]='\\';
      sanitiseOut[outLen++]='"';
      break;
    default:
      sanitiseOut[outLen++]=in[i];      
    }
    sanitiseOut[outLen]=0;
  }
  
  return sanitiseOut;
}

void stripped_free(struct stripped *s)
{
  int i;
  if (!s) return;
  for(i=0;i<s->value_count;i++) {
    free(s->keys[i]); free(s->values[i]);
  }
  free(s);
  return;
}

struct stripped *parse_stripped(char *filename)
{
  char *in;
  int in_len;
  int fd=open(filename,O_RDONLY);
  if (fd<0) return NULL;
  
  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    fprintf(stderr,"Could not stat file '%s'\n",filename);
    close(fd); return NULL;
  }

  if (stat.st_size>65536) {
    fprintf(stderr,"File '%s' is too long (must be <= %d bytes)\n",
	     filename,65536);
    close(fd); return NULL;
  }

  in=mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (in==MAP_FAILED) {
    fprintf(stderr,"Could not memory map file '%s'\n",filename);
    close(fd); return NULL; 
  }
  in_len=stat.st_size;
  close(fd);

  struct stripped *s=calloc(sizeof(struct stripped),1);

  int l=0;
  int line_number=1;
  char line[1024];
  char key[1024],value[1024];
  int i;

  for(i=0;i<=in_len;i++) {
    if (l>1000) { 
      fprintf(stderr,"line:%d:Data line too long.\n",line_number);
      stripped_free(s);
      munmap(in,stat.st_size);
      return NULL; }
    if ((i==in_len)||(in[i]=='\n')||(in[i]=='\r')) {
      if (s->value_count>1000) {
	fprintf(stderr,"line:%d:Too many data lines (must be <=1000).\n",line_number);
	stripped_free(s);
	munmap(in,stat.st_size);
	return NULL;
      }
      // Process key=value line
      line[l]=0; 
      if ((l>0)&&(line[0]!='#')) {
	if (sscanf(line,"%[^=]=%[^\n]",key,value)==2) {
	  s->keys[s->value_count]=strdup(key);
	  s->values[s->value_count]=strdup(value);
	  s->value_count++;
	} else {
	  fprintf(stderr,"line:%d:Malformed data line (%s:%d).\n",line_number,
		  __FILE__,__LINE__);
	  stripped_free(s);
	  munmap(in,stat.st_size);
	  return NULL;
	}
      }
      line_number++; l=0;
    } else {
      line[l++]=in[i];
    }
  }
  return s;
}

int generateMap(char *recipeDir,char *recipe_name, char *outputDir)
{
  char filename[1024];

  snprintf(filename,1024,"%s/%s.recipe",recipeDir,recipe_name);
  struct recipe *r=recipe_read_from_file(filename);
  if (!r) {
    fprintf(stderr,"Could not read recipe file '%s'\n",filename);
    return -1;
  }

  snprintf(filename,1024,"%s/maps/",outputDir);
  mkdir(filename,0777);

  int markerCount=0;

  // Process each form instance
  snprintf(filename,1024,"%s/%s",outputDir,recipe_name);
  DIR *d=opendir(filename);
  if (d) {
    snprintf(filename,1024,"%s/maps/%s.html",outputDir,recipe_name);
    FILE *f=fopen(filename,"w");

    fprintf(f,"%s",htmlTop);
    
    struct dirent *de;
    while((de=readdir(d))) {
      if (strlen(de->d_name)>strlen(".stripped"))
	if (!strcasecmp(&de->d_name[strlen(de->d_name)-strlen(".stripped")],".stripped"))
	  {
	    // It's a stripped file -- read it and add a data point for the first 
	    // location field present.
	    char formDetail[8192];
	    int formDetailLen=0;
	    int haveLocation=0;
	    float lat=0,lon=0;
	    snprintf(filename,1024,"%s/%s/%s",outputDir,recipe_name,de->d_name);
	    struct stripped *s=parse_stripped(filename);
	    if (s) {
	      fprintf(stderr,"  %d fields in '%s'\n",s->value_count,filename);
	      int i,j;
	      for(i=0;i<s->value_count;i++) {
		for(j=0;j<r->field_count;j++)
		  if (!strcasecmp(s->keys[i],r->fields[j].name)) {
		    switch (r->fields[j].type) {
		    case FIELDTYPE_LATLONG:
		      if (sscanf(s->values[i],"%f %f",&lat,&lon)==2) haveLocation=1;
		      break;
		    case FIELDTYPE_INTEGER:
		      // calculate summary statistics
		      break;
		    case FIELDTYPE_BOOLEAN:
		      // calculate summary statistics
		      break;
		    case FIELDTYPE_ENUM:
		      // calculate summary statistics
		      break;
		    }
		  }
		if (!formDetailLen) {
		  formDetailLen+=snprintf(formDetail,8192,"<div id=\\\"marker%d\\\" style=\\\"height:\\\"+String(viewportheight*0.2)+\\\"px;overflow:auto;\\\"><table border=1 padding=2>\\n",markerCount++);
		}
		formDetailLen
		  +=snprintf(&formDetail[formDetailLen],8192-formDetailLen,
			     "<tr><td>%s</td><td>%s</td>\\n",
			     s->keys[i],sanitise(s->values[i]));
	      }
	      if (formDetailLen) {
		formDetailLen
		  +=snprintf(&formDetail[formDetailLen],8192-formDetailLen,
			     "</table></div>\\n");
	      } else {
		formDetailLen
		  +=snprintf(&formDetail[formDetailLen],8192-formDetailLen,
			     "Form empty.\\n");
	      }
	      if (haveLocation) {
		formDetail[formDetailLen]=0;
		fprintf(f," L.marker([%f, %f]).addTo(map).bindPopup(\"%s\");\n",
			lat,lon,formDetail);
	      }
	      stripped_free(s);
	    }
	  }
    }
    closedir(d);
    
    fprintf(f,"%s",htmlBottom);
    
    fclose(f);
  } else {
    fprintf(stderr,"There do not appear to be any form instances for '%s'\n",
	    recipe_name);
    fprintf(stderr,"  ('%s' is non-existent)\n",filename);
  }
  return 0;
}


int generateMaps(char *recipeDir, char *outputDir)
{
  DIR *d=opendir(recipeDir);

  char filename[1024];
  snprintf(filename,1024,"%s/maps/index.html",outputDir);
  fprintf(stderr,"Trying to create %s\n",filename);
  FILE *idx=fopen(filename,"w");
  perror("result");

  struct dirent *de;

  while((de=readdir(d))) {
    int end=strlen(de->d_name);
  // Cut .recipe from filename
  if (end>strlen(".recipe"))
    if (!strcasecmp(".recipe",&de->d_name[end-strlen(".recipe")]))
      {
	// We have a recipe file
	char recipe_name[1024];
	strcpy(recipe_name,de->d_name);
	recipe_name[end-strlen(".recipe")]=0;
	fprintf(stderr,"Recipe '%s'\n",recipe_name);

	if (idx) fprintf(idx,"<a href=\"%s.html\">%s</a> ",recipe_name,recipe_name);
	if (idx) fprintf(idx,"<a href=\"../csv/%s.csv\">CSV</a><br>\n",recipe_name);

	generateMap(recipeDir,recipe_name,outputDir);
      }      
  }
  
  fclose(idx);
  closedir(d);
  return 0;
}

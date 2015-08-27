/*
  (C) Paul Gardner-Stephen 2012-5.
  * 
  * CREATE specification stripped file from a Magpi XHTML form
  * Generate .recipe and .template files
  */

/*
  Copyright (C) 2012-5 Paul Gardner-Stephen
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <expat.h>
#ifdef ANDROID
#include <jni.h>
#include <android/log.h>
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "libsmac", __VA_ARGS__))
#endif

char *implied_meta_fields =
  "userid:string:0:0:0\n"
  "accesstoken:string:0:0:0\n"
  "formid:string:0:0:0\n"
  "lastsubmittime:magpitimestamp:0:0:0\n"
  "endrecordtime:magpitimestamp:0:0:0\n"
  "startrecordtime:magpitimestamp:0:0:0\n"
  "version:string:0:0:0\n"
  "uuid:magpiuuid:0:0:0\n"
  "latitude:float:-90:90:0\n"
  "longitude:float:-200:200:0\n";
char *implied_meta_fields_template =
  "<userid>$userid$</userid>\n"
  "<accesstoken>$accesstoken$</accesstoken>\n"
  "<formid>$formid$</formid>\n"
  "<lastsubmittime>$lastsubmittime$</lastsubmittime>\n"
  "<endrecordtime>$endrecordtime$</endrecordtime>\n"
  "<startrecordtime>$startrecordtime$</startrecordtime>\n"
  "<version>$version$</version>\n"
  "<uuid>$uuid$</uuid>\n"
  "<geotag>\n"
  "  <longitude>$longitude$</longitude>\n"
  "  <latitude>$latitude$</latitude>\n"
  "</geotag>\n";

char *strgrow(char *in, char *new)
{
  char *out = malloc(strlen(in)+strlen(new)+1);
  snprintf(out,strlen(in)+strlen(new)+1,"%s%s",in,new);
  free(in);
  return out;
}

int xhtmlToRecipe(char *xmltext,int size,char *formname,char *formversion,
		  char *recipetext,int *recipeLen,
		  char *templatetext,int *templateLen);


#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

//TODO 30.05.2014 	: Handle if there are two fields with same name
//		            : Add tests
//		            : Improve constraints work
//
//Creation specification stripped file from ODK XML
//FieldName:Type:Minimum:Maximum:Precision,Select1,Select2,...,SelectN

char     *xhtmlFormName = "", *xhtmlFormVersion = "";

char    *xhtml2template[1024];
int      xhtml2templateLen = 0;
char    *xhtml2recipe[1024];
int      xhtml2recipeLen = 0;

int      xhtml_in_instance = 0;

char    *selects[1024];
int      xhtmlSelectsLen = 0;
char    *xhtmlSelectElem = NULL;
int      xhtmlSelectFirst = 1;
int      xhtml_in_value = 0;

#define MAXCHARS 1000000

char temp[1024];

void
start_xhtml(void *data, const char *el, const char **attr) //This function is called  by the XML library each time it sees a new tag 
{   
  char    *node_name = "", *node_type = "", *node_constraint = "", *str = "";
  int     i ;
  
  if (xhtml_in_instance) { // We are between <instance> tags, so we want to get everything to create template file
    str = calloc (4096, sizeof(char*));
    strcpy (str, "<");
    strcat (str, el);
    for (i = 0; attr[i]; i += 2) { 
      strcat (str, " ");
      strcat (str, attr[i]);
      strcat (str, "=\"");
      strcat (str, attr[i+1]);
      strcat (str, "\"");
    }
    strcat (str, ">");
    strcat(str,"$"); strcat(str,el); strcat(str,"$");
    xhtml2template[xhtml2templateLen++] = str;
  }

  //Looking for bind elements to create the recipe file
  else if ((!strcasecmp("bind",el))||(!strcasecmp("xf:bind",el))) 
    {
      for (i = 0; attr[i]; i += 2) //Found a bind element, look for attributes
	{ 	    
	  //Looking for attribute nodeset
	  if (!strncasecmp("nodeset",attr[i],strlen("nodeset"))) {
	    char *last_slash = strrchr(attr[i+1], '/');
	    node_name = strdup(last_slash+1);
	  }
	    
	  //Looking for attribute type
	  if (!strncasecmp("type",attr[i],strlen("type"))) {
	    if (!strcasecmp(attr[i+1],"xsd:dropdown")) {
	      // Dropdown is a synonym for select1 (multiple choice)
	      node_type = strdup("select1");
	    } else if (!strcasecmp(attr[i+1],"xsd:radio")) {
	      // So is radio
	      node_type = strdup("select1");
	    } else if (!strcasecmp(attr[i+1],"xsd:checkbox")) {
	      // ... and checkbox (which is like radio, but allows multiple selections)
	      node_type = strdup("selectn");
	    } else {
	      const char *attribute=attr[i+1];
	      // Skip "XXX:" prefixes on types
	      if (strstr(attribute,":")) attribute=strstr(attribute,":")+1;
	      node_type  = strdup(attribute);
	    }
	  }
			
	  //Looking for attribute constraint
	  if (!strncasecmp("constraint",attr[i],strlen("constraint"))) {
	    node_constraint  = strdup(attr[i+1]);
	  }
	}
		
      //Now we got node_name, node_type, node_constraint
      //Lets build output
      fprintf(stderr,"Parsing field %s:%s\n", node_name,node_type);  

      if ((!strcasecmp(node_type,"select"))
	  ||(!strcasecmp(node_type,"select1"))
	  ) // Select, special case we need to wait later to get all informations (ie the range)
	{
	  snprintf(temp,1024,"%s:enum:0:0:0:",node_name);	 
	  selects[xhtmlSelectsLen] = strdup(temp);
	  xhtmlSelectsLen++;
	}
      else if (!strcasecmp(node_type,"selectn")) // multiple-choice checkbox (allows multiple selections at the same time)
	{
	  snprintf(temp,1024,"%s:multi:0:0:0:",node_name);
	  selects[xhtmlSelectsLen] = strdup(temp);
	  xhtmlSelectsLen++;	  
	}
      else if ((!strcasecmp(node_type,"decimal"))
	       ||(!strcasecmp(node_type,"integer"))
	       ||(!strcasecmp(node_type,"int"))) // Integers and decimal
        {
	  fprintf(stderr,"Parsing INT field %s:%s\n", node_name,node_type);  
	  snprintf(temp,1024,"%s:%s",node_name,node_type);
	  xhtml2recipe[xhtml2recipeLen] = strdup(temp);
            
	  if (strlen(node_constraint)) {
	    char *ptr = node_constraint;
	    int a, b;
		    
	    //We look for 0 to 2 digits
	    while( ! isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
	    a = atoi(ptr);
	    while( isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
	    while( ! isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
	    b = atoi(ptr);
	    if (b<=a) b=a+999;
	    fprintf(stderr,"%s:%s:%d:%d:0\n",
		    node_name,node_type,
		    MIN(a, b), MAX(a, b));
	    snprintf(temp,1024,"%s:%s:%d:%d:0",node_name,node_type,MIN(a,b),MAX(a,b));
	    free(xhtml2recipe[xhtml2recipeLen]);
	    xhtml2recipe[xhtml2recipeLen] = strdup(temp);

	  } else {
	    // Default to integers being in the range 0 to 999.
	    snprintf(temp,1024,"%s:%s:0:999:0",node_name,node_type);
	    free(xhtml2recipe[xhtml2recipeLen]);
	    xhtml2recipe[xhtml2recipeLen] = strdup(temp);
	  }
	  xhtml2recipeLen++;
		  
	}
      else if (strcasecmp(node_type,"binary")) // All others type except binary (ignore binary fields in succinct data)
        {
	  if (!strcasecmp(node_name,"instanceID")) {
	    snprintf(temp,1024,"%s:uuid",node_name);
	    xhtml2recipe[xhtml2recipeLen] = strdup(temp);
	  }else{    
	    printf("xhtml2recipeLen = %d\n",xhtml2recipeLen);
	    
	    snprintf(temp,1024,"%s:%s",node_name,node_type);
	    xhtml2recipe[xhtml2recipeLen] = strdup(temp);
	  }
	  snprintf(temp,1024,"%s:0:0:0",xhtml2recipe[xhtml2recipeLen]);
	  free(xhtml2recipe[xhtml2recipeLen]);
	  xhtml2recipe[xhtml2recipeLen] = strdup(temp);
	  xhtml2recipeLen++;
	}
    }
    
  //Now look for selects specifications, we wait until to find a select node
  else if ((!strcasecmp("xf:select1",el))||(!strcasecmp("xf:select",el))) 
    {
      for (i = 0; attr[i]; i += 2) //Found a select element, look for attributes
        { 
	  if (!strcasecmp("bind",attr[i])) {
	    const char *last_slash = strrchr(attr[i+1], '/');
	    // Allow for non path-indicated bindings in XHTML forms
	    if (!last_slash) last_slash=attr[i+1]; else last_slash++;
	    printf("Found multiple-choice selection definition '%s'\n",last_slash);
	    node_name  = strdup(last_slash);
	    xhtmlSelectElem  = node_name;
	    xhtmlSelectFirst = 1; 
	  }
        }
    }
    
  //We are in a select node and we need to find a value element
  else if ((xhtmlSelectElem)&&((!strcasecmp("value",el))||(!strcasecmp("xf:value",el)))) 
    {
      xhtml_in_value = 1;
    }
    
  //We reached the start of the data in the instance, so start collecting fields
  else if (!strcasecmp("data",el)) 
    {
      xhtml_in_instance = 1;
    }
  else if (!strcasecmp("xf:model",el))
    {
      // Form name is the id attribute of the xf:model tag
      for (i = 0; attr[i]; i += 2) { 
	if (!strcasecmp("id",attr[i])) {
	  xhtmlFormName  = strdup(attr[i+1]);
	}
	if (!strcasecmp("dd:formid",attr[i])) {
	  xhtmlFormVersion = strdup(attr[i+1]);
	}
      }
    }
     
}

void characterdata_xhtml(void *data, const char *el, int len)
//This function is called  by the XML library each time we got data in a tag
{
  int i;
   
    
  if ( xhtmlSelectElem && xhtml_in_value) 
    {
      char x[len+2]; //el is not null terminated, so copy it to x and add \0
      memcpy(x,el,len);
      memcpy(x+len,"",1);
    
      for (i = 0; i<xhtmlSelectsLen; i++)
        { 
	  if (!strncasecmp(xhtmlSelectElem,selects[i],strlen(xhtmlSelectElem))) {
	    if (xhtmlSelectFirst) {
	      xhtmlSelectFirst = 0; 
	    }else{
	      selects[i] = strgrow (selects[i] ,",");
	    }
	    selects[i] = strgrow (selects[i] ,x);
	  }
        }
    }
    
    		
	

}

void end_xhtml(void *data, const char *el) //This function is called  by the XML library each time it sees an ending of a tag
{
  char *str = "";

  if (xhtmlSelectElem && ((!strcasecmp("xf:select1",el))||(!strcasecmp("xf:select",el))))  {
    xhtmlSelectElem = NULL;
  }
    
  if (xhtml_in_value && ((!strcasecmp("value",el))||(!strcasecmp("xf:value",el))))  {
    xhtml_in_value = 0;
  }
    
  if (xhtml_in_instance &&(!strcasecmp("data",el))) {
    xhtml_in_instance = 0;
  }
    
  if (xhtml_in_instance) { // We are between <instance> tags, we want to get everything
    str = calloc (4096, sizeof(char*));
    strcpy (str, "</");
    strcat (str, el);
    strcat (str, ">\n");
    xhtml2template[xhtml2templateLen++] = str;
  }
}  

int appendto(char *out,int *used,int max,char *stuff);

int xhtml_recipe_create(char *input)
{
  FILE *f=fopen(input,"r");
  char filename[512] = "";
  size_t size;
  char *xmltext;
  
  if (!f) {
    fprintf(stderr,"Could not read XHTML file '%s'\n",input);
    return -1;
  }
    
  //Open Xml File
  xmltext = malloc(MAXCHARS);
  size = fread(xmltext, sizeof(char), MAXCHARS, f);

  char recipetext[65536];
  int recipeLen=65536;

  char templatetext[65536];
  int templateLen=65536;

  char formname[1024];
  char formversion[1024];
    
  int r = xhtmlToRecipe(xmltext,size,formname,formversion,
			recipetext,&recipeLen,
			templatetext,&templateLen);

  if (r) {
    fprintf(stderr,"xhtml2recipe failed\n");
    return(1);
  }

  //Create output for RECIPE
  snprintf(filename,512,"%s.recipe",formversion);
  fprintf(stderr,"Writing recipe to '%s'\n",filename);
  f=fopen(filename,"w");
  fprintf(f,"%s",recipetext);
  fclose(f);

  //Create output for TEMPLATE
  snprintf(filename,512,"%s.template",formversion);
  fprintf(stderr,"Writing template to '%s'\n",filename);
  f=fopen(filename,"w");
  fprintf(f,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<form>\n<meta>\n");
  fprintf(f,"%s",implied_meta_fields_template);
  fprintf(f,"</meta>\n<data>\n");
  fprintf(f,"%s",templatetext);
  fprintf(f,"</data>\n</form>\n");
  fclose(f);

  return 0;
}
    
int xhtmlToRecipe(char *xmltext,int size,char *formname,char *formversion,
		  char *recipetext,int *recipeLen,
		  char *templatetext,int *templateLen)
{
  XML_Parser parser;
  int i ;

  // Reset parser state.
  xhtml2templateLen = 0;
  xhtml2recipeLen = 0;
  xhtml_in_instance = 0;
  xhtmlSelectsLen = 0;
  xhtmlSelectElem = NULL;
  xhtmlSelectFirst = 1;
  xhtml_in_value = 0;
  
  //ParserCreation
  parser = XML_ParserCreate(NULL);
  if (parser == NULL) {
    fprintf(stderr, "ERROR: %s: Parser not created\n",__FUNCTION__);
    return (1);
  }
    
  // Tell expat to use functions start() and end() each times it encounters the start or end of an element.
  XML_SetElementHandler(parser, start_xhtml, end_xhtml);    
  // Tell expat to use function characterData()
  XML_SetCharacterDataHandler(parser,characterdata_xhtml);

#ifdef ANDROID
  LOGI("About to call XML_Parse() with %d bytes of input",size);
#endif
  
  //Parse Xml Text
  if (XML_Parse(parser, xmltext, strlen(xmltext), XML_TRUE) ==
      XML_STATUS_ERROR) {
#ifdef ANDROID
    LOGI("XML_Parse() failed");
#endif
    fprintf(stderr,
	    "ERROR: %s: Cannot parse , file may be too large or not well-formed XML\n",
	    __FUNCTION__);
    return (1);
  }
  
  // Build recipe output
  int recipeMaxLen=*recipeLen;
  *recipeLen=0;

  // Start with implied fields
  strcpy(recipetext,implied_meta_fields);
  *recipeLen=strlen(recipetext);

  // Now add explicit fields
  for(i=0;i<xhtml2recipeLen;i++){
    if (appendto(recipetext,recipeLen,recipeMaxLen,xhtml2recipe[i])) {
      fprintf(stderr,"ERROR: %s:%d: %s() recipe text overflow.\n",
	      __FILE__,__LINE__,__FUNCTION__);      
      return -1;
    }
    if (appendto(recipetext,recipeLen,recipeMaxLen,"\n")) {
      fprintf(stderr,"ERROR: %s:%d: %s() recipe text overflow.\n",
	      __FILE__,__LINE__,__FUNCTION__);      
      return -1;
    }
  }
  for(i=0;i<xhtmlSelectsLen;i++){
    if (appendto(recipetext,recipeLen,recipeMaxLen,selects[i])) {
      fprintf(stderr,"ERROR: %s:%d: %s() recipe text overflow.\n",
	      __FILE__,__LINE__,__FUNCTION__);      
      return -1;
    }
    if (appendto(recipetext,recipeLen,recipeMaxLen,"\n")) {
      fprintf(stderr,"ERROR: %s:%d: %s() recipe text overflow.\n",
	      __FILE__,__LINE__,__FUNCTION__);      
      return -1;
    }
  }
  
  int templateMaxLen=*templateLen;
  *templateLen=0;
  for(i=0;i<xhtml2templateLen;i++){
    if (appendto(templatetext,templateLen,templateMaxLen,xhtml2template[i])) {
      fprintf(stderr,"ERROR: %s:%d: %s() template text overflow.\n",
	      __FILE__,__LINE__,__FUNCTION__);      
      return -1;
    }    
  }

  snprintf(formname,1024,"%s",xhtmlFormName);
  snprintf(formversion,1024,"%s",xhtmlFormVersion);
#ifdef ANDROID
  LOGI("xhtmlToRecipe(): formname='%s'",formname);
  LOGI("xhtmlToRecipe(): formversion='%s'",formversion);
#endif
  
  XML_ParserFree(parser);
  fprintf(stderr, "\n\nSuccessfully parsed %i characters !\n", (int)size);
  fprintf(stderr,"xhtmlFormName=%s, xhtmlFormVersion=%s\n",
	  xhtmlFormName,xhtmlFormVersion);

#ifdef ANDROID
  LOGI("XML_Parse() succeeded, xhtml2recipeLen = %d, recipeLen=%d",
       xhtml2recipeLen,*recipeLen);
#endif
  
  return (0);
}

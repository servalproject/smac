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
#include <expat.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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
	    node_name  = calloc (strlen(last_slash), sizeof(char*));
	    memcpy (node_name, last_slash+1, strlen(last_slash));
	  }
	    
	  //Looking for attribute type
	  if (!strncasecmp("type",attr[i],strlen("type"))) {
	      
	    node_type  = calloc (strlen(attr[i + 1]), sizeof(char*));
	    memcpy (node_type, attr[i + 1], strlen(attr[i + 1]));
	  }
			
	  //Looking for attribute constraint
	  if (!strncasecmp("constraint",attr[i],strlen("constraint"))) {
	    node_constraint  = calloc (strlen(attr[i + 1]), sizeof(char*));
	    memcpy (node_constraint, attr[i + 1], strlen(attr[i + 1]));
	  }
	}
		
      //Now we got node_name, node_type, node_constraint
      //Lets build output        
      if ((!strcasecmp(node_type,"select"))||(!strcasecmp(node_type,"select1"))) // Select, special case we need to wait later to get all informations (ie the range)
	{
	  selects[xhtmlSelectsLen] = node_name;
	  strcat (selects[xhtmlSelectsLen] ,":");
	  strcat (selects[xhtmlSelectsLen] ,"enum");
	  strcat (selects[xhtmlSelectsLen] ,":0:0:0:");
	  xhtmlSelectsLen++;
	} 
      else if ((!strcasecmp(node_type,"decimal"))||(!strcasecmp(node_type,"int"))) // Integers and decimal
        {
	  //printf("%s:%s", node_name,node_type);  
	  xhtml2recipe[xhtml2recipeLen] = node_name;
	  strcat (xhtml2recipe[xhtml2recipeLen] ,":");
	  strcat (xhtml2recipe[xhtml2recipeLen] ,node_type);
            
	  if (strlen(node_constraint)) {
	    char *ptr = node_constraint;
	    char str[15];
	    int a, b;
		    
	    //We look for 0 to 2 digits
	    while( ! isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
	    a = atoi(ptr);
	    while( isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
	    while( ! isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
	    b = atoi(ptr);
	    if (b<=a) b=a+999;
	    //printf(":%d:%d:0", MIN(a, b), MAX(a, b));
	    strcat (xhtml2recipe[xhtml2recipeLen] ,":");
	    sprintf(str, "%d", MIN(a, b));
	    strcat (xhtml2recipe[xhtml2recipeLen] ,str);
	    strcat (xhtml2recipe[xhtml2recipeLen] ,":");
	    sprintf(str, "%d", MAX(a, b));
	    strcat (xhtml2recipe[xhtml2recipeLen] ,str);
	    strcat (xhtml2recipe[xhtml2recipeLen] ,":0");
	  } else {
	    // Default to integers being in the range 0 to 999.
	    strcat (xhtml2recipe[xhtml2recipeLen] ,":0:999:0");
	  }
	  xhtml2recipeLen++;
		  
	}
      else if (strcasecmp(node_type,"binary")) // All others type except binary (ignore binary fields in succinct data)
        {
	  if (!strcasecmp(node_name,"instanceID")) {
	    xhtml2recipe[xhtml2recipeLen] = node_name;
	    strcat (xhtml2recipe[xhtml2recipeLen] ,":");
	    strcat (xhtml2recipe[xhtml2recipeLen] ,"uuid");
	  }else{    
	    xhtml2recipe[xhtml2recipeLen] = node_name;
	    strcat (xhtml2recipe[xhtml2recipeLen] ,":");
	    strcat (xhtml2recipe[xhtml2recipeLen] ,node_type);
	  }
	  strcat (xhtml2recipe[xhtml2recipeLen] ,":0:0:0");
	  xhtml2recipeLen++;
	}
    }
    
  //Now look for selects specifications, we wait until to find a select node
  else if ((!strcasecmp("select1",el))||(!strcasecmp("select",el))) 
    {
      for (i = 0; attr[i]; i += 2) //Found a select element, look for attributes
        { 
	  if (!strcasecmp("ref",attr[i])) {
	    char *last_slash = strrchr(attr[i+1], '/');
	    node_name  = calloc (strlen(last_slash), sizeof(char*));
	    memcpy (node_name, last_slash+1, strlen(last_slash));
	  }
        }
      xhtmlSelectElem  = calloc (strlen(node_name), sizeof(char*));
      memcpy (xhtmlSelectElem, node_name, strlen(node_name));
      xhtmlSelectFirst = 1; 
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
	  xhtmlFormName  = calloc (strlen(el), sizeof(char*));
	  memcpy (xhtmlFormName, attr[i+1], strlen(attr[i+1]));
	}
	if (!strcasecmp("dd:formid",attr[i])) {
	  xhtmlFormVersion = calloc (strlen(attr[i+1]), sizeof(char*));
	  memcpy (xhtmlFormVersion, attr[i+1], strlen(attr[i+1]));
	}
      }
    }
     
    
}

void characterdata_xhtml(void *data, const char *el, int len) //This function is called  by the XML library each time we got data in a tag
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
	      strcat (selects[i] ,",");
	    }
	    strcat (selects[i] ,x);
	  }
        }
    }
    
    		
	

}

void end_xhtml(void *data, const char *el) //This function is called  by the XML library each time it sees an ending of a tag
{
  char *str = "";
    
  if (xhtmlSelectElem && ((!strcasecmp("select1",el))||(!strcasecmp("select",el))))  {
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
    strcat (str, ">");
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

  char recipetext[4096];
  int recipeLen=4096;

  char templatetext[16384];
  int templateLen=16384;

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
  strcpy(filename,formname);
  strcat(filename,".");
  strcat(filename,formversion);
  strcat(filename,".recipe");
  fprintf(stderr,"Writing recipe to '%s'\n",filename);
  f=fopen(filename,"w");
  fprintf(f,"%s",recipetext);
  fclose(f);

  //Create output for TEMPLATE
  strcpy(filename,formname);
  strcat(filename,".");
  strcat(filename,formversion);
  strcat(filename,".template");
  fprintf(stderr,"Writing template to '%s'\n",filename);
  f=fopen(filename,"w");
  fprintf(f,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<form>\n<meta>\n");
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
  
  //ParserCreation
  parser = XML_ParserCreate(NULL);
  if (parser == NULL) {
    fprintf(stderr, "Parser not created\n");
    return (1);
  }
    
  // Tell expat to use functions start() and end() each times it encounters the start or end of an element.
  XML_SetElementHandler(parser, start_xhtml, end_xhtml);    
  // Tell expat to use function characterData()
  XML_SetCharacterDataHandler(parser,characterdata_xhtml);
  
  //Parse Xml Text
  if (XML_Parse(parser, xmltext, strlen(xmltext), XML_TRUE) ==
      XML_STATUS_ERROR) {
    fprintf(stderr,
	    "Cannot parse , file may be too large or not well-formed XML\n");
    return (1);
  }
  
  // Build recipe output
  int recipeMaxLen=*recipeLen;
  *recipeLen=0;
  
  for(i=0;i<xhtml2recipeLen;i++){
    if (appendto(recipetext,recipeLen,recipeMaxLen,xhtml2recipe[i])) return -1;
    if (appendto(recipetext,recipeLen,recipeMaxLen,"\n")) return -1;
  }
  for(i=0;i<xhtmlSelectsLen;i++){
    if (appendto(recipetext,recipeLen,recipeMaxLen,selects[i])) return -1;
    if (appendto(recipetext,recipeLen,recipeMaxLen,"\n")) return -1;
  }
  
  int templateMaxLen=*templateLen;
  *templateLen=0;
  for(i=0;i<xhtml2templateLen;i++){
    if (appendto(templatetext,templateLen,templateMaxLen,xhtml2template[i]))
      return -1;
    if (appendto(templatetext,templateLen,templateMaxLen,"\n")) return -1;
  }

  snprintf(formname,1024,"%s",xhtmlFormName);
  snprintf(formversion,1024,"%s",xhtmlFormVersion);
  
  XML_ParserFree(parser);
  fprintf(stderr, "\n\nSuccessfully parsed %i characters !\n", (int)size);
  fprintf(stderr,"xhtmlFormName=%s, xhtmlFormVersion=%s\n",
	  xhtmlFormName,xhtmlFormVersion);
  return (0);
}

/*
(C) Paul Gardner-Stephen 2012-4.
* 
* CREATE specification stripped file from an ODK XML form
* Generate .recipe and .template files
*/

/*
Copyright (C) 2012-4 Paul Gardner-Stephen
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

int xmlToRecipe(char *xmltext,int size,char *formname,char *formversion,
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

char     *formName = "", *formVersion = "";

char    *xml2template[1024];
int      xml2templateLen = 0;
char    *xml2recipe[1024];
int      xml2recipeLen = 0;


int      in_instance = 0;
int      in_instance_first = 0;


char    *selects[1024];
int      selectsLen = 0;
char    *selectElem = NULL;
int      selectFirst = 1;
int      in_value = 0;



#define MAXCHARS 1000000



void
start(void *data, const char *el, const char **attr) //This function is called  by the XML library each time it sees a new tag 
{   
    char    *node_name = "", *node_type = "", *node_constraint = "", *str = "";
	int     i ;
    
    if (in_instance) { // We are between <instance> tags, so we want to get everything to create template file
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
        xml2template[xml2templateLen++] = str;
        
        if (in_instance_first) { // First node since we are in instance, it's the Form Name that we want to get
            in_instance_first = 0;
            for (i = 0; attr[i]; i += 2) { 
                if (!strcasecmp("version",attr[i])) {
		  formVersion = strdup(attr[i+1]);
                }
                if (!strcasecmp("id",attr[i])) {
		  formName = strdup(attr[i+1]);
                }
            }
        }

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
            selects[selectsLen] = node_name;
            strcat (selects[selectsLen] ,":");
            strcat (selects[selectsLen] ,"enum");
            strcat (selects[selectsLen] ,":0:0:0:");
            selectsLen++;
		} 
	else if ((!strcasecmp(node_type,"decimal"))||(!strcasecmp(node_type,"int"))) // Integers and decimal
        {
            //printf("%s:%s", node_name,node_type);  
            xml2recipe[xml2recipeLen] = node_name;
            strcat (xml2recipe[xml2recipeLen] ,":");
            strcat (xml2recipe[xml2recipeLen] ,node_type);
            
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
                strcat (xml2recipe[xml2recipeLen] ,":");
                sprintf(str, "%d", MIN(a, b));
                strcat (xml2recipe[xml2recipeLen] ,str);
                strcat (xml2recipe[xml2recipeLen] ,":");
                sprintf(str, "%d", MAX(a, b));
                strcat (xml2recipe[xml2recipeLen] ,str);
                strcat (xml2recipe[xml2recipeLen] ,":0");
            } else {
	        // Default to integers being in the range 0 to 999.
                strcat (xml2recipe[xml2recipeLen] ,":0:999:0");
            }
            xml2recipeLen++;
		  
		}
	else if (strcasecmp(node_type,"binary")) // All others type except binary (ignore binary fields in succinct data)
        {
            if (!strcasecmp(node_name,"instanceID")) {
                xml2recipe[xml2recipeLen] = node_name;
                strcat (xml2recipe[xml2recipeLen] ,":");
                strcat (xml2recipe[xml2recipeLen] ,"uuid");
            }else{    
                xml2recipe[xml2recipeLen] = node_name;
                strcat (xml2recipe[xml2recipeLen] ,":");
                strcat (xml2recipe[xml2recipeLen] ,node_type);
            }
            strcat (xml2recipe[xml2recipeLen] ,":0:0:0");
            xml2recipeLen++;
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
        selectElem  = calloc (strlen(node_name), sizeof(char*));
		memcpy (selectElem, node_name, strlen(node_name));
        selectFirst = 1; 
    }
    
    //We are in a select node and we need to find a value element
    else if ((selectElem)&&((!strcasecmp("value",el))||(!strcasecmp("xf:value",el)))) 
    {
        in_value = 1;
    }
    
    //We reached an instance element, means we have to take everything in it for the .template
    else if (!strcasecmp("instance",el)) 
    {
        in_instance = 1;
        in_instance_first = 1;
    }
     
    
}

void characterdata(void *data, const char *el, int len) //This function is called  by the XML library each time we got data in a tag
{
	int i;
   
    
    if ( selectElem && in_value) 
    {
        char x[len+2]; //el is not null terminated, so copy it to x and add \0
        memcpy(x,el,len);
        memcpy(x+len,"",1);
    
        for (i = 0; i<selectsLen; i++)
        { 
			if (!strncasecmp(selectElem,selects[i],strlen(selectElem))) {
                    if (selectFirst) {
                        selectFirst = 0; 
                    }else{
                        strcat (selects[i] ,",");
                    }
                    strcat (selects[i] ,x);
			}
        }
    }
    
    		
	

}

void end(void *data, const char *el) //This function is called  by the XML library each time it sees an ending of a tag
{
    char *str = "";
    
    if (selectElem && ((!strcasecmp("select1",el))||(!strcasecmp("select",el))))  {
       selectElem = NULL;
    }
    
    if (in_value && ((!strcasecmp("value",el))||(!strcasecmp("xf:value",el))))  {
       in_value = 0;
    }
    
    if (in_instance &&(!strcasecmp("instance",el))) {
        in_instance = 0;
    }
    
     if (in_instance) { // We are between <instance> tags, we want to get everything
        str = calloc (4096, sizeof(char*));
        strcpy (str, "</");
        strcat (str, el);
        strcat (str, ">");
        xml2template[xml2templateLen++] = str;
    }
}  

int appendto(char *out,int *used,int max,char *stuff)
{
  int l = strlen(stuff);
  if (((*used)+l)>=max) return -1;
  strcpy(&out[*used],stuff);
  *used+=l;
  return 0;
}

int recipe_create(char *input)
{
    FILE *f=fopen(input,"r");
    char filename[512] = "";
    size_t size;
    char *xmltext;

    if (!f) {
      fprintf(stderr,"Could not read XML file '%s'\n",input);
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
    
    int r = xmlToRecipe(xmltext,size,formname,formversion,
			recipetext,&recipeLen,
			templatetext,&templateLen);

    if (r) {
      fprintf(stderr,"xml2recipe failed\n");
      return(1);
    }

    //Create output for RECIPE
    snprintf(filename,512,"%s.%s.recipe",formname,formversion);
    fprintf(stderr,"Writing recipe to '%s'\n",filename);
    f=fopen(filename,"w");
    fprintf(f,"%s",recipetext);
    fclose(f);

    //Create output for TEMPLATE
    snprintf(filename,512,"%s.%s.template",formname,formversion);
    fprintf(stderr,"Writing template to '%s'\n",filename);
    f=fopen(filename,"w");
    fprintf(f,"%s",templatetext);
    fclose(f);

    return 0;
}
    
int xmlToRecipe(char *xmltext,int size,char *formname,char *formversion,
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
  XML_SetElementHandler(parser, start, end);    
  // Tell expat to use function characterData()
  XML_SetCharacterDataHandler(parser,characterdata);
  
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
  
  for(i=0;i<xml2recipeLen;i++){
    if (appendto(recipetext,recipeLen,recipeMaxLen,xml2recipe[i])) return -1;
    if (appendto(recipetext,recipeLen,recipeMaxLen,"\n")) return -1;
  }
  for(i=0;i<selectsLen;i++){
    if (appendto(recipetext,recipeLen,recipeMaxLen,selects[i])) return -1;
    if (appendto(recipetext,recipeLen,recipeMaxLen,"\n")) return -1;
  }
  
  int templateMaxLen=*templateLen;
  *templateLen=0;
  for(i=0;i<xml2templateLen;i++){
    if (appendto(templatetext,templateLen,templateMaxLen,xml2template[i]))
      return -1;
    if (appendto(templatetext,templateLen,templateMaxLen,"\n")) return -1;
  }

  snprintf(formname,1024,"%s",formName);
  snprintf(formversion,1024,"%s",formVersion);
  
  XML_ParserFree(parser);
  fprintf(stderr, "\n\nSuccessfully parsed %i characters !\n", (int)size);
  fprintf(stderr,"formName=%s, formVersion=%s\n",
	  formName,formVersion);
  return (0);
}

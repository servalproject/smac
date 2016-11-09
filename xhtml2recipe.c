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

// xhtml2recipe.c Handling Recipes 2

#include <stdbool.h>
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

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MAXSUBFORMS 10
#define MAXCHARS 1000000

typedef struct node_recipe {
	bool is_subform_node; //false if it's a subform recipe, true if not
	int recipeLen; //Length of the recipe
	char formname[1024]; //Form name
	char formversion[1024]; //Form ID/version
	char recipetext[65536]; //Text of the recipe
	char *xhtml2recipe[1024]; //Temporary buffer to parse the recipe binds
	int xhtml2recipeLen; //Length of xhtml2recipe
	char *selects[1024]; //Temporary buffer to parse the recipe selects
	int xhtmlSelectsLen; //Length of xhtmlSelectsLen
	struct node_recipe *next;
}recipe_node;

typedef struct node_tag {
	char tagname[1024]; //Name of the tag (question on the form)
	char formversion[1024]; //Form version of the parent form of the tag
	struct node_tag *next;
}tag_node;

typedef struct node_subform {
	char subformid[1024]; //ID of the subform
	//char* nodeset;
	struct node_subform *next;
	struct node_subform *prev;
}subform_node;

int xhtmlToRecipe(char *xmltext,int size,
		  char *templatetext,int *templateLen);

char *strgrow(char *in, char *new);

subform_node* create_list_subforms(const char *formversion);
tag_node* create_list_tags(const char* tagname, const char* formversion);
recipe_node* create_list_recipes(const char *formversion);

subform_node* add_to_list_subforms(const char *formversion);
tag_node* add_to_list_tags(const char* tagname, const char* formversion, bool add_to_end);
recipe_node* add_to_list_recipes(const char* formversion, bool add_to_end);

tag_node* search_in_list_tags(const char* tagname, tag_node **prev);
recipe_node* search_in_list_recipes(const char* formversion, recipe_node **prev);

void print_list_tags(void);
void print_list_recipes(void);

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

char *implied_meta_fields_subforms =
	"formid:string:0:0:0\n"
	"question:string:0:0:0\n"
	"uuid:magpiuuid:0:0:0\n";

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

char *implied_meta_fields_template_subform =
"<formid>$formid$</formid>\n"
"<question>$question$</question>\n";

char *implied_data_fields_template_subform =
"<uuid>$uuid$</uuid>\n";

char *strgrow(char *in, char *new)
{
  char *out = malloc(strlen(in)+strlen(new)+1);
  snprintf(out,strlen(in)+strlen(new)+1,"%s%s",in,new);
  free(in);
  return out;
}

recipe_node *head_r = NULL;
recipe_node *curr_r = NULL;

tag_node *head_t = NULL;
tag_node *curr_t = NULL;

subform_node *head_s = NULL;
subform_node *curr_s = NULL;

//TODO 30.05.2014 	: Handle if there are two fields with same name
//		            : Add tests
//		            : Improve constraints work
//
//Creation specification stripped file from ODK XML
//FieldName:Type:Minimum:Maximum:Precision,Select1,Select2,...,SelectN

char     *xhtmlFormName = "", *xhtmlFormVersion = "";

char    *xhtml2template[1024];
int      xhtml2templateLen = 0;

char    *xhtmlSelectElem = NULL;

// Stuff that is concerning the form and not the recipes
int      xhtmlSelectFirst = 1;
int      xhtml_in_value = 0;
int      xhtml_in_instance = 0;
int		 xhtml_in_subform = 0;

char temp[1024];

char	*subform_tags[MAXSUBFORMS];

void
start_xhtml(void *data, const char *el, const char **attr) //This function is called  by the XML library each time it sees a new tag 
{   

  char    *node_name = "", *node_type = "", *node_constraint = "";
  char	  *str = "", *form_concat = "", *temp_type = "";

  int     i ;
  int     k ;
  //Only to debug : Print each element
  //printf("(start_xhtml) element is: %s \n",el);
  
  if(attr[0]&&!strcmp(attr[0],"dd:formid")) {
	  add_to_list_recipes(attr[1], true);
	  curr_r->is_subform_node = false;
	  add_to_list_subforms(attr[1]);
	  printf("(start_xhtml) Form start ! : attr[0] = %s, curr_s -> formversion = %s \n",attr[0],curr_s->subformid);
  }

  if (xhtml_in_instance) { // We are between <instance> tags, so we want to get everything to create template file


		  if(attr[0]&&!strcmp(attr[0],"dd:subformid")) {

			    //linked lists stuff to structure well the data
			    add_to_list_recipes(attr[1], true);
			    curr_r->is_subform_node = true;
			    add_to_list_subforms(attr[1]);
			    add_to_list_tags(el,attr[1],true);
			    //strcat(curr_s->nodeset,el);

		  	  	xhtml_in_subform++; //xhtml_in_subform represents the depth in subforms
		  	  	subform_tags[xhtml_in_subform] = (char *)el;
		  	  	printf("(start_xhtml) subform instance start ! : attr[0] = %s , curr_s -> formversion = %s \n",attr[0],curr_s->subformid);

		  	  	str = calloc (4096, sizeof(char*));
		  	  	strcat (str, "<dd:subform xmlns:dd=\"http://datadyne.org/javarosa\"> \n");
		  	  	strcat(str, "<meta> \n");
		  	  	strcat(str, implied_meta_fields_template_subform);
		  	  	strcat(str, "</meta> \n");
		  	  	strcat(str, "<data> \n");
		  	  	strcat(str, implied_data_fields_template_subform);

		  	  	xhtml2template[xhtml2templateLen++] = str;

		  } else {

			  	 if(curr_s != NULL) {
			  		 //linked lists stuff to structure well the data
			  		 add_to_list_tags(el,curr_s->subformid,true);
			  	 }

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

	    //linked list stuff to structure the data

	    //find the subform where this field is in
	    curr_t = search_in_list_tags(node_name,NULL);

	      for (k = 0; attr[k]; k += 2) //look for type attribute
		{
	    	  if(!strncasecmp("type",attr[k],strlen("type"))) {
	    		  printf("found temp type !! Temp type = %s \n",attr[k+1]);
	    		  temp_type = strdup(attr[k+1]);
	    	  }
		}

	    if(curr_t != NULL && strcasecmp(temp_type,"xsd:subform")) {
	    	//use the right recipe node in the linked list to parse info
	    	curr_r = search_in_list_recipes(curr_t->formversion,NULL);
	    }
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
	    } else if (!strcasecmp(attr[i+1],"xsd:subform")) {
		      // ... and subforms
	  	  	  form_concat = calloc (4096, sizeof(char*));
	  	  	  strcpy (form_concat, "subform/");
	  	  	  strcat (form_concat, curr_t->formversion);
	  	  	  node_name = strdup(form_concat);
	    	  node_type = strdup("subform");
	    }
	    else {
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
	  curr_r->selects[curr_r->xhtmlSelectsLen] = strdup(temp);
	  printf("curr_r->selects =%s",curr_r->selects[curr_r->xhtmlSelectsLen]);
	  curr_r->xhtmlSelectsLen++;
	}
      else if (!strcasecmp(node_type,"selectn")) // multiple-choice checkbox (allows multiple selections at the same time)
	{
	  snprintf(temp,1024,"%s:multi:0:0:0:",node_name);
	  curr_r->selects[curr_r->xhtmlSelectsLen] = strdup(temp);
	  curr_r->xhtmlSelectsLen++;
	}
      else if ((!strcasecmp(node_type,"decimal"))
	       ||(!strcasecmp(node_type,"integer"))
	       ||(!strcasecmp(node_type,"int"))) // Integers and decimal
        {
	  fprintf(stderr,"Parsing INT field %s:%s\n", node_name,node_type);  
	  snprintf(temp,1024,"%s:%s",node_name,node_type);

	  printf("---\n%s\n---\n",temp);

	  curr_r->xhtml2recipe[curr_r->xhtml2recipeLen] = strdup(temp);
	  printf("curr_r->xhtml2recipe =%s",curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);

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
	    free(curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);
	    curr_r->xhtml2recipe[curr_r->xhtml2recipeLen] = strdup(temp);
	    printf("curr_r->xhtml2recipe =%s",curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);
	  } else {
	    // Default to integers being in the range 0 to 999.
	    snprintf(temp,1024,"%s:%s:0:999:0",node_name,node_type);
		printf("---\n%s\n---\n",temp);

	    free(curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);
	    curr_r->xhtml2recipe[curr_r->xhtml2recipeLen] = strdup(temp);
	    printf("curr_r->xhtml2recipe =%s",curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);
	  }
	  curr_r->xhtml2recipeLen++;
	}
      else if (strcasecmp(node_type,"binary")/*&&strcasecmp(node_type,"subform")*/)
    	  // All others type except binary (ignore binary fields in succinct data)
    	  // We don't write subform binds,
    	  // the link between a form and its included subforms is easily made in the stripped file
    	  // We actually need to write it because the decompressor uses it
        {
	  if (!strcasecmp(node_name,"instanceID")) {
	    snprintf(temp,1024,"%s:uuid",node_name);
	    curr_r->xhtml2recipe[curr_r->xhtml2recipeLen] = strdup(temp);
	  }else{    
	    printf("curr_r->xhtml2recipeLen = %d\n",curr_r->xhtml2recipeLen);
	    
	    snprintf(temp,1024,"%s:%s",node_name,node_type);
		printf("---\n%s\n---\n",temp);
	    curr_r->xhtml2recipe[curr_r->xhtml2recipeLen] = strdup(temp);
	    printf("curr_r->xhtml2recipe =%s",curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);
	  }
	  snprintf(temp,1024,"%s:0:0:0",curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);
	  printf("---\n%s\n---\n",temp);
	  free(curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);
	  curr_r->xhtml2recipe[curr_r->xhtml2recipeLen] = strdup(temp);
	  printf("HIHIHIcurr_r->xhtml2recipe =%s \n",curr_r->xhtml2recipe[curr_r->xhtml2recipeLen]);
	  curr_r->xhtml2recipeLen++;
	}

    }

  //.recipe
    
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

	    //find the subform where this field is in
	    char* form_id_to_use = search_in_list_tags(xhtmlSelectElem,NULL)->formversion;

	    //use the right recipe node in the linked list to parse info
	    curr_r = search_in_list_recipes(form_id_to_use,NULL);
    
      for (i = 0; i<curr_r->xhtmlSelectsLen; i++)
        { 
	  if (!strncasecmp(xhtmlSelectElem,curr_r->selects[i],strlen(xhtmlSelectElem))) {
	    if (xhtmlSelectFirst) {
	      xhtmlSelectFirst = 0; 
	    }else{
	    	curr_r->selects[i] = strgrow (curr_r->selects[i] ,",");
	    }
	    curr_r->selects[i] = strgrow (curr_r->selects[i] ,x);
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
    
  if (	  (xhtml_in_instance&&xhtml_in_subform == 0)
		  ||
		  (xhtml_in_subform>0&&strcasecmp(subform_tags[xhtml_in_subform],el))
     )
  {

	  printf("Subform end ! don't write the element in the template \n");
	  str = calloc (4096, sizeof(char*));
	  strcpy (str, "</");
	  strcat (str, el);
	  strcat (str, ">\n");
  	  xhtml2template[xhtml2templateLen++] = str;
  }

  if(xhtml_in_subform && !strcasecmp(subform_tags[xhtml_in_subform],el)) {

	  printf("(end_xhtml) Subform_tags contains elements ! => Subform end ! Element is %s \n",el);
	  xhtml_in_subform--;
	  str = calloc (4096, sizeof(char*));
	  strcpy(str, "</data> \n");
	  strcat(str, "</dd:subform> \n");
	  xhtml2template[xhtml2templateLen++] = str;

	  //linked list stuff to structure the data well
	  curr_s = curr_s->prev;
	  printf("(start_xhtml) End of a subform/form ! curr_s -> formversion = %s \n",curr_s->subformid);
  }
}

int appendto(char *out,int *used,int max,char *stuff);

//Generate the recipe (Spec stripped data) and write it into .recipe and .template file
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

  char templatetext[65536];
  int templateLen=65536;

  int r = xhtmlToRecipe(xmltext,size,templatetext,&templateLen);

  if (r) {
    fprintf(stderr,"xhtml2recipe failed\n");
    return(1);
  }

  recipe_node *recipenode = head_r;

  print_list_recipes();

  printf("Finished xhtmlToRecipe parsing, now about to parse eveything in files ... \n");
  while(recipenode != NULL)
  {

	  //Create output for RECIPE
	  snprintf(filename,512,"%s.recipe",recipenode->formversion);
	  fprintf(stderr,"Writing recipe to '%s'\n",filename);
	  f=fopen(filename,"w");
	  fprintf(f,"%s",recipenode->recipetext);
	  fclose(f);

	  recipenode = recipenode->next;
  }

	  //Create output for TEMPLATE
	  snprintf(filename,512,"%s.template",head_r->formversion);
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
    
int xhtmlToRecipe(char *xmltext,int size,
		  char *templatetext,int *templateLen)
{
  XML_Parser parser;
  int i;
  // Reset parser state.
  xhtml2templateLen = 0;
  //xhtml2recipeLen = 0;
  xhtml_in_instance = 0;
  //xhtmlSelectsLen = 0;
  xhtmlSelectElem = NULL;
  xhtmlSelectFirst = 1;
  xhtml_in_value = 0;

  curr_r = head_r;

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

  while(curr_r != NULL)
  {
	  // Build recipe output
	    int recipeMaxLen = 65536;
	    curr_r->recipeLen=0;

	    // Start with implied fields
	    if(curr_r->is_subform_node == false) {
	    	strcpy(curr_r->recipetext,implied_meta_fields);
	    } else {
	    	strcpy(curr_r->recipetext,implied_meta_fields_subforms);
	    }
	    curr_r->recipeLen=strlen(curr_r->recipetext);

	    //printf("###CURRENT RECIPELEN = %d \n",curr_r->recipeLen);

	    // Now add explicit fields
	    for(i=0;i<curr_r->xhtml2recipeLen;i++){

	      //printf("####CURRENT RECIPENODE XHTML2RECIPE: %s \n",curr_r->xhtml2recipe[i]);
	      if (appendto(curr_r->recipetext,&(curr_r->recipeLen),recipeMaxLen,curr_r->xhtml2recipe[i])) {
	        fprintf(stderr,"ERROR: %s:%d: %s() recipe text overflow.\n",
	  	      __FILE__,__LINE__,__FUNCTION__);
	        return -1;
	      }
	      //printf("####CURRENT RECIPENODE RECIPETEXT: %s \n",curr_r->recipetext);
	      if (appendto(curr_r->recipetext,&(curr_r->recipeLen),recipeMaxLen,"\n")) {
	        fprintf(stderr,"ERROR: %s:%d: %s() recipe text overflow.\n",
	  	      __FILE__,__LINE__,__FUNCTION__);
	        return -1;
	      }
	    }
	    for(i=0;i<curr_r->xhtmlSelectsLen;i++){
	      if (appendto(curr_r->recipetext,&(curr_r->recipeLen),recipeMaxLen,curr_r->selects[i])) {
	        fprintf(stderr,"ERROR: %s:%d: %s() recipe text overflow.\n",
	  	      __FILE__,__LINE__,__FUNCTION__);
	        return -1;
	      }
	      if (appendto(curr_r->recipetext,&(curr_r->recipeLen),recipeMaxLen,"\n")) {
	        fprintf(stderr,"ERROR: %s:%d: %s() recipe text overflow.\n",
	  	      __FILE__,__LINE__,__FUNCTION__);
	        return -1;
	      }
	    }

	  fprintf(stderr, "\n\nSuccessfully parsed a part of %i characters in a recipe !\n", (int)size);
	  fprintf(stderr,"xhtmlFormName=%s, xhtmlFormVersion=%s\n",
			  curr_r->formname,curr_r->formversion);

#ifdef ANDROID
  LOGI("xhtmlToRecipe(): formname='%s'",recipenode->formname);
  LOGI("xhtmlToRecipe(): formversion='%s'",recipenode->formversion);
#endif

#ifdef ANDROID
  LOGI("XML_Parse() succeeded, xhtml2recipeLen = %d, recipeLen=%d",
		  recipenode->xhtml2recipeLen,recipenode->recipeLen);
#endif

  curr_r = curr_r ->next;
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

  XML_ParserFree(parser);
  
  return (0);
}

recipe_node* create_list_recipes(const char *formversion)
{
    printf("\n creating recipes list headnode with formversion [%s]\n",formversion);
    recipe_node *recipenode = (recipe_node*)malloc(sizeof(recipe_node));
    if(NULL == recipenode)
    {
        printf("\n Node creation failed \n");
        return NULL;
    }

    strcpy(recipenode->formversion,formversion);
    recipenode->xhtml2recipeLen = 0;
    recipenode->xhtmlSelectsLen = 0;
    recipenode->recipeLen = 65536;
    head_r = curr_r = recipenode;
    return recipenode;
}

tag_node* create_list_tags(const char* tagname, const char* formversion)
{
    printf("\n creating tags list headnode with tagname [%s] and formversion [%s]\n",tagname, formversion);
    tag_node *tagnode = (tag_node*)malloc(sizeof(tag_node));
    if(NULL == tagnode)
    {
        printf("\n Node creation failed \n");
        return NULL;
    }

    strcpy(tagnode->tagname,tagname);
    strcpy(tagnode->formversion,formversion);
    head_t = curr_t = tagnode;
    return tagnode;
}

subform_node* create_list_subforms(const char *formversion)
{
    printf("\n creating subforms list headnode with formversion [%s]\n",formversion);
    subform_node *subformnode = (subform_node*)malloc(sizeof(subform_node));
    if(NULL == subformnode)
    {
        printf("\n Node creation failed \n");
        return NULL;
    }

    strcpy(subformnode->subformid,formversion);
    //subformnode->nodeset = "";
    head_s = curr_s = subformnode;
    return subformnode;
}

recipe_node* add_to_list_recipes(const char* formversion, bool add_to_end)
{
    if(NULL == head_r)
    {
        return (create_list_recipes(formversion));
    }

    if(add_to_end)
        printf("\n Adding node to end of recipes list with formversion [%s]\n",formversion);
    else
        printf("\n Adding node to beginning of recipes list with formversion [%s]\n",formversion);

    recipe_node *recipenode = (recipe_node*)malloc(sizeof(recipe_node));
    if(NULL == recipenode)
    {
        printf("\n Node creation failed \n");
        return NULL;
    }
    strcpy(recipenode->formversion,formversion);
    recipenode->xhtml2recipeLen = 0;
    recipenode->xhtmlSelectsLen = 0;
    recipenode->next = NULL;

    if(add_to_end)
    {
        curr_r->next = recipenode;
        curr_r = recipenode;
    }
    else
    {
        recipenode->next = head_r;
        head_r = recipenode;
    }
    return recipenode;
}

tag_node* add_to_list_tags(const char* tagname, const char* formversion, bool add_to_end)
{
    if(NULL == head_t)
    {
        return (create_list_tags(tagname, formversion));
    }

    if(add_to_end)
        printf("\n Adding node to end of tags list with tagname [%s] and formversion [%s]\n",tagname,formversion);
    else
        printf("\n Adding node to beginning of tags list with tagname [%s] and formversion [%s]\n",tagname,formversion);

    tag_node *tagnode = (tag_node*)malloc(sizeof(tag_node));
    if(NULL == tagnode)
    {
        printf("\n Node creation failed \n");
        return NULL;
    }
    strcpy(tagnode->tagname,tagname);
    strcpy(tagnode->formversion,formversion);
    tagnode->next = NULL;

    if(add_to_end)
    {
        curr_t->next = tagnode;
        curr_t = tagnode;
    }
    else
    {
        tagnode->next = head_t;
        head_t = tagnode;
    }
    return tagnode;
}

subform_node* add_to_list_subforms(const char *formversion)
{
    if(NULL == head_s)
    {
        return (create_list_subforms(formversion));
    }

    printf("\n Adding node to end of subforms list with formversion [%s]\n",formversion);

    subform_node *subformnode = (subform_node*)malloc(sizeof(subform_node));
    if(NULL == subformnode)
    {
        printf("\n Node creation failed \n");
        return NULL;
    }
    strcpy(subformnode->subformid,formversion);
    //subformnode->nodeset = "";
    subformnode->next = NULL;

    	//double linked list so we can move forward when we meet a new subform
    	//and move backward when we meet the end of subform
    	//To move forward : simply add a new subform node (only case to move forward)
    	//To move backward : curr_s = curr_s->prev
        curr_s->next = subformnode;
        subformnode->prev = curr_s;
        curr_s = subformnode;

    return subformnode;
}

recipe_node* search_in_list_recipes(const char* formversion, recipe_node **prev)
{
    recipe_node *recipenode = head_r;
    recipe_node *tmp = NULL;
    bool found = false;

    printf("\n Searching in the recipes list for formversion value [%s] \n",formversion);

    while(recipenode != NULL)
    {
        if(!strcmp(recipenode->formversion,formversion))
        {
            found = true;
            break;
        }
        else
        {
            tmp = recipenode;
            recipenode = recipenode->next;
        }
    }

    if(true == found)
    {
        if(prev)
            *prev = tmp;
        printf("\n Found a corresponding node in recipes list ! formversion [%s] \n",recipenode->formversion);
        return recipenode;
    }
    else
    {
        return NULL;
    }
}

tag_node* search_in_list_tags(const char* tagname, tag_node **prev)
{
    tag_node *tagnode = head_t;
    tag_node *tmp = NULL;
    bool found = false;

    printf("\n Searching in the tags list for tagname value [%s] \n",tagname);

    while(tagnode != NULL)
    {
        if(!strcmp(tagnode->tagname,tagname))
        {
            found = true;
            break;
        }
        else
        {
            tmp = tagnode;
            tagnode = tagnode->next;
        }
    }

    if(true == found)
    {
        if(prev)
            *prev = tmp;

        printf("\n Found a corresponding node in tags list ! tagname [%s] and formversion [%s] \n",tagnode->tagname,tagnode->formversion);
        return tagnode;
    }
    else
    {
        return NULL;
    }
}

void print_list_recipes(void)
{
    recipe_node *recipenode = head_r;

    printf("\n -------Printing list Start------- \n");
    while(recipenode != NULL)
    {
    	printf(" *** \n");
        printf("\n Formname: [%s] \n",recipenode->formname);
        printf("\n Formversion: [%s] \n",recipenode->formversion);
        printf("\n Recipetext: [%s] \n",recipenode->recipetext);
        int i=0;
        for(i=0;i<recipenode->xhtml2recipeLen;i++){
            printf("\n xhtml2recipe array: [%s] \n",recipenode->xhtml2recipe[i]);
        }
        for(i=0;i<recipenode->xhtmlSelectsLen;i++){
        	printf("\n selects array: [%s] \n",recipenode->selects[i]);
        }
        //printf("\n Nodeset:[%s] \n",recipenode->nodeset);
        recipenode = recipenode->next;
        printf("\n");
    }
    printf("\n -------Printing list End------- \n");

    return;
}

void print_list_tags(void)
{
    tag_node *tagnode = head_t;

    printf("\n -------Printing list Start------- \n");
    while(tagnode != NULL)
    {
    	printf(" *** \n");
    	printf("\n Tagname: [%s] \n",tagnode->tagname);
        printf("\n Formversion: [%s] \n",tagnode->formversion);
        tagnode = tagnode->next;
        printf("\n");
    }
    printf("\n -------Printing list End------- \n");

    return;
}



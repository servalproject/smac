#include <expat.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
//TODO 	: Afficher une erreur si deux fois le meme nom de field
//		: Proteger et ajouter des tests
//		: Ameliorer le traitement de la contrainte, afficher erreur si non pris en compte
//
//Creation specification stripped file from ODK XML
//FieldName,Type,Minimum,Maximum,Precision
char    *selects[1024];
char    *selectElem = NULL;
int     selectsLen = 0;
int     in_value = 0;

#define MAXCHARS 1000000

void
start(void *data, const char *el, const char **attr)
{   
    char    *node_name = "", *node_type = "", *node_constraint = "";
	int     i ;
    
    //Looking for bind elements
	if ((!strncasecmp("bind",el,strlen("bind")))||(!strncasecmp("xf:bind",el,strlen("xf:bind")))) 
    {
        for (i = 0; attr[i]; i += 2) //Found a bind element, look for attributes
        { 
			//printf(" %s='%s'", attr[i], attr[i + 1]); //Display attributes
			
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
        
		if ((!strcasecmp(node_type,"select"))||(!strcasecmp(node_type,"select1"))) // Select
		{
            selects[selectsLen] = node_name;
            strcat (selects[selectsLen] ,":");
            strcat (selects[selectsLen] ,node_type);
            strcat (selects[selectsLen] ,":0:0:0");
            selectsLen++;
		} 
		else if ((!strcasecmp(node_type,"decimal"))||(!strcasecmp(node_type,"int"))) // Integers and decimal
        {
            printf("%s:%s", node_name,node_type);  
            if (strlen(node_constraint)) {
                char *ptr = node_constraint;
                int a, b;
		    
                //We look for 0 to 2 digits
                while( ! isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
                a = atoi(ptr);
                while( isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
                while( ! isdigit(*ptr) && (ptr<node_constraint+strlen(node_constraint))) ptr++;
                b = atoi(ptr);
                printf(":%d:%d:0", MIN(a, b), MAX(a, b));
            } else {
                printf(":0:0:0");
            }
		  printf("\n");
		}
		else if (strcasecmp(node_type,"binary")) // All others type except binary (ignore binary fields in succinct data)
        {
            printf("%s:%s:0:0:0\n", node_name,node_type);
		}
	}
    
    //Now look for selects specifications
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
        
    }
    
    else if ((selectElem)&&((!strcasecmp("value",el))||(!strcasecmp("xf:value",el)))) 
    {
        in_value = 1;
    }
     
    
}

void characterdata(void *data, const char *el, int len)
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
                    strcat (selects[i] ,",");
                    strcat (selects[i] ,x);
			}
        }
    }
    
    		
	

}

void end(void *data, const char *el)
{
    if (selectElem && ((!strcasecmp("select1",el))||(!strcasecmp("select",el))))  {
       selectElem = NULL;
    }
    
    if (in_value && ((!strcasecmp("value",el))||(!strcasecmp("xf:value",el))))  {
       in_value = 0;
    }
}  

int main(int argc, char **argv)
{
	if (argc != 2) {
    	fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    	return (1);
    }
	
    FILE *f=fopen(argv[1],"r");
    XML_Parser parser;
    size_t size;
    char *xmltext;
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
    //Open Xml File
    xmltext = malloc(MAXCHARS);
    size = fread(xmltext, sizeof(char), MAXCHARS, f);
    
    //Parse Xml Text
    if (XML_Parse(parser, xmltext, strlen(xmltext), XML_TRUE) ==
        XML_STATUS_ERROR) {
    	fprintf(stderr,
    		"Cannot parse , file may be too large or not well-formed XML\n");
    	return (1);
    }
    
    //Finish treatment for selects
    for(i=0;i<selectsLen;i++){
        fprintf(stderr, "%s\n",selects[i]);
    }
    //Close
    fclose(f);
    XML_ParserFree(parser);
    fprintf(stderr, "Successfully parsed %i characters !\n", (int)size);
    return (0);
}

 


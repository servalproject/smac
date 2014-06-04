/*
  Generate HTML map interface to show collected data for each form.

  (C) Copyright Paul Gardner-Stephen, 2014.

*/

#include <stdio.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

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

int generateMap(char *recipe_name, char *outputDir)
{
  char filename[1024];
  snprintf(filename,1024,"%s/maps/",outputDir);
  mkdir(filename,0777);

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

	generateMap(recipe_name,outputDir);
      }      
  }
  
  closedir(d);
  return 0;
}

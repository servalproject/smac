/*
  Generate HTML map interface to show collected data for each form.

  (C) Copyright Paul Gardner-Stephen, 2014.

*/

#include <stdio.h>
#include <strings.h>
#include <dirent.h>

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
      }      
  }
  
  closedir(d);
  return 0;
}

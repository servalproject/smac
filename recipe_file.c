#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#include "log.h"
#include "recipe.h"

int recipe_load_file(char *filename, char *out, int out_size) {
  unsigned char *buffer;

  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    LOGE("Could not open file '%s'", filename);
    return -1;
  }

  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    LOGE("Could not stat file '%s'", filename);
    close(fd);
    return -1;
  }

  if (stat.st_size > out_size) {
    LOGE("File '%s' is too long (must be <= %d bytes)", filename, out_size);
    close(fd);
    return -1;
  }

  buffer = mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer == MAP_FAILED) {
    LOGE("Could not memory map file '%s'", filename);
    close(fd);
    return -1;
  }

  bcopy(buffer, out, stat.st_size);

  munmap(buffer, stat.st_size);
  close(fd);

  return stat.st_size;
}

struct recipe *recipe_read_from_file(char *filename) {
  struct recipe *recipe = NULL;

  unsigned char *buffer;

  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    LOGE("Could not open recipe file '%s'", filename);
    return NULL;
  }

  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    LOGE("Could not stat recipe file '%s'", filename);
    close(fd);
    return NULL;
  }

  buffer = mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer == MAP_FAILED) {
    LOGE("Could not memory map recipe file '%s'", filename);
    close(fd);
    return NULL;
  }

  recipe = recipe_read(filename, (char *)buffer, stat.st_size);

  munmap(buffer, stat.st_size);
  close(fd);

  if (recipe && recipe->field_count == 0) {
    recipe_free(recipe);
    LOGE("Recipe contains no field definitions");
    return NULL;
  }

  return recipe;
}

static struct recipe * open_recipe(const char *formid, void *f){
  char recipe_file[1024];
  sprintf(recipe_file, "%s/%s.recipe", (const char *)f, formid);
  LOGI("Trying to load '%s' as a recipe\n", recipe_file);
  return recipe_read_from_file(recipe_file);
}

struct recipe *recipe_find_recipe(char *recipe_dir, unsigned char *formhash) {
  DIR *dir = opendir(recipe_dir);
  struct dirent *de;
  if (!dir)
    return NULL;
  while ((de = readdir(dir)) != NULL) {
    if (strlen(de->d_name) > strlen(".recipe")) {
      if (!strcasecmp(&de->d_name[strlen(de->d_name) - strlen(".recipe")],
                      ".recipe")) {
        char recipe_path[1024];
        snprintf(recipe_path, 1024, "%s/%s", recipe_dir, de->d_name);
        struct recipe *r = recipe_read_from_file(recipe_path);
        if (0)
          LOGE("Is %s a recipe?", recipe_path);
        if (r) {
          if (1) {
            LOGE("Considering form %s (formhash %02x%02x%02x%02x%02x%02x)",
                 recipe_path, r->formhash[0], r->formhash[1], r->formhash[2],
                 r->formhash[3], r->formhash[4], r->formhash[5]);
          }
          if (!memcmp(formhash, r->formhash, 6)) {
            return r;
          }
          recipe_free(r);
        }
      }
    }
  }
  return NULL;
}

int recipe_compress_file(stats_handle *h, char *recipe_dir, char *input_file,
                         char *output_file) {
  unsigned char *buffer;

  int fd = open(input_file, O_RDONLY);
  if (fd == -1) {
    LOGE("Could not open uncompressed file '%s'", input_file);
    return -1;
  }

  struct stat stat;
  if (fstat(fd, &stat) == -1) {
    LOGE("Could not stat uncompressed file '%s'", input_file);
    close(fd);
    return -1;
  }

  buffer = mmap(NULL, stat.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer == MAP_FAILED) {
    LOGE("Could not memory map uncompressed file '%s'", input_file);
    close(fd);
    return -1;
  }

  // Parse formid from stripped file so that we know which recipe to use
  char formid[1024] = "";
  for (int i = 0; i < stat.st_size; i++) {
    if (sscanf((const char *)&buffer[i], "formid=%[^\n]", formid) == 1)
      break;
  }

  unsigned char *stripped = buffer;
  int stripped_len = stat.st_size;

  if (!formid[0]) {
    // Input file is not a stripped file. Perhaps it is a record to be
    // compressed?
    stripped = calloc(65536, 1);
    stripped_len = xml2stripped(NULL, (const char *)buffer, stat.st_size,
                                (char *)stripped, 65536);

    for (int i = 0; i < stripped_len; i++) {
      if (sscanf((const char *)&stripped[i], "formid=%[^\n]", formid) == 1)
        break;
    }
  }

  if (!formid[0]) {
    LOGE(
        "stripped file contains no formid field to identify matching recipe");
    return -1;
  }

  struct recipe *recipe = open_recipe(formid, (void *)recipe_dir);
  // A form can be given in place of the recipe directory
  if (!recipe) {
    LOGI("Trying to load '%s' as a form specification to convert to recipe",
         recipe_dir);
    char form_spec_text[1048576];
    int form_spec_len =
        recipe_load_file(recipe_dir, form_spec_text, sizeof(form_spec_text));
    recipe = recipe_read_from_specification(form_spec_text);
  }
  if (!recipe)
    return -1;

  unsigned char out_buffer[1024];
  int r = recipe_compress(h, open_recipe, (void *)recipe_dir, recipe, (char *)stripped, stripped_len,
                          out_buffer, 1024);
  recipe_free(recipe);

  munmap(buffer, stat.st_size);
  close(fd);

  if (r < 0)
    return -1;

  FILE *f = fopen(output_file, "w");
  if (!f) {
    LOGE("Could not write succinct data compressed file '%s'", output_file);
    return -1;
  }
  int wrote = fwrite(out_buffer, r, 1, f);
  fclose(f);
  if (wrote != 1) {
    LOGE("Could not write %d bytes of compressed succinct data into '%s'", r,
         output_file);
    return -1;
  }

  return r;
}

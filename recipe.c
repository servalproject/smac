/*
  Compress a key value pair file according to a recipe.
  The recipe indicates the type of each field.
  For certain field types the precision or range of allowed
  values can be specified to further aid compression.
*/

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

#include "arithmetic.h"
#include "charset.h"
#include "md5.h"
#include "packed_stats.h"
#include "recipe.h"
#include "smac.h"
#include "subforms.h"
#include "visualise.h"


#ifdef ANDROID
time_t timegm(struct tm *tm) {
  time_t ret;
  char *tz;

  tz = getenv("TZ");
  setenv("TZ", "", 1);
  tzset();
  ret = mktime(tm);
  if (tz)
    setenv("TZ", tz, 1);
  else
    unsetenv("TZ");
  tzset();
  return ret;
}
#endif

int recipe_parse_fieldtype(const char *name)
{
  if (!strcasecmp(name,"integer")) return FIELDTYPE_INTEGER;
  if (!strcasecmp(name,"int")) return FIELDTYPE_INTEGER;
  if (!strcasecmp(name,"float")) return FIELDTYPE_FLOAT;
  if (!strcasecmp(name,"decimal")) return FIELDTYPE_FIXEDPOINT;
  if (!strcasecmp(name,"fixedpoint")) return FIELDTYPE_FIXEDPOINT;
  if (!strcasecmp(name,"boolean")) return FIELDTYPE_BOOLEAN;
  if (!strcasecmp(name,"bool")) return FIELDTYPE_BOOLEAN;
  if (!strcasecmp(name,"timeofday")) return FIELDTYPE_TIMEOFDAY;
  if (!strcasecmp(name,"timestamp")) return FIELDTYPE_TIMEDATE;
  if (!strcasecmp(name,"datetime")) return FIELDTYPE_TIMEDATE;
  if (!strcasecmp(name,"magpitimestamp")) return FIELDTYPE_MAGPITIMEDATE;
  if (!strcasecmp(name,"date")) return FIELDTYPE_DATE;
  if (!strcasecmp(name,"latlong")) return FIELDTYPE_LATLONG;
  if (!strcasecmp(name,"geopoint")) return FIELDTYPE_LATLONG;
  if (!strcasecmp(name,"text")) return FIELDTYPE_TEXT;
  if (!strcasecmp(name,"string")) return FIELDTYPE_TEXT;
  if (!strcasecmp(name,"image")) return FIELDTYPE_TEXT;
  if (!strcasecmp(name,"information")) return FIELDTYPE_TEXT;
  if (!strcasecmp(name,"uuid")) return FIELDTYPE_UUID;
  if (!strcasecmp(name,"magpiuuid")) return FIELDTYPE_MAGPIUUID;
  if (!strcasecmp(name,"enum")) return FIELDTYPE_ENUM;
  if (!strcasecmp(name,"multi")) return FIELDTYPE_MULTISELECT;
  if (!strcasecmp(name,"subform")) return FIELDTYPE_SUBFORM;

  // strings in magpi form definitions
  if (!strcasecmp(name, "dropdown")) return FIELDTYPE_ENUM;
  if (!strcasecmp(name, "radio")) return FIELDTYPE_ENUM;
  if (!strcasecmp(name, "select1")) return FIELDTYPE_ENUM;
  if (!strcasecmp(name, "select2")) return FIELDTYPE_ENUM;
  if (!strcasecmp(name, "selectn")) return FIELDTYPE_MULTISELECT;
  if (!strcasecmp(name, "checkbox")) return FIELDTYPE_MULTISELECT;

  return -1;
}

const char *recipe_field_type_name(int f)
{
  switch(f) {
  case FIELDTYPE_INTEGER: return    "integer";
  case FIELDTYPE_FLOAT: return    "float";
  case FIELDTYPE_FIXEDPOINT: return    "fixedpoint";
  case FIELDTYPE_BOOLEAN: return    "boolean";
  case FIELDTYPE_TIMEOFDAY: return    "timeofday";
  case FIELDTYPE_TIMEDATE: return    "timestamp";
  case FIELDTYPE_MAGPITIMEDATE: return    "magpitimestamp";
  case FIELDTYPE_DATE: return    "date";
  case FIELDTYPE_LATLONG: return    "latlong";
  case FIELDTYPE_TEXT: return    "text";
  case FIELDTYPE_UUID: return    "uuid";
  case FIELDTYPE_MAGPIUUID: return    "magpiuuid";
  case FIELDTYPE_ENUM: return    "enum";
  case FIELDTYPE_MULTISELECT: return    "multi";
  case FIELDTYPE_SUBFORM: return    "subform";
  default: return "unknown";
  }
}

void recipe_free(struct recipe *recipe) {
  while (recipe) {
    struct field *field = recipe->field_list;
    while (field) {
      struct enum_value *val = field->enum_value;
      while (val) {
        struct enum_value *v = val;
        val = val->next;
        free(v);
      }
      struct field *f = field;
      field = field->next;
      free(f);
    }
    struct recipe *r = recipe;
    recipe = recipe->next;
    free(r);
  }
}

int recipe_form_hash(char *recipe_file, unsigned char *formhash,
                     char *formname) {
  MD5_CTX md5;
  unsigned char hash[16];

  // Get basename of form for computing hash
  char recipe_name[1024];
  int start = 0;
  int end = strlen(recipe_file);
  int i;
  // Cut path from filename
  for (i = 0; recipe_file[i]; i++)
    if (recipe_file[i] == '/')
      start = i + 1;
  // Cut .recipe from filename
  if (end > strlen(".recipe"))
    if (!strcasecmp(".recipe", &recipe_file[end - strlen(".recipe")]))
      end = end - strlen(".recipe") - 1;
  int j = 0;
  for (i = start; i <= end; i++)
    recipe_name[j++] = recipe_file[i];
  recipe_name[j] = 0;

  MD5_Init(&md5);
  // Include version of SMAC in hash, so that we never accidentally
  // mis-interpret things.
  MD5_Update(&md5, "SMAC Binary Format v2", strlen("SMAC Binary Format v2"));
  LOGI("Calculating recipe file formhash from '%s' (%d chars)", recipe_name,
       (int)strlen(recipe_name));
  MD5_Update(&md5, recipe_name, strlen(recipe_name));
  MD5_Final(hash, &md5);

  bcopy(hash, formhash, 6);

  if (formname)
    strcpy(formname, recipe_name);
  return 0;
}

struct recipe *recipe_read(char *formname, char *buffer, int buffer_size) {
  if (buffer_size < 1 || buffer_size > 1048576) {
    LOGE("Recipe file empty or too large (>1MB).");
    return NULL;
  }

  uint8_t formhash[6];
  char form_name[1024];

  // Get recipe hash
  recipe_form_hash(formname, formhash, form_name);
  LOGI("recipe_read(): Computing formhash based on form name '%s'", formname);

  struct recipe *recipe =
      calloc(sizeof(struct recipe) + strlen(form_name) + 1, 1);
  if (!recipe) {
    LOGE("Allocation of recipe structure failed.");
    return NULL;
  }
  strcpy(recipe->formname, form_name);
  memcpy(recipe->formhash, formhash, sizeof(formhash));

  int i;
  int l = 0;
  int line_number = 1;
  char line[16384];
  char name[16384], type[16384];
  int min, max, precision;
  char enumvalues[16384];

  struct field **field_tail = &recipe->field_list;
  for (i = 0; i <= buffer_size; i++) {
    if (l > 16380) {
      LOGE("line:%d:Line too long.", line_number);
      recipe_free(recipe);
      return NULL;
    }
    if ((i == buffer_size) || (buffer[i] == '\n') || (buffer[i] == '\r')) {
      if (recipe->field_count > 1000) {
        LOGE("line:%d:Too many field definitions (must be <=1000).",
             line_number);
        recipe_free(recipe);
        return NULL;
      }
      // Process recipe line
      line[l] = 0;
      if ((l > 0) && (line[0] != '#')) {
        enumvalues[0] = 0;
        if (sscanf(line, "%[^:]:%[^:]:%d:%d:%d:%[^\n]", name, type, &min, &max,
                   &precision, enumvalues) >= 5) {
          int fieldtype = recipe_parse_fieldtype(type);
          if (fieldtype == -1) {
            LOGE("line:%d:Unknown or misspelled field type '%s'.",
                 line_number, type);
            recipe_free(recipe);
            return NULL;
          } else {
            // Store parsed field
            struct field *field = *field_tail =
                calloc(sizeof(struct field) + strlen(name) + 1, 1);
            bzero(field, sizeof(struct field));

            strcpy(field->name, name);
            field->type = fieldtype;
            field->minimum = min;
            field->maximum = max;
            field->precision = precision;
            field_tail = &field->next;

            if (fieldtype == FIELDTYPE_ENUM ||
                fieldtype == FIELDTYPE_MULTISELECT) {

              struct enum_value **enum_tail = &field->enum_value;
              char enum_value[1024];
              int e = 0;
              int en = 0;
              int i;
              for (i = 0; i <= strlen(enumvalues); i++) {
                if ((enumvalues[i] == ',') || (enumvalues[i] == 0)) {
                  // End of field
                  enum_value[e] = 0;
                  if (en >= MAX_ENUM_VALUES) {
                    LOGE("line:%d:enum has too many values (max=32)",
                         line_number);
                    recipe_free(recipe);
                    return NULL;
                  }
                  struct enum_value *val = *enum_tail =
                      calloc(sizeof(struct enum_value) + e + 1, 1);
                  val->next = NULL;
                  enum_tail = &val->next;
                  strcpy(val->value, enum_value);
                  en++;
                  e = 0;
                } else {
                  // next character of field
                  enum_value[e++] = enumvalues[i];
                }
              }
              if (en < 1) {
                LOGE("line:%d:Malformed enum field definition: must "
                     "contain at least one value option.",
                     line_number);
                recipe_free(recipe);
                return NULL;
              }
              field->enum_count = en;
            }

            recipe->field_count++;
          }
        } else {
          LOGE("line:%d:Malformed field definition.", line_number);
          recipe_free(recipe);
          return NULL;
        }
      }
      line_number++;
      l = 0;
    } else {
      line[l++] = buffer[i];
    }
  }
  return recipe;
}

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

struct recipe *recipe_read_from_specification(char *xmlform_c) {
  int magpi_mode = 0;
  if (xmlform_c && (!strncasecmp("<html", xmlform_c, 5)))
    magpi_mode = 1;
  int r;

  LOGI("start of form: '%c%c%c%c%c%c%c%c%c%c'", xmlform_c[0], xmlform_c[1],
         xmlform_c[2], xmlform_c[3], xmlform_c[4], xmlform_c[5], xmlform_c[6],
         xmlform_c[7], xmlform_c[8], xmlform_c[9]);

  LOGI("magpi_mode=%d", magpi_mode);

  if (magpi_mode) {
    char *recipe_text[1024];
    char *form_versions[1024];
    bzero(recipe_text, sizeof recipe_text);
    bzero(form_versions, sizeof form_versions);

    r = xhtmlToRecipe(xmlform_c, NULL, (char **)&recipe_text,
                      (char **)&form_versions);
    if (r < 0)
      return NULL;

    struct recipe *ret =
        recipe_read(form_versions[0], recipe_text[0], strlen(recipe_text[0]));
    int i;
    for (i = 0; i < 1024; i++) {
      if (recipe_text[i])
        free(recipe_text[i]);
      if (form_versions[i])
        free(form_versions[i]);
    }

    return ret;
  } else {
    char form_name[1024];
    char form_version[1024];
    char recipetext[65536];
    int recipetextLen = 65536;
    char templatetext[1048576];
    int templatetextLen = 1048576;

    r = xmlToRecipe(xmlform_c, strlen(xmlform_c), form_name, form_version,
                    recipetext, &recipetextLen, templatetext, &templatetextLen);
    if (r < 0)
      return NULL;

    return recipe_read(form_name, recipetext, recipetextLen);
  }
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




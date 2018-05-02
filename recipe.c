/*
  Compress a key value pair file according to a recipe.
  The recipe indicates the type of each field.
  For certain field types the precision or range of allowed
  values can be specified to further aid compression.
*/

#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
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

int encryptAndFragment(char *filename, int mtu, char *outputdir,
                       char *publickeyhex);
int defragmentAndDecrypt(char *inputdir, char *outputdir, char *passphrase);
int recipe_create(char *input);
int xhtml_recipe_create(char *recipe_dir, char *input);
int recipe_decompress(stats_handle *h, struct recipe *recipe, char *recipe_dir,
                      char *out, int out_size, char *recipe_name,
                      range_coder *c, bool is_subform, bool is_record);

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

int recipe_parse_boolean(char *b) {
  if (!b)
    return 0;
  switch (b[0]) {
  case 'y':
  case 'Y':
  case 't':
  case 'T':
  case '1':
    return 1;
  default:
    return 0;
  }
}

int round_string_by_one_digit(char *s) {
  int len = strlen(s);
  if (len < 1)
    return -1;
  int has_decimal = strstr(s, ".") ? 1 : 0;
  if (s[len - 1] < '0' || s[len - 1] > '9')
    return -1;

  int t = len - 1;
  s[t--] = '0';
  int bump = 1;
  while ((t >= 0) && (s[t] != '.')) {
    if (s[t] != '0') {
      if (s[t] != '9') {
        if (bump)
          s[t]++;
        bump = 0;
        break;
      } else {
        if (has_decimal)
          s[t] = '0';
        bump = 1;
      }
    } else
      bump = 0;
    t--;
  }
  return 0;
}

int recipe_decode_field(struct field *field, stats_handle *stats,
                        range_coder *c, char *value, int value_size) {
  int normalised_value;
  int minimum;
  int maximum;
  int precision;

  int r;

  precision = field->precision;

  switch (field->type) {
  case FIELDTYPE_INTEGER:
    minimum = field->minimum;
    maximum = field->maximum;
    normalised_value = range_decode_equiprobable(c, maximum - minimum + 2);
    if (normalised_value == (maximum - minimum + 1)) {
      // out of range value, so decode it as a string.
      LOGE("FIELDTYPE_INTEGER: Illegal value - decoding string "
           "representation.");
      r = stats3_decompress_bits(c, (unsigned char *)value, &value_size, stats,
                                 NULL);
    } else
      sprintf(value, "%d", normalised_value + minimum);
    return 0;
  case FIELDTYPE_FLOAT: {
    // Sign
    int sign = range_decode_equiprobable(c, 2);
    // Exponent
    int exponent = range_decode_equiprobable(c, 256) - 128;
    // Mantissa
    int mantissa = 0;
    int b;
    b = range_decode_equiprobable(c, 256);
    mantissa |= b << 16;
    b = range_decode_equiprobable(c, 256);
    mantissa |= b << 8;
    b = range_decode_equiprobable(c, 256);
    mantissa |= b << 0;
    float f = mantissa * 1.0 / 0xffffff;
    if (sign)
      f = -f;
    f = ldexp(f, exponent);
    LOGE("sign=%d, exp=%d, mantissa=%x, f=%f", sign, exponent, mantissa, f);
    sprintf(value, "%f", f);

    // Now enforce reasonable numbers of significant digits
    if (strstr(value, ".") && (exponent >= 0)) {
      int sane_decimal_places = 6;
      if (exponent >= 4)
        sane_decimal_places = 5;
      if (exponent >= 7)
        sane_decimal_places = 4;
      if (exponent >= 10)
        sane_decimal_places = 3;
      if (exponent >= 14)
        sane_decimal_places = 2;
      if (exponent >= 17)
        sane_decimal_places = 1;
      if (exponent >= 20)
        sane_decimal_places = 0;
      int actual_decimal_places =
          strlen(value) - 1 - (strstr(value, ".") - value);
      LOGI("%s has %d decimal places. %d would be sane.", value,
             actual_decimal_places, sane_decimal_places);
      for (int i = sane_decimal_places; i < actual_decimal_places; i++)
        round_string_by_one_digit(value);
      LOGI("  after rounding it is [%s]", value);
    }

    // Trim trailing 0s after the decimal place
    if (strstr(value, ".")) {
      while (value[0] && (value[strlen(value) - 1] == '0'))
        value[strlen(value) - 1] = 0;
      // and trailing decimal place
      if (value[0] && (value[strlen(value) - 1] == '.'))
        value[strlen(value) - 1] = 0;
    }
    return 0;
  }
  case FIELDTYPE_BOOLEAN:
    normalised_value = range_decode_equiprobable(c, 2);
    sprintf(value, "%d", normalised_value);
    return 0;
    break;
  case FIELDTYPE_MULTISELECT: {
    int vlen = 0;
    // Get bitmap of enum fields
    struct enum_value *val = field->enum_value;
    while (val) {
      if (range_decode_equiprobable(c, 2)) {
        // Field value is present
        if (vlen) {
          value[vlen++] = '|';
          value[vlen] = 0;
        }
        sprintf(&value[vlen], "%s", val->value);
        vlen += strlen(val->value);
      }
      val = val->next;
    }
    return 0;
    break;
  }
  case FIELDTYPE_ENUM:
    normalised_value = range_decode_equiprobable(c, field->enum_count);
    if (normalised_value < 0 || normalised_value >= field->enum_count) {
      LOGE("enum: range_decode_equiprobable returned illegal value %d for "
           "range %d..%d",
           normalised_value, 0, field->enum_count - 1);
      return -1;
    }
    struct enum_value *val = field->enum_value;
    int i = normalised_value;
    while (val && i > 0) {
      i--;
      val = val->next;
    }
    strcpy(value, val->value);
    LOGI("enum: decoding %s as %d of %d", value, normalised_value,
         field->enum_count);
    return 0;
    break;
  case FIELDTYPE_TEXT:
    r = stats3_decompress_bits(c, (unsigned char *)value, &value_size, stats,
                               NULL);
    return 0;
  case FIELDTYPE_TIMEDATE:
    // time is 32-bit seconds since 1970.
    // Format as yyyy-mm-ddThh:mm:ss+hh:mm
    {
      // SMAC has a bug with encoding large ranges, so break into smaller pieces
      time_t t = 0;
      t = range_decode_equiprobable(c, 0x8000) << 16;
      t |= range_decode_equiprobable(c, 0x10000);
      LOGI("TIMEDATE: decoding t=%d", (int)t);
      struct tm tm;
      // gmtime_r(&t,&tm);
      localtime_r(&t, &tm);
      sprintf(value, "%04d-%02d-%02dT%02d:%02d:%02d+00:00", tm.tm_year + 1900,
              tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
      return 0;
    }
  case FIELDTYPE_MAGPITIMEDATE:
    // time encodes each field precisely, allowing years 0 - 9999
    // Format as yyyy-mm-dd hh:mm:ss
    {
      // time_t t=range_decode_equiprobable(c,0x7fffffff);
      struct tm tm;
      bzero(&tm, sizeof(tm));

      tm.tm_year = range_decode_equiprobable(c, 10000);
      tm.tm_mon = range_decode_equiprobable(c, 12);
      tm.tm_mday = range_decode_equiprobable(c, 31);
      tm.tm_hour = range_decode_equiprobable(c, 25);
      tm.tm_min = range_decode_equiprobable(c, 60);
      tm.tm_sec = range_decode_equiprobable(c, 62);

      sprintf(value, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year, tm.tm_mon + 1,
              tm.tm_mday + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
      return 0;
    }
  case FIELDTYPE_DATE:
    // Date encoded using:
    // normalised_value=y*31*31+(m-1)*31+(d-1);
    // (this allows US versus rest of the world confusion of dates to be
    // preserved correctly). So year = value / 31*31 ...
    {
      if (precision == 0)
        precision = 24;
      int minimum = 0;
      int maximum = 10000 * 31 * 31;
      maximum = maximum >> (24 - precision);
      int separator_is_slash = range_decode_equiprobable(c, 2);
      int normalised_value =
          range_decode_equiprobable(c, maximum - minimum + 1);
      int year = normalised_value / (31 * 31);
      int day_of_year = normalised_value - (year * 31 * 31);
      int month = day_of_year / 31 + 1;
      int day_of_month = day_of_year % 31 + 1;
      // American versus rest of us date ordering is preserved from parsing`
      if (separator_is_slash)
        sprintf(value, "%04d/%02d/%04d", year, month, day_of_month);
      else
        sprintf(value, "%02d-%02d-%04d", day_of_month, month, year);
      return 0;
    }
  case FIELDTYPE_UUID: {
    int i, j = 5;
    sprintf(value, "uuid:");
    for (i = 0; i < 16; i++) {
      int b = 0;
      if ((!field->precision) || (i < field->precision))
        b = range_decode_equiprobable(c, 256);
      switch (i) {
      case 4:
      case 6:
      case 8:
      case 10:
        value[j++] = '-';
      }
      sprintf(&value[j], "%02x", b);
      j += 2;
      value[j] = 0;
    }
    return 0;
  }
  case FIELDTYPE_MAGPIUUID:
    // 64bit hex followed by seconds since UNIX epoch?
    {
      int i, j = 0;
      value[0] = 0;
      for (i = 0; i < 8; i++) {
        int b = 0;
        b = range_decode_equiprobable(c, 256);
        sprintf(&value[j], "%02x", b);
        j += 2;
        value[j] = 0;
      }
      // 48 bits of milliseconds since unix epoch
      long long timestamp = 0;
      for (i = 0; i < 6; i++) {
        timestamp = timestamp << 8LL;
        int b = range_decode_equiprobable(c, 256);
        timestamp |= b;
      }
      sprintf(&value[j], "-%lld", timestamp);
      return 0;
    }
  case FIELDTYPE_LATLONG: {
    int ilat, ilon;
    double lat, lon;
    switch (field->precision) {
    case 0:
    case 34:
      ilat = range_decode_equiprobable(c, 182 * 112000);
      ilat -= 90 * 112000;
      ilon = range_decode_equiprobable(c, 361 * 112000);
      ilon -= 180 * 112000;
      lat = ilat / 112000.0;
      lon = ilon / 112000.0;
      break;
    case 16:
      ilat = range_decode_equiprobable(c, 182);
      ilat -= 90;
      ilon = range_decode_equiprobable(c, 361);
      ilon -= 180;
      lat = ilat;
      lon = ilon;
      break;
    default:
      LOGE("Illegal LATLONG precision of %d bits.  Should be 16 or 34.",
           field->precision);
      return -1;
    }
    sprintf(value, "%.5f %.5f", lat, lon);
    return 0;
  }
  default:
    LOGE("Attempting decompression of unsupported field type of '%s'.",
         recipe_field_type_name(field->type));
    return -1;
  }

  return 0;
}

int parseHexDigit(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return 0;
}

int parseHexByte(char *hex) {
  return (parseHexDigit(hex[0]) << 4) | parseHexDigit(hex[1]);
}

int recipe_encode_field(struct field *field, stats_handle *stats,
                        range_coder *c, char *value) {
  int normalised_value;
  int minimum;
  int maximum;
  int precision;
  int h, m, s, d, y;
  float lat, lon;
  int ilat, ilon;

  precision = field->precision;

  switch (field->type) {
  case FIELDTYPE_INTEGER:
    normalised_value = atoi(value) - field->minimum;
    minimum = field->minimum;
    maximum = field->maximum;
    if (maximum <= minimum) {
      LOGE("Illegal range: min=%d, max=%d", minimum, maximum);
      LOGI("Illegal range: min=%d, max=%d", minimum, maximum);
      return -1;
    }
    if (normalised_value < 0 || normalised_value > (maximum - minimum + 1)) {
      LOGE("Illegal value: min=%d, max=%d, value=%d", minimum, maximum,
           atoi(value));
      LOGI("Illegal value: min=%d, max=%d, value=%d", minimum, maximum,
           atoi(value));
      range_encode_equiprobable(c, maximum - minimum + 2,
                                maximum - minimum + 1);
      int r = stats3_compress_append(c, (unsigned char *)value, strlen(value),
                                     stats, NULL);
      return r;
    }
    return range_encode_equiprobable(c, maximum - minimum + 2,
                                     normalised_value);
  case FIELDTYPE_FLOAT: {
    float f = atof(value);
    int sign = 0;
    int exponent = 0;
    int mantissa = 0;
    if (f < 0) {
      sign = 1;
      f = -f;
    } else
      sign = 0;
    double m = frexp(f, &exponent);
    mantissa = m * 0xffffff;
    if (exponent < -127)
      exponent = -127;
    if (exponent > 127)
      exponent = 127;
    LOGE("encoding sign=%d, exp=%d, mantissa=%x, f=%f", sign, exponent,
         mantissa, atof(value));
    // Sign
    range_encode_equiprobable(c, 2, sign);
    // Exponent
    range_encode_equiprobable(c, 256, exponent + 128);
    // Mantissa
    range_encode_equiprobable(c, 256, (mantissa >> 16) & 0xff);
    range_encode_equiprobable(c, 256, (mantissa >> 8) & 0xff);
    return range_encode_equiprobable(c, 256, (mantissa >> 0) & 0xff);
  }
  case FIELDTYPE_FIXEDPOINT:
  case FIELDTYPE_BOOLEAN:
    normalised_value = recipe_parse_boolean(value);
    minimum = 0;
    maximum = 1;
    return range_encode_equiprobable(c, maximum - minimum + 1,
                                     normalised_value);
  case FIELDTYPE_TIMEOFDAY:
    if (sscanf(value, "%d:%d.%d", &h, &m, &s) < 2)
      return -1;
    // XXX - We don't support leap seconds
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
      return -1;
    normalised_value = h * 3600 + m * 60 + s;
    minimum = 0;
    maximum = 24 * 60 * 60;
    if (precision == 0)
      precision = 17; // 2^16 < 24*60*60 < 2^17
    if (precision < 17) {
      normalised_value = normalised_value >> (17 - precision);
      minimum = minimum >> (17 - precision);
      maximum = maximum >> (17 - precision);
      maximum += 1; // make sure that normalised_value cannot = maximum
    }
    return range_encode_equiprobable(c, maximum - minimum + 1,
                                     normalised_value);
  case FIELDTYPE_TIMEDATE: {
    struct tm tm;
    int tzh = 0, tzm = 0, csec;
    int r;
    bzero(&tm, sizeof(tm));
    if ((r = sscanf(value, "%d-%d-%dT%d:%d:%d.%d%d:%d", &tm.tm_year, &tm.tm_mon,
                    &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &csec,
                    &tzh, &tzm)) < 6) {
      LOGI("r=%d", r);
      return -1;
    }

    // Validate fields
    if (tm.tm_year < 0 || tm.tm_year > 2299)
      return -1;
    if (tm.tm_mon < 1 || tm.tm_mon > 12)
      return -1;
    if (tm.tm_mday < 1 || tm.tm_mday > 31)
      return -1;
    if (tm.tm_hour < 0 || tm.tm_hour > 24)
      return -1;
    if (tm.tm_min < 0 || tm.tm_min > 59)
      return -1;
    if (tm.tm_sec < 0 || tm.tm_sec > 61)
      return -1;
    if (csec > 99)
      return -1;
    if (tzh < -15 || tzh > 15)
      return -1;
    if (tzm < 0 || tzm > 59)
      return -1;

#if defined(__sgi) || defined(__sun)
#else
    tm.tm_gmtoff = tzm * 60 + tzh * 3600;
#endif

#if 0
      range_encode_equiprobable(c,2200,tm.tm_year);
      range_encode_equiprobable(c,12,tm.tm_mon-1);
      range_encode_equiprobable(c,31,tm.tm_mday);
      range_encode_equiprobable(c,25,tm.tm_hour);
      range_encode_equiprobable(c,60,tm.tm_min);
      range_encode_equiprobable(c,62,tm.tm_sec); // leap seconds
      // Encode centi-seconds and timezone data if present
      if (r>=7) {
	range_encode_equiprobable(c,2,1);
	range_encode_equiprobable(c,100,csec);
	range_encode_equiprobable(c,31,tzh+15); // +/- 15 hours
	b=range_encode_equiprobable(c,60,tzm);
      } else
	b=range_encode_equiprobable(c,2,0);
#endif

    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    time_t t = mktime(&tm);
    minimum = 1;
    maximum = 0x7fffffff;
    normalised_value = t;

    int b;
    b = range_encode_equiprobable(c, 0x8000, t >> 16);
    b = range_encode_equiprobable(c, 0x10000, t & 0xffff);
    LOGI("TIMEDATE: encoding t=%d", (int)t);
    return b;
  }
  case FIELDTYPE_MAGPITIMEDATE: {
    struct tm tm;
    // int tzh=0,tzm=0;
    int r;
    bzero(&tm, sizeof(tm));
    if ((r = sscanf(value, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon,
                    &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec)) < 6) {
      LOGI("r=%d", r);
      return -1;
    }

    // Validate fields
    if (tm.tm_year < 0 || tm.tm_year > 9999)
      return -1;
    if (tm.tm_mon < 1 || tm.tm_mon > 12)
      return -1;
    if (tm.tm_mday < 1 || tm.tm_mday > 31)
      return -1;
    if (tm.tm_hour < 0 || tm.tm_hour > 24)
      return -1;
    if (tm.tm_min < 0 || tm.tm_min > 59)
      return -1;
    if (tm.tm_sec < 0 || tm.tm_sec > 61)
      return -1;

    // Encode each field: requires about 40 bits, but safely encodes all values
    // without risk of timezone munging on Android
    range_encode_equiprobable(c, 10000, tm.tm_year);
    range_encode_equiprobable(c, 12, tm.tm_mon - 1);
    range_encode_equiprobable(c, 31, tm.tm_mday - 1);
    range_encode_equiprobable(c, 25, tm.tm_hour);
    range_encode_equiprobable(c, 60, tm.tm_min);
    return range_encode_equiprobable(c, 62, tm.tm_sec);
  }
  case FIELDTYPE_DATE:
    // ODK does YYYY/MM/DD
    // Magpi does MM-DD-YYYY.  Some other things may do DD-MM-YYYY
    // The different delimiter allows us to discern between the two main
    // formats. For US versus Standard MDY vs DMY ordering, we really don't have
    // any choice but to remember it. So as a result, we allow months to be 1-31
    // as well.
    LOGE("Parsing FIELDTYPE_DATE value '%s'", value);
    int separator = 0;
    if (sscanf(value, "%d/%d/%d", &y, &m, &d) == 3) {
      separator = '/';
    } else if (sscanf(value, "%d-%d-%d", &d, &m, &y) == 3) {
      separator = '-';
    } else
      return -1;

    // XXX Not as efficient as it could be (assumes all months have 31 days)
    if (y < 1 || y > 9999 || m < 1 || m > 31 || d < 1 || d > 31) {
      LOGE("Invalid field value");
      return -1;
    }
    normalised_value = y * (31 * 31) + (m - 1) * 31 + (d - 1);
    minimum = 0;
    maximum = 10000 * 31 * 31;
    if (precision == 0)
      precision = 24; // 2^23 < maximum < 2^24
    if (precision < 24) {
      normalised_value = normalised_value >> (24 - precision);
      minimum = minimum >> (24 - precision);
      maximum = maximum >> (24 - precision);
      maximum += 1; // make sure that normalised_value cannot = maximum
    }
    if (separator == '/')
      range_encode_equiprobable(c, 2, 1);
    else
      range_encode_equiprobable(c, 2, 0);
    return range_encode_equiprobable(c, maximum - minimum + 1,
                                     normalised_value);
  case FIELDTYPE_LATLONG:
    // Allow space or comma between LAT and LONG
    if ((sscanf(value, "%f %f", &lat, &lon) != 2) &&
        (sscanf(value, "%f,%f", &lat, &lon) != 2))
      return -1;
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180)
      return -1;
    ilat = lroundf(lat);
    ilon = lroundf(lon);
    ilat += 90;  // range now 0..181 (for -90 to +90, inclusive)
    ilon += 180; // range now 0..360 (for -180 to +180, inclusive)
    if (precision == 16) {
      // gradicule resolution
      range_encode_equiprobable(c, 182, ilat);
      return range_encode_equiprobable(c, 361, ilon);
    } else if (precision == 0 || precision == 34) {
      // ~1m resolution
      ilat = lroundf(lat * 112000);
      ilon = lroundf(lon * 112000);
      ilat += 90 * 112000;  // range now 0..181 (for -90 to +90, inclusive)
      ilon += 180 * 112000; // range now 0..359 (for -179 to +180, inclusive)
      // encode latitude
      range_encode_equiprobable(c, 182 * 112000, ilat);
      return range_encode_equiprobable(c, 361 * 112000, ilon);
    } else
      return -1;
  case FIELDTYPE_MULTISELECT: {
    // Multiselect has labels for each item selected, with a pipe
    // character in between.  We encode each as a boolean using 1 bit.
    unsigned long long bits = 0;
    int o = 0;
    // Generate bitmask of selected items
    while (o < strlen(value)) {
      char valuetext[1024];
      int vtlen = 0;
      while (value[o] != '|' && value[o]) {
        if (vtlen < 1000)
          valuetext[vtlen++] = value[o];
        o++;
      }
      valuetext[vtlen] = 0;

      int v = 0;
      struct enum_value *val = field->enum_value;
      while (val) {
        if (!strcasecmp(valuetext, val->value))
          break;
        val = val->next;
        v++;
      }
      if (v < field->enum_count)
        bits |= (1 << v);
      if (value[o] == '|')
        o++;
    }
    // Encode each checkbox using a single bit
    for (o = 0; o < field->enum_count; o++) {
      // Work out whether box is ticked
      int n = ((bits >> o) & 1);
      range_encode_equiprobable(c, 2, n);
    }
    return 0;
    break;
  }
  case FIELDTYPE_ENUM: {
    struct enum_value *val = field->enum_value;
    normalised_value = 0;
    while (val) {
      if (!strcasecmp(value, val->value))
        break;

      val = val->next;
      normalised_value++;
    }
    if (normalised_value >= field->enum_count) {
      LOGE("Value '%s' is not in enum list for '%s'.", value, field->name);
      return -1;
    }
    maximum = field->enum_count;
    LOGI("enum: encoding %s as %d of %d", value, normalised_value, maximum);
    return range_encode_equiprobable(c, maximum, normalised_value);
  }
  case FIELDTYPE_TEXT: {
    int before = c->bits_used;
    // Trim to precision specified length if non-zero
    if (field->precision > 0) {
      if (strlen(value) > field->precision)
        value[field->precision] = 0;
    }
    int r = stats3_compress_append(c, (unsigned char *)value, strlen(value),
                                   stats, NULL);
    LOGI("'%s' encoded in %d bits", value, c->bits_used - before);
    if (r)
      return -1;
    return 0;
  }
  case FIELDTYPE_MAGPIUUID:
    // 64bit hex followed by milliseconds since UNIX epoch (48 bits to last us
    // many centuries)
    {
      int i, j = 0;
      unsigned char uuid[8];
      i = 0;
      for (i = 0; i < 16; i += 2) {
        uuid[j] = parseHexByte(&value[i]);
        range_encode_equiprobable(c, 256, uuid[j++]);
      }
      long long timestamp = strtoll(&value[17], NULL, 10);
      timestamp &= 0xffffffffffffLL;
      for (i = 0; i < 6; i++) {
        int b = (timestamp >> 40LL) & 0xff;
        range_encode_equiprobable(c, 256, b);
        timestamp = timestamp << 8LL;
      }
      return 0;
    }
  case FIELDTYPE_UUID: {
    // Parse out the 128 bits (=16 bytes) of UUID, and encode as much as we have
    // been asked.
    // XXX Will accept all kinds of rubbish
    int i, j = 0;
    unsigned char uuid[16];
    i = 0;
    if (!strncasecmp(value, "uuid:", 5))
      i = 5;
    for (; value[i]; i++) {
      if (j == 16) {
        j = 17;
        break;
      }
      if (value[i] != '-') {
        uuid[j++] = parseHexByte(&value[i]);
        i++;
      }
    }
    if (j != 16) {
      LOGE("Malformed UUID field.");
      return -1;
    }
    // write appropriate number of bytes
    int precision = field->precision;
    if (precision < 1 || precision > 16)
      precision = 16;
    for (i = 0; i < precision; i++) {
      range_encode_equiprobable(c, 256, uuid[i]);
    }
    return 0;
  }
  }

  return -1;
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

int recipe_decompress_with_hash(stats_handle *h, char *recipe_dir,
                                unsigned char *in, int in_len, char *out,
                                int out_size, char *recipe_name) {

  if (!recipe_dir) {
    LOGE("No recipe directory provided.");
    return -1;
  }

  if (!in) {
    LOGE("No input provided.");
    return -1;
  }
  if (!out) {
    LOGE("No output buffer provided.");
    return -1;
  }
  if (in_len >= 1024) {
    LOGE("Input must be <1KB.");
    return -1;
  }

  // Make new range coder with 1KB of space
  range_coder *c = range_new_coder(1024);

  // Point range coder bit stream to input buffer
  bcopy(in, c->bit_stream, in_len);
  c->bit_stream_length = in_len * 8;
  range_decode_prefetch(c);

  // Read form id hash from the succinct data stream.
  unsigned char formhash[6];
  int i;
  for (i = 0; i < 6; i++)
    formhash[i] = range_decode_equiprobable(c, 256);
  LOGI("formhash from succinct data message = %02x%02x%02x%02x%02x%02x",
       formhash[0], formhash[1], formhash[2], formhash[3], formhash[4],
       formhash[5]);

  struct recipe *recipe = recipe_find_recipe(recipe_dir, formhash);

  int r = recipe_decompress(h, recipe, recipe_dir, out, out_size, recipe_name,
                            c, false, false);

  recipe_free(recipe);
  range_coder_free(c);

  return r;
}

int recipe_decompress(stats_handle *h, struct recipe *recipe, char *recipe_dir,
                      char *out, int out_size, char *recipe_name,
                      range_coder *c, bool is_subform, bool is_record) {

  if (!recipe) {
    LOGE("No recipe provided.");
    return -1;
  }
  snprintf(recipe_name, 1024, "%s", recipe->formname);

  int written = 0;
  int i = 0;
  int limit_in_recipe;
  int r2;

  struct field *field = recipe->field_list;
  // If its a new record to write, we restart the loop into the recipe but we
  // skip the meta fields (formid and question).
  if (!is_record) {
    i = 0;
  } else {
    i = 2;
    field = field->next->next;
    r2 = snprintf(&out[written], out_size - written, "{\n");
  }

  // If we write a subform instance, we start with meta fields and there are
  // only 2 meta fields for subforms (formid and question)
  if (is_subform && !is_record) {
    limit_in_recipe = 2;
  } else {
    limit_in_recipe = recipe->field_count;
  }

  for (; i < limit_in_recipe; i++) {
    int field_present = range_decode_equiprobable(c, 2);

    LOGI("%sdecompressing value for '%s' of type '%d'",
         field_present ? "" : "not ", field->name,
         field->type);

    char *underscore = strchr(field->name, '/');

    // If we find the underscore in the name, it is a subform...
    char *subformID;
    if (underscore != NULL) {
      subformID = strdup(underscore + 1);
    }

    if (field_present && underscore == NULL) {
      char value[1024];
      int r = recipe_decode_field(field, h, c, value, 1024);
      if (r) {
        return -1;
      }
      LOGI("  the value is '%s'", value);

      r2 = snprintf(&out[written], out_size - written, "%s=%s\n",
                    field->name, value);
      if (r2 > 0)
        written += r2;
    }
    // The type is a subform
    else if (field_present && underscore != NULL) {

      r2 = snprintf(&out[written], out_size - written, "{\n");
      if (r2 > 0)
        written += r2;

      char recipe_path[1024];

      snprintf(recipe_path, 1024, "%s/%s.%s", recipe_dir, subformID, "recipe");

      struct recipe *subform_recipe = recipe_read_from_file(recipe_path);

      recipe_decompress(h, subform_recipe, recipe_dir, out, out_size,
                        recipe_name, c, true, false);

      r2 = snprintf(&out[written], out_size - written, "}\n");
      if (r2 > 0)
        written += r2;
    } else {
      // Field not present.
      // Magpi uses ~ to indicate an empty field, so insert.
      // ODK Collect shouldn't care about the presence of the ~'s, so we
      // will always insert them.
      int r2 = snprintf(&out[written], out_size - written, "%s=~\n",
                        field->name);
      if (r2 > 0)
        written += r2;
    }
    field = field->next;
  }

  if (is_subform && is_record) {
    r2 = snprintf(&out[written], out_size - written, "}\n");
  } else if (is_subform) {
    if (range_decode_equiprobable(c, 2)) {
      // The coder detected that another record has to be written
      // We have to reload the recipe
      // Last argument is true because it's a record to write (=> skip meta
      // fields)
      recipe_decompress(h, recipe, recipe_dir, out, out_size, recipe_name, c,
                        true, true);
    }
  }

  return written;
}

int recipe_compress(stats_handle *h, char *recipe_dir, struct recipe *recipe,
                    char *in, int in_len, unsigned char *out, int out_size) {
  /*
    Eventually we want to support full skip logic, repeatable sections and so
    on. For now we will allow skip sections by indicating missing fields. This
    approach lets us specify fields implicitly by their order in the recipe (NOT
    in the completed form). This entails parsing the completed form, and then
    iterating through the RECIPE and considering each field in turn.  A single
    bit per field will be used to indicate whether it is present.  This can be
    optimized later.
  */

  if (!recipe) {
    LOGE("No recipe provided.");
    return -1;
  }
  if (!in) {
    LOGE("No input provided.");
    return -1;
  }
  if (!out) {
    LOGE("No output buffer provided.");
    return -1;
  }

  // Make new range coder with 1KB of space
  range_coder *c = range_new_coder(1024);
  if (!c) {
    LOGE("Could not instantiate range coder.");
    return -1;
  }

  // Write form hash first
  int i;
  LOGI("form hash = %02x%02x%02x%02x%02x%02x", recipe->formhash[0],
       recipe->formhash[1], recipe->formhash[2], recipe->formhash[3],
       recipe->formhash[4], recipe->formhash[5]);
  for (i = 0; i < sizeof(recipe->formhash); i++)
    range_encode_equiprobable(c, 256, recipe->formhash[i]);

  struct record *record = parse_stripped_with_subforms(in, in_len);
  if (!record) {
    range_coder_free(c);
    return -1;
  }

  int out_count =
      compress_record_with_subforms(recipe_dir, recipe, record, c, h);
  record_free(record);
  if (out_count < 0) {
    range_coder_free(c);
    return -1;
  }

  // Get result and store it, unless it is too big for the output buffer
  range_conclude(c);
  int bytes = (c->bits_used / 8) + ((c->bits_used & 7) ? 1 : 0);
  if (bytes > out_size) {
    range_coder_free(c);
    LOGE("Compressed data too big for output buffer");
    return -1;
  }

  bcopy(c->bit_stream, out, bytes);
  LOGI("Used %d bits (%d bytes).", c->bits_used, bytes);

  range_coder_free(c);

  return bytes;
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

  char recipe_file[1024];

  sprintf(recipe_file, "%s/%s.recipe", recipe_dir, formid);
  LOGI("Trying to load '%s' as a recipe", recipe_file);
  struct recipe *recipe = recipe_read_from_file(recipe_file);
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
  int r = recipe_compress(h, recipe_dir, recipe, (char *)stripped, stripped_len,
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

int recipe_stripped_to_csv_line(char *recipe_dir, char *recipe_name,
                                char *output_dir, char *stripped,
                                int stripped_data_len, char *csv_out,
                                int csv_out_size) {
  // Read recipe, CSV encode each field if present, append fields to line,
  // return.
  char recipe_file[1024];
  snprintf(recipe_file, 1024, "%s/%s.recipe", recipe_dir, recipe_name);
  LOGE("Reading recipe from '%s' for CSV generation.", recipe_file);

  if (csv_out_size < 8192) {
    LOGE("Not enough space to extract CSV line.");
    return -1;
  }

  int state = 0;
  int i;

  char *fieldnames[1024];
  char *values[1024];
  int field_count = 0;

  char field[1024];
  int field_len = 0;

  char value[1024];
  int value_len = 0;

  // Read fields from stripped.
  for (i = 0; i < stripped_data_len; i++) {
    if (stripped[i] == '=' && (state == 0)) {
      state = 1;
    } else if (stripped[i] < ' ') {
      if (state == 1) {
        // record field=value pair
        field[field_len] = 0;
        value[value_len] = 0;
        fieldnames[field_count] = strdup(field);
        values[field_count] = strdup(value);
        field_count++;
      }
      state = 0;
      field_len = 0;
      value_len = 0;
    } else {
      if (field_len > 1000 || value_len > 1000)
        return -1;
      if (state == 0)
        field[field_len++] = stripped[i];
      else
        value[value_len++] = stripped[i];
    }
  }

  struct recipe *r = recipe_read_from_file(recipe_file);
  if (!r) {
    LOGE("Failed to read recipe file '%s' during CSV extraction.",
         recipe_file);
    return -1;
  }

  int n = 0;

  struct field *field_def = r->field_list;
  while(field_def){
    char *v = "";
    for (i = 0; i < field_count; i++) {
      if (!strcasecmp(fieldnames[i], field_def->name)) {
        v = values[i];
        break;
      }
    }
    n += snprintf(&csv_out[n], 8192 - n, "%s%s", n ? "," : "", v);
    field_def = field_def->next;
  }
  recipe_free(r);

  for (i = 0; i < field_count; i++) {
    free(fieldnames[i]);
    free(values[i]);
  }

  csv_out[n++] = '\n';
  csv_out[n] = 0;

  return 0;
}

int recipe_decompress_file(stats_handle *h, char *recipe_dir, char *input_file,
                           char *output_directory) {
  // struct recipe *recipe=recipe_read_from_file(recipe_file);
  // if (!recipe) return -1;

  unsigned char *buffer;

  int fd = open(input_file, O_RDONLY);
  if (fd == -1) {
    LOGE("Could not open succinct data file '%s'", input_file);
    return -1;
  }

  struct stat st;
  if (fstat(fd, &st) == -1) {
    LOGE("Could not stat succinct data file '%s'", input_file);
    close(fd);
    return -1;
  }

  buffer = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (buffer == MAP_FAILED) {
    LOGE("Could not memory map succinct data file '%s'", input_file);
    close(fd);
    return -1;
  }

  LOGI("About to call recipe_decompress");
  char recipe_name[1024] = "";
  char out_buffer[1048576];

  int r = recipe_decompress_with_hash(h, recipe_dir, buffer, st.st_size,
                                      out_buffer, 1048576, recipe_name);

  //  Older way to call the decompress function
  //  int r=recipe_compress(h,recipe_dir,buffer,st.st_size,out_buffer,1048576,
  //  		  recipe_name);

  LOGI("Got back from recipe_decompress: r=%d, fd=%d, st.st_size=%d, buffer=%p",
       r, fd, (int)st.st_size, buffer);

  LOGI("%s:%d", __FILE__, __LINE__);
  munmap(buffer, st.st_size);
  LOGI("%s:%d", __FILE__, __LINE__);
  close(fd);
  LOGI("%s:%d", __FILE__, __LINE__);

  if (r < 0) {
    LOGI("%s:%d", __FILE__, __LINE__);
    // LOGE("Could not find matching recipe file for
    // %s.",input_file);
    LOGI("Could not find matching recipe file for %s.", input_file);
    return -1;
  }
  LOGI("%s:%d", __FILE__, __LINE__);

  char stripped_name[80];
  MD5_CTX md5;
  unsigned char hash[16];
  char output_file[1024];

  MD5_Init(&md5);
  MD5_Update(&md5, out_buffer, r);
  MD5_Final(hash, &md5);
  snprintf(stripped_name, 80, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
           hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6],
           hash[7], hash[8], hash[9]);

  // make directories if required
  snprintf(output_file, 1024, "%s/%s", output_directory, recipe_name);
  mkdir(output_file, 0777);

  // now write stripped file out
  LOGI("Writing stripped data to %s", stripped_name);
  snprintf(output_file, 1024, "%s/%s/%s.stripped", output_directory,
           recipe_name, stripped_name);

  if (stat(output_file, &st)) {
    // Stripped file does not yet exist, so append line to CSV file.
    char line[8192];
    if (!recipe_stripped_to_csv_line(recipe_dir, recipe_name, output_directory,
                                     out_buffer, r, line, 8192)) {
      char csv_file[1024];
      snprintf(csv_file, 1024, "%s", output_directory);
      mkdir(csv_file, 0777);
      snprintf(csv_file, 1024, "%s/csv", output_directory);
      mkdir(csv_file, 0777);
      snprintf(csv_file, 1024, "%s/csv/%s.csv", output_directory, recipe_name);
      FILE *f = fopen(csv_file, "a");
      LOGI("Appending CSV line: %s", line);
      if (f) {
        int wrote = fwrite(line, strlen(line), 1, f);
        if (wrote < strlen(line)) {
          LOGE("Failed to produce CSV line (short write)");
        }
      }
      fclose(f);
    } else {
      LOGE("Failed to produce CSV line.");
    }
  } else {
    LOGE("Not writing CSV line for form, as we have already seen it.");
  }

  FILE *f = fopen(output_file, "w");
  if (!f) {
    LOGE("Could not write decompressed file '%s'", output_file);
    return -1;
  }
  int wrote = fwrite(out_buffer, r, 1, f);
  fclose(f);
  if (wrote != 1) {
    LOGE("Could not write %d bytes of decompressed data into '%s'", r,
         output_file);
    return -1;
  }

  // now produce the XML.
  // We need to give it the template file.  Fortunately, we know the recipe
  // name, so we can build the template path from that.
  char template_file[1024];
  snprintf(template_file, 1024, "%s/%s.template", recipe_dir, recipe_name);
  char template[1048576];
  int template_len =
      recipe_load_file(template_file, template, sizeof(template));
  if (template_len < 1) {
    LOGE("Could not read template file '%s'", template_file);
    return -1;
  }
  char xml[65536];

  int x = stripped2xml(out_buffer, r, template, template_len, xml, sizeof(xml));

  char xml_file[1024];
  snprintf(xml_file, 1024, "%s/%s/%s.xml", output_directory, recipe_name,
           stripped_name);
  f = fopen(xml_file, "w");
  if (!f) {
    LOGE("Could not write xml file '%s'", xml_file);
    return -1;
  }
  wrote = fwrite(xml, x, 1, f);
  fclose(f);
  if (wrote != 1) {
    LOGE("Could not write %d bytes of XML data into '%s'", x, xml_file);
    return -1;
  }

  LOGI("Finished extracting succinct data file.");
  if (r != -1) {
    // Mark file as processed, so that we can clean up after ourselves
    char file[8192];
    snprintf(file, 8192, "%s.processed", input_file);
    int fd = open(file, O_RDWR | O_CREAT, 0777);
    if (fd != -1)
      close(fd);
  } else {
    LOGE("Decompression of SD file result code = %d", r);
  }
  return r;
}

int recipe_main(int argc, char *argv[]) {
  if (argc <= 2) {
    LOGE("'smac recipe' command requires further arguments.");
    return -1;
  }

  if (!strcasecmp(argv[2], "parse")) {
    if (argc <= 3) {
      LOGE("'smac recipe parse' requires name of recipe to load.");
      return (-1);
    }
    struct recipe *recipe = recipe_read_from_file(argv[3]);
    if (!recipe) {
      return (-1);
    }
    LOGI("recipe=%p", recipe);
    LOGI("recipe->field_count=%d", recipe->field_count);
  } else if (!strcasecmp(argv[2], "compress")) {
    if (argc <= 5) {
      LOGE("'smac recipe compress' requires recipe directory, input "
           "and output files.");
      return (-1);
    }
    stats_handle *h=stats_new_handle("stats.dat");
    if (!h) {
      char working_dir[1024];
      getcwd(working_dir,1024);
      LOGE("Could not read stats.dat (pwd='%s').",working_dir);
      return -1;
    }
    /* Preload tree for speed */
    stats_load_tree(h);

    LOGI(
        "Test-Dialog: About to compress the stripped data file: %s into: %s ",
        argv[4], argv[5]);

    int r = recipe_compress_file(h, argv[3], argv[4], argv[5]);
    stats_handle_free(h);
    return r;
  } else if (!strcasecmp(argv[2], "map")) {
    if (argc <= 4) {
      LOGE("usage: smac map <recipe directory> <output directory>");
      return (-1);
    }
    return generateMaps(argv[3], argv[4]);
  } else if (!strcasecmp(argv[2], "encrypt")) {
    if (argc <= 6) {
      LOGE("usage: smac encrypt <file> <MTU> <output directory> "
           "<public key hex>");
      return (-1);
    }
    return encryptAndFragment(argv[3], atoi(argv[4]), argv[5], argv[6]);
  } else if (!strcasecmp(argv[2], "decrypt")) {
    if (argc <= 5) {
      LOGE("usage: smac decrypt <input directory> <output "
           "directory> <pass phrase>");
      return (-1);
    }
    return defragmentAndDecrypt(argv[3], argv[4], argv[5]);
  } else if (!strcasecmp(argv[2], "create")) {
    if (argc <= 3) {
      LOGE("usage: smac recipe create <XML form> ");
      return (-1);
    }
    return recipe_create(argv[3]);
  } else if (!strcasecmp(argv[2], "xhcreate")) {
    if (argc <= 4) {
      LOGE("usage: smac recipe xhcreate <recipe directory> <XHTML form>");
      return (-1);
    }
    LOGI("Test-Dialog: About to XHCreate/Create recipe file(s) from the "
           "XHTML form %s ",
           argv[3]);
    return xhtml_recipe_create(argv[3], argv[4]);
  } else if (!strcasecmp(argv[2], "decompress")) {
    if (argc <= 5) {
      LOGE("usage: smac recipe decompress <recipe directory> "
           "<succinct data message> <output directory>");
      return (-1);
    }
    stats_handle *h=stats_new_handle("stats.dat");
    if (!h) {
      char working_dir[1024];
      getcwd(working_dir,1024);
      LOGE("Could not read stats.dat (pwd='%s').",working_dir);
      return -1;
    }
    /* Preload tree for speed */
    stats_load_tree(h);
    // If succinct data message is a directory, try decompressing all files in
    // it.
    struct stat st;
    if (stat(argv[4], &st)) {
      perror("Could not stat succinct data file/directory.");
      return (-1);
    }
    if (st.st_mode & S_IFDIR) {
      // Directory: so process each file inside
      DIR *dir = opendir(argv[4]);
      struct dirent *de = NULL;
      char filename[1024];
      int e = 0;
      while ((de = readdir(dir)) != NULL) {
        snprintf(filename, 1024, "%s/%s", argv[4], de->d_name);
        LOGI("Trying to decompress %s as succinct data message", filename);
        if (recipe_decompress_file(h, argv[3], filename, argv[5]) == -1) {
          LOGI("Failed to decompress %s as succinct data message", filename);
          e++;
        } else {
          LOGE("Decompressed %s", filename);
          LOGI("Decompressed succinct data message %s", filename);
        }
      }
      closedir(dir);
      LOGI("Finished extracting files.  %d failures.", e);
      stats_handle_free(h);
      if (e)
        return 1;
      else
        return 0;
    } else {
      LOGI("Test-Dialog: About to decompress the succinct data (SD) file: %s "
             "into: %s",
             argv[4], argv[5]);
      int r = recipe_decompress_file(h, argv[3], argv[4], argv[5]);
      stats_handle_free(h);
      return r;
    }
  } else if (!strcasecmp(argv[2], "strip")) {
    char stripped[65536];
    char xml_data[1048576];
    int xml_len = 0;
    if (argc < 4) {
      LOGE("usage: smac recipe strip <xml input> [stripped output].");
      return -1;
    }
    LOGI("Test-Dialog: About to strip the XML record: %s into the following "
           "file: %s",
           argv[3], argv[4]);
    xml_len = recipe_load_file(argv[3], xml_data, sizeof(xml_data));
    int stripped_len =
        xml2stripped(NULL, xml_data, xml_len, stripped, sizeof(stripped));
    if (stripped_len < 0) {
      LOGE("Failed to strip '%s'", argv[3]);
      return -1;
    }
    if (argv[4] == NULL)
      printf("%s", stripped);
    else {
      FILE *f = fopen(argv[4], "w");
      if (!f) {
        LOGE("Failed to write stripped output to '%s'", argv[4]);
        return -1;
      }
      fprintf(f, "%s", stripped);
      fclose(f);
      return 0;
    }
  } else if (!strcasecmp(argv[2], "rexml")) {
    char stripped[8192];
    int stripped_len = 0;
    char template[65536];
    int template_len = 0;
    char xml[65536];
    if (argc < 5) {
      LOGE("usage: smac recipe rexml <stripped> <template> [xml output].");
      return -1;
    }
    stripped_len = recipe_load_file(argv[3], stripped, sizeof(stripped));
    if (stripped_len < 0) {
      LOGE("Failed to read '%s'", argv[3]);
      return -1;
    }
    template_len = recipe_load_file(argv[4], template, sizeof(template));
    if (template_len < 0) {
      LOGE("Failed to read '%s'", argv[4]);
      return -1;
    }
    LOGI("Test-Dialog: About to rexml the stripped file: %s into the "
           "following file: %s ",
           argv[3], argv[5]);
    int xml_len = stripped2xml(stripped, stripped_len, template, template_len,
                               xml, sizeof(xml));
    if (xml_len < 0) {
      LOGE("Failed to rexml '%s'", argv[3]);
      return -1;
    }
    if (argv[5] == NULL)
      printf("%s", xml);
    else {
      FILE *f = fopen(argv[5], "w");
      if (!f) {
        LOGE("Failed to write rexml output to '%s'", argv[5]);
        return -1;
      }
      fprintf(f, "%s", xml);
      fclose(f);
      return 0;
    }

  } else {
    LOGE("unknown 'smac recipe' sub-command '%s'.", argv[2]);
    return -1;
  }

  return 0;
}

#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "recipe.h"
#include "packed_stats.h"
#include "smac.h"
#include "log.h"
#include "subforms.h"
#include "md5.h"

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


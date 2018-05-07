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

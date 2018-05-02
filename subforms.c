/*
Copyright (C) 2016 Paul Gardner-Stephen, Flinders University.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "arithmetic.h"
#include "charset.h"
#include "log.h"
#include "packed_stats.h"
#include "recipe.h"
#include "smac.h"
#include "subforms.h"
#include "visualise.h"

int record_free(struct record *r) {
  LOGI("record_free(%p)\n", r);
  if (!r)
    return -1;
  for (int i = 0; i < r->field_count; i++) {
    if (r->fields[i].key)
      free(r->fields[i].key);
    r->fields[i].key = NULL;
    if (r->fields[i].value)
      free(r->fields[i].value);
    r->fields[i].value = NULL;
    if (r->fields[i].subrecord)
      record_free(r->fields[i].subrecord);
    r->fields[i].subrecord = NULL;
  }
  free(r);
  return 0;
}

#define INDENT(X) &"                    "[20 - X]
void dump_record_r(struct record *r, int offset) {
  if (!r)
    return;

  LOGI("%sRecord @ %p:\n", INDENT(offset), r);
  for (int i = 0; i < r->field_count; i++) {
    if (r->fields[i].key) {
      LOGI("%s  [%s]=[%s]\n", INDENT(offset), r->fields[i].key,
           r->fields[i].value);
    } else {
      dump_record_r(r->fields[i].subrecord, offset + 2);
    }
  }
}

void dump_record(struct record *r) { dump_record_r(r, 0); }

struct record *parse_stripped_with_subforms(char *in, int in_len) {
  struct record *record = calloc(sizeof(struct record), 1);
  assert(record);
  LOGI("record is %p\n", record);
  struct record *current_record = record;
  int i;
  char line[1024];
  char key[1024], value[1024];
  int line_number = 1;
  int l = 0;

  for (i = 0; i <= in_len; i++) {
    if ((i == in_len) || (in[i] == '\n') || (in[i] == '\r')) {
      // Process key=value line
      if (l >= 1000)
        l = 0; // ignore long lines
      line[l] = 0;
      LOGI(">> processing line @ %d '%s'\n", i, line);
      if (line[0] == '{') {
        // Start of sub-form
        // Move current record down into a new sub-record at this point

        if (current_record->field_count >= MAX_FIELDS) {
          LOGE("line:%d:Too many data lines (must be <=%d, or increase "
               "MAX_FIELDS).\n",
               line_number, MAX_FIELDS);
          record_free(record);
          return NULL;
        }

        {
          struct record *sub_record = calloc(sizeof(struct record), 1);
          assert(sub_record);
          sub_record->parent = current_record;

          current_record->fields[current_record->field_count].subrecord =
              sub_record;
          current_record->field_count++;
          current_record = sub_record;
          LOGI("Nesting down to sub-record at %p\n", current_record);
        }
      } else if (line[0] == '}') {
        // End of sub-form
        if (!current_record->parent) {
          LOGE("line:%d:} without matching {.\n", line_number);
          record_free(record);
          return NULL;
        }
        LOGI("Popping up to parent record at %p\n", current_record->parent);

        // Find the question field name, so that we can promote it to our caller
        char *question = NULL;
        for (int i = 0; i < current_record->field_count; i++) {
          if (current_record->fields[i].key)
            if (!strcmp("question", current_record->fields[i].key)) {
              // Found it
              question = current_record->fields[i].value;
            }
        }
        if ((!question) && (current_record->parent)) {
          // question field in typically in the surrounding enclosure
          for (int i = 0; i < current_record->parent->field_count; i++) {
            if (current_record->parent->fields[i].key)
              if (!strcmp("question", current_record->parent->fields[i].key)) {
                // Found it
                question = current_record->parent->fields[i].value;
              }
          }
        }
        if (!question) {
          LOGE("line:%d:No 'question' value in sub-form.\n", line_number);
          record_free(record);
          return NULL;
        }

        // Step back up to parent
        current_record = current_record->parent;

      } else if ((l > 0) && (line[0] != '#')) {
        if (sscanf(line, "%[^=]=%[^\n]", key, value) == 2) {
          if (current_record->field_count >= MAX_FIELDS) {
            LOGE("line:%d:Too many data lines (must be <=%d, or increase "
                 "MAX_FIELDS).\n",
                 line_number, MAX_FIELDS);
            record_free(record);
            return NULL;
          }
          LOGI("[%s]=[%s]\n", key, value);
          current_record->fields[current_record->field_count].key = strdup(key);
          current_record->fields[current_record->field_count].value =
              strdup(value);
          current_record->field_count++;
        } else {
          LOGE("line:%d:Malformed data line (%s:%d): '%s'\n", line_number,
               __FILE__, __LINE__, line);
          record_free(record);
          return NULL;
        }
      }
      line_number++;
      l = 0;
    } else {
      if (l < 1000) {
        line[l++] = in[i];
      } else {
        if (l == 1000) {
          LOGE("line:%d:Line too long -- ignoring (must be < 1000 "
               "characters).\n",
               line_number);
        }
        l++;
      }
    }
  }

  if (current_record->parent) {
    LOGE("line:%d:End of input, but } expected.\n", line_number);
    record_free(record);
    return NULL;
  }

  LOGI("Read %d data lines, %d values.\n", line_number, record->field_count);

  // dump_record(record);

  return record;
}

int compress_record_with_subforms(char *recipe_dir, struct recipe *recipe,
                                  struct record *record, range_coder *c,
                                  stats_handle *h) {
  int i;
  struct field *field = recipe->field_list;
  while (field) {
    // look for this field in keys[]
    if (!strncasecmp(field->name, "subform", strlen("subform"))) {
      LOGI("Spotted subform '%s'\n", field->name);
      char recipe_file[1024];
      char *formid = &field->name[strlen("subform/")];
      sprintf(recipe_file, "%s/%s.recipe", recipe_dir, formid);
      LOGI("Trying to load '%s' as a recipe\n", recipe_file);
      struct recipe *recipe = recipe_read_from_file(recipe_file);
      if (!recipe) {
        return -1;
      }

      /* We are now set to encode however many instances of this sub-form
         occur for this question.
         We use a binary decision before each.
         But first, we have to find the correct field in the record
      */
      for (i = 0; i < record->field_count; i++) {
        if (record->fields[i].subrecord) {
          struct record *s = record->fields[i].subrecord;
          // Here is a sub-record enclosure. Is it the right one?
          // Look for a formid= key pair, and check the value
          int found = 0;
          for (int j = 0; j < s->field_count; j++)
            if (s->fields[j].key)
              if (!strcasecmp(s->fields[j].key, "formid"))
                if (!strcasecmp(s->fields[j].value, formid)) {
                  // Bingo, we have found it.
                  found = 1;
                  break;
                }
          if (found) {
            LOGI("Found enclosure for this sub-form in %p\n", s);
            for (int j = 0; j < s->field_count; j++) {
              if (s->fields[j].subrecord) {
                LOGI("  Found sub-record in field #%d\n", j);
                struct record *ss = s->fields[j].subrecord;
                for (int k = 0; k < ss->field_count; k++)
                  LOGI("    [%s]=[%s]\n",
                       ss->fields[k].key ? ss->fields[k].key : "NULL",
                       ss->fields[k].value ? ss->fields[k].value : "NULL");
                range_encode_equiprobable(c, 2, 1);
                compress_record_with_subforms(recipe_dir, recipe, ss, c, h);
              }
            }
          }
        }
      }
      // Mark end of list of instances of sub-records for this question
      range_encode_equiprobable(c, 2, 0);

    } else {
      for (i = 0; i < record->field_count; i++) {
        if (record->fields[i].key)
          if (!strcasecmp(record->fields[i].key, field->name))
            break;
      }
      if (i < record->field_count) {
        // Field present
        LOGI("Found field ('%s', value '%s')\n", field->name,
             record->fields[i].key);
        // Record that the field is present.
        range_encode_equiprobable(c, 2, 1);
        // Now, based on type of field, encode it.
        if (recipe_encode_field(field, h, c, record->fields[i].value)) {
          range_coder_free(c);
          LOGE("Could not record value '%s' for field '%s' (type %d)\n",
               record->fields[i].key, field->name, field->type);
          return -1;
        }
        LOGI(" ... encoded value '%s'", record->fields[i].key);
      } else {
        // Field missing: record this fact and nothing else.
        LOGI("No field ('%s')\n", field->name);
        range_encode_equiprobable(c, 2, 0);
      }
    }
    field = field->next;
  }

  // Successfully compressed -- return
  return 0;
}

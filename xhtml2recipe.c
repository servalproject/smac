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
#include <malloc.h>
#include <stdlib.h>
#include <assert.h>

#include "log.h"
#include "recipe.h"
#include "expat.h"

#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))
#define MAXSUBFORMS 10
#define MAXCHARS 1000000

struct str_builder{
    size_t str_len;
    size_t alloc_len;
    char *str;
};

typedef struct node_recipe {
    bool is_subform_node; //false if it's a subform recipe, true if not
    char formname[1024]; //Form name
    char formversion[1024]; //Form ID/version
    struct str_builder recipe;
    struct str_builder selects[1024];//Temporary buffers to parse the recipe selects
    int xhtmlSelectsLen;//Length of selects
    struct node_recipe *next;
} recipe_node;

typedef struct node_tag {
    struct node_tag *next;
    char *tagname; //Name of the tag (question on the form)
    char *formversion; //Form version of the parent form of the tag
    char buff[]; // store the two strings at the end of the struct
} tag_node;

typedef struct node_subform {
    struct node_subform *next;
    struct node_subform *prev;
    char subformid[]; //ID of the subform
} subform_node;

struct parse_state {
    recipe_node *head_r;
    recipe_node *curr_r;

    tag_node *head_t;
    tag_node *curr_t;

    subform_node *head_s;
    subform_node *curr_s;

    //TODO 30.05.2014 	: Handle if there are two fields with same name
    //		            : Add tests
    //		            : Improve constraints work
    //
    //Creation specification stripped file from ODK XML
    //FieldName:Type:Minimum:Maximum:Precision,Select1,Select2,...,SelectN

    char *xhtmlFormName, *xhtmlFormVersion;

    struct str_builder xhtml2template;

    char *xhtmlSelectElem;

    // Stuff that is concerning the form and not the recipes
    int xhtmlSelectFirst;
    int xhtml_in_value;
    int xhtml_in_instance;
    int xhtml_in_subform;

    char *subform_tags[MAXSUBFORMS];
};

static subform_node *add_to_list_subforms(struct parse_state *state, const char *formversion);

static tag_node *add_to_list_tags(struct parse_state *state, const char *tagname, const char *formversion,
                           bool add_to_end);

static recipe_node *
add_to_list_recipes(struct parse_state *state, const char *formversion, bool add_to_end);

static tag_node *search_in_list_tags(struct parse_state *state, const char *tagname, tag_node **prev);

static recipe_node *
search_in_list_recipes(struct parse_state *state, const char *formversion, recipe_node **prev);

const char *implied_meta_fields =
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

const char *implied_meta_fields_subforms =
        "formid:string:0:0:0\n"
                "question:string:0:0:0\n"
                "uuid:magpiuuid:0:0:0\n";

const char *implied_meta_fields_template =
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

const char *implied_meta_fields_template_subform =
        "<formid>$formid$</formid>\n"
                "<question>$question$</question>\n";

const char *implied_data_fields_template_subform =
        "<uuid>$uuid$</uuid>\n";

static void strgrow(struct str_builder *in, const char *new_segment) {
  if (!new_segment || !*new_segment)
    return;

  size_t len = strlen(new_segment);
  if (in->alloc_len < in->str_len + len +1){
    size_t new_len = in->alloc_len;
    if (new_len < 256)
	new_len = 256;
    while(new_len < in->str_len + len +1){
	new_len *= 2;
    }
    char *out = malloc(new_len);
    in->alloc_len = new_len;
    if (in->str){
	strcpy(out, in->str);
	free(in->str);
    }
    in->str = out;
  }
  strcpy(&in->str[in->str_len], new_segment);
  in->str_len+=len;
}

static void
start_xhtml(void *data, const char *el,
            const char **attr) //This function is called  by the XML library each time it sees a new tag
{
    struct parse_state *state = data;
    char temp[4096];
    char *node_name = "";
    const char *node_constraint = "", *temp_type = "";

    int i;
    int k;
    //Only to debug : Print each element
    //LOGI("(start_xhtml) element is: %s ",el);

    if (attr[0] && !strcmp(attr[0], "dd:formid")) {
	add_to_list_recipes(state, attr[1], true);
        state->curr_r->is_subform_node = false;
        strgrow(&state->curr_r->recipe, implied_meta_fields);
        add_to_list_subforms(state, attr[1]);
        LOGI("(start_xhtml) Form start ! : attr[0] = %s, curr_s -> formversion = %s", attr[0],
               state->curr_s->subformid);
    }

    if (state->xhtml_in_instance) { // We are between <instance> tags, so we want to get everything to create template file


        if (attr[0] && !strcmp(attr[0], "dd:subformid")) {
	    
            //linked lists stuff to structure well the data
            add_to_list_recipes(state, attr[1], true);
            strgrow(&state->curr_r->recipe, implied_meta_fields_subforms);
            state->curr_r->is_subform_node = true;
            add_to_list_subforms(state, attr[1]);
            add_to_list_tags(state, el, attr[1], true);
            //strcat(curr_s->nodeset,el);

            state->xhtml_in_subform++; //xhtml_in_subform represents the depth in subforms
            state->subform_tags[state->xhtml_in_subform] = (char *) el;
            LOGI("(start_xhtml) subform instance start ! : attr[0] = %s , curr_s -> formversion = %s",
                   attr[0], state->curr_s->subformid);

            strgrow(&state->xhtml2template, "<dd:subform xmlns:dd=\"http://datadyne.org/javarosa\"> \n");
            strgrow(&state->xhtml2template, "<meta> \n");
            strgrow(&state->xhtml2template, implied_meta_fields_template_subform);
            strgrow(&state->xhtml2template, "</meta> \n");
            strgrow(&state->xhtml2template, "<data> \n");
            strgrow(&state->xhtml2template, implied_data_fields_template_subform);

        } else {

            if (state->curr_s != NULL) {
                //linked lists stuff to structure well the data
                add_to_list_tags(state, el, state->curr_s->subformid, true);
            }

            strgrow(&state->xhtml2template, "<");
            strgrow(&state->xhtml2template, el);
            for (i = 0; attr[i]; i += 2) {
                strgrow(&state->xhtml2template, " ");
                strgrow(&state->xhtml2template, attr[i]);
                strgrow(&state->xhtml2template, "=\"");
                strgrow(&state->xhtml2template, attr[i + 1]);
                strgrow(&state->xhtml2template, "\"");
            }
            strgrow(&state->xhtml2template, ">$");
            strgrow(&state->xhtml2template, el);
            strgrow(&state->xhtml2template, "$");
        }

    }
        //Looking for bind elements to create the recipe file
    else if ((!strcasecmp("bind", el)) || (!strcasecmp("xf:bind", el))) {
	int type = -1;
        for (i = 0; attr[i]; i += 2) //Found a bind element, look for attributes
        {
            //Looking for attribute nodeset
            if (!strncasecmp("nodeset", attr[i], strlen("nodeset"))) {
                node_name = strrchr(attr[i + 1], '/')+1;

                //linked list stuff to structure the data

                //find the subform where this field is in
                state->curr_t = search_in_list_tags(state, node_name, NULL);

                for (k = 0; attr[k]; k += 2) //look for type attribute
                {
                    if (!strncasecmp("type", attr[k], strlen("type"))) {
                        LOGI("found temp type !! Temp type = %s ", attr[k + 1]);
                        temp_type = attr[k + 1];
                    }
                }

                if (state->curr_t != NULL && strcasecmp(temp_type, "xsd:subform")) {
                    //use the right recipe node in the linked list to parse info
                    state->curr_r = search_in_list_recipes(state, state->curr_t->formversion, NULL);
                }
            }

            //Looking for attribute type
            if (!strncasecmp("type", attr[i], strlen("type"))) {
                const char *attribute = attr[i + 1];
                // Skip "XXX:" prefixes on types
                if (strstr(attribute, ":"))
                    attribute = strstr(attribute, ":") + 1;

                type = recipe_parse_fieldtype(attribute);
            }

            //Looking for attribute constraint
            if (!strncasecmp("constraint", attr[i], strlen("constraint"))) {
                node_constraint = attr[i + 1];
            }
        }

        //Now we got node_name, node_type, node_constraint
        //Lets build output
        if (!strcasecmp(node_name, "instanceID"))
            type = FIELDTYPE_UUID;

        if (type != -1){
            int min=0;
            int max=0;
            if (type == FIELDTYPE_INTEGER || type == FIELDTYPE_FIXEDPOINT) {
                // Default to integers being in the range 0 to 999.
                max = 999;
                if (strlen(node_constraint)) {
                    const char *ptr = node_constraint;
                    //We look for 0 to 2 digits
                    while (!isdigit(*ptr) && (ptr < node_constraint + strlen(node_constraint)))
                        ptr++;
                    int a = atoi(ptr);
                    while (isdigit(*ptr) && (ptr < node_constraint + strlen(node_constraint)))
                        ptr++;
                    while (!isdigit(*ptr) && (ptr < node_constraint + strlen(node_constraint)))
                        ptr++;
                    int b = atoi(ptr);
                    if (b <= a) b = a + 999;
                    min = MIN(a, b);
                    max = MAX(a, b);
                }
            }

            if (type == FIELDTYPE_SUBFORM) {
                sprintf(temp, "subform/%s:subform:0:0:0",
                        state->curr_t->formversion);
            } else {
                sprintf(temp, "%s:%s:%d:%d:0",
                        node_name, recipe_field_type_name(type), min, max);
            }

            if (type == FIELDTYPE_MULTISELECT || type == FIELDTYPE_ENUM){
                strgrow(&state->curr_r->selects[state->curr_r->xhtmlSelectsLen], temp);
                state->curr_r->xhtmlSelectsLen++;
            }else{
                strgrow(&state->curr_r->recipe, temp);
                strgrow(&state->curr_r->recipe, "\n");
            }
        }
    }

        //.recipe

        //Now look for selects specifications, we wait until to find a select node
    else if ((!strcasecmp("xf:select1", el)) || (!strcasecmp("xf:select", el))) {
        for (i = 0; attr[i]; i += 2) //Found a select element, look for attributes
        {
            if (!strcasecmp("bind", attr[i])) {
                const char *last_slash = strrchr(attr[i + 1], '/');
                // Allow for non path-indicated bindings in XHTML forms
                if (!last_slash) last_slash = attr[i + 1]; else last_slash++;
                LOGI("Found multiple-choice selection definition '%s'", last_slash);
                state->xhtmlSelectElem = strdup(last_slash);
                state->xhtmlSelectFirst = 1;
            }
        }
    }

        //We are in a select node and we need to find a value element
    else if ((state->xhtmlSelectElem) &&
             ((!strcasecmp("value", el)) || (!strcasecmp("xf:value", el)))) {
	state->xhtml_in_value = 1;
    }

        //We reached the start of the data in the instance, so start collecting fields
    else if (!strcasecmp("data", el)) {
	state->xhtml_in_instance = 1;
    } else if (!strcasecmp("xf:model", el)) {
	// Form name is the id attribute of the xf:model tag
        for (i = 0; attr[i]; i += 2) {
            if (!strcasecmp("id", attr[i])) {
                state->xhtmlFormName = strdup(attr[i + 1]);
            }
            if (!strcasecmp("dd:formid", attr[i])) {
                state->xhtmlFormVersion = strdup(attr[i + 1]);
            }
        }
    }
}

static void characterdata_xhtml(void *data, const char *el, int len)
//This function is called  by the XML library each time we got data in a tag
{
    int i;
    struct parse_state *state = data;
    if (state->xhtmlSelectElem && state->xhtml_in_value) {
        char x[len + 2]; //el is not null terminated, so copy it to x and add \0
        memcpy(x, el, (size_t)len);
        memcpy(x + len, "", 1);

        //find the subform where this field is in
        char *form_id_to_use = search_in_list_tags(state, state->xhtmlSelectElem,
                                                   NULL)->formversion;

        //use the right recipe node in the linked list to parse info
        state->curr_r = search_in_list_recipes(state, form_id_to_use, NULL);
        for (i = 0; i<state->curr_r->xhtmlSelectsLen; i++){
            if (state->curr_r->selects[i].str && !strncasecmp(state->xhtmlSelectElem, state->curr_r->selects[i].str,
                             strlen(state->xhtmlSelectElem))) {
                if (state->xhtmlSelectFirst) {
                    state->xhtmlSelectFirst = 0;
                    strgrow(&state->curr_r->selects[i], ":");
                } else {
                    strgrow(&state->curr_r->selects[i], ",");
                }
                strgrow(&state->curr_r->selects[i], x);
            }
        }
    }
}

static void end_xhtml(void *data,
               const char *el) //This function is called  by the XML library each time it sees an ending of a tag
{
    struct parse_state *state = data;
    if (state->xhtmlSelectElem &&
        ((!strcasecmp("xf:select1", el)) || (!strcasecmp("xf:select", el)))) {
	free(state->xhtmlSelectElem);
        state->xhtmlSelectElem = NULL;
    }

    if (state->xhtml_in_value && ((!strcasecmp("value", el)) || (!strcasecmp("xf:value", el)))) {
        state->xhtml_in_value = 0;
    }

    if (state->xhtml_in_instance && (!strcasecmp("data", el))) {
        state->xhtml_in_instance = 0;
    }

    if ((state->xhtml_in_instance && state->xhtml_in_subform == 0)
        ||
        (state->xhtml_in_subform > 0 &&
         strcasecmp(state->subform_tags[state->xhtml_in_subform], el))
            ) {

        LOGI("Subform end ! don't write the element in the template \n");
        strgrow(&state->xhtml2template, "</");
        strgrow(&state->xhtml2template, el);
        strgrow(&state->xhtml2template, ">\n");
    }

    if (state->xhtml_in_subform && !strcasecmp(state->subform_tags[state->xhtml_in_subform], el)) {

        LOGI("(end_xhtml) Subform_tags contains elements ! => Subform end ! Element is %s",
               el);
        state->xhtml_in_subform--;
        strgrow(&state->xhtml2template,  "</data>\n</dd:subform>\n");

        //linked list stuff to structure the data well
        state->curr_s = state->curr_s->prev;
        LOGI("(start_xhtml) End of a subform/form ! curr_s -> formversion = %s",
               state->curr_s->subformid);
    }
}

//Generate the recipe (Spec stripped data) and write it into .recipe and .template file
int xhtml_recipe_create(const char *recipe_dir, const char *input) {
    FILE *f = fopen(input, "r");
    char filename[1024] = "";
    size_t size;
    char *xmltext;

    if (!f) {
        LOGE("Could not read XHTML file '%s'", input);
        return -1;
    }

    //Open Xml File
    xmltext = malloc(MAXCHARS);
    size = fread(xmltext, sizeof(char), MAXCHARS, f);
    xmltext[size]=0;

    char *templatetext = NULL;
    char *recipe_text[1024];
    char *form_versions[1024];
    bzero(recipe_text, sizeof recipe_text);
    bzero(form_versions, sizeof form_versions);

    int r = xhtmlToRecipe(xmltext, &templatetext, (char **) &recipe_text, (char **) &form_versions);
    free(xmltext);

    if (r) {
        LOGE("xhtml2recipe failed\n");
        return (1);
    }

    int i;
    for (i=0;i<1024;i++){
        if (!recipe_text[i])
            break;
        //Create output for RECIPE
        snprintf(filename, sizeof filename, "%s/%s.recipe", recipe_dir, form_versions[i]);
        LOGE("Writing recipe to '%s'", filename);
        f = fopen(filename, "w");
        fputs(recipe_text[i], f);
        fclose(f);
    }

    //Create output for TEMPLATE
    snprintf(filename, sizeof filename, "%s/%s.template", recipe_dir, form_versions[0]);
    LOGE("Writing template to '%s'", filename);
    f = fopen(filename, "w");
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<form>\n<meta>\n");
    fprintf(f, "%s", implied_meta_fields_template);
    fprintf(f, "</meta>\n<data>\n");
    fprintf(f, "%s", templatetext);
    fprintf(f, "</data>\n</form>\n");
    fclose(f);

    if (templatetext){
        free(templatetext);
    }
    for (i=0;i<1024;i++) {
        if (recipe_text[i]){
            free(recipe_text[i]);
        }
	if (form_versions[i]){
            free(form_versions[i]);
	}
    }

    return 0;
}

int xhtmlToRecipe(const char *xmltext, char **template_text, char *recipe_text[1024], char *form_versions[1024]) {
    int ret=-1;
    XML_Parser parser;
    int i;
    struct parse_state state;
    bzero(&state, sizeof state);

    //ParserCreation
    parser = XML_ParserCreate(NULL);
    if (parser == NULL) {
        LOGE("ERROR: %s: Parser not created", __FUNCTION__);
        return 1;
    }

    state.xhtmlSelectFirst = 1;
    state.curr_r = state.head_r;

    XML_SetUserData(parser, &state);

    // Tell expat to use functions start() and end() each times it encounters the start or end of an element.
    XML_SetElementHandler(parser, start_xhtml, end_xhtml);
    // Tell expat to use function characterData()
    XML_SetCharacterDataHandler(parser, characterdata_xhtml);

    //Parse Xml Text
    if (XML_Parse(parser, xmltext, (int)strlen(xmltext), XML_TRUE) ==
        XML_STATUS_ERROR) {
        LOGE("ERROR: %s: Cannot parse , file may be too large or not well-formed XML",
                __FUNCTION__);
        goto cleanup;
    }
    int recipe_count=0;

    while (state.curr_r != NULL) {
        // Finish recipe output

        for (i = 0; i < state.curr_r->xhtmlSelectsLen; i++) {
            strgrow(&state.curr_r->recipe, state.curr_r->selects[i].str);
            strgrow(&state.curr_r->recipe, "\n");
        }

        LOGI("xhtmlFormName=%s, xhtmlFormVersion=%s, len=%d",
                state.curr_r->formname, state.curr_r->formversion, (int)state.curr_r->recipe.str_len);

        if (recipe_text){
            recipe_text[recipe_count]=state.curr_r->recipe.str;
            state.curr_r->recipe.str = NULL;
        }
        if (form_versions)
            form_versions[recipe_count]=strdup(state.curr_r->formversion);
        recipe_count++;

        state.curr_r = state.curr_r->next;
    }

    if (template_text){
        *template_text = state.xhtml2template.str;
        state.xhtml2template.str = NULL;
    }

    ret = 0;

cleanup:
    if (state.xhtmlFormName){
        free(state.xhtmlFormName);
    }
    if (state.xhtmlFormVersion){
        free(state.xhtmlFormVersion);
    }

    recipe_node *recipenode = state.head_r;
    while(recipenode){
        if (recipenode->recipe.str){
            free(recipenode->recipe.str);
	}
        for (i = 0; i < recipenode->xhtmlSelectsLen; i++){
            if (recipenode->selects[i].str){
                free(recipenode->selects[i].str);
	    }
        }
        recipe_node *f = recipenode;
        recipenode = recipenode->next;
        free(f);
    }
    tag_node *tagnode = state.head_t;
    while(tagnode){
        tag_node *f = tagnode;
        tagnode = tagnode->next;
        free(f);
    }

    subform_node *subformnode = state.head_s;
    while(subformnode){
        subform_node *f = subformnode;
        subformnode = subformnode->next;
        free(f);
    }
    if (state.xhtml2template.str){
        free(state.xhtml2template.str);
    }

    XML_ParserFree(parser);

    return ret;
}

static recipe_node *
add_to_list_recipes(struct parse_state *state, const char *formversion, bool add_to_end) {
    if (add_to_end)
        LOGI("Adding node to end of recipes list with formversion [%s]", formversion);
    else
        LOGI("Adding node to beginning of recipes list with formversion [%s]", formversion);

    recipe_node *recipenode = (recipe_node *) malloc(sizeof(recipe_node));
    if (NULL == recipenode) {
        LOGI("Node creation failed");
        return NULL;
    }
    bzero(recipenode, sizeof(recipe_node));
    strcpy(recipenode->formversion, formversion);
    recipenode->xhtmlSelectsLen = 0;
    
    if (add_to_end && state->curr_r) {
        state->curr_r->next = recipenode;
        state->curr_r = recipenode;
    } else {
        recipenode->next = state->head_r;
        state->head_r = recipenode;
	if (!state->curr_r)
	  state->curr_r = state->head_r;
    }
    return recipenode;
}

static tag_node *add_to_list_tags(struct parse_state *state, const char *tagname, const char *formversion,
                           bool add_to_end) {
    if (add_to_end)
        LOGI("Adding node to end of tags list with tagname [%s] and formversion [%s]",
               tagname, formversion);
    else
        LOGI("Adding node to beginning of tags list with tagname [%s] and formversion [%s]",
               tagname, formversion);

    size_t tag_len = strlen(tagname);
    size_t version_len = strlen(formversion);

    tag_node *tagnode = (tag_node *) malloc(sizeof(tag_node) + tag_len + version_len + 2);
    if (NULL == tagnode) {
        LOGI("Node creation failed");
        return NULL;
    }
    bzero(tagnode, sizeof(tag_node));

    tagnode->tagname = &tagnode->buff[0];
    strcpy(tagnode->tagname, tagname);
    tagnode->formversion = &tagnode->buff[tag_len+1];
    strcpy(tagnode->formversion, formversion);
    tagnode->next = NULL;

    if (tagnode){
      if (add_to_end && state->curr_t) {
	  state->curr_t->next = tagnode;
	  state->curr_t = tagnode;
      } else {
	  tagnode->next = state->head_t;
	  state->head_t = tagnode;
	  if (!state->curr_t)
	    state->curr_t = state->head_t;
      }
    }
    return tagnode;
}

static subform_node *add_to_list_subforms(struct parse_state *state, const char *formversion) {
    LOGI("Adding node to end of subforms list with formversion [%s]", formversion);

    subform_node *subformnode = (subform_node *) malloc(sizeof(subform_node) + strlen(formversion) + 1);
    if (NULL == subformnode) {
        LOGI("Node creation failed");
        return NULL;
    }
    bzero(subformnode, sizeof(subform_node));
    strcpy(subformnode->subformid, formversion);
    //double linked list so we can move forward when we meet a new subform
    //and move backward when we meet the end of subform
    //To move forward : simply add a new subform node (only case to move forward)
    //To move backward : curr_s = curr_s->prev
    if (state->curr_s){
      if (state->curr_s->next)
	state->curr_s->next->prev = subformnode;
      subformnode->next = state->curr_s->next;
      state->curr_s->next = subformnode;
    }
    else
      state->head_s = subformnode;

    subformnode->prev = state->curr_s;
    state->curr_s = subformnode;

    return subformnode;
}

static recipe_node *
search_in_list_recipes(struct parse_state *state, const char *formversion, recipe_node **prev) {
    recipe_node *recipenode = state->head_r;
    recipe_node *tmp = NULL;

    LOGI("Searching in the recipes list for formversion value [%s]", formversion);

    while (recipenode != NULL) {
        if (!strcmp(recipenode->formversion, formversion)) {
            if (prev)
                *prev = tmp;
            LOGI("Found a corresponding node in recipes list ! formversion [%s]",
                 recipenode->formversion);
            return recipenode;
        } else {
            tmp = recipenode;
            recipenode = recipenode->next;
        }
    }

    return NULL;
}

static tag_node *search_in_list_tags(struct parse_state *state, const char *tagname, tag_node **prev) {
    tag_node *tagnode = state->head_t;
    tag_node *tmp = NULL;

    LOGI("Searching in the tags list for tagname value [%s]", tagname);

    while (tagnode != NULL) {
        if (!strcmp(tagnode->tagname, tagname)) {
	    if (prev)
		*prev = tmp;

	    LOGI("Found a corresponding node in tags list ! tagname [%s] and formversion [%s]",
		   tagnode->tagname, tagnode->formversion);
	    return tagnode;
        } else {
            tmp = tagnode;
            tagnode = tagnode->next;
        }
    }
    return NULL;
}

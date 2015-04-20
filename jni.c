#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <jni.h>
#include <android/log.h>
 
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "libsmac", __VA_ARGS__))

#include "charset.h"
#include "visualise.h"
#include "arithmetic.h"
#include "packed_stats.h"
#include "smac.h"
#include "recipe.h"
#define MAX_FRAGMENTS 64
int encryptAndFragmentBuffer(unsigned char *in_buffer,int in_len,
			     char *fragments[MAX_FRAGMENTS],int *fragment_count,
			     int mtu,char *publickeyhex);
void recipe_free(struct recipe *recipe);
int recipe_compress(stats_handle *h,struct recipe *recipe,
		    char *in,int in_len, unsigned char *out, int out_size);

jobjectArray error_message(JNIEnv * env, char *message)
{
  LOGI("%s",message);
      
  jobjectArray result=
    (jobjectArray)(*env)->NewObjectArray(env,1,
					 (*env)->FindClass(env,"java/lang/String"),
					 (*env)->NewStringUTF(env,""));
  (*env)->SetObjectArrayElement(env,result,0,(*env)->NewStringUTF(env,message));

  return result;
}

char form_name[1024],form_version[1024];

JNIEXPORT jobjectArray JNICALL Java_org_servalproject_succinctdata_jni_xml2succinctfragments
(JNIEnv * env, jobject jobj,
 jstring xmlforminstance,
 jstring xmlformspecification,
 jstring formname,
 jstring formversion,
 jstring succinctpath,
 jint mtu)
{
  const char *xmldata= (*env)->GetStringUTFChars(env,xmlforminstance,0);
  const char *formname_c= (*env)->GetStringUTFChars(env,formname,0);
  const char *formversion_c= (*env)->GetStringUTFChars(env,formversion,0);
  const char *path= (*env)->GetStringUTFChars(env,succinctpath,0);
  const char *xmlform_c= (*env)->GetStringUTFChars(env,xmlformspecification,0);
  
  char stripped[65536];
  unsigned char succinct[1024];
  int succinct_len=0;
  char filename[1024];

  // Read public key hex
  snprintf(filename,1024,"%s/%s.%s.publickey",path,formname_c,formversion_c);
  LOGI("Opening recipient public key file %s",filename);

  // Default to public key of Serval succinct data server
  char publickeyhex[1024]="74f3a36029b0e60084d42bd9cafa3f2b26fe802b0a6f024ff00451481c9bba4a";

  {
    FILE *f=fopen(filename,"r");
    if (f) {
      int r=fread(publickeyhex,1,1023,f);
      if (r<64) {
	char message[1024];
	snprintf(message,1024,"Failed to read from public key file");
	return error_message(env,message);
      }
      publickeyhex[r]=0;
    }
    while(publickeyhex[strlen(publickeyhex)-1]<' ') publickeyhex[strlen(publickeyhex)-1]=0;
    fclose(f);
  }

  struct recipe *recipe=NULL;
  
  if (xmlformspecification==NULL||xmlform_c==NULL||!strlen(xmlform_c)) {
    // Read recipe file
    snprintf(filename,1024,"%s/%s.%s.recipe",path,formname_c,formversion_c);
    LOGI("Opening recipe file %s",filename);
    recipe=recipe_read_from_file(filename);
    if (!recipe) {
      char message[1024];
      snprintf(message,1024,"Could not read recipe file %s",filename);
      return error_message(env,message);
    }    
  } else {
    // Create recipe from form specification
    char recipetext[65536];
    int recipetextLen=65536;
    char templatetext[65536];
    int templatetextLen=65536;
    int r;
    if (magpi_mode)
      r=xhtmlToRecipe(xmlform_c,strlen(xmlform_c),
		      form_name,form_version,
		      recipetext,&recipetextLen,
		      templatetext,&templatetextLen);
    else
      r=xmlToRecipe(xmlform_c,strlen(xmlform_c),
		      form_name,form_version,
		      recipetext,&recipetextLen,
		      templatetext,&templatetextLen);
    if (r) {
      return error_message(env,"Could not create recipe from form specification");
    }
    formname_c=form_name; formversion_c=form_version;
    recipe = recipe_read(form_name,recipetext,recipetextLen);    
    if (!recipe) {
      char message[1024];
      snprintf(message,1024,"Could not set recipe");
      return error_message(env,message);
    }
  }

  // Transform XML to stripped data first.
  int stripped_len;

  stripped_len = xml2stripped(formname_c,xmldata,strlen(xmldata),stripped,sizeof(stripped));

  LOGI("Stripped data is %d bytes long",stripped_len);
  
  if (stripped_len>0) {
    // Produce succinct data

    // Get stats handle
    char filename[1024];
    snprintf(filename,1024,"%s/smac.dat",path);
    stats_handle *h=stats_new_handle(filename);

    if (!h) {
      recipe_free(recipe);
      char message[1024];
      snprintf(message,1024,"Could not read SMAC stats file %s",filename);
      return error_message(env,message);
    }

    // Compress stripped data to form succinct data
    succinct_len=recipe_compress(h,recipe,stripped,stripped_len,succinct,sizeof(succinct));

    LOGI("Binary succinct data is %d bytes long",stripped_len);

    // Clean up after ourselves
    stats_handle_free(h);
    recipe_free(recipe);
    snprintf(filename,1024,"%s/%s.%s.recipe",path,formname_c,formversion_c);

    if (succinct_len<1) {
      recipe_free(recipe);
      char message[1024];
      snprintf(message,1024,"recipe_compess failed with recipe file %s. h=%p, recipe=%p, stripped_len=%d",filename,h,recipe,stripped_len);
      return error_message(env,message);
    }
  } else {
    recipe_free(recipe);
    char message[1024];
    snprintf(message,1024,"Failed to strip XML using recipe file %s.",filename);
    return error_message(env,message);
  }

  char *fragments[MAX_FRAGMENTS];
  int fragment_count=0;
  encryptAndFragmentBuffer(succinct,succinct_len,fragments,&fragment_count,mtu,
			   publickeyhex);

  LOGI("Succinct data formed into %d fragments",fragment_count);
  
  jobjectArray result=
    (jobjectArray)(*env)->NewObjectArray(env,fragment_count,
				      (*env)->FindClass(env,"java/lang/String"),
         (*env)->NewStringUTF(env,""));
  for(int i=0;i<fragment_count;i++) {
    (*env)->SetObjectArrayElement(env,result,i,(*env)->NewStringUTF(env,fragments[i]));
    free(fragments[i]);
  }
    
  return result;  
}


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

JNIEXPORT jbyteArray JNICALL Java_org_servalproject_succinctdata_jni_xml2succinct
(JNIEnv * env, jobject jobj,
 jstring xmlforminstance,
 jstring formname,
 jstring formversion,
 jstring succinctpath)
{
  const char *xmldata= (*env)->GetStringUTFChars(env,xmlforminstance,0);
  const char *formname_c= (*env)->GetStringUTFChars(env,formname,0);
  const char *formversion_c= (*env)->GetStringUTFChars(env,formversion,0);
  const char *path= (*env)->GetStringUTFChars(env,succinctpath,0);
  
  char stripped[8192];
  unsigned char succinct[1024];
  int succinct_len=0;

  // Read recipe file
  char filename[1024];
  snprintf(filename,1024,"%s/%s.%s.recipe",path,formname_c,formversion_c);
  LOGI("Opening recipe file %s",filename);
  struct recipe *recipe=recipe_read_from_file(filename);
  
  if (!recipe) {
    LOGI("Could not read recipe file %s",filename);
    jbyteArray result=(*env)->NewByteArray(env, 1);
    unsigned char ret=2;
    (*env)->SetByteArrayRegion(env, result, 0, 1, &ret);
    return result;
  }

  // Transform XML to stripped data first.
  int stripped_len=xml2stripped(formname_c,xmldata,strlen(xmldata),stripped,8192);

  if (stripped_len>0) {
    // Produce succinct data

    // Get stats handle
    snprintf(filename,1024,"%s/smac.dat",path);
    stats_handle *h=stats_new_handle(filename);

    if (!h) {
      LOGI("Could not read SMAC stats file %s",filename);
      recipe_free(recipe);
      jbyteArray result=(*env)->NewByteArray(env, 1);
      unsigned char ret=1;
      (*env)->SetByteArrayRegion(env, result, 0, 1, &ret);
      return result;
    }

    // Compress stripped data to form succinct data
    succinct_len=recipe_compress(h,recipe,stripped,stripped_len,succinct,sizeof(succinct));

    // Clean up after ourselves
    stats_handle_free(h);
    recipe_free(recipe);

    if (succinct_len<1) {
      LOGI("recipe_compess failed for recipename=%s.",recipefile);
      jbyteArray result=(*env)->NewByteArray(env, 1);
      unsigned char ret=3;
      (*env)->SetByteArrayRegion(env, result, 0, 1, &ret);
      return result;
    }
  } else {
      LOGI("Failed to strip XML.",recipefile);
      recipe_free(recipe);
      jbyteArray result=(*env)->NewByteArray(env, 1);
      unsigned char ret=4;
      (*env)->SetByteArrayRegion(env, result, 0, 1, &ret);
      return result;
  }
  
  jbyteArray result=(*env)->NewByteArray(env, succinct_len);
  (*env)->SetByteArrayRegion(env, result, 0, succinct_len, succinct);
  
  return result;  
}


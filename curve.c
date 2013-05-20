/*
Copyright (C) 2012-2013 Paul Gardner-Stephen
 
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

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <sys/mman.h>

#include "arithmetic.h"
#include "charset.h"
#include "packed_stats.h"
#include "unicode.h"


int compare_tolerance=0;
int compare_doublet(const void *a,const void *b)
{
  const doublet *aa=a;
  const doublet *bb=b;
  if (abs(aa->a-bb->a)>compare_tolerance)
    {
      if (aa->a<bb->a) return 1;
      if (aa->a>bb->a) return -1;
    }
  if (aa->b>bb->b) return 1;
  if (aa->b<bb->b) return -1;
  return 0;
}

struct probability_vector *curves[0x10000];
int curves_setup=0;

int logTableSetup=0;
double *logTable;

int calcLogTable()
{
  int a;
  logTable=malloc(sizeof(double)*0x1000000);
  for(a=0;a<=0xffffff;a++)
    logTable[a]=-log((a+1)*1.0/0xffffff)/log(2);
  logTableSetup=1;
  return 0;
}

double calcRatioMetric(int a,int b)
{
  // a = predicted freq
  // b = actual freq

  double p_b=(b+1)*1.0/0xffffff;

  if (logTableSetup)
    return p_b*(logTable[a]-logTable[b]);

  double bits_a=-log((a+1)*1.0/0xffffff)/log(2);
  double bits_b=-log(p_b)/log(2);
  
  // return average number of bits saved or wasted due to
  // difference in predicted and actual probability
  return p_b*(bits_a-bits_b);

}

double calcCurve(int curve_number,
		 struct probability_vector *sample_curve,
		 struct probability_vector *plotted_curve)
{
  // a=0..5.11 by 0.01, b=0..127 by 1
  double a=(curve_number&0x1ff)/100.0;
  double b=curve_number>>9;

  if (!curves_setup) {
    int i;
    for(i=0;i<0x10000;i++) curves[i]=NULL;
    curves_setup=1;
  }

  int i;
  double e=0;
  double s=0;
  if (!curves[curve_number]) {
    // Calculate curve and remember for next time
    curves[curve_number]=calloc(sizeof(struct probability_vector),1);
    if (!curves[curve_number]) return -1;
    for(i=0;i<CHARCOUNT;i++) s+=pow((CHARCOUNT+1-(i+1)),b)/pow((i+1),a);
    for(i=0;i<CHARCOUNT;i++) 
      curves[curve_number]->v[i]
	= // (i?curves[curve_number]->v[i-1]:0)+
	pow((CHARCOUNT+1-(i+1)),b)/pow((i+1),a)/s*(0xffffff-CHARCOUNT);
  }
  struct probability_vector *curve=curves[curve_number];
  for(i=0;i<CHARCOUNT;i++)
    {
      if (plotted_curve) plotted_curve->v[i]=curve->v[i];
      if (sample_curve) {
	// double thise=abs(curve->v[i]-sample_curve->v[i]);
	// thise/=0xffffff*1.0;
	// thise*=thise;
	// e+=thise;

	double ratio=calcRatioMetric(curve->v[i],sample_curve->v[i]);
	
	e+=ratio;
      }
    }
  return e;
}

int compareCurves(struct probability_vector *a, struct probability_vector *b)
{
  int i;
  for(i=0;i<CHARCOUNT;i++) {
    double thise=abs(a->v[i]-b->v[i]);
    thise/=0xffffff*1.0;
    thise*=thise;	
    double ratio=calcRatioMetric(a->v[i],b->v[i]);
    fprintf(stderr,"    %02x:a%06x:p%06x:%f:%f\n",
	    i,b->v[i],a->v[i],thise,ratio);
  }
  return 0;
}

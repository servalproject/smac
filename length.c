/*
Copyright (C) 2012 Paul Gardner-Stephen
 
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

#include "arithmetic.h"
#include "charset.h"
#include "packed_stats.h"

int encodeLength(range_coder *c,int len,stats_handle *h)
{
  range_encode_symbol(c,(unsigned int *)h->messagelengths,1024,len);

  return 0;
}

int decodeLength(range_coder *c,stats_handle *h)
{
  int len=range_decode_symbol(c,(unsigned int *)h->messagelengths,1024);
  return len;
}

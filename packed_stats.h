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

typedef struct node {
  long long count;
  unsigned int counts[69];
  struct node *children[69];
} node;

struct node *extractNode(char *string,int len,FILE *f);
struct node *extractNodeAt(char *s,unsigned int nodeAddress,int count,FILE *f);
int extractVector(char *string,int len,FILE *f,unsigned int v[69]);

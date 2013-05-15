CC=gcc
COPT=-g -Wall -O3
CFLAGS=-g -Wall -O3
DEFS=

OBJS=	main.o \
	\
	method_stats3.o \
	\
	unicode.o \
	case.o \
	length.o \
	lowercasealpha.o \
	nonalpha.o \
	packedascii.o \
	packed_stats.o \
	\
	charset.o \
	entropyutil.o \
	\
	arithmetic.o \
	gsinterpolative.o \
	\
	visualise.o

HDRS=	charset.h arithmetic.h packed_stats.h unicode.h visualise.h Makefile

all: method_stats3 arithmetic gsinterpolative gen_stats

smaz_test: smaz_test.c smaz.c
	gcc -o smaz_test -O2 -Wall -W -ansi -pedantic smaz.c smaz_test.c

clean:
	rm -rf smaz_test *.o gen_stats method_stats3

arithmetic:	arithmetic.c arithmetic.h
# Build for running tests
	gcc $(CFLAGS) -DSTANDALONE -o arithmetic arithmetic.c

extract_tweets:	extract_tweets.o
	gcc $(CFLAGS) -o extract_tweets extract_tweets.o

gen_stats:	gen_stats.o arithmetic.o packed_stats.o gsinterpolative.o charset.o unicode.o
	gcc $(CFLAGS) -o gen_stats gen_stats.o arithmetic.o packed_stats.o gsinterpolative.o charset.o unicode.o

method_stats3:	$(OBJS)
	gcc -g -Wall -o method_stats3 $(OBJS)

gsinterpolative:	gsinterpolative.c $(OBJS)
	gcc -g -Wall -DSTANDALONE -o gsinterpolative{,.c} arithmetic.o

%.o:	%.c $(HDRS)
	$(CC) $(CFLAGS) $(DEFS) -c $< -o $@

test:	gsinterpolative arithmetic
	./gsinterpolative
	./arithmetic
	./method_stats3 twitter_corpus*.txt

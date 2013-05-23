CC=gcc
COPT=-g -Wall -O3
CFLAGS=-g -Wall -O3
DEFS=

OBJS=	main.o \
	\
	smac.o \
	\
	unicode.o \
	case.o \
	curve.o \
	length.o \
	lowercasealpha.o \
	nonalpha.o \
	packedascii.o \
	packed_stats.o \
	permutations.o \
	\
	charset.o \
	entropyutil.o \
	\
	arithmetic.o \
	gsinterpolative.o \
	\
	visualise.o

HDRS=	charset.h arithmetic.h packed_stats.h unicode.h visualise.h Makefile

all: smac arithmetic gsinterpolative gen_stats

clean:
	rm -rf gen_stats smac

arithmetic:	arithmetic.c arithmetic.h
# Build for running tests
	gcc $(CFLAGS) -DSTANDALONE -o arithmetic arithmetic.c

extract_tweets:	extract_tweets.o
	gcc $(CFLAGS) -o extract_tweets extract_tweets.o

gen_stats:	gen_stats.o arithmetic.o packed_stats.o gsinterpolative.o charset.o unicode.o curve.o permutations.o
	gcc $(CFLAGS) -o gen_stats gen_stats.o arithmetic.o packed_stats.o gsinterpolative.o charset.o unicode.o curve.o permutations.o

smac:	$(OBJS)
	gcc -g -Wall -o smac $(OBJS)

gsinterpolative:	gsinterpolative.c $(OBJS)
	gcc -g -Wall -DSTANDALONE -o gsinterpolative{,.c} arithmetic.o

%.o:	%.c $(HDRS)
	$(CC) $(CFLAGS) $(DEFS) -c $< -o $@

test:	gsinterpolative arithmetic
	./gsinterpolative
	./arithmetic
	./smac twitter_corpus*.txt

out.odt:	content.xml
	cp content.xml odt-shell/
	cd odt-shell ; zip -r ../out.odt *

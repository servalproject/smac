CC=gcc
COPT=-g -Wall
CFLAGS=-g -Wall

all: tweet_freq arithmetic # smaz_test gen_stats tweet_stats.c

smaz_test: smaz_test.c smaz.c
	gcc -o smaz_test -O2 -Wall -W -ansi -pedantic smaz.c smaz_test.c

clean:
	rm -rf smaz_test

arithmetic:	arithmetic.c arithmetic.h
	# Build for running tests
	gcc -g -Wall -DSTANDALONE -o arithmetic arithmetic.c

gen_stats:	gen_stats.c
	gcc -g -Wall -o gen_stats gen_stats.c

tweet_stats.c:	gen_stats twitter_corpus*.txt
	cat twitter_corpus*.txt |./gen_stats > message_stats.c

tweet_freq:	tweet_freq.o message_stats.o arithmetic.o gsinterpolative.o lowercasealpha.o charset.o
	gcc -g -Wall -o tweet_freq tweet_freq.o message_stats.o arithmetic.o gsinterpolative.o lowercasealpha.o charset.o

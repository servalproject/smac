CC=gcc
COPT=-g -Wall
CFLAGS=-g -Wall

all: smaz_test gen_stats tweet_stats.c

smaz_test: smaz_test.c smaz.c
	gcc -o smaz_test -O2 -Wall -W -ansi -pedantic smaz.c smaz_test.c

clean:
	rm -rf smaz_test

gen_stats:	gen_stats.c
	gcc -g -Wall -o gen_stats gen_stats.c

tweet_stats.c:	gen_stats some_tweets.txt
	./gen_stats <some_tweets.txt >tweet_stats.c

tweet_freq:	tweet_freq.o tweet_stats.o gsinterpolative.o
	gcc -g -Wall -o tweet_freq tweet_freq.o tweet_stats.o gsinterpolative.o

eng_freq:	eng_freq.o gsinterpolative.o
	gcc -g -Wall -o eng_freq eng_freq.o gsinterpolative.o
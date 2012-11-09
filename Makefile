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

#twitter_corpus1.txt:	some_tweets.json extract_tweet_text
#	./extract_tweet_text

tweet_stats.c:	gen_stats twitter_corpus*.txt
	cat twitter_corpus*.txt |./gen_stats > tweet_stats.c

tweet_freq:	tweet_freq.o tweet_stats.o arithmetic.o gsinterpolative.o
	gcc -g -Wall -o tweet_freq tweet_freq.o tweet_stats.o arithmetic.o gsinterpolative.o

eng_freq:	eng_freq.o gsinterpolative.o
	gcc -g -Wall -o eng_freq eng_freq.o gsinterpolative.o

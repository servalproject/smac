CC=gcc
CFLAGS=-g -Wall -O3 -Inacl/include -std=gnu99 -I. -DHAVE_BCOPY=1 -DHAVE_MEMMOVE=1
LIBS=-lm
DEFS=

OBJS=	main.o \
	\
	smac.o \
	\
	recipe.o \
	xml2recipe.o \
	xhtml2recipe.o \
	map.o \
	dexml.o \
	md5.o \
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
	visualise.o \
	\
	crypto.o \
	nacl/src/crypto_box_curve25519xsalsa20poly1305_ref/keypair.o \
	nacl/src/crypto_box_curve25519xsalsa20poly1305_ref/before.o \
	nacl/src/crypto_box_curve25519xsalsa20poly1305_ref/after.o \
	nacl/src/crypto_box_curve25519xsalsa20poly1305_ref/box.o \
	nacl/src/crypto_core_hsalsa20_ref/core.o \
	nacl/src/crypto_scalarmult_curve25519_ref/base.o \
	nacl/src/crypto_scalarmult_curve25519_ref/smult.o \
	nacl/src/crypto_secretbox_xsalsa20poly1305_ref/box.o \
	nacl/src/crypto_onetimeauth_poly1305_ref/auth.o \
	nacl/src/crypto_onetimeauth_poly1305_ref/verify.o \
	nacl/src/crypto_verify_16_ref/verify.o \
	nacl/src/crypto_stream_xsalsa20_ref/xor.o \
	nacl/src/crypto_stream_xsalsa20_ref/stream.o \
	nacl/src/crypto_core_salsa20_ref/core.o \
	nacl/src/crypto_stream_salsa20_ref/xor.o \
	nacl/src/crypto_stream_salsa20_ref/stream.o \
	\
	xmlparse.o \
	xmlrole.o \
	xmltok_impl.o \
	xmltok_ns.o \
	xmltok.o \
        \
	timegm.o

HDRS=	charset.h arithmetic.h packed_stats.h unicode.h visualise.h recipe.h Makefile

all: smac arithmetic gen_stats

clean:
	rm -rf gen_stats smac

arithmetic:	arithmetic.c arithmetic.h
# Build for running tests
	gcc $(CFLAGS) -DTESTMODE -o arithmetic arithmetic.c

extract_tweets:	extract_tweets.o
	gcc $(CFLAGS) -o extract_tweets extract_tweets.o

gen_stats:	gen_stats.o arithmetic.o packed_stats.o gsinterpolative.o charset.o unicode.o
	gcc $(CFLAGS) -o gen_stats gen_stats.o arithmetic.o packed_stats.o gsinterpolative.o charset.o unicode.o

smac:	$(OBJS)
	gcc -g -Wall -o smac $(OBJS) $(LIBS)

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

extract_instance_with_library:	extract_instance_with_library.c Makefile
	$(CC) $(CFLAGS) -o extract_instance_with_library extract_instance_with_library.c -lexpat

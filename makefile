all: keylist.o tskl

CFLAGS = -O2 -Wall -Wextra -fno-exceptions \
	-fno-strict-aliasing -std=c11 -D_DEBUG -g

keylist.o: src/keylist.c | build
	cc -c $< -o build/$@ $(CFLAGS) -Iinclude -D_NO_DBG_PRINT

tskl: build/keylist.o tests/tskl.c src/vector.c | build
	cc $^ -o bin/$@ $(CFLAGS) -Iinclude

tsvec: tests/tsvec.cxx src/vector.c | build
	cc -c src/vector.c -o build/vector.o $(CFLAGS) -Iinclude
	g++ $< build/vector.o -o bin/$@ $(CFLAGS) -Iinclude

tspool: tests/tspool.c src/vector.c src/pool_alloc.c | build
	cc $^ -o bin/$@ $(CFLAGS) -Iinclude

kvsymdb: 
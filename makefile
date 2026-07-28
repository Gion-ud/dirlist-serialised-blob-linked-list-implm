#all: keylist.o tskl
all: build/kvsymdb.o  bin/symdb bin/tssym
#all: bin/list add_bin_path

_C_CXX_FLAGS = -O2 -Wall -Wextra -fno-exceptions \
	-fno-strict-aliasing -D_DEBUG #-DNDEBUG -g

CFLAGS = $(_C_CXX_FLAGS) -std=c11

CXXFLAGS = $(_C_CXX_FLAGS) -std=c++11 -fno-rtti

keylist.o: src/keylist.c | build
	cc -c $< -o build/$@ $(CFLAGS) -Iinclude -D_NO_DBG_PRINT

tskl: build/keylist.o tests/tskl.c src/vector.c | build
	cc $^ -o bin/$@ $(CFLAGS) -Iinclude

tsvec: tests/tsvec.cxx src/vector.c | build
	cc -c src/vector.c -o build/vector.o $(CFLAGS) -Iinclude
	g++ $< build/vector.o -o bin/$@ $(CFLAGS) -Iinclude

tspool: tests/tspool.c src/vector.c src/pool_alloc.c | build
	cc $^ -o bin/$@ $(CFLAGS) -Iinclude


build/kvsymdb.o: src/kvsymdb.cxx | build
	cc -c $< -o $@ $(CXXFLAGS) -Iinclude -Isrc

bin/tssym: build/kvsymdb.o tests/tssym.cxx | bin
	cc $^ -o $@ $(CXXFLAGS) -Iinclude -Isrc -Llib -lstdc++ -lgcc -lgcc_s -lz -lmman

bin/symdb: build/kvsymdb.o tests/symdb.c | bin
	cc $^ -o $@ $(CFLAGS) -Iinclude -Isrc -Llib -lstdc++ -lgcc -lgcc_s -lz -lmman

bin/list: src/linked_list.cxx | bin
	cc -x c++ $^ -o $@ $(CXXFLAGS) -Iinclude -Llib -lstdc++ -lgcc -lgcc_s

add_bin_path:
	export PATH="$$PATH:$$(pwd)/bin"

clean:
	rm build/*

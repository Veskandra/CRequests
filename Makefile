CC      ?= gcc
AR      ?= ar
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Iinclude -D_WIN32_WINNT=0x0601
LDLIBS  := -lws2_32 -lsecur32 -lcrypt32

SRC := src/util.c src/url.c src/cookies.c src/transport.c src/http.c src/client.c
OBJ := $(SRC:.c=.o)
LIB := libcrequests.a

EXAMPLES := build/get.exe build/post_json.exe build/session.exe
DLL_OBJ  := $(patsubst src/%.c,build/dll/%.o,$(SRC))

.PHONY: all clean test examples dll dll-test

all: $(LIB) examples

$(LIB): $(OBJ)
	$(AR) rcs $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

build:
	-mkdir build

examples: $(EXAMPLES)

build/get.exe: examples/get.c $(LIB) | build
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

build/post_json.exe: examples/post_json.c $(LIB) | build
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

build/session.exe: examples/session.c $(LIB) | build
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

build/test_crequests.exe: tests/test_crequests.c $(LIB) | build
	$(CC) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: build/test_crequests.exe
	python tests/run_tests.py build/test_crequests.exe

# Build a Windows DLL (libcrequests.dll) with a MinGW import library.  The
# public header marks functions with __declspec(dllexport/dllimport) when
# CREQUESTS_SHARED is defined.
build/dll:
	-mkdir build\dll

build/dll/%.o: src/%.c | build/dll
	$(CC) $(CFLAGS) -DCREQUESTS_BUILD -DCREQUESTS_SHARED -c $< -o $@

libcrequests.dll: $(DLL_OBJ)
	$(CC) -shared -o $@ $^ $(LDLIBS) \
		-Wl,--exclude-all-symbols -Wl,--out-implib,libcrequests.dll.a

dll: libcrequests.dll

build/dll_smoke.exe: tests/dll_smoke.c libcrequests.dll | build
	$(CC) $(CFLAGS) -DCREQUESTS_SHARED tests/dll_smoke.c libcrequests.dll.a $(LDLIBS) -o $@

dll-test: build/dll_smoke.exe
	copy /Y libcrequests.dll build\libcrequests.dll >NUL
	build\dll_smoke.exe

clean:
	rm -f $(OBJ) $(DLL_OBJ) $(LIB) libcrequests.dll libcrequests.dll.a build/*.exe build/*.dll
	-rmdir build/dll build 2>/dev/null || true

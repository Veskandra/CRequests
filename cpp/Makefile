CC       ?= gcc
CXX      ?= g++
AR       ?= ar
CFLAGS   ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -I../include -D_WIN32_WINNT=0x0601
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic -Iinclude -I../include -D_WIN32_WINNT=0x0601
LDLIBS   := -lws2_32 -lsecur32 -lcrypt32

CORE_SRC := ../src/util.c ../src/url.c ../src/cookies.c ../src/transport.c ../src/http.c ../src/client.c
CORE_OBJ := $(patsubst ../src/%.c,build/core/%.o,$(CORE_SRC))
CPP_OBJ  := build/cpprequests.o
LIB      := libcpprequests.a
DLL_OBJ  := $(patsubst ../src/%.c,build/dll/core/%.o,$(CORE_SRC)) build/dll/cpprequests.o

EXAMPLES := build/get.exe build/post_json.exe build/session.exe build/json_demo.exe

.PHONY: all clean examples test dll dll-test

all: $(LIB) examples

$(LIB): $(CPP_OBJ) $(CORE_OBJ)
	$(AR) rcs $@ $^

build:
	-mkdir build

build/core:
	-mkdir build\core

build/dll:
	-mkdir build\dll

build/dll/core:
	-mkdir build\dll\core

build/core/%.o: ../src/%.c | build/core
	$(CC) $(CFLAGS) -c $< -o $@

build/cpprequests.o: src/cpprequests.cpp include/cpprequests.hpp include/cpprequests/json.hpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

examples: $(EXAMPLES)

build/get.exe: examples/get.cpp $(LIB) | build
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDLIBS) -o $@

build/post_json.exe: examples/post_json.cpp $(LIB) | build
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDLIBS) -o $@

build/session.exe: examples/session.cpp $(LIB) | build
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDLIBS) -o $@

build/json_demo.exe: examples/json_demo.cpp $(LIB) | build
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDLIBS) -o $@

build/test_cpprequests.exe: tests/test_cpprequests.cpp $(LIB) | build
	$(CXX) $(CXXFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: build/test_cpprequests.exe
	python tests/run_tests.py build/test_cpprequests.exe

# C++ DLL.  C++ DLLs are compiler/runtime-specific; this build is intended for
# the same MinGW-w64 x64 toolchain that produced it.
build/dll/core/%.o: ../src/%.c | build/dll/core
	$(CC) $(CFLAGS) -c $< -o $@

build/dll/cpprequests.o: src/cpprequests.cpp include/cpprequests.hpp include/cpprequests/json.hpp | build/dll
	$(CXX) $(CXXFLAGS) -DCPPR_BUILD -DCPPR_SHARED -c $< -o $@

libcpprequests.dll: $(DLL_OBJ)
	$(CXX) -shared -o $@ $^ $(LDLIBS) \
		-Wl,--exclude-all-symbols -Wl,--out-implib,libcpprequests.dll.a

dll: libcpprequests.dll

build/dll_smoke.exe: tests/dll_smoke.cpp libcpprequests.dll | build
	$(CXX) $(CXXFLAGS) -DCPPR_SHARED tests/dll_smoke.cpp \
		libcpprequests.dll.a $(LDLIBS) -o $@

dll-test: build/dll_smoke.exe
	copy /Y libcpprequests.dll build\libcpprequests.dll >NUL
	python tests/run_tests.py build/dll_smoke.exe

clean:
	rm -f $(CPP_OBJ) $(CORE_OBJ) $(DLL_OBJ) $(LIB) libcpprequests.dll \
		libcpprequests.dll.a build/*.exe build/*.dll
	-rmdir build/core build/dll/core build/dll build 2>/dev/null || true

target=Release
dir=/Users/masa/
CXX := clang++
RTTIFLAG := -fno-rtti -fno-exceptions
CXXFLAGS := $(shell llvm-config --cxxflags) $(RTTIFLAG) -O0 -g3
LLVMLDFLAGS := $(shell llvm-config --ldflags --libs --system-libs) -ldl

SOURCES = ast.cpp
OBJECTS = $(SOURCES:.cpp=.o)
EXES = $(OBJECTS:.o=)
CLANGLIBS = \
	-lclangFrontend \
	-lclangSerialization \
	-lclangDriver \
	-lclangTooling \
	-lclangParse \
	-lclangSema \
	-lclangAnalysis \
	-lclangEdit \
	-lclangAST \
	-lclangLex \
	-lclangBasic \
	-lclangRewriteCore \
	-lclangRewriteFrontend \
	-lcurses

all: $(OBJECTS) insert-wb
.PHONY: clean install

%: %.o
	$(CXX) -o $@ $< $(CLANGLIBS)

insert-wb: ast.o
	$(CXX) -o $@ $^ $(CLANGLIBS) $(LLVMLDFLAGS)

clean:
	-rm -f $(EXES) $(OBJECTS) ast.o compile_commands.json a.out *~

install: insert-wb
	cp $< $$(dirname $$(which clang))

test: insert-wb
	./insert-wb test/struct.c

.PHONY: all test clean up

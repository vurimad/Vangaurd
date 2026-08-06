#!/bin/sh

if [ -z $1 ]; then
	MODE="release"
else
	MODE=$1
fi

echo "Building RedLexer in $MODE mode"

if [ $MODE = "debug" ]; then
	FOLDER=x64_Linux.Debug
	FLAGS=" -g "
else
	FOLDER=x64_Linux.Release
	FLAGS=" -O3 "
fi

BISON=../../../external/bison/bin/bison.elf
export BISON_PKGDATADIR=../../../external/bison/share/bison
FLEX=../../../external/flex/bin/flex.elf

OUTPUTNAME=libredlexer_native.a

echo "Creating token header from bison"
$BISON --defines="./gen/bison_tokens.h" -o "./gen/bison_unused.cxx" "../../../src/common/scriptCompiler/src/scriptFileParser.bison"

echo "Building Flex Scanner"
$FLEX -o "./gen/scripts.cxx" --never-interactive --batch -Cfe "src/scripts.flex"

mkdir -p "../temp/$FOLDER"
clang++ src/lexer.cpp -c $FLAGS -std=c++14 -m64 -march=core-avx2 -o ../temp/$FOLDER/lexer.o -I./src -I./include -DUNICODE -finput-charset=UTF-8
clang++ src/state.cpp -c $FLAGS -std=c++14 -m64 -march=core-avx2 -o ../temp/$FOLDER/state.o -I./src -I./include -DUNICODE -finput-charset=UTF-8

mkdir -p "../lib/$FOLDER"
ar rcs ../lib/$FOLDER/$OUTPUTNAME ../temp/$FOLDER/lexer.o ../temp/$FOLDER/state.o

echo "Output ../lib/$FOLDER/$OUTPUTNAME"
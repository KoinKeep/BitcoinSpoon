#!/bin/bash

cd code

GCC=gcc
GPP=g++

for file in *.c
do
 filename=$(echo "$file" | tr / _)
 echo $GCC $GCCFLAGS -g -c "$file" -o objects/"${filename%.*}".o
 $GCC $GCCFLAGS -g -c "$file" -o objects/"${filename%.*}".o
done

for file in *.cpp
do
 filename=$(echo "$file" | tr / _)
 echo $GPP $GCCFLAGS -g -c "$file" -o objects/"${filename%.*}".o
 $GPP $GCCFLAGS -g -c "$file" -o objects/"${filename%.*}".o
done

ar rvs objects/all.a objects/*.o

cd ..

echo "Compiling test"
$GCC $GCCFLAGS -g test/test.c code/objects/all.a libraries/objects/all.a -lgmp -lstdc++ -o test/test

echo "Compiling example"
$GCC $GCCFLAGS -g -Icode test/example.c code/objects/all.a libraries/objects/all.a -lsqlite3 -lgmp -lstdc++ -o test/example

echo "Compiling webserver_example"
$GCC $GCCFLAGS -g -Icode test/webserver_example.c code/objects/all.a libraries/objects/all.a -lsqlite3 -lgmp -lstdc++ -o test/webserver_example

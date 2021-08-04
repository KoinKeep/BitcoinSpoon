#!/bin/bash

cd code

for file in *.c
do
 filename=$(echo "$file" | tr / _)
 echo gcc $GCCFLAGS -g -c "$file" -o objects/"${filename%.*}".o
 gcc $GCCFLAGS -g -c "$file" -o objects/"${filename%.*}".o
done

for file in *.cpp
do
 filename=$(echo "$file" | tr / _)
 echo g++ $GCCFLAGS -g -c "$file" -o objects/"${filename%.*}".o
 g++ $GCCFLAGS -g -c "$file" -o objects/"${filename%.*}".o
done

ar rvs objects/all.a objects/*.o

cd ..

echo "Compiling test"
gcc $GCCFLAGS -g test/test.c code/objects/all.a libraries/objects/all.a -lgmp -lstdc++ -o test/test

echo "Compiling example"
gcc $GCCFLAGS -g -Icode test/example.c code/objects/all.a libraries/objects/all.a -lsqlite3 -lgmp -lstdc++ -o test/example

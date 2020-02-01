#!/bin/bash

cd code

for file in *.c
do
 filename=$(echo "$file" | tr / _)
 echo gcc -g -c "$file" -o objects/"${filename%.*}".o
 gcc -g -c "$file" -o objects/"${filename%.*}".o
done

for file in *.cpp
do
 filename=$(echo "$file" | tr / _)
 echo ++ -g -c "$file" -o objects/"${filename%.*}".o
 g++ -g -c "$file" -o objects/"${filename%.*}".o
done

ar rvs objects/all.a objects/*.o

cd ..

echo "Compiling test"
gcc -g test/test.c code/objects/all.a libraries/objects/all.a -lgmp -lstdc++ -o test/test

echo "Compiling example"
gcc -g -Icode test/example.c code/objects/all.a libraries/objects/all.a -lsqlite3 -lgmp -lstdc++ -o test/example

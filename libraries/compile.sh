#!/bin/bash

echo "==== Compiling raw source files ===="

for file in */*.c
do
 echo Compile $file
 filename=$(echo "$file" | tr / _)
 gcc -c "$file" -o objects/"${filename%.*}".o
done

for file in */*.cpp
do
 echo Compile $file
 filename=$(echo "$file" | tr / _)
 g++ -c "$file" -o objects/"${filename%.*}".o
done

echo "==== Let's make libsecp256k1 now ===="

cd secp256k1

./autogen.sh
./configure --enable-experimental --enable-module-ecdh
make

cd ..

cd objects

ar -x ../secp256k1/.libs/libsecp256k1.a

cd ..

ar rvs objects/all.a objects/*.o

#!/bin/bash

cd libraries
./compile.sh && result=true || result=false
cd ..

if ! [ $result ]; then
 exit
fi

./fast_compile.sh

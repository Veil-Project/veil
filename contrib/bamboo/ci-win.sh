#!/bin/bash
set -e
cd depends/
echo `pwd`
make HOST=x86_64-w64-mingw32
cd ../
./autogen.sh
./configure --prefix=`pwd`/depends/x86_64-w64-mingw32
make
echo "done"

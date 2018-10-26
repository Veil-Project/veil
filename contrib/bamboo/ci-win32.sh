#!/bin/bash
set -e
cd depends/
echo `pwd`
make HOST=i686-w64-mingw32
cd ../
./autogen.sh
./configure --prefix=`pwd`/depends/i686-w64-mingw32
make
echo "done"


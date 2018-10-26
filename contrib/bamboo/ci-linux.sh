#!/bin/bash
set -e
cd depends/
echo `pwd`
make HOST=x86_64-linux-gnu
cd ../
./autogen.sh
./configure --prefix=`pwd`/depends/x86_64-linux-gnu
make
echo "done"

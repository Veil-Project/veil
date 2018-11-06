#!/bin/bash
set -e
cd depends/
echo `pwd`
make HOST=x86_64-apple-darwin11
cd ../
./autogen.sh
./configure --prefix=`pwd`/depends/x86_64-apple-darwin11
make
echo "done"

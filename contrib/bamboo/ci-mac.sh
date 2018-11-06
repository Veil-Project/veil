#!/bin/bash
set -e
tar -C `pwd`/depends/SDKs/ -xf ~/SDKs/MacOSX10.11.sdk.tar.gz
cd depends/
echo `pwd`
make HOST=x86_64-apple-darwin11
cd ../
./autogen.sh
./configure --prefix=`pwd`/depends/x86_64-apple-darwin11
make
echo "done"

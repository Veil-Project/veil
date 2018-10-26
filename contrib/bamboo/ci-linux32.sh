#!/bin/bash
set -e
cd depends/
echo `pwd`
make HOST=i686-pc-linux-gnu
cd ../
./autogen.sh
./configure --prefix=`pwd`/depends/i686-pc-linux-gnu
make
echo "done"

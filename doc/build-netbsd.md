NetBSD build guide
======================
(updated for NetBSD 7.0)

This guide describes how to build veild and command-line utilities on NetBSD.

This guide does not contain instructions for building the Qt GUI.

Preparation
-------------

You will need the following modules, which can be installed via pkgsrc or pkgin:

```
autoconf
automake
boost
db4
git
gmake
libevent
libtool
python27
gmp

```

Download the source code:
```
git clone https://github.com/Veil-Project/veil
```

See [dependencies.md](dependencies.md) for a complete overview.

### Building Bitcoin Core

**Important**: Use `gmake` (the non-GNU `make` will exit with an error).

With wallet:
```
./autogen.sh
./configure CPPFLAGS="-I/usr/pkg/include" LDFLAGS="-L/usr/pkg/lib" BOOST_CPPFLAGS="-I/usr/pkg/include" BOOST_LDFLAGS="-L/usr/pkg/lib"
gmake
```

Without wallet:
```
./autogen.sh
./configure --disable-wallet CPPFLAGS="-I/usr/pkg/include" LDFLAGS="-L/usr/pkg/lib" BOOST_CPPFLAGS="-I/usr/pkg/include" BOOST_LDFLAGS="-L/usr/pkg/lib"
gmake
```

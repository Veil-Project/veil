Veil
====

Setup
---------------------
Veil client builds the backbone of the network. It downloads and, by default, stores the entire history of Veil transactions; depending on the speed of your computer and network connection, the synchronization process can take anywhere from a few hours to a day or more.

To download Veil, visit [veil-project.com](https://veil-project.com/get-started/).

Running
---------------------
The following are some helpful notes on how to run Veil on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/veil-qt`  (GUI) or
- `bin/veild`    (headless)
- `bin/veil-cli` (headless)
- `bin/veild-tx` (headless)

### Windows

Unpack the files into a directory, and then run veil-qt.exe.

### macOS

Drag Veil to your applications folder, and then run Veil.

### Need Help?

for help and more information.
* Ask for help on [#Discord](https://discord.gg/XwuC8Nu).
* Ask for help on the [BitcoinTalk](https://bitcointalk.org/index.php?topic=5065331.0) forums.

Building
---------------------
The following are developer notes on how to build Veil on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The Veil repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Travis CI](travis-ci.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [VIPS](vips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)


### Miscellaneous
---------------------
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.

# macOS Build Instructions and Notes

The commands in this guide should be executed in a Terminal application.
The built-in one is located in
```
/Applications/Utilities/Terminal.app
```

## Preparation
Install the macOS command line tools:

```shell
xcode-select --install
```

When the popup appears, click `Install`.

Then install [Homebrew](https://brew.sh).

## Dependencies
```shell
    brew install automake libtool boost miniupnpc openssl pkg-config protobuf python qt@5 libevent qrencode gmp
```

If you run into issues, check [Homebrew's troubleshooting page](https://docs.brew.sh/Troubleshooting).
See [dependencies.md](dependencies.md) for a complete overview.

If you want to build the disk image with `make deploy` (.dmg / optional), you need RSVG:
```shell
brew install librsvg
```


#### Berkeley DB

It is recommended to use Berkeley DB 4.8. If you have to build it yourself,
you can use [this](/contrib/install_db4.sh) script to install it
like so:

```shell
CFLAGS="-Wno-error=implicit-function-declaration"  ./contrib/install_db4.sh .
```

from the root of the repository.

**Note**: You only need Berkeley DB if the wallet is enabled (see the section *Disable-Wallet mode* below).

## Build Veil Core
1. Clone the Veil source code and cd into `veil`
     ```shell
     git clone https://github.com/Veil-Project/veil.git
     
     cd veil
     ```

2.  Build Veil:

    Configure and build the headless Veil binaries as well as the GUI (if Qt is found).

    You can disable the GUI build by passing `--without-gui` to configure.
    ```shell
    ./autogen.sh
    ./configure
    make
    ```

3.  It is recommended to build and run the unit tests:
    ```shell
    make check
    ```

4.  You can also create a  `.dmg` that contains the `.app` bundle (optional):
    ```shell
    make deploy
    ```

## Disable-wallet mode
When the intention is to run only a P2P node without a wallet, Veil may be
compiled in disable-wallet mode with:
```shell
./configure --disable-wallet
```

In this case there is no dependency on [*Berkeley DB*](#berkeley-db) and [*SQLite*](#sqlite).

Mining is also possible in disable-wallet mode using the `getblocktemplate` RPC call.
## Running

Veil is now available at `./src/veild`

Before running, it's recommended that you create an RPC configuration file.
    ```shell
    echo -e "rpcuser=veilrpc\nrpcpassword=$(xxd -l 16 -p /dev/urandom)" > "/Users/${USER}/Library/Application Support/Veil/veil.conf"

    chmod 600 "/Users/${USER}/Library/Application Support/Veil/veil.conf"
    ```

The first time you run veild, it will start downloading the blockchain. This process could 
take many hours, or even days on slower than average systems.

You can monitor the download process by looking at the debug.log file:
    ```shell
    tail -f $HOME/Library/Application\ Support/Veil/debug.log
    ```

## Other commands:
```shell
    ./src/veild -daemon # Starts the veil daemon.
    ./src/veil-cli --help # Outputs a list of command-line options.
    ./src/veil-cli help # Outputs a list of RPC commands when the daemon is running.
```

## Notes

* Tested on OS X 10.10 Yosemite through macOS 10.13 High Sierra on 64-bit Intel processors only.

* Building with downloaded Qt binaries is not officially supported. See the notes in [#7714](https://github.com/Veil-Project/veil/issues/7714)

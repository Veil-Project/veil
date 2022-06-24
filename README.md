Veil integration/staging tree
=============================

[![GitHub release (latest by date)](https://img.shields.io/github/v/release/veil-project/veil?color=%23001e58&cacheSeconds=3600)](https://github.com/Veil-Project/veil/releases)
[![GitHub Release Date](https://img.shields.io/github/release-date/veil-project/veil?color=%23001e58&cacheSeconds=3600)](https://github.com/Veil-Project/veil/releases)

https://veil-project.com

What is Veil?
---------------

Veil is a cryptocurrency released in 2019 setting a new standard in privacy-focused networks. Combining a lightened version of the Zerocoin protocol with RingCT, Veil provides always-on anonymity. Dandelion and Bulletproofs provide for enhanced privacy and decreased data size, respectively, for Veil's network transactions.

Hybrid cpu-mineable Proof-of-Work and Proof-of-Stake consensus provided a fair coin distribution  during Veil's first year (no pre-mine or ICO), with three new algorithms in 2020 replacing Veil's original X16RT, which originally gave enhanced protection against FPGAs. Veil's triple Proof-of-Work algorithms, SHA256D, RandomX, and ProgPoW provide highly distributed security, and the opportunity for Veil users to earn yield through staking rewards, while providing for CPU and GPU-based mining.

Finally, network-encoded budgeting ensures the internal funding necessary for long-term sustainability.

More information about the Veil Project—including the project roadmap, news, detailed specifications and team member bios—can be found at the project website:

[https://www.veil-project.com](https://www.veil-project.com)

License
-------

Veil is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

Development Process
-------------------

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/Veil-Project/veil/tags) are created
regularly to indicate new official, stable release versions of Veil.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

Testing
-------

Testing and code review is the bottleneck for development; we get more pull
requests than we can review and test on short notice. Please be patient and help out by testing
other people's pull requests, and remember this is a security-critical project where any mistake might cost people
lots of money.

### Automated Testing

Developers are strongly encouraged to write [unit tests](src/test/README.md) for new code, and to
submit new unit tests for old code. Unit tests can be compiled and run
(assuming they weren't disabled in configure) with: `make check`. Further details on running
and extending unit tests can be found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written
in Python, that are run automatically on the build server.
These tests can be run (if the [test dependencies](/test) are installed) with: `test/functional/test_runner.py`

The Travis CI system makes sure that every pull request is built for Windows, Linux, and macOS, and that unit/sanity tests are run automatically.

### Manual Quality Assurance (QA) Testing

Changes should be tested by somebody other than the developer who wrote the
code. This is especially important for large or high-risk changes. It is useful
to add a test plan to the pull request description if testing the changes is
not straightforward.

Translations
------------

Translations will be generated through integration with Transifex, i.e. periodically pulled from the Transifex platform, and merged into the git repository. Details of the translation process will be added to this page once integration is complete.

Important: We can not accept translation changes as pull requests, as subsequent pulls from Transifex would automatically overwrite them.

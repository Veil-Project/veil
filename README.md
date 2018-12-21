Veil integration/staging tree
=============================

[![Build Status](https://travis-ci.org/Veil-Project/veil.svg?branch=master)](https://travis-ci.org/Veil-Project/veil)

https://veil-project.com

What is Veil?
----------------

Veil is a cryptocurrency intending to set a new standard in the space of privacy-focused networks. Combining the Zerocoin protocol with RingCT, Veil provides always-on anonymity. Dandelion and Bulletproofs provide for enhanced privacy and decreased data size, respectively, for its network transactions. 

Hybrid Proof-of-Work and Proof-of-Stake consensus during its first year provide for fair coin distribution (i.e. no pre-mines or ICOs), highly distributed security, and the opportunity Veil users to earn yield through staking rewards. An enhanced hashing algorithm evolved from X16R—called X16RT—provides additional support for fair distribution through enhanced protection against FPGAs.

Finally, network-encoded budgeting for both operations and a dedicated research & development entity, known as Veil Labs, ensures the internal funding necessary for long-term sustainability.

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

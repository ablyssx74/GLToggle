# GLToogle v1.0.6

## Fixed
- **The update check reads the version number from its first digit.** The check compares the program's version with the `VERSION` file on GitHub. The old comparison needed a letter in front of the number (the `v` of `v1.0.6`), so a version file that held just the number was read as 0.0.0 and no update was ever announced. It now takes the first number of the form X.Y.Z in either text, with or without the letter. Nothing else changed.

## Install
Install `GLToogle-1.0.6-1-x86_64.hpkg` on 64-bit Haiku, for the whole system. (There is no 32-bit package: the Makefile builds for 64-bit Haiku only.)

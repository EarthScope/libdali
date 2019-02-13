# libdali: the DataLink client library

This package contains the source code, documentation and example code
for libdali, the DataLink client library.  For further information
regarding the library interface and usage see the documentation in the
'doc' directory, including a Users Guide and man pages.

The library should work in Linux, BSD (and derivatives like macOS),
Solaris and MS-Windows environments.

For installation instructions see the INSTALL file.

## Building and Installing 

In most Unix/Linux environments a simple 'make' will build the program.

The CC and CFLAGS environment variables can be used to configure
the build parameters.

## Extras 

The 'example' directory includes an example DataLink client that uses
libdali.

The 'doc' directory includes all associated documentation including
a Users Guide and man pages for library functions.

## Threading

The library is thread-safe under Unix-like environments with the
condition that each connection parameter set (DLCP) is handled by a
single thread.  Thread independent logging schemes are possible.
Under Windows the library is probably not thread-safe.

## Licensing

GNU GPL version 3.  See included LICENSE file for details.

Copyright (C) 2019 Chad Trabant, IRIS Data Management Center

## Acknowlegements

Numerous improvements have been incorporated based on feedback and
patches submitted by others.  Individual acknowlegements are included
in the ChangeLog.

## Pronunciation?

lib = 'l' + [eye] + 'b'  (as in library, long 'i')
dali = 'da' + [lee] (as in Salvador Dali)

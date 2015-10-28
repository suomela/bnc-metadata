bnc-metadata
============

Dependencies
------------

http://mesonbuild.com/ and

    brew install boost pugixml ninja

Compilation
-----------

Debugging:

    mkdir build
    BOOST_ROOT="/usr/local" meson build
    cd build
    ninja

Faster:

    mkdir build-opt
    BOOST_ROOT="/usr/local" meson --buildtype=debugoptimized build-opt
    cd build-opt
    ninja


Usage
-----

    ./bnc-metadata BNC-ROOT


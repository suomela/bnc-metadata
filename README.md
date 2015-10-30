bnc-metadata
============

Extract detailed metadata from the spoken parts of BNC.


Dependencies
------------

Install http://mesonbuild.com/ and

    brew install boost pugixml ninja


Compilation
-----------

    mkdir build
    BOOST_ROOT="/usr/local" meson build
    cd build
    ninja


Usage
-----

    ./bnc-metadata BNC-DIRECTORY ...

Example:

    ./bnc-metadata ~/bnc/Texts/K

This will create the following SQLite datase:

    bnc.db

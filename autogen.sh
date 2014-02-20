#! /bin/sh
set -x

libtoolize
autoreconf -i -f
intltoolize -c --automake --force

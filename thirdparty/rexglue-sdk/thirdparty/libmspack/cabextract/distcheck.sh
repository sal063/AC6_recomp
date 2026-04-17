#!/bin/sh
# checks the project can be distributed

./rebuild.sh && make distcheck &&
 make clean &&
 ./configure --with-external-libmspack && make check all &&
 DISTCHECK_CONFIGURE_FLAGS=--with-external-libmspack make distcheck

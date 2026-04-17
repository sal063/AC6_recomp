#!/bin/sh
# rebuilds the entire project

./cleanup.sh && ./autogen.sh && ./configure && make check all

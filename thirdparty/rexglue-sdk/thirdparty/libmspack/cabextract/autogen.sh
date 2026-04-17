#!/bin/sh
# Runs the autoreconf tool, creating the configure script

autoreconf -i -W all
echo you can now run ./configure

#!/bin/sh

set -e

autoreconf --force --install --symlink --warnings=all

args="\
--prefix=/usr"

if test -z "${NOCONFIGURE}"; then
  ./configure $args "$@"
  make clean
fi

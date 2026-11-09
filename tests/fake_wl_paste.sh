#!/bin/sh
case "$FAKE_WL" in
  hang) exec sleep 5 ;;
  empty) exit 1 ;;
  *) printf 'the selected words' ;;
esac

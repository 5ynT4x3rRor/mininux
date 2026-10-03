# TinyC MiniNux

This directory contains the freestanding TinyC implementation.

The lexer intentionally has no dependency on `stdio`, `stdlib` or `libtcc`.
It is the first stage toward a compiler binary stored at `/bin/tinyc`.
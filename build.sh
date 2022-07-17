#!/bin/bash
gcc -o tree_view.so tree_view.c $(yed --print-cflags) $(yed --print-ldflags)

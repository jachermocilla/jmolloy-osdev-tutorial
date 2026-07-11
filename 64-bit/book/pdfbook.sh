#!/bin/bash
make clean ; make pdf
mv build/pdf/book.pdf ./mastering-c-for-kernel-dev_book.pdf
make clean

#!/bin/bash
echo "![](./images/cover.png)" | pandoc -f markdown -o cover.pdf
make clean ; make pdf
#mv build/pdf/book.pdf ./mastering-c-for-kernel-dev_book.pdf
pdftk cover.pdf build/pdf/book.pdf  cat output ./mastering-c-for-kernel-dev_book.pdf
make clean
rm cover.pdf

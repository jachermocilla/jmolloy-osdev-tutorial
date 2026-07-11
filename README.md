# jmolloy-osdev-tutorial

64-bit port is available [here](./64-bit)

Source code from http://www.jamesmolloy.co.uk/tutorial_html ( [mirror](https://jachermocilla.github.io/jmolloy-osdev-tutorial) )

Known Bugs: https://wiki.osdev.org/James_Molloy%27s_Tutorial_Known_Bugs

For me, a very good tutorial on how to write an operating system from 
scratch, touching on the essentials, piece by piece.

Tested on Ubuntu 22.04 x86_64


Disable automatic opening in window after mount.

```
$ ./update.sh <folder name>
$ qemu-system-i386 -fda floppy.img -boot a
```

If the folder name ends with "_initrd"
```
$ ./update-initrd.sh <folder name>
$ qemu-system-i386 -fda floppy-initrd.img -boot a
```

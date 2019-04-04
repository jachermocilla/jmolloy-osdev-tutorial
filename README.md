# jmolloy-osdev-tutorial
Source code from http://www.jamesmolloy.co.uk/tutorial_html

Known Bugs: https://wiki.osdev.org/James_Molloy%27s_Tutorial_Known_Bugs

A very good tutorial on how to write an operating system from 
scratch, touching on the essentials, piece by piece.

Tested on Ubuntu 16.04 x86_64


Disable automatic opening in window after mount.
```
$ gsettings set org.gnome.desktop.media-handling automount-open false
```

```
$ ./update.sh <folder name>
$ qemu-system-i386 -fda floppy.img -boot a
```

If the folder name ends with "_initrd"
```
$ ./update-initrd.sh <folder name>
$ qemu-system-i386 -fda floppy-initrd.img -boot a
```

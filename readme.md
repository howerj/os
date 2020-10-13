# Portable FAT-32 Library

* Project: FAT-32 library and Utilities in C
* Author: Richard James Howe
* Email: howe.r.j.89@gmail.com
* License: The Unlicense
* Repo: https://github.com/howerj/fat32

**This project is a work in progress and is liable to not work/not do as it
says, use it at your own risk**.

# To Do

* Implement a portable FAT-32 library in pure C that can format and manipulated
  FAT-32 images. The facilities for read/writing should be passed as callbacks
  to the library so the library can be ported to an embedded platform.
* If possible implement FAT-12 and FAT-16, so long as the task it not too
  onerous.
* Add unit tests.
* Add specifications and my own documentation
* Make a utility that allows the creation of FAT32 images given a root
  directory. This will require non-portable code to be added to recursively
  walk a given directory and generate a list of files and directories to
  include in an archive. To keep things portable, an external python script
  could be used to generate that list.  Code for creating and formatting a blank 
  disk will also need to be implemented.
* Implement a command line interface that implements the following commands;
  cat, ls, cd, open, read, write, close, unlink, mkdir, rmdir, df, stat.
* Turn this file into a manual page that describes the project, and the FAT-32
  file system.
* Make a variant in Rust?


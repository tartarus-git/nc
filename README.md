# nc (netcat)

This is a reimplementation of the classic ```nc``` utility. The reason I made it is because Windows doesn't come with it by default. There are probably other versions of this program that also run on Windows, but I wanted to make this anyway.
It doesn't have all the features that the classic ```nc``` has, but it's got the ones that I find useful as of this moment.

This program compiles for Windows as well as Linux, which is pretty cool.

# How To Build

## Prerequisites

```
clang++-11 or later (for Linux)
MSVC 19.29 or later (for Windows)
git (not just for cloning, the makefile also uses git to clean up the repo)
make
bash
(sh (either as itself or as a link to a separate backwards-compatible shell) should be available on every Linux system AFAIK, so no worries here)
```

## Guide For Linux

After cloning the repo (there aren't currently any submodules, but I might add some later so maybe use recursive cloning just in case), go into the directory and run ```make```. As of now, ```make``` uses the normal Bourne Shell to execute recipe lines, but I might add some bash specific stuff later and switch to bash, so your best bet is to have bash available when running ```make```.

## Guide For Windows

As of now, you can't use ```make``` on Windows in this repo. You have to run the supplied batch file from the top-level folder to build the program: ```build_on_windows.bat```
This script will put the output into the top-level folder, NOT in the bin folder.

# Command Usage

The program itself explains this just fine, simply run the following:
```
nc --help
```
It'll tell you everything you need to know.

# Installation

There isn't really anything to install. The resulting binary has no dependencies. Just move it where ever you want to move it (like a folder that's in the PATH or something) and use it.

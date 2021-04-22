# Decaf-to-Mips-Compiler
## Introduction
## Usage
Before compilation, please make sure `g++`, `flex` and `bison` are available to use.
First build the program using `make` to generate an executable.
```
make clean
make

```
The executable takes as input `stdin` and outputs to `stdout`. You may want to specifiy paths to input and output file.
`./dcc < ./samples/t8.decaf > test.s`

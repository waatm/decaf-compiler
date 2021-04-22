# Decaf-to-Mips-Compiler
## Introduction
This is a Decaf compiler project in C/C++. Decaf is a strongly-typed, object-oriented language with support for inheritance and encapsulation. By design, it keeps many features in C/C++, but still way simpler. The target language is Mips assembly language.

This project is an aggregation of work in a series of 5 sub projects. Sources codes, projects specs and instructions were provided in the EECS483 course.
## Usage
Before compilation, please make sure `g++`, `flex` and `bison` are available to use.
First build the program using `make` to generate an executable.
```
make clean
make
```
The executable takes as input `stdin` and outputs to `stdout`. You may want to specifiy paths to input and output file.
`./dcc < ./samples/t8.decaf > test.s`
To execute the Mips program, make sure the Spim simulator is available. Use `spim` to launch it .
`spim -f test.s`
## Implementations
### Lexical and Syntax Analysis
In project 1 and 2, the compiler used Flex and Bison to handle lexical and syntax analysis and built an abstract syntax tree (AST) as output.
### Semantic Abalysis
In project 3, the compiler performs semantic analysis to conduct scope checking and type checking. The program will print out appropriate error messages for the violations. 
### Code Generation
Once all error checking is done, the compiler generates three-address code (TAC) as intermediate representation (IR), and emits MIPS assembly code. The MIPS assembly code can be executed with the SPIM simulator.
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

Specifically, in project 1 the compiler used regular expressions to recognize different token types and in project 2, types for all the terminals and non-terminals and the production rules based on the context free grammars for decaf language were defined.
### Semantic Abalysis
In project 3, the compiler performed semantic analysis to conduct scope checking and type checking. The program would print out appropriate error messages for the violations.

A polymorphic `Check()` method was built in the AST classes. The semantic analyzer would do an in-order walk of the tree, visiting and checking each node.

### Code Generation
In project 4, the compiler would traverse the AST agian and use another polymorphic function `Emit()` to generate three-address code (TAC) as intermediate representation (IR), and move forward to emit MIPS assembly code.

### Register Allocation and Optimization
In the final project, the goal was to improve the back end and optimize the codes the compiler generated. Note that in project 4, only 2 of 18 available general purpose registers were used in the provided source code. In project 5, there was another pass in the compiler to perform dataflow analysis, specifically live variable analysis to help improve the register allocation scheme.

The compiler would traverse the generated TAC code, construct the control flow graph (CFG) in each function and compute the live variables at each program point. The compiler would then take as input the IN, OUT and KILL set generated in the previous step, build the interference graph and apply *Chaitin’s k-coloring algorithm* on the graph. Hence, each TAC instruction would be either assigned a register or spilled into memory.
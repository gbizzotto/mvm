JITting brainfuck virtual machine

Compile with

g++ -O3 -std=c++11 -Wall -o mvm mvm.cpp

Run with

	cat program_file | ./mvm

	cat input_file | ./mvm program_file

	echo "input" | ./mvm program_file

	cat program_file ! input_file | ./mvm

with ! being a file that contains the character "!".

If you use

	./mvm program_file

Press, CTRL+D to close stdin and start the program execution

mvm is, as of 2015, the second fastest known brainfuck VM, loosing in some cases to bfli.
It features innovative optimizations and a specialized (as in not general-purpose) x86-64 JIT.

See https://code.google.com/p/esotope-bfc/wiki/Comparison for the vocabulary used below.

Common optimizations:

	Removes "+-" and such

	Compression of consecutive commands

	Replaces "[-]" and "[+]" by a new "set-to-zero" instruction 

	Simple loop detection

	Seek loops

	Pointer propagation, called "add maps" in mvm

	Repeat loops, called "mul maps" in mvm

Original optimizations:

	Removes set-to-zero after "]"

	Removes "+" and "-" before set-to-zero

	Removes set-to-zero, "+" and "-" before input command

	Replaces "+" and "-" after "]" by set instead of add/sub

	Simplifies consecutive "[" and "]", e.g. "[[" becomes "[". This is not as simple as the compression of consecutive commands.

	Accepts all input before the beginning of the program and prints all output after the end of the program, simplifying the JIT

	Preallocates 3MB of memory, thus avoiding memory checks at runtime.

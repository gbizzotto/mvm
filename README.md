JITting brainfuck virtual machine

Compile with
g++ -O3 -std=c++11 -Wall -o mvm mvm.cpp

Run with
cat <program_file> | ./mvm
cat <input_file> | ./mvm <program_file>
echo "input" | ./mvm <program_file>
cat <program_file> ! <input_file> | ./mvm

with ! being a file that contains the character "!".

If you use
./mvm <program_file>

Press, CTRL+D to close stdin and start the program execution

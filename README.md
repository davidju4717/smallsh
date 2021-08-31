# Smallsh

Smallsh is a basic LINUX shell that prompts user for commands that can be run in the foreground or background.

## Features:

• Provides expansion for the variable $$. </br>
• Executes 3 commands "exit", "cd", "status" via code built into
the shell. </br>
• Executes other commands by creating new process and using the exec family of functions.</br>
• supports input/output redirection.</br>
• Implements custom handlers for 2 signals, SIGINT and SIGTSTP.</br>

## Notes:

• Must compile as no executable is included</br>
• To compile with command:
gcc --std=gnu99 -o smallsh main.c</br>
• To execute:
./smallsh

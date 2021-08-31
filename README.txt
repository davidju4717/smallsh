# Smallsh

Smallsh is a basic LINUX shell that prompts user for commands that can be run in the foreground or background.
Provides expansion for the variable $$. Executes 3 commands "exit", "cd", "status" via code built into
the shell, while executing other commands by creating new process and using the exec family of functions.
It also supports input/output redirection, and the implementation of custom handlers for 2 signals, SIGINT
and SIGTSTP.


## Notes:

1. Must compile as no executable is included

To compile with command:
gcc --std=gnu99 -o smallsh main.c

To execute:
./smallsh

/*
* smallsh is a shell that prompts user for commands that can be run in the foreground or background.
* Provides expansion for the variable $$. Executes 3 commands "exit", "cd", "status" via code built into
* the shell, while executing other commands by creating new process and using the exec family of functions.
* It also supports input/output redirection, and the implementation of custom handlers for 2 signals, SIGINT
* and SIGTSTP.
*/

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#define USERINPUT 2048

void handle_SIGTSTP(int signo);
void checkBackgroundProcesses(void);
void variableExpansion(char *source, char *searchValue, int pid);
struct command *processCommand(char *command);
int runCommand(struct command *currCommand, int wstatus);

/* global variable */
_Bool foregroundMode = 0;

/* struct for command information */
struct command {
    char* arguments[512];
    char *inputFile;
    char *outputFile;
    _Bool backgroundRun;
};

int main(void) {

    char input[USERINPUT];
    int pid = getpid();
    struct command *currCommand;
    int fStatus = 0;
    int index;

    // the parent process will ignore SIGINT
    struct sigaction SIGINT_action ={0};
    SIGINT_action.sa_handler = SIG_IGN;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags=SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // the parent process will be able to switch in and out of only-foreground mode with SIGTSTP
    struct sigaction SIGTSTP_action ={0};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags=SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while (1) {

        // check for completed background processes before prompting user
        checkBackgroundProcesses();

        // prompt user for command
        printf(": ");
        fflush(stdout);
        fgets(input, USERINPUT, stdin);
        input[strcspn(input, "\n")] = '\0';      // remove \n from fgets

        // check for blank lines and comments
        if (strcmp(input, "") == 0) {
            continue;
        }
        if (input[0] == '#') {
            continue;
        }
        // expand any instance of "$$" in a command into the process ID of the smallsh itself
        variableExpansion(input, "$$", pid);

        // break the input string into individual strings and store it in a structure
        currCommand = processCommand(input);

        // run the command and store the last foreground status
        fStatus = runCommand(currCommand,fStatus);

        // free all allocated memory and reset variables
        free(currCommand->inputFile);
        currCommand->inputFile=NULL;
        free(currCommand->outputFile);
        currCommand->outputFile=NULL;
        for (index=0;(currCommand->arguments)[index] ; index++) {
            (currCommand->arguments)[index] = NULL;
        }
        free(currCommand);
        currCommand=NULL;
    }

    return EXIT_SUCCESS;
}


/*
 * Signal handler for SIGTSTP. Change boolean global variable foregroundMode back and forth from
 * True and False when the signal is received. Backgound processes will be seen as foreground processes
 * if foregroundMode is True.
 */
void handle_SIGTSTP(int signo) {
    // if foregroundMode is False, print message and convert it to True
    if (foregroundMode == 0) {
        char* message = " Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50);
        fflush(stdout);
        foregroundMode = 1;
    }

        // if foregroundMode is True, print message and convert it to False
    else if (foregroundMode == 1) {
        char* message = " Exiting foreground-only mode\n";
        write (STDOUT_FILENO, message, 30);
        fflush(stdout);
        foregroundMode = 0;
    }

    char* prompt = ": ";
    write(STDOUT_FILENO, prompt, 3);
    fflush(stdout);
}


/*
 * This function checks for any terminated background processes. If found, prints out
 * the pid of the terminated background process and either its exit status or its
 * terminating signal.
 */
void checkBackgroundProcesses(void) {

    int bPid, bStatus;

    // check for completed background processes
    bPid = waitpid(-1, &bStatus, WNOHANG);
    while (bPid > 0) {
        // Print the completed process
        printf("background pid %d is done: ", bPid);
        fflush(stdout);

        // print out how the process was ended, its exit status or its terminating signal
        if (WIFEXITED(bStatus)) {
            printf("exit value %d\n", WEXITSTATUS(bStatus));
            fflush(stdout);
        }
        else if (WIFSIGNALED(bStatus)) {
            printf("terminated by signal %d\n", WTERMSIG(bStatus));
            fflush(stdout);
        }

        bPid = waitpid(-1, &bStatus, WNOHANG);
    }
}

/*
 * Takes a string, a variable (substring) and an integer as parameters. A new string is created by
 * expanding any instance of the variable in the original string into the integer. The original string is replaced
 * by the new modified string. Strategy learned from Stack OverFlow.
*/
void variableExpansion(char *string, char *variable, int integer) {

    // obtain length of the substring to be replaced
    int variable_len = strlen(variable);

    // convert number to string and obtain its length
    int integer_len = snprintf(NULL, 0, "%d", integer);
    char *intToString = malloc(integer_len + 1);
    snprintf(intToString, integer_len + 1, "%d", integer);

    // create buffer for the new modified string
    // create pointers to the buffer, to the original string, and to the occurrence of variable in the string
    char newString[2048];
    char *pNewString = &newString[0];
    char *pString = string;
    char *pVariable;

    // new modified string will be created by copying substrings and advancing pointers
    while (1) {

        // check if the variable to be replaced is in the string
        pVariable = strstr(pString, variable);
        // if no variable left to be replaced, copy the rest of original string to the buffer
        if (pVariable == NULL) {
            strcpy(pNewString, pString);
            break;
        }

        // Copy the substring before the variable into buffer and move buffer pointer as many places
        memcpy(pNewString, pString, pVariable - pString);
        pNewString += pVariable - pString;

        // Copy the number converted into string into buffer and move buffer pointer as many places
        memcpy(pNewString, intToString, integer_len);
        pNewString += integer_len;

        // Move the original string pointer to the character right after the variable
        pString = pVariable + variable_len;
    }

    // The modified string will replace the original string
    strcpy(string, newString);
    free(intToString);
}


/*
 * Parse the command inputted as parameter which is space delimited and create
 * command structure with information about the command, its arguments and
 * its files for input and output. Returns the structure.
*/
struct command *processCommand(char *command) {

    char *saveptr1;
    _Bool backgroundRun = 0;
    int i=0;

    struct command *currCommand = malloc(sizeof(struct command));

    // check if command is executed in the background, update backgroundRun boolean and remove '&'
    if (!strcmp(strrchr(command, '\0') - 1, "&")) {
        currCommand->backgroundRun = 1;
        command[strlen(command) - 1] = '\0';
    } else {
        currCommand->backgroundRun = 0;
    }

    // reset all arguments to NULL
    for (int index=0; index<512; index++) {
        (currCommand->arguments)[index] = NULL;
    }

    // The first token is the command, which will be the first element in the arguments
    char *token = strtok_r(command, " ", &saveptr1);
    (currCommand->arguments)[i] = strdup(token);

    token = strtok_r(NULL, " ", &saveptr1);
    for (i = 1; token != NULL; i++) {

        // obtain inputFile if "<" is found
        if (strcmp(token, "<") == 0) {
            token = strtok_r(NULL, " ", &saveptr1);
            currCommand->inputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(currCommand->inputFile, token);
        }

            // obtain outputfile if ">" is found
        else if (strcmp(token, ">") == 0) {
            token = strtok_r(NULL, " ", &saveptr1);
            currCommand->outputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(currCommand->outputFile, token);
        }

            // store the rest of the arguments
        else {
            (currCommand->arguments)[i] = strdup(token);
        }

        // move to the next string
        token = strtok_r(NULL, " ", &saveptr1);
    }

    // When it is a background command and the foregroundMode is False:
    // set /dev/null for input and output if they are not specified in the command
    if (currCommand->backgroundRun == 1 && foregroundMode == 0) {
        if (currCommand->inputFile == NULL) {
            currCommand->inputFile = calloc(11, sizeof(char));
            strcpy(currCommand->inputFile, "/dev/null");
        }
        if (currCommand->outputFile == NULL && foregroundMode == 0) {
            currCommand->outputFile = calloc(11, sizeof(char));
            strcpy(currCommand->outputFile, "/dev/null");
        }
    }
    return currCommand;
}


/*
 * Takes a command structure and the status of the last foreground process. Besides "exit", "cd"
 * and "status" which are built in commands, the other commands will be executed in a child process. The
 * function returns the status of the last foreground process (the 3 built in command are not considered
 * foreground processes).
 */
int runCommand(struct command *currCommand, int fStatus){

    struct sigaction ignore_action = {0}, default_action = {0};

    // built in exit command
    if (strcmp((currCommand->arguments)[0], "exit") == 0){
        // must kill any other processes or jobs that your shell has started before it terminates itself.
        exit(0);
    }

        // built in cd command, if no parameters provided, user is redirected to HOME directory
    else if (strcmp((currCommand->arguments)[0], "cd") == 0){
        if ((currCommand->arguments)[1] == NULL) {
            chdir(getenv("HOME"));
        } else {
            chdir((currCommand->arguments)[1]);
        }
    }
        // built in status command, returns prints out either the
        // exit status or the terminating signal of the last foreground process
    else if (strcmp((currCommand->arguments)[0], "status") == 0){
        if (WIFEXITED(fStatus)) {
            printf("exit value %d\n", WEXITSTATUS(fStatus));
        }
        else if (WIFSIGNALED(fStatus)) {
            printf("terminated by signal %d\n", WTERMSIG(fStatus));
        }
    }

        // non built in commands
    else{
        // Fork a new process
        pid_t spawnPid = fork();

        switch(spawnPid){
            case -1:
                perror("fork()\n");
                exit(1);
                break;
            case 0: // child process

                // all child processes (fore/background) must ignore SIGTSTP
                ignore_action.sa_handler = SIG_IGN;
                sigfillset(&ignore_action.sa_mask);
                ignore_action.sa_flags=SA_RESTART;
                sigaction(SIGTSTP, &ignore_action, NULL);

                // if it is a foreground child process or the foregroundMode is true, it will not ignore SIGINT
                if (currCommand->backgroundRun == 0 || foregroundMode== 1) {
                    default_action.sa_handler= SIG_DFL;
                    sigaction(SIGINT, &default_action, NULL);
                }

                // check if there is an inputfile
                if (currCommand->inputFile != NULL) {
                    // Open source file, return message if file cannot be opened
                    int sourceFD = open(currCommand->inputFile, O_RDONLY);
                    if (sourceFD == -1) {
                        printf("cannot open %s for input\n", (currCommand->inputFile));
                        fflush(stdout);
                        exit(1);
                    }

                    // Redirect stdin to source file
                    int result = dup2(sourceFD, 0);
                    if (result == -1) {
                        perror("source dup2()");
                    }
                    fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
                }

                // check if there is an outputfile
                if (currCommand->outputFile != NULL) {
                    int targetFD = open(currCommand->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    // Open target file, return message if file cannot be opened
                    if (targetFD == -1) {
                        printf("cannot open %s for output\n", (currCommand->outputFile));
                        fflush(stdout);
                        exit(1);
                    }

                    // Redirect stdout to target file
                    int result = dup2(targetFD, 1);
                    if (result == -1) {
                        perror("target dup2");
                    }
                    fcntl(targetFD, F_SETFD, FD_CLOEXEC);
                }

                // execute the command
                if (execvp((currCommand->arguments)[0], currCommand->arguments)) {
                    printf("%s: command not found\n", (currCommand->arguments)[0]);
                    fflush(stdout);
                    exit(1);
                }
                break;
            default:
                // In the parent process, wait for child's termination if foreground or if foregroundMode is True
                if (currCommand->backgroundRun==0 || foregroundMode==1){
                    spawnPid = waitpid(spawnPid, &fStatus, 0);
                    // print out message if foreground process terminated by signal
                    if (WIFSIGNALED(fStatus)) {
                        printf(" terminated by signal %d\n", WTERMSIG(fStatus));
                        fflush(stdout);
                    }
                }

                // if it is a background process, print the process pid
                if (currCommand->backgroundRun==1 && foregroundMode==0){
                    printf("background pid is %d\n", spawnPid);
                    fflush(stdout);
                }
                break;
        }
    }
    return fStatus;

}
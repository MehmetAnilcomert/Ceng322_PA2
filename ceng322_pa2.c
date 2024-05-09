#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#define MAX_COMMAND_LENGTH 100  // Maximum length of a command
#define MAX_ARGS 11             // Maximum number of arguments for a command
#define HISTORY_SIZE 10         // Size of the command history

// Array to store command history
char history[HISTORY_SIZE][MAX_COMMAND_LENGTH];
int history_count = 0;          // Counter for the number of commands in history

// Function for adding a command to the history array
// it is working with FIFO principle to obtain last 10 command in order
void add_to_history(const char *command) {
    if (history_count == HISTORY_SIZE) {
        // Shifting history to make space for new command
        for (int i = 0; i < HISTORY_SIZE - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strncpy(history[HISTORY_SIZE - 1], command, MAX_COMMAND_LENGTH);    // For copying command into the last element
        history[HISTORY_SIZE - 1][MAX_COMMAND_LENGTH - 1] = '\0';           // Ensuring that the last character of the new command is the null terminator
    } else {
        strncpy(history[history_count], command, MAX_COMMAND_LENGTH);
        history[history_count][MAX_COMMAND_LENGTH - 1] = '\0';
        history_count++;
    }
}


// Function for changing the current working directory
void change_directory(char **args) {
    char *path;

    if (args[1] == NULL) {  // If there is no argument to change dir, new directory is default dir. 
        char *home_directory = getenv("HOME");
        if (home_directory == NULL) {
            fprintf(stderr, "HOME environment variable not set\n"); 
            return;
        }
        path = home_directory;
    } else {
        if (args[1][0] == '/') {
            // Absolute path is directly assigned.
            path = args[1];
        } else {
            // Relative path
            char current_directory[MAX_COMMAND_LENGTH];     
            if (getcwd(current_directory, sizeof(current_directory)) == NULL) {     // retrieving current working directory
                perror("getcwd");
                return;
            }
            path = malloc(strlen(current_directory) + strlen(args[1]) + 2);    // For allocating memory to relative path
            if (path == NULL) {
                perror("malloc");
                return;
            }
            strcpy(path, current_directory);
            strcat(path, "/");
            strcat(path, args[1]);
        }
    }
    if (chdir(path) != 0) {  // It returns a non-zero value, this means an error is indicated
        perror("chdir");    
    } else {
        setenv("PWD", path, 1);  // For setting the environment variable PWD to the value of path
    }
    if (args[1] != NULL && args[1][0] != '/') {     // It frees dynamically allocated memory for relative path.
        free(path);
    }
}

// Function to execute built-in commands (cd, pwd, history, exit)
void execute_builtin_command(char **args) {
    if (strcmp(args[0], "cd") == 0) {       // If the given command is cd
        change_directory(args);
        char full_command[MAX_COMMAND_LENGTH] = {0};
        strcpy(full_command, "cd");
        if (args[1] != NULL) {
            strcat(full_command, " ");                                                     // For appending
            strncat(full_command, args[1], MAX_COMMAND_LENGTH - strlen(full_command) - 1); // For appending 
        }
    } else if (strcmp(args[0], "pwd") == 0) { // If the given command is pwd
        char cwd[MAX_COMMAND_LENGTH];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        }
    } else if (strcmp(args[0], "history") == 0) { // If the given command is history
        int count = history_count > HISTORY_SIZE ? HISTORY_SIZE : history_count;
        for (int i = 0; i < count; i++) {
            printf("%d: %s\n", i + 1, history[i]);
        }
    } else if (strcmp(args[0], "exit") == 0) {     // If the given command is exit
        printf("Exiting...\n"); // Last message in order to indicate exiting process through the user.
        exit(0);
    }
}

// Function to execute a command sequence with optional background execution (non built-in commands)
// it also handles commands includes &&, and waits until first argument to finish correctly and then executes second argument
// Sample command: gcc main.c && ./a.out 
int run_sequence_command(char **args, int background) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1; // error
    } else if (pid == 0) {
        if (execvp(args[0], args) == -1) {
            fprintf(stderr, "Error: Command not found\n"); // If there is a typo in command.
            exit(EXIT_FAILURE);
        }
    } else {
        if (!background) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
        } else {
            printf("Background process with PID: %d\n", pid);
        }
    }
    return 0; // success or background mode
}

// Function to parse a command and execute it
void process_command_line(char *command) {
    char *args[MAX_ARGS];
    char *left_args[MAX_ARGS];
    char *right_args[MAX_ARGS];
    char *second_command[MAX_ARGS];
    char *token;
    int i = 0, j = 0, in_pipe = 0, background = 0, has_second_command = 0;

    add_to_history(command);  // Adding the full command line to history immediately

    // Initial tokenization to handle spaces and basic command splitting
    token = strtok(command, " \t\n");
    while (token != NULL) {
        if (strcmp(token, "&") == 0 && !in_pipe && !has_second_command) {
            background = 1;
            break;
        } else if (strcmp(token, "|") == 0) {
            in_pipe = 1;
            left_args[j] = NULL;
            j = 0;
        } else if (strcmp(token, "&&") == 0) {
            left_args[j] = NULL;
            i = 0;
            has_second_command = 1;
        } else {
            if (in_pipe) {
                right_args[j++] = token;
            } else if (has_second_command) {
                second_command[i++] = token;
            } else {
                left_args[j++] = token;
            }
        }
        token = strtok(NULL, " \t\n");
    }
    left_args[j] = NULL;
    right_args[j] = NULL;
    second_command[i] = NULL;

    // Checking for built-in commands before any execution
    if (left_args[0] && (strcmp(left_args[0], "cd") == 0 || strcmp(left_args[0], "pwd") == 0 ||
        strcmp(left_args[0], "history") == 0 || strcmp(left_args[0], "exit") == 0)) {
        execute_builtin_command(left_args);
        return;
    }
    
    if (in_pipe) {
        // Handling command that has pipe operator
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            perror("pipe");
            return;
        }
        pid_t pid1 = fork();
        if (pid1 == 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            execvp(left_args[0], left_args);
            perror("execvp");
            exit(EXIT_FAILURE);
        }
        pid_t pid2 = fork();
        if (pid2 == 0) {
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            execvp(right_args[0], right_args);
            perror("execvp");
            exit(EXIT_FAILURE);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        if (!background) {
            waitpid(pid1, NULL, 0);
            waitpid(pid2, NULL, 0);
        } else {
            printf("Background processes started with PID: %d and %d\n", pid1, pid2);
        }
    } else if (has_second_command) {
        // Handling sequential execution with &&
        int exit_status = run_sequence_command(left_args, background);
        if (exit_status == 0) {
            run_sequence_command(second_command, background);
        }
    } else {
        // Normal command execution
        run_sequence_command(left_args, background);
    }
}

int main() {
    char command[MAX_COMMAND_LENGTH];

    while (1) {
        printf("myshell> ");
        // To force the output buffer to be flushed.
        fflush(stdout);

        // To read a line of input from the standard input stream.
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        // Removing newline character from the command.
        command[strcspn(command, "\n")] = '\0';

        // In order to parse and execute the command
        process_command_line(command);
    }

    return 0;
}
#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT 1024
#define MAX_PATHS 64
#define MAX_COMMANDS 64

char error_message[30] = "An error has occurred\n";

int execute_command(char *command, char *paths[], FILE *output_file);

void update_paths(char *paths[], char *op, char *arg);

void sig_stop_handler(int sig) {
    exit(0);
}

int main(int argc, char *argv[]) {
    // TODO: Test on other shells than just ZSH
    signal(SIGSTOP, sig_stop_handler);

    // "Your initial shell path should contain one directory: /bin"
    char *paths[MAX_PATHS] = {};
    paths[0] = strdup("/bin");

    FILE *batch_file = NULL;
    char input[MAX_INPUT];

    // Check if we were provided multiple batch files, which isn't allowed
    if (argc > 2) {
        write(STDERR_FILENO, error_message, strlen(error_message));
        exit(1);
    }

    // Check if we are running in batch mode
    if (argc == 2) {
        batch_file = fopen(argv[1], "r");
        if (batch_file == NULL) {
            // Not a valid file
            write(STDERR_FILENO, error_message, strlen(error_message));
            exit(1);
        }
    }

    // Keep looping until
    int isRunning = 1;
    while (isRunning) {
        if (batch_file) {
            // we get our input from a file
            if (fgets(input, MAX_INPUT, batch_file) == NULL) {
                return 0;  // End of batch file
            }
        } else {
            // Only print the prompt when not executing a batch file
            printf("smash> ");
            // Flush because we don't use a newline at the end, which stops some automatic behavior to autoflush
            fflush(stdout);

            if (fgets(input, MAX_INPUT, stdin) == NULL) {
                break;
            }
        }

        // Remove newline character from input
        input[strcspn(input, "\n")] = '\0';

        // Parse multiple commands separated by ';' or '&'
        char *separator = ";&";

        char *commands[MAX_COMMANDS];
        char command_separator[MAX_COMMANDS];
        int command_pid[MAX_COMMANDS] = {0}; // zero this array

        int input_length = strlen(input);
        int command_start = 0; // index of the start of the current command
        int command_count = 0;

        for (int i = 0; i < input_length; i++) {
            if (strchr(separator, input[i]) != NULL) {
                // found a command separator
                int command_length = i - command_start;
                if (command_length > 0) {
                    // add the command to the list
                    commands[command_count] = &input[command_start];
                    command_separator[command_count] = input[i];
                    // Add null terminator
                    input[i] = '\0';
                    command_count++;
                    if (command_count >= MAX_COMMANDS - 1) {
                        // reached the maximum number of commands
                        break;
                    }
                }
                command_start = i + 1;
            }
        }

        // add the last command to the list
        int command_length = input_length - command_start;
        if (command_length > 0) {
            commands[command_count] = &input[command_start];
            // last command is terminated by ';' to make things easier
            command_separator[command_count] = ';';
            command_count++;
        }

        // add a NULL command at the end of the list
        commands[command_count] = NULL;

        int j = 0;
        while (commands[j] != NULL) {
            char *output_str = NULL;

            // Check for output redirection
            char *output_redirect = strchr(commands[j], '>');
            if (output_redirect != NULL) {
                // Check for multiple output redirection commands
                char *second_output_redirect = strchr(output_redirect + 1, '>');
                if (second_output_redirect != NULL) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    break;
                }
                output_str = strtok(output_redirect + 1, " ");
                // Check for multiple output files
                char *second_output_str = strtok(NULL, " ");
                if (second_output_str != NULL) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    break;
                }
                commands[j][output_redirect - commands[j]] = '\0';
            }


            // Open the file for redirection while we can still exit early
            FILE *output_file = NULL;
            if (output_str != NULL) {
                output_file = fopen(output_str, "w");
                if (output_file == NULL) {
                    // Stop parsing this line
                    break;
                }
            }

            // Remove leading and trailing spaces and tabs
            char *command = commands[j];
            while (*command == ' ' || *command == '\t') {
                command++;
            }
            char *end = command + strlen(command) - 1;
            while ((*end == ' ' || *end == '\t') && end > command) {
                end--;
            }
            // Make the string end where there is whitespace
            *(end + 1) = '\0';

            if (strlen(command) == 0) {
                j++;
                continue;
            } else if (strcmp(command, "exit") == 0) {
                // If only all commands were this simple
                isRunning = 0;
                break;
            } else if (strncmp(command, "cd ", 3) == 0) {
                // cd command
                if (chdir(command + 3) == -1) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                }
            } else if (strncmp(command, "path", 4) == 0) {
                // path command
                char *op = strtok(input + 4, " ");
                char *arg = strtok(NULL, " ");
                if (strtok(NULL, " ") != NULL) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    break;
                }
                update_paths(paths, op, arg);
            } else {
                // Execute command
                int pid = execute_command(command, paths, output_file);

                char curCommandSeparator = command_separator[j];
                if (pid > 0) {
                    // parent
                    int status;
                    // & is a special case where we should execute in parallel
                    if (curCommandSeparator == '&') {
                        command_pid[j] = pid;
                    } else {
                        waitpid(pid, &status, 0);
                    }
                } else {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                }
            }

            j++;
        }

        int k = 0;
        while (k < j) {
            int status;

            int pid = command_pid[k];
            if (pid != 0) {
                waitpid(pid, &status, 0);
            }

            k++;
        }
    }

    return 0;
}

int execute_command(char *command, char *paths[], FILE *output_file) {
    char *args[MAX_INPUT / 2 + 1];
    char *arg = strtok(command, " ");
    int i = 0;

    while (arg != NULL) {
        args[i++] = arg;
        arg = strtok(NULL, " ");
    }
    args[i] = NULL;

    pid_t pid = fork();
    if (pid == 0) {
        int found = 0;
        for (int i = 0; paths[i] != NULL; i++) {
            char path[MAX_INPUT];
            snprintf(path, MAX_INPUT, "%s/%s", paths[i], args[0]);
            if (access(path, X_OK) == 0) {
                if (output_file != NULL && dup2(fileno(output_file), STDOUT_FILENO) == -1) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    exit(1);
                }
                if (output_file != NULL && dup2(fileno(output_file), STDERR_FILENO) == -1) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    exit(1);
                }
                execv(path, args);
                found = 1;
                break;
            }
        }
        if (!found) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            exit(1);
        }
    }
    // parent process
    return pid;
}

// Complicated enough to justify it's own function
void update_paths(char *paths[], char *op, char *arg) {
    if (strcmp(op, "add") == 0) {
        if (arg == NULL) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return;
        }

        for (int i = 0; i < MAX_PATHS; i++) {
            if (paths[i] == NULL) {
                paths[i] = strdup(arg);;
                break;
            }
        }
    } else if (strcmp(op, "remove") == 0) {
        if (arg == NULL) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return;
        }

        int found = 0;
        for (int i = 0; i < MAX_PATHS; i++) {
            if (paths[i] != NULL && strcmp(paths[i], arg) == 0) {
                free(paths[i]); // stop memory leak
                found = 1;
            }
            if (found && i != MAX_PATHS - 1) {
                paths[i] = paths[i + 1];
            }
        }
        if (!found) {
            write(STDERR_FILENO, error_message, strlen(error_message));
        }
    } else if (strcmp(op, "clear") == 0) {
        if (arg != NULL) {
            write(STDERR_FILENO, error_message, strlen(error_message));
            return;
        }
        for (int i = 0; i < MAX_PATHS; i++) {
            paths[i] = NULL;
        }
    } else {
        write(STDERR_FILENO, error_message, strlen(error_message));
    }
}
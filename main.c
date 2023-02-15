#include <stdio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_INPUT 1024
#define MAX_PATHS 64
#define MAX_COMMANDS 64

char error_message[30] = "An error has occurred\n";

void execute_command(char *command, char *paths[], FILE *output_file);

void update_paths(char *paths[], char *op, char *arg);

int main(int argc, char *argv[]) {
    char *paths[MAX_PATHS] = {"/bin", "/usr/bin"}; // Default paths
    FILE *batch_file = NULL;
    char input[MAX_INPUT];
    char *commands[MAX_COMMANDS];

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

    // Flag to stop running, due to the need to break out of inner loops
    int isRunning = 1;
    // infinite loop to keep asking the user questions
    while (isRunning) {
        if (batch_file) {
            // we get our input from a file
            if (fgets(input, MAX_INPUT, batch_file) == NULL) {
                return 0;  // End of batch file
            }
            printf("%s", input);
        } else {
            // Only print the prompt when not executing a batch file
            printf("smash> ");
            // Flush because we don't use a newline at the end, which stops some automatic behavior
            fflush(stdout);

            if (fgets(input, MAX_INPUT, stdin) == NULL) {
                break;  // End of input
            }
        }

        // Remove newline character from input
        input[strcspn(input, "\n")] = '\0';

        // Parse multiple commands separated by ';' or '&'
        char *separator = ";&";

        char *command = strtok(input, separator);
        int i = 0;
        while (command != NULL) {
            commands[i++] = command;
            command = strtok(NULL, separator);
        }
        commands[i] = NULL;

        int j = 0;
        while (commands[j] != NULL) {
            char *output_str = NULL;

            // Check for output redirection
            char *output_redirect = strchr(commands[j], '>');
            if (output_redirect != NULL) {
                output_str = strtok(output_redirect + 1, " ");
                commands[j][output_redirect - commands[j]] = '\0';
            }

            // Open the file for redirection while we can still exit early
            FILE *output_file = NULL;
            if (output_str != NULL) {
                output_file = fopen(output_str, "w");
                if (output_file == NULL) {
                    perror(output_redirect);
                    exit(EXIT_FAILURE);
                }
            }

            // Remove leading/trailing whitespace from command
            char *command = commands[j];
            while (*command == ' ') {
                command++;
            }
            char *end = command + strlen(command) - 1;
            while (*end == ' ' && end > command) {
                end--;
            }
            // Make the string end where there is whitespace
            *(end + 1) = '\0';

            if (strcmp(command, "exit") == 0) {
                // If only all commands were this simple
                isRunning = 0;
                break;
            } else if (strncmp(command, "cd ", 3) == 0) {
                // cd command
                if (chdir(input + 3) == -1) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                }
            } else if (strncmp(command, "path", 4) == 0) {
                // path command
                char *op = strtok(input + 4, " ");
                char *arg = strtok(NULL, " ");
                if (strtok(NULL, " ") != NULL) {
                    write(STDERR_FILENO, error_message, strlen(error_message));
                    isRunning = 0;
                    break;
                }
                update_paths(paths, op, arg);
            } else {
                // Execute command
                execute_command(command, paths, output_file);
            }

            j++;
        }
    }

    if (batch_file) {
        fclose(batch_file);
    }

    return 0;
}

void execute_command(char *command, char *paths[], FILE *output_file) {
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
                    perror("dup2");
                    exit(EXIT_FAILURE);
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
    } else if (pid > 0) {
        // parent
        int status;
        waitpid(pid, &status, 0);
    } else {
        write(STDERR_FILENO, error_message, strlen(error_message));
    }
}

// Complicated enough to justify it's own function
void update_paths(char *paths[], char *op, char *arg) {
    if (strcmp(op, "add") == 0) {
        if (arg == NULL) {
            fprintf(stderr, "Error: Missing argument for 'add' command\n");
            return;
        }

        for (int i = 0; i < MAX_PATHS; i++) {
            if (paths[i] == NULL) {
                paths[i] = arg;
                break;
            }
        }
    } else if (strcmp(op, "remove") == 0) {
        if (arg == NULL) {
            fprintf(stderr, "Error: Missing argument for 'add' command\n");
            return;
        }

        int found = 0;
        for (int i = 0; i < MAX_PATHS; i++) {
            if (paths[i] != NULL && strcmp(paths[i], arg) == 0) {
                found = 1;
            }
            if (found && i != MAX_PATHS - 1) {
                paths[i] = paths[i + 1];
            }
        }
        if (!found) {
            fprintf(stderr, "Error: Path '%s' not found\n", arg);
        }
    } else if (strcmp(op, "clear") == 0) {
        if (arg != NULL) {
            fprintf(stderr, "Error: 'clear' command does not take arguments\n");
            return;
        }
        for (int i = 0; i < MAX_PATHS; i++) {
            paths[i] = NULL;
        }
    } else {
        fprintf(stderr, "Error: Invalid path command '%s'\n", op);
    }
}
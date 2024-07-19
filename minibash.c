#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>

// Constants definitions
#define MAX_ARGS 4
#define MAX_CMD_LEN 1024
#define MAX_BG_PROCESSES 100

// Stack to manage background processes
pid_t bg_process_stack[MAX_BG_PROCESSES];
int bg_process_top = -1;

// Helper function to trim leading and trailing spaces
char *trim_whitespace(char *str)
{
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str))
        str++;
    if (*str == 0)
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';

    return str;
}

// Function to print error message in red
void error_message(char *msg)
{
    printf("\033[1;31m%s\033[0m\n", msg); // Red colored message
}

// Push a process to the background process stack
void push_bg_process(pid_t pid)
{
    if (bg_process_top < MAX_BG_PROCESSES - 1) // Checking for stack overflow
    {
        bg_process_stack[++bg_process_top] = pid; // Add to stack
    }
    else
    {
        error_message("Background process stack overflow.");
    }
}

// Pop a process from the background process stack
pid_t pop_bg_process()
{
    if (bg_process_top >= 0) // Checking for stack underflow
    {
        return bg_process_stack[bg_process_top--]; // Remove from stack
    }
    else
    {
        error_message("No background process found.");
        return -1;
    }
}

// Signal handler for Ctrl+C
void handle_sigint(int sig)
{
    // Print message to indicate how to properly exit minibash
    printf("\nminibash$ ");
    fflush(stdout); // Ensure the prompt is printed immediately
}

// Function to execute normal commands
int execute_command(char *cmd)
{
    char *args[MAX_ARGS]; // Argument list for execvp
    int argc = 0;
    char *token = strtok(cmd, " ");

    // Parse command arguments
    while (token != NULL)
    {
        args[argc++] = token;      // Add to args array
        token = strtok(NULL, " "); // Continue the tokenization from next word
    }
    args[argc] = NULL;

    if (argc < 1 || argc > MAX_ARGS)
    {
        error_message("Invalid number of arguments. Maximum is 4.");
        return -1;
    }

    // Create a child process
    pid_t pid = fork();
    if (pid == 0) // Child process
    {
        execvp(args[0], args);                                // Execute the command
        perror("\033[1;31mexecvp in execute_command\033[0m"); // If execvp fails, print an error in red
        exit(EXIT_FAILURE);                                   // Exit child process if execvp fails
    }
    else if (pid > 0) // Parent process
    {
        int status;
        waitpid(pid, &status, 0); // Wait for the child process to finish

        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status); // Return the exit status of the child process
        }
        else
        {
            return -1; // Return -1 if the child process did not terminate normally
        }
    }
    else
    {
        perror("\033[1;31mfork in execute_command\033[0m"); // If fork fails, print an error in red
        return -1;
    }
}

// Function to execute pipe commands
void execute_pipe(char *cmd)
{
    char *commands[5]; // Command list for piped commands
    int num_commands = 0;
    char *token = strtok(cmd, "|");

    // Parse piped commands
    while (token != NULL && num_commands < 5)
    {
        commands[num_commands++] = token; // Extract the first command and store in array
        token = strtok(NULL, "|");        // Continue from next work
    }

    if (num_commands > 4)
    {
        error_message("Error: Too many commands for piping. Maximum is 4.");
        return;
    }

    int in_fd = 0, pipe_fds[2]; // File descriptors for pipes
    for (int i = 0; i < num_commands; i++)
    {
        // Create a pipe
        if (pipe(pipe_fds) == -1)
        {
            perror("\033[1;31mpipe in execute_pipe\033[0m"); // Prints error in red
            return;
        }
        pid_t pid = fork(); // Create a child process
        if (pid == 0)       // Child process
        {
            // Redirect input from the previous pipe (or stdin for the first command)
            if (dup2(in_fd, 0) == -1)
            {
                perror("\033[1;31mdup2 in execute_pipe (input redirection)\033[0m");
                exit(EXIT_FAILURE);
            }
            // Redirect output to the pipe (except for the last command)
            if (i < num_commands - 1 && dup2(pipe_fds[1], 1) == -1)
            {
                perror("\033[1;31mdup2 in execute_pipe (output redirection)\033[0m");
                exit(EXIT_FAILURE);
            }
            close(pipe_fds[0]);           // Close the read end of the pipe
            execute_command(commands[i]); // Execute the command
            exit(EXIT_FAILURE);           // Exit child process if execution fails
        }
        else if (pid > 0) // Parent process
        {
            wait(NULL);          // Wait for the child process to finish
            close(pipe_fds[1]);  // Close the write end of the pipe
            in_fd = pipe_fds[0]; // Save the read end for the next command
        }
        else
        {
            perror("\033[1;31mfork in execute_pipe\033[0m"); // If fork fails, print an error
            return;
        }
    }
}

// Function to handle input and output redirection
void execute_redirection(char *cmd, int type)
{
    char *args[MAX_ARGS]; // Argument list for execvp
    char *filename = NULL;
    int argc = 0;
    char *token = strtok(cmd, " "); // Tokenize the input command

    // Parse command arguments
    while (token != NULL && token[0] != '<' && token[0] != '>')
    {
        args[argc++] = token;
        token = strtok(NULL, " ");
    }
    args[argc] = NULL;

    // Get the filename for redirection
    if (argc < 1 || argc > MAX_ARGS || token == NULL || (filename = strtok(NULL, " ")) == NULL)
    {
        error_message("Invalid arguments or no file specified for redirection.");
        return;
    }

    // Open the file for redirection
    int fd;
    if (type == 0)
    { // Input redirection
        fd = open(filename, O_RDONLY);
    }
    else if (type == 1)
    { // Output redirection (overwrite)
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    else
    { // Output redirection (append)
        fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
    }

    if (fd < 0)
    {
        perror("\033[1;31mopen in execute_redirection\033[0m"); // If open fails, print an error in red
        return;
    }

    // Create a child process
    pid_t pid = fork();
    if (pid == 0) // Child process
    {
        // Redirect input or output to the file
        if ((type == 0 && dup2(fd, 0) == -1) || (type != 0 && dup2(fd, 1) == -1))
        {
            perror("\033[1;31mdup2 in execute_redirection\033[0m"); // print error_message in red
            exit(EXIT_FAILURE);
        }
        close(fd);                                                // Close the file descriptor
        execvp(args[0], args);                                    // Execute the command
        perror("\033[1;31mexecvp in execute_redirection\033[0m"); // If execvp fails, print an error
        exit(EXIT_FAILURE);                                       // Exit child process if execvp fails
    }
    else if (pid > 0) // Parent process
    {
        close(fd);  // Close the file descriptor in the parent process
        wait(NULL); // Wait for the child process to finish
    }
    else
    {
        perror("\033[1;31mfork in execute_redirection\033[0m"); // If fork fails, print an error
    }
}

// Function to execute commands in the background
void execute_background(char *cmd)
{
    char *args[MAX_ARGS]; // Argument list for execvp
    int argc = 0;
    char *token = strtok(cmd, " "); // Tokenize the input command

    // Parse command arguments
    while (token != NULL && strcmp(token, "+") != 0)
    {
        args[argc++] = token;
        token = strtok(NULL, " ");
    }
    args[argc] = NULL;

    if (argc < 1 || argc > MAX_ARGS)
    {
        error_message("Invalid number of arguments. Maximum is 4.");
        return;
    }

    // Create a child process
    pid_t pid = fork();
    if (pid == 0) // Child process
    {
        setsid();                                                // Create a new session
        execvp(args[0], args);                                   // Execute the command
        perror("\033[1;31mexecvp in execute_background\033[0m"); // If execvp fails, print an error
        exit(EXIT_FAILURE);                                      // Exit child process if execvp fails
    }
    else if (pid > 0) // Parent process
    {
        push_bg_process(pid); // Add the background process to the stack
        printf("Process running in background with PID %d\n", pid);
    }
    else
    {
        perror("\033[1;31mfork in execute_background\033[0m"); // If fork fails, print an error
    }
}

// Function to bring the last background process to the foreground
void execute_foreground()
{
    pid_t pid = pop_bg_process(); // Get the last background process
    if (pid > 0)
    {
        waitpid(pid, NULL, 0); // Wait for the process to finish
        printf("Process %d brought to foreground.\n", pid);
    }
}

// Function to execute commands sequentially
void execute_sequential(char *cmd)
{
    char *commands[5]; // Command list for sequential commands
    int num_commands = 0;
    char *token = strtok(cmd, ";"); // Tokenize the input command by semicolon

    // Parse sequential commands
    while (token != NULL && num_commands < 4)
    {
        commands[num_commands++] = token;
        token = strtok(NULL, ";");
    }

    if (num_commands > 4)
    {
        error_message("Error: Too many commands for sequential execution. Maximum is 4.");
        return;
    }

    for (int i = 0; i < num_commands; i++)
    {
        execute_command(commands[i]); // Execute each command sequentially
    }
}

// Function to handle conditional execution with && and ||
void execute_conditional(char *cmd)
{
    char *commands[5]; // Command list for conditional commands
    int types[4];      // Type of each separator: 1 for &&, 2 for ||
    int num_commands = 0;
    char *token = strtok(cmd, "&|");

    // Parse conditional commands and separators
    while (token != NULL && num_commands < 5)
    {
        commands[num_commands++] = trim_whitespace(token);
        token = strtok(NULL, "&|");

        if (token != NULL)
        {
            if (*(token - 1) == '&')
            {
                types[num_commands - 1] = 1; // AND
            }
            else
            {
                types[num_commands - 1] = 2; // OR
            }
        }
    }

    if (num_commands > 5)
    {
        error_message("Error: Too many commands for conditional execution. Maximum is 5.");
        return;
    }

    int last_status = 0;
    int skip_next = 0;
    for (int i = 0; i < num_commands; i++)
    {
        if (skip_next)
        {
            skip_next = 0;
            continue;
        }

        int pipefd[2];
        if (pipe(pipefd) == -1)
        {
            perror("\033[1;31mpipe in execute_conditional\033[0m");
            return;
        }

        pid_t pid = fork();
        if (pid == 0) // Child process
        {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO); // Also redirect stderr
            close(pipefd[1]);

            exit(execute_command(commands[i]));
        }
        else if (pid > 0) // Parent process
        {
            close(pipefd[1]);

            int status;
            waitpid(pid, &status, 0);

            if (WIFEXITED(status))
            {
                last_status = WEXITSTATUS(status);
            }
            else
            {
                last_status = -1;
            }

            // Print output and error
            char buffer[1024];
            ssize_t bytes_read;
            while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
            {
                buffer[bytes_read] = '\0';
                printf("%s", buffer);
            }
            close(pipefd[0]);

            // Check if we should continue based on the result and the next separator
            if (i < num_commands - 1)
            {
                if (types[i] == 1 && last_status != 0)
                {
                    break; // Stop if AND fails
                }
                else if (types[i] == 2 && last_status == 0)
                {
                    skip_next = 1; // Skip next command if OR succeeds
                }
            }
        }
        else
        {
            perror("\033[1;31mfork in execute_conditional\033[0m");
            return;
        }
    }
}

// Function to count words in a file
void count_words(char *filename)
{
    while (isspace((unsigned char)*filename))
        filename++; // Trim leading spaces

    if (access(filename, F_OK) != 0)
    {
        error_message("File does not exist or is not accessible.");
        return;
    }

    FILE *file = fopen(filename, "r"); // Open the file for reading
    if (!file)
    {
        perror("\033[1;31mfopen in count_words\033[0m");
        return;
    }

    int words = 0;
    char c;
    int in_word = 0;

    // Count words in the file
    while ((c = fgetc(file)) != EOF)
    {
        if (isspace((unsigned char)c))
        {
            in_word = 0; // Sets flag for if the loop is in a word or not
        }
        else if (!in_word) // If in word then add the word count
        {
            in_word = 1;
            words++;
        }
    }

    fclose(file); // Close the file
    printf("Word count: %d\n", words);
}

// Function to concatenate files
void concatenate_files(char *cmd)
{
    char *files[5]; // List of files to concatenate
    int num_files = 0;
    char *token = strtok(cmd, "~"); // Tokenize the input command by ~

    // Parse file names
    while (token != NULL && num_files < 5)
    {
        files[num_files++] = token;
        token = strtok(NULL, "~");
    }

    if (num_files > 4)
    {
        error_message("Error: Too many files for concatenation. Maximum is 4.");
        return;
    }

    for (int i = 0; i < num_files; i++)
    {
        files[i] = trim_whitespace(files[i]); // Trim leading and trailing spaces

        // Check if the file has a .txt extension
        if (strstr(files[i], ".txt") == NULL)
        {
            error_message("Error: File is not a .txt file.");
            return;
        }

        // Check if the file exists and is accessible
        if (access(files[i], F_OK) != 0)
        {
            error_message("Error: File does not exist or is not accessible.");
            return;
        }

        // Open the file for reading
        FILE *file = fopen(files[i], "r");
        if (!file)
        {
            perror("\033[1;31mfopen in concatenate_files\033[0m");
            return;
        }

        // Close the file since we need to print nothing
        fclose(file);
    }

    // If all files are valid, concatenate and print their contents
    for (int i = 0; i < num_files; i++)
    {
        FILE *file = fopen(files[i], "r"); // Open the file for reading
        if (!file)
        {
            perror("\033[1;31mfopen in concatenate_files\033[0m");
            return;
        }

        // Print the contents of the file
        char c;
        while ((c = fgetc(file)) != EOF)
        {
            putchar(c);
        }

        fclose(file); // Close the file
    }
}

// Function to handle exit
void handle_exit()
{
    printf("Exiting minibash...\n");
    exit(0);
}

// Function to print help information
void print_help()
{
    printf("Usage of minibash:\n");
    printf("1. Normal Commands: Command with up to 4 arguments.\n");
    printf("2. Special Commands: dter - Exit minibash. help - Print this help information.\n");
    printf("3. Background Processes: Command ending with + to run in background. fore - Bring last background process to foreground.\n");
    printf("4. Input/Output Redirection: < for input redirection. > for output redirection (overwrite). >> for output redirection (append).\n");
    printf("5. Piping: Use | to pipe up to 4 commands.\n");
    printf("6. Sequential Execution: Use ; to separate up to 4 commands.\n");
    printf("7. Conditional Execution: Use && for AND and || for OR with up to 4 commands.\n");
    printf("8. Word Count in File: Use # followed by filename.\n");
    printf("9. Concatenate Files: Use ~ to concatenate up to 4 files.\n");
}

int main()
{
    // Set up signal handler for Ctrl+C
    signal(SIGINT, handle_sigint);

    char cmd[MAX_CMD_LEN]; // Command buffer

    // Main loop to process user commands
    while (1)
    {
        printf("minibash$ "); // Print minibash prompt
        if (fgets(cmd, MAX_CMD_LEN, stdin) == NULL)
        {
            perror("\033[1;31mfgets in main\033[0m");
            continue;
        }

        // Remove newline character from the input
        cmd[strcspn(cmd, "\n")] = 0;

        // If the input is empty, continue to the next iteration
        if (cmd[0] == '\0')
            continue;

        // Handle help command
        if (strcmp(cmd, "help") == 0)
        {
            print_help();
            continue;
        }

        // Handle dter command to exit
        if (strcmp(cmd, "dter") == 0)
        {
            handle_exit();
        }
        if (strstr(cmd, "&&") || strstr(cmd, "||"))
        {
            execute_conditional(cmd);
        }
        // Execute command based on special characters
        else if (strchr(cmd, '|'))
        {
            execute_pipe(cmd);
        }
        else if (strchr(cmd, '>'))
        {
            if (strstr(cmd, ">>"))
            {
                execute_redirection(cmd, 2);
            }
            else
            {
                execute_redirection(cmd, 1);
            }
        }
        else if (strchr(cmd, '<'))
        {
            execute_redirection(cmd, 0);
        }
        else if (strchr(cmd, '+'))
        {
            execute_background(cmd);
        }
        else if (strcmp(cmd, "fore") == 0)
        {
            execute_foreground();
        }
        else if (strchr(cmd, ';'))
        {
            execute_sequential(cmd);
        }#
        else if (cmd[0] == '#')
        {
            count_words(cmd + 1);
        }
        else if (strchr(cmd, '~'))
        {
            concatenate_files(cmd);
        }
        else
        {
            execute_command(cmd);
        }
    }

    return 0;
}

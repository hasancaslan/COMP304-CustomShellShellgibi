#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <ifaddrs.h>
#include <readline/readline.h>
#include <readline/history.h>

char **all_commands;
const char *sysname = "shellgibi";

enum return_codes {
    SUCCESS = 0,
    EXIT = 1,
    UNKNOWN = 2,
};
struct command_t {
    char *name;
    bool background;
    bool auto_complete;
    int arg_count;
    char **args;
    char *redirects[3];        // in/out redirection
    struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
    int i = 0;
    printf("Command: <%s>\n", command->name);
    printf("\tIs Background: %s\n", command->background ? "yes" : "no");
    printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
    printf("\tRedirects:\n");
    for (i = 0; i < 3; i++)
        printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
    printf("\tArguments (%d):\n", command->arg_count);
    for (i = 0; i < command->arg_count; ++i)
        printf("\t\tArg %d: %s\n", i, command->args[i]);
    if (command->next) {
        printf("\tPiped to:\n");
        print_command(command->next);
    }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
    if (command->arg_count) {
        for (int i = 0; i < command->arg_count; ++i)
            free(command->args[i]);
        free(command->args);
    }
    for (int i = 0; i < 3; ++i)
        if (command->redirects[i])
            free(command->redirects[i]);
    if (command->next) {
        free_command(command->next);
        command->next = NULL;
    }
    free(command->name);
    free(command);
    return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
    char cwd[1024], hostname[1024];
    gethostname(hostname, sizeof(hostname));
    getcwd(cwd, sizeof(cwd));
    printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
    return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
    const char *splitters = " \t"; // split at whitespace
    int index, len;
    len = strlen(buf);
    while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
    {
        buf++;
        len--;
    }
    while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
        buf[--len] = 0; // trim right whitespace

    if (len > 0 && buf[len - 1] == '?') // auto-complete
        command->auto_complete = true;
    if (len > 0 && buf[len - 1] == '&') // background
        command->background = true;

    char *pch = strtok(buf, splitters);
    command->name = (char *) malloc(strlen(pch) + 1);
    if (pch == NULL)
        command->name[0] = 0;
    else
        strcpy(command->name, pch);

    command->args = (char **) malloc(sizeof(char *));

    int redirect_index;
    int arg_index = 0;
    char temp_buf[1024], *arg;
    while (1) {
        // tokenize input on splitters
        pch = strtok(NULL, splitters);
        if (!pch)
            break;
        arg = temp_buf;
        strcpy(arg, pch);
        len = strlen(arg);

        if (len == 0)
            continue;                                         // empty arg, go for next
        while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
        {
            arg++;
            len--;
        }
        while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
            arg[--len] = 0; // trim right whitespace
        if (len == 0)
            continue; // empty arg, go for next

        // piping to another command
        if (strcmp(arg, "|") == 0) {
            struct command_t *c = malloc(sizeof(struct command_t));
            int l = strlen(pch);
            pch[l] = splitters[0]; // restore strtok termination
            index = 1;
            while (pch[index] == ' ' || pch[index] == '\t')
                index++; // skip whitespaces

            parse_command(pch + index, c);
            pch[l] = 0; // put back strtok termination
            command->next = c;
            continue;
        }

        // background process
        if (strcmp(arg, "&") == 0)
            continue; // handled before

        // handle input redirection
        redirect_index = -1;
        if (arg[0] == '<')
            redirect_index = 0;
        if (arg[0] == '>') {
            if (len > 1 && arg[1] == '>') {
                redirect_index = 2;
                arg++;
                len--;
            } else
                redirect_index = 1;
        }
        if (redirect_index != -1) {
            command->redirects[redirect_index] = malloc(len);
            strcpy(command->redirects[redirect_index], arg + 1);
            continue;
        }

        // normal arguments
        if (len > 2 &&
            ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
        {
            arg[--len] = 0;
            arg++;
        }
        command->args = (char **) realloc(command->args, sizeof(char *) * (arg_index + 1));
        command->args[arg_index] = (char *) malloc(len + 1);
        strcpy(command->args[arg_index++], arg);
    }
    command->arg_count = arg_index;
    return 0;
}

void prompt_backspace() {
    putchar(8);   // go back 1
    putchar(' '); // write empty over
    putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
    int index = 0;
    char c;
    char buf[4096];
    static char oldbuf[4096];

    // tcgetattr gets the parameters of the current terminal
    // STDIN_FILENO will tell tcgetattr that it should write the settings
    // of stdin to oldt
    static struct termios backup_termios, new_termios;
    tcgetattr(STDIN_FILENO, &backup_termios);
    new_termios = backup_termios;
    // ICANON normally takes care that one line at a time will be processed
    // that means it will return if it sees a "\n" or an EOF or an EOL
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    //FIXME: backspace is applied before printing chars
    show_prompt();
    int multicode_state = 0;
    buf[0] = 0;
    while (1) {
        c = getchar();
        // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

        if (c == 9) // handle tab
        {
            buf[index++] = '?'; // autocomplete
            break;
        }

        if (c == 127) // handle backspace
        {
            if (index > 0) {
                prompt_backspace();
                index--;
            }
            continue;
        }
        if (c == 27 && multicode_state == 0) // handle multi-code keys
        {
            multicode_state = 1;
            continue;
        }
        if (c == 91 && multicode_state == 1) {
            multicode_state = 2;
            continue;
        }
        if (c == 65 && multicode_state == 2) // up arrow
        {
            int i;
            while (index > 0) {
                prompt_backspace();
                index--;
            }
            for (i = 0; oldbuf[i]; ++i) {
                putchar(oldbuf[i]);
                buf[i] = oldbuf[i];
            }
            index = i;
            continue;
        } else
            multicode_state = 0;

        putchar(c); // echo the character
        buf[index++] = c;
        if (index >= sizeof(buf) - 1)
            break;
        if (c == '\n') // enter key
            break;
        if (c == 4) // Ctrl+D
            return EXIT;
    }
    if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
        index--;
    buf[index++] = 0; // null terminate string

    strcpy(oldbuf, buf);

    parse_command(buf, command);

    print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
    return SUCCESS;
}

int process_command(struct command_t *command);

char **shellgibi_autocomplete(const char *text, int start, int end);

int load_all_commands();

int main() {
    rl_attempted_completion_function = shellgibi_autocomplete;
    rl_completer_quote_characters = "'\"";
    rl_completer_word_break_characters = " ";
    rl_bind_key('\t', rl_menu_complete);

    while (1) {
        struct command_t *command = malloc(sizeof(struct command_t));
        memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

        int code;

        code = load_all_commands();
        if (code == UNKNOWN)
            break;

        code = prompt(command);
        if (code == EXIT)
            break;

        code = process_command(command);
        if (code == EXIT)
            break;

        free_command(command);
    }

    printf("\n");
    return 0;
}

char *shellgibi_autocomplete_generator(const char *str, int start) {
    static int index, length;
    char *cmd_name;

    if (!start) {
        index = 0;
        length = strlen(str);
    }

    while ((cmd_name = all_commands[index++])) {
        if (rl_completion_quote_character) {
            cmd_name = strdup(cmd_name);

            if (strncmp(cmd_name, str, length) == 0) {
                return cmd_name;
            } else {
                free(cmd_name);
            }
        }

        return NULL;
    }

    char **shellgibi_autocomplete(const char *text, int start, int end) {
        rl_attempted_completion_over = 1;
        return rl_completion_matches(str, auto_gen);
    }

    int load_all_commands() {
        char *path_var = strdup(getenv("PATH"));
        char **paths = malloc(32 * sizeof(char *));
        all_commands = malloc(65536 * sizeof(char *));

        //Getting all paths
        int i = 0;
        char *token, *tmp;

        token = strtok_r(path_var, ":;", &tmp);
        while (1) {
            token = strtok_r(NULL, ":;", &tmp);
            if (token == NULL)break;
            paths[i] = (char *) malloc(1024 * sizeof(char));
            strcpy(paths[i], token);
            i++;
        }
        i--;

        DIR *directory;
        struct dirent *entry;
        char *file_path = malloc(2048 * sizeof(char));
        int counter = 0;
        for (i; i > -1; i--) {
            if ((directory = opendir(paths[i])) != NULL) {
                while ((entry = readdir(directory)) != NULL) {
                    strcpy(file_path, paths[i]);
                    strcat(file_path, "/");
                    strcat(file_path, entry->d_name);
                    if (access(file_path, X_OK) != 0)
                        continue;
                    all_commands[counter] = malloc(1024 * sizeof(char));
                    strcpy(all_commands[counter], entry->d_name);
                    counter++;
                }
                closedir(directory);
            } else {
                return UNKNOWN;
            }
        }

        //Full file completion for current working directory
        char cwd[1024];
        getcwd(cwd, sizeof(cwd));
        if ((directory = opendir(cwd)) != NULL) {
            while ((entry = readdir(directory)) != NULL) {
                all_commands[counter] = malloc(1024 * sizeof(char));
                strcpy(all_commands[counter], entry->d_name);
                counter++;
            }
            closedir(directory);
        } else {
            return UNKNOWN;
        }

        return SUCCESS;
    }

    int weather_forecast(char *out_file) {
        char *const args[] = {"wttrin", "-l", "Istanbul", "-p", "-o", out_file};
        return execvp(args[0], args);
    }


    int process_command(struct command_t *command) {
        int r;

        if (strcmp(command->name, "") == 0)
            return SUCCESS;

        if (strcmp(command->name, "exit") == 0)
            return EXIT;

        if (strcmp(command->name, "cd") == 0) {
            if (command->arg_count > 0) {
                r = chdir(command->args[0]);
                if (r == -1)
                    printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
                return SUCCESS;
            }
        }


        int num_pipes = 1;

        struct command_t *tmp_cmd = malloc(sizeof(struct command_t));
        memcpy(tmp_cmd, command, sizeof(struct command_t));

        while (tmp_cmd->next) {
            num_pipes++;
            tmp_cmd = tmp_cmd->next;
        }

        int tmp_in = dup(0);
        int tmp_out = dup(1);

        int input = dup(0);
        if (command->redirects[0])
            input = open(command->redirects[0], O_RDONLY);
        else
            input = tmp_in;

        int output = dup(1);
        int status;
        pid_t pid;

        for (int i = 0; i < num_pipes; i++) {
            int r;

            if (strcmp(command->name, "") == 0)
                return SUCCESS;

            if (strcmp(command->name, "exit") == 0)
                return EXIT;

            if (strcmp(command->name, "cd") == 0) {
                if (command->arg_count > 0) {
                    r = chdir(command->args[0]);
                    if (r == -1)
                        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
                    return SUCCESS;
                }
            }

            dup2(input, 0);
            close(input);

            if (i == num_pipes - 1) {
                if (command->redirects[1])
                    output = open(command->redirects[1], O_WRONLY | O_TRUNC | O_CREAT,
                                  S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);

                else if (command->redirects[2])
                    output = open(command->redirects[2], O_WRONLY | O_APPEND | O_CREAT,
                                  S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);

                else
                    output = tmp_out;


            } else {
                int fd[2];
                pipe(fd);
                output = fd[1];
                input = fd[0];
            }

            dup2(output, 1);
            close(output);


            pid = fork();
            if (pid == 0) // child
            {
                char path[500];
                /// This shows how to do exec with environ (but is not available on MacOs)
                // extern char** environ; // environment variables
                // execvpe(command->name, command->args, environ); // exec+args+path+environ

                /// This shows how to do exec with auto-path resolve
                // add a NULL argument to the end of args, and the name to the beginning
                // as required by exec

                // increase args size by 2
                command->args = (char **) realloc(
                        command->args, sizeof(char *) * (command->arg_count += 2));

                // shift everything forward by 1
                for (int i = command->arg_count - 2; i > 0; --i)
                    command->args[i] = command->args[i - 1];

                // set args[0] as a copy of name
                command->args[0] = strdup(command->name);
                // set args[arg_count-1] (last) to NULL
                command->args[command->arg_count - 1] = NULL;

                if (strcmp(command->name, "alarm") == 0) {
                    char cwd[1024];
                    getcwd(cwd, sizeof(cwd));
                    char *time = command->args[0];//This is the time in the form HH.MM
                    char *music_file_name = command->args[1]; //This is the name of the music file
                    char time_array[2][5]; //The array we builded for outting time elements
                    char *parsed_time;
                    int i = 0;
                    parsed_time = strtok(time, ".");
                    while (parsed_time != NULL) {
                        strcpy(time_array[i], parsed_time);
                        parsed_time = strtok(NULL, ".");
                        i++;
                    }

                    FILE *music_file;
                    music_file = fopen("play.sh",
                                       "w"); //We keep the command for playing the music inside of this executable file
                    fclose(music_file);

                    FILE *crontab_file;
                    crontab_file = fopen("crontab_file",
                                         "w"); //We pass the crontabFile to crontab function with execv command below
                    fclose(crontab_file);
                    char *arguments[] = {"crontab", "crontab_file", NULL};
                    execv("/usr/bin/crontab", arguments);
                    return SUCCESS;
                } else if (strcmp(command->name, "myjobs") == 0) {
                    system("ps -u");
                    return SUCCESS;
                } else if (strcmp(command->name, "pause") == 0) {
                    char *cmd = "kill -STOP ";
                    strcat(cmd, *command->args);
                    system(cmd);
                    return SUCCESS;
                } else if (strcmp(command->name, "mybg") == 0) {
                    char *cmd = "kill -CONT ";
                    strcat(cmd, *command->args);
                    system(cmd);
                    system("bg");
                    return SUCCESS;
                } else if (strcmp(command->name, "myfg") == 0) {
                    char *cmd = "kill -CONT ";
                    strcat(cmd, *command->args);
                    system(cmd);
                    system("fg");
                    return SUCCESS;
                } else if ((strcmp(command->name, "istforecast") == 0)) {
                    weather_forecast(command->args[0]);
                    return SUCCESS;
                } else if if (strcmp(command->name, "fib") == 0) {
                        int i, n, t1 = 0, t2 = 1, nextTerm;
                        n = command->args[0];
                        for (i = 1; i <= n; ++i) {
                            printf("%d, ", t1);
                            nextTerm = t1 + t2;
                            t1 = t2;
                            t2 = nextTerm;
                        }
                        printf("\n")
                        return SUCCESS;
                    }


                //execvp(command->name, command->args); // exec+args+path
                /// TODO: do your own exec with path resolving using execv()

                strcpy(path, "/usr/bin/");
                strcat(path, command->name);
                execv(path, command->args);


                exit(0);
            } else {
                if (command->next) {
                    command = command->next;
                    continue;
                }

                do {
                    waitpid(pid, &status, WUNTRACED);
                } while (!WIFEXITED(status) && !WIFSIGNALED(status)); // wait for child process to finish

                dup2(input, 0);
                dup2(output, 1);
                close(input);
                close(output);
                return SUCCESS;
            }
        }

        dup2(input, 0);
        dup2(output, 1);
        close(input);
        close(output);

        printf("-%s: %s: command not found\n", sysname, command->name);
        return UNKNOWN;
    }


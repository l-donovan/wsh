#include "builtins.h"
#include "config.h"
#include "control.h"
#include "utils.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

char ch;
bool esc_seq = false;
char cmd_buf[CMD_BUF_SIZE];
char esc_buf[ESC_BUF_SIZE];
char prompt[PROMPT_SIZE];
unsigned int cmd_buf_len = 0;
unsigned int esc_buf_len = 0;

unsigned int last_status;
bool skip_next;

std::map<std::string, std::string> executable_map;
std::map<std::string, std::string> alias_map;

std::map<std::string, int (*)(char**)> builtins_map = {
    { "exit",  builtins::exit },
    { "cd",    builtins::cd },
    { "about", builtins::about },
    { "and",   builtins::cand },
    { "or",    builtins::cor },
    { "out",   builtins::out },
    { "err",   builtins::err },
    { "in",    builtins::in }
};

// This is a lookahead assertion that makes sure we aren't inside of a string
std::string not_in_quotes("(?=([^\"\\\\]*(\\\\.|\"([^\"\\\\]*\\\\.)*[^\"\\\\]*\"))*[^\"]*$)");

void files_in_dir(std::map<std::string, std::string> *executable_map, std::string path) {
    struct stat info;

    if (stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
        for (const auto & entry : std::filesystem::directory_iterator(path)) {
            executable_map->emplace(entry.path().filename(), entry.path());
        }
    } else {
        std::cout << path << " is not a valid directory" << std::endl;
    }
}

void load_path() {
    const char* c_path = std::getenv("PATH");

    std::string path(c_path);
    path += ':';

    size_t pos = 0;
    std::string dir;
    while ((pos = path.find(':')) != std::string::npos) {
        dir = path.substr(0, pos);
        files_in_dir(&executable_map, dir);
        path.erase(0, pos + 1);
    }
}

void load_prompt() {
    const char* raw_prompt = std::getenv("WSH_PROMPT");
    if (raw_prompt == nullptr) {
        strcpy(prompt, DEFAULT_PROMPT);
    } else {
        // Here's where we'd actually handle prompt replacement
        strcpy(prompt, raw_prompt);
    }
}

int cmd_execute(char **args) {
    pid_t pid, wpid;
    int status;

    pid = fork();

    if (pid == 0) {
        // Child process
        if (execv(args[0], args) == -1) {
            // We would update some sort of status here or something
            perror("Error when running child process");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Error forking
        perror("Error when forking child process");
    } else {
        // Parent process
        // We want to run waitpid before checking the conditions, hence the do {} while
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));

        last_status = WEXITSTATUS(status);
    }

    return 1;
}

char** cmd_tokenize(char *cmd) {
    char** tokens = (char**) malloc(ARG_BUF_SIZE * sizeof(char*));

    if (!tokens) {
        perror("Error when allocating arguments buffer");
        exit(EXIT_FAILURE);
    }

    std::string buf(cmd);
    buf += ' ';

    size_t pos = 0;
    std::string arg;
    unsigned int argc = 0;
    while ((pos = buf.find_first_of(" \t")) != std::string::npos) {
        arg = buf.substr(0, pos);
        buf.erase(0, pos + 1);

        // In case this is an empty argument, usually caused by trailing whitespace
        if (pos == 0) {
            continue;
        }

        // If we are looking at the command itself...
        if (argc == 0) {
            // and the command isn't a builtin...
            if (builtins_map.find(arg) == builtins_map.end()) {
                // and the command is in PATH...
                auto it = executable_map.find(arg);
                if (it != executable_map.end()) {
                    // expand it to its full path
                    arg = executable_map.find(arg)->second;
                }
            }
        }

        // Allocate memory for the argument
        tokens[argc] = (char*) malloc((arg.length() + 1) * sizeof(char));

        if (!tokens[argc]) {
            perror("Error when allocating argument buffer");
            exit(EXIT_FAILURE);
        }

        // Copy the C string to the allocated memory
        strcpy(tokens[argc], arg.c_str());

        // Check if we need to expand our argument buffer
        if (++argc >= ARG_BUF_SIZE) {
            tokens = (char**) realloc(tokens, (argc + 1) * sizeof(char*));

            if (!tokens) {
                perror("Error when reallocating argument buffer");
                exit(EXIT_FAILURE);
            }
        }
    }

    tokens[argc] = NULL;

    return tokens;
}

bool process_esc_seq() {
    std::cmatch cm;

    if (std::regex_match(esc_buf, cm, std::regex("\\[([ABCD])"))) {
        if (cm[1] == "A") {
            std::cout << "UP" << std::endl;
        } else if (cm[1] == "B") {
            std::cout << "DOWN" << std::endl;
        } else if (cm[1] == "C") {
            std::cout << "RIGHT" << std::endl;
        } else if (cm[1] == "D") {
            std::cout << "LEFT" << std::endl;
        }

        return true;
    }

    // Here's where we would actually do something with the escape sequence
    return false;
}

void replace_variables(std::string &input) {
    std::string output(input);
    int offset = 0;

    std::smatch match;
    std::regex variable("\\{(\\w+)\\}");
    std::string::const_iterator search_start(input.cbegin());

    while (regex_search(search_start, input.cend(), match, variable)) {
        const char* c_var = std::getenv(match[1].str().c_str());
        std::string var(c_var ?: "");
        output.replace(match.position() + offset, match.length(), var);
        offset += match.position() + var.length();
        search_start = match.suffix().first;
    }

    input = output;
}

void process_cmd() {
    if (!cmd_buf_len) {
        // No point in running an empty command
        return;
    }

    // Lines are delimited by semicolons
    std::string input(cmd_buf);
    std::regex line_delim(";" + not_in_quotes);
    std::sregex_token_iterator iter(input.begin(), input.end(), line_delim, -1);
    std::sregex_token_iterator end;

    // Iterate through each line
    for (; iter != end; ++iter) {
        // If we should skip this command, skip it and reset the flag
        if (skip_next) {
            skip_next = false;
            continue;
        }

        std::string line_str = *iter;
        trim(line_str);
        replace_variables(line_str);

        // Lookup our command
        char **tokens = cmd_tokenize(line_str.data());

        // Find if this is a builtin command
        auto it = builtins_map.find(tokens[0]);
        if (it != builtins_map.end()) {
            auto builtin_fn = it->second;
            int result = builtin_fn(tokens);
            if (result != CODE_CONTINUE) {
                exit(result - 1);
            }
        } else {
            cmd_execute(tokens);
        }
    }
}

int main(int argc, char **argv) {
    load_path();
    load_prompt();

    std::cout << prompt;

    while ((ch = getch()) != EOF) {
        if (esc_seq) {
            if (esc_buf_len < ESC_BUF_SIZE - 1) {
                // Add our newest character
                esc_buf[esc_buf_len++] = ch;

                // If we aren't done yet, move along
                if (!process_esc_seq()) {
                    continue;
                }
            } else {
                // Escape sequence is too large
                perror("Escape buffer reached maximum size");
            }

            // Clear the buffer, reset the pointer
            memset(esc_buf, 0, ESC_BUF_SIZE);
            esc_buf_len = 0;
            esc_seq = false;
        } else {
            switch (ch) {
                case 0x1b: // ESCAPE
                    esc_seq = true;
                    break;
                case 0x7f: // BACKSPACE (DELETE)
                    if (cmd_buf_len) {
                        std::cout << "\x08 \x08";
                        cmd_buf[--cmd_buf_len] = 0;
                    }
                    break;
                case 0x0a: // NEWLINE
                    std::cout << std::endl;
                    process_cmd();
                    memset(cmd_buf, 0, CMD_BUF_SIZE);
                    cmd_buf_len = 0;
                    std::cout << prompt;
                    break;
                default:
                    std::cout << ch;
                    cmd_buf[cmd_buf_len++] = ch;
                    break;
            }
        }
    }

    return 0;
}

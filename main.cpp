#include "builtins.h"
#include "config.h"
#include "control.h"
#include "utils.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <pwd.h>
#include <regex>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void load_path();
void load_prompt();
void load_rc();
void process_keypress(char);
void process_cmd();
bool process_esc_seq();
void files_in_dir(std::string);
void execute_script(std::string);
void suggest(int);
void replace_variables(std::string&);
int cmd_execute(char**);
char** cmd_tokenize(char*);

char c;
char cmd_buf[CMD_BUF_SIZE];
char esc_buf[ESC_BUF_SIZE];
char prompt[PROMPT_SIZE];

unsigned int cmd_buf_len = 0;
unsigned int esc_buf_len = 0;
unsigned int last_status = 0;
unsigned int history_idx = 0;

bool echo_input  = true;
bool esc_seq     = false;
bool skip_next   = false;
bool pipe_input  = false;
bool pipe_output = false;
bool suggesting  = false;

int pipefd_input[2];
int pipefd_output[2];

struct passwd *pw = getpwuid(getuid());
const char *homedir = pw->pw_dir;

std::map<std::string, std::string> executable_map;
std::map<std::string, std::string> alias_map;
std::vector<std::string> history;
std::string cmd_str;

std::map<std::string, int (*)(char**)> builtins_map = {
    { "exit",     builtins::bexit },
    { "cd",       builtins::bcd },
    { "about",    builtins::babout },
    { "and",      builtins::band },
    { "or",       builtins::bor },
    { "redirect", builtins::bredirect },
    { "silence",  builtins::bsilence },
    { "set",      builtins::bset },
    { "reload",   builtins::breload },
    { "alias",    builtins::balias },
    { "unalias",  builtins::bunalias }
};

// This is a lookahead assertion that makes sure we aren't inside of a string
std::string not_in_quotes("(?=([^\"\\\\]*(\\\\.|\"([^\"\\\\]*\\\\.)*[^\"\\\\]*\"))*[^\"]*$)");

void files_in_dir(std::string path) {
    struct stat info;

    if (stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR)) {
        for (const auto & entry : std::filesystem::directory_iterator(path)) {
            executable_map.emplace(entry.path().filename(), entry.path());
        }
    } else {
        std::cerr << path << " is not a valid directory" << std::endl;
    }
}

inline bool file_exists(const std::string& name) {
    struct stat buffer;
    return stat(name.c_str(), &buffer) == 0;
}

void execute_script(std::string filename) {
    echo_input = false;
    std::fstream fin(filename, std::fstream::in);
    while (fin >> std::noskipws >> c) {
        process_keypress(c);
    }
    echo_input = true;
}

void load_path() {
    const char* c_path = std::getenv("PATH");

    if (c_path != nullptr) {
        executable_map.clear();

        std::string path(c_path);
        path += ':';

        size_t pos = 0;
        std::string dir;
        while ((pos = path.find(':')) != std::string::npos) {
            dir = path.substr(0, pos);
            files_in_dir(dir);
            path.erase(0, pos + 1);
        }
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

void load_rc() {
    std::string home_path(homedir);
    home_path += '/';
    home_path += RC_FILENAME;

    std::string local_path(".");
    local_path += '/';
    local_path += RC_FILENAME;

    if (file_exists(home_path)) {
        execute_script(home_path);
    }

    if (file_exists(local_path)) {
        execute_script(local_path);
    }
}

int cmd_execute(char **args) {
    pid_t pid, wpid;
    int status;

    if (pipe_input || pipe_output) {
        int tmp_a = pipefd_input[0], tmp_b = pipefd_input[1];

        pipefd_input[READ_END] = pipefd_output[READ_END];
        pipefd_input[WRITE_END] = pipefd_output[WRITE_END];

        pipefd_output[READ_END] = tmp_a;
        pipefd_output[WRITE_END] = tmp_b;
    }

    pid = fork();

    if (pid == 0) {
        // Child process

        if (pipe_input) {
            close(pipefd_input[WRITE_END]);

            // Send pipe to stdin
            dup2(pipefd_input[READ_END], STDIN_FILENO);
        }

        if (pipe_output) {
            close(pipefd_output[READ_END]);

            // Send stdout and stderr to pipe
            dup2(pipefd_output[WRITE_END], STDOUT_FILENO);
            dup2(pipefd_output[WRITE_END], STDERR_FILENO);
        }

        if (execv(args[0], args) == -1) {
            // We would update some sort of status here or something
            perror(args[0]);
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("Error when forking child process");
    } else {
        // Parent process
        // We want to run waitpid before checking the conditions, hence the do {} while

        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));

        last_status = WEXITSTATUS(status);

        if (pipe_input) {
            close(pipefd_input[READ_END]);
            pipe(pipefd_input);
        }

        if (pipe_output) {
            close(pipefd_output[WRITE_END]);
        }

        pipe_input = pipe_output;
        pipe_output = false;
    }

    return 1;
}

char** cmd_tokenize(char *cmd) {
    std::smatch match;
    std::regex whitespace("\\s+" + not_in_quotes);
    std::string input(cmd);
    std::string arg;
    input += ' ';
    std::string::const_iterator search_start(input.cbegin());
    unsigned int argc = 0;

    std::ptrdiff_t const match_count(std::distance(
        std::sregex_iterator(input.begin(), input.end(), whitespace),
        std::sregex_iterator()));

    char** tokens = (char**) malloc((match_count + 1) * sizeof(char*));

    if (!tokens) {
        perror("Error when allocating arguments buffer");
        exit(EXIT_FAILURE);
    }

    while (regex_search(search_start, input.cend(), match, whitespace)) {
        arg = match.prefix();

        // Trim the quotes off of a string argument
        if (arg.at(0) == '"' && arg.at(arg.length() - 1) == '"') {
            arg = arg.substr(1, arg.length() - 2);
        }

        // If we are looking at the command itself...
        if (argc == 0) {
            // and the command has an alias...
            auto alias = alias_map.find(arg);
            if (alias != alias_map.end()) {
                // substitute the alias
                arg = alias->second;
            }

            // and the command isn't a builtin...
            if (builtins_map.find(arg) == builtins_map.end()) {
                // and it's on PATH...
                auto executable = executable_map.find(arg);
                if (executable != executable_map.end()) {
                    // expand it to its full path
                    arg = executable->second;
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

        ++argc;

        search_start = match.suffix().first;
    }

    tokens[match_count] = nullptr;

    return tokens;
}

void suggest(int direction) {
    if (history.empty())
        return;

    // Only change history_idx if we are already suggesting
    if (suggesting) {
        if (direction == DIRECTION_UP && history_idx < history.size() - 1)
            ++history_idx;
        if (direction == DIRECTION_DOWN && history_idx > 0)
            --history_idx;
    }

    std::string suggestion = history.at(history_idx);
    strcpy(cmd_buf, suggestion.c_str());

    // Move back to right after the prompt, then clear the line and print our suggestion
    std::cout << "\e[" << (cmd_buf_len + suggesting - 1) << "D\e[K";
    std::cout << cmd_buf;

    cmd_buf_len = suggestion.length();
    suggesting = true;
}

bool process_esc_seq() {
    std::cmatch cm;

    if (std::regex_match(esc_buf, cm, std::regex("\\[([ABCD])"))) {
        if (cm[1] == "A") { // UP
            suggest(DIRECTION_UP);
        } else if (cm[1] == "B") { // DOWN
            suggest(DIRECTION_DOWN);
        } else if (cm[1] == "C") { // RIGHT
            std::cout << "RIGHT" << std::endl;
        } else if (cm[1] == "D") { // LEFT
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

    std::regex tilde("~");
    search_start = input.cbegin();
    std::string home(homedir);
    offset = 0;

    while (regex_search(search_start, input.cend(), match, tilde)) {
        output.replace(match.position() + offset, match.length(), home);
        offset += match.position() + home.length();
        search_start = match.suffix().first;
    }

    input = output;
}

void process_cmd() {
    // No point in running an empty command
    if (!cmd_buf_len)
        return;

    // Comments are easy enough to handle
    if (cmd_buf[0] == '#')
        return;

    // Commands are delimited by semicolons or pipes
    std::smatch match;
    std::regex cmd_separator("[;\\|]" + not_in_quotes);
    std::string input(cmd_buf);
    input += ';';
    std::string cmd;
    bool is_pipe;
    std::string::const_iterator search_start(input.cbegin());

    while (regex_search(search_start, input.cend(), match, cmd_separator)) {
        search_start = match.suffix().first;

        if (skip_next) {
            skip_next = false;
            continue;
        }

        pipe_output = match.str().at(0) == '|';
        cmd = match.prefix();
        trim(cmd);
        replace_variables(cmd);

        // Lookup our command
        char **tokens = cmd_tokenize(cmd.data());

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

void process_keypress(char ch) {
    if (esc_seq) {
        if (esc_buf_len < ESC_BUF_SIZE - 1) {
            // Add our newest character
            esc_buf[esc_buf_len++] = ch;

            // If we aren't done yet, move along
            if (!process_esc_seq())
                return;
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
                    if (echo_input)
                        std::cout << "\x08 \x08";
                    cmd_buf[--cmd_buf_len] = 0;
                }
                break;
            case 0x0a: // NEWLINE
                if (echo_input)
                    std::cout << std::endl;
                process_cmd();
                history_idx = 0;
                cmd_str = cmd_buf;
                // If this command is running silently, we don't want it in our history.
                // We also don't want it in our history if this command is the same as the
                // last non-silent command we executed.
                if (echo_input && (history.empty() || history.front() != cmd_str))
                    history.insert(history.begin(), cmd_str);
                memset(cmd_buf, 0, cmd_buf_len);
                cmd_buf_len = 0;
                suggesting = false;
                if (echo_input)
                    std::cout << prompt;
                break;
            default:
                if (echo_input)
                    std::cout << ch;
                cmd_buf[cmd_buf_len++] = ch;
                break;
        }
    }
}

int main(int argc, char **argv) {
    load_rc();
    load_path();
    load_prompt();

    setenv("SHELL", SHELL_NAME, true);

    // Setup our pipes
    pipe(pipefd_input);
    pipe(pipefd_output);

    // Loading from script
    if (argc > 1) {
        execute_script(std::string(argv[1]));
        return 0;
    }

    if (echo_input)
        std::cout << prompt;

    while ((c = getch()) != EOF) {
        process_keypress(c);
    }

    return 0;
}

#include "builtins.h"
#include "config.h"
#include "control.h"
#include "global.h"
#include "utils.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <map>
#include <pwd.h>
#include <regex>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <variant>

using std::string;

void select_completions(int);
void print_completions(int);
void initialize_path();
void load_path();
void load_prompt();
void load_rc();
void process_keypress(char);
void files_in_dir(string);
void execute_script(string);
void suggest(int);
void cmd_enter(string);
void cmd_launch(std::vector<command>, bool);
int cmd_execute(char**, bool, bool);
bool process_esc_seq();
string parse_path_file(string);

char c;
unsigned int last_status = 0;
unsigned int history_idx = 0;
int completion_idx = -1;
int input_idx = INSERT_END;
bool echo_input  = true;
bool in_esc_seq  = false;
bool skip_next   = false;
bool pipe_input  = false;
bool pipe_output = false;
bool or_output   = false;
bool and_output  = false;
bool bg_command  = false;
bool with_var    = false;
bool suggesting  = false;
bool control_c   = false;
bool subcommand  = false;
bool completing  = false;
int pipefd_input[2];
int pipefd_output[2];
int pipefd_subc[2];
char subc_buf[1024];

std::map<string, string> executable_map;
std::map<string, string> alias_map;
std::map<string, int (*)(int, char**)> builtins_map;
std::vector<string> history;
std::vector<string> matches;
string esc_seq;
string cmd_str;
string prompt;
string subc_out;
string arg;

NullStream null;

inline std::ostream& sout() {
    return echo_input ? std::cout : null;
}

inline std::ostream& serr() {
    return echo_input ? std::cerr : null;
}

void files_in_dir(string path) {
    struct stat info;

    if (dir_exists(path.c_str())) {
        for (const auto & entry : std::filesystem::directory_iterator(path))
            executable_map.emplace(entry.path().filename(), entry.path());
    } else {
        serr() << path << " is not a valid directory" << std::endl;
    }
}

void execute_script(string filename) {
    echo_input = false;
    std::fstream fin(filename, std::fstream::in);

    while (fin >> std::noskipws >> c)
        process_keypress(c);

    echo_input = true;
}

string parse_path_file(string filepath) {
    string path;

    if (file_exists(filepath)) {
        std::ifstream infile(filepath);
        string dir;

        while (infile >> dir)
            path += dir + ":";
    }

    return path;
}

void initialize_path() {
    string path = parse_path_file("/etc/paths");

    if (dir_exists("/etc/paths.d")) {
        for (const auto & entry : std::filesystem::directory_iterator("/etc/paths.d"))
            path += parse_path_file(entry.path());

        if (!path.empty())
            path.pop_back();
    }

    setenv("PATH", path.c_str(), true);
}

void load_path() {
    executable_map.clear();

    const char* c_path = std::getenv("PATH");
    string path(c_path ?: "");
    path += ':';

    size_t pos = 0;
    string dir;
    while ((pos = path.find(':')) != string::npos) {
        dir = path.substr(0, pos);
        files_in_dir(dir);
        path.erase(0, pos + 1);
    }
}

void load_prompt() {
    const char* raw_prompt_c = std::getenv("WSH_PROMPT");
    string raw_prompt(raw_prompt_c ?: DEFAULT_PROMPT);
    prompt = escape_string(raw_prompt);
}

void load_rc() {
    string local_path(".");
    local_path += '/';
    local_path += RC_FILENAME;

    if (file_exists(local_path)) {
        execute_script(local_path);
        return;
    }

    struct passwd *pw = getpwuid(getuid());
    string home_path(pw->pw_dir);
    home_path += '/';
    home_path += RC_FILENAME;

    if (file_exists(home_path)) {
        execute_script(home_path);
        return;
    }
}

int cmd_execute(char **args, bool is_subcommand, bool is_background) {
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
        } else if (is_subcommand) {
            close(pipefd_subc[READ_END]);

            // Send stdout and stderr to subcommand pipe
            dup2(pipefd_subc[WRITE_END], STDOUT_FILENO);
            dup2(pipefd_subc[WRITE_END], STDERR_FILENO);
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

        if (!is_background) {
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
            } else if (is_subcommand) {
                close(pipefd_subc[WRITE_END]);

                subc_out.clear();

                while (int nread = read(pipefd_subc[READ_END], subc_buf, 1023)) {
                    subc_buf[nread] = '\0';
                    subc_out += subc_buf;
                }

                // Trailing newlines break lots of things with subcommands
                if (subc_out.back() == '\n') {
                    subc_out.pop_back();
                }

                close(pipefd_subc[READ_END]);
                pipe(pipefd_subc);
            }

            pipe_input = pipe_output;
            pipe_output = false;
        }
    }

    return 1;
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

    string suggestion = history.at(history_idx);

    // Move back to right after the prompt, then clear the line and print our suggestion
    if (!cmd_str.empty()) {
        sout() << "\e[" << cmd_str.length() << "D"
               << "\e[K";
    }

    cmd_str = suggestion;
    sout() << cmd_str;

    suggesting = true;
}

bool process_esc_seq() {
    std::smatch match;

    if (std::regex_match(esc_seq, match, std::regex("\\[([ABCD])"))) {
        if (completing) {
            if (match[1] == "A") { // UP
                select_completions(DIRECTION_UP);
                print_completions(completion_idx);
            } else if (match[1] == "B") { // DOWN
                select_completions(DIRECTION_DOWN);
                print_completions(completion_idx);
            } else if (match[1] == "C") { // RIGHT
                select_completions(DIRECTION_RIGHT);
                print_completions(completion_idx);
            } else if (match[1] == "D") { // LEFT
                select_completions(DIRECTION_LEFT);
                print_completions(completion_idx);
            }
        } else {
            if (match[1] == "A") { // UP
                suggest(DIRECTION_UP);
            } else if (match[1] == "B") { // DOWN
                suggest(DIRECTION_DOWN);
            } else if (match[1] == "C") { // RIGHT
                if (input_idx != INSERT_END) {
                    if (++input_idx > cmd_str.length() - 1)
                        input_idx = INSERT_END;

                    sout() << "\e[C";
                }
            } else if (match[1] == "D") { // LEFT
                if (input_idx != 0)
                    sout() << "\e[D";

                if (input_idx == INSERT_END)
                    input_idx = cmd_str.length() - 1;
                else if (input_idx > 0)
                    --input_idx;
            }
        }

        return true;
    }

    return false;
}

char** vec_to_charptr(std::vector<string> vec_tokens) {
    // Now we have to translate our vector into a nullptr-terminated char**
    char** tokens = (char**) malloc((vec_tokens.size() + 1) * sizeof(char*));
    if (!tokens) {
        perror("Error when allocating arguments buffer");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < vec_tokens.size(); ++i) {
        std::string arg = vec_tokens[i];

        tokens[i] = (char*) malloc((arg.length() + 1) * sizeof(char));
        if (!tokens[i]) {
            perror("Error when allocating argument buffer");
            exit(EXIT_FAILURE);
        }

        strcpy(tokens[i], arg.c_str());
    }

    tokens[vec_tokens.size()] = nullptr;

    return tokens;
}

std::vector<string> flatten_alias(std::vector<command> tokens) {
    std::vector<string> out;

    auto cmd = tokens.front();

    for (int i = 0; i < cmd.args.size(); ++i) {
        Argument arg = cmd.args[i];
        string sub;

        for (int j = 0; j < arg.size(); ++j) {
            auto arg_component = arg[j];

            if (std::holds_alternative<string>(arg_component)) {
                sub += std::get<string>(arg_component);
            }
        }

        out.push_back(sub);
    }

    return out;
}

void cmd_launch(std::vector<command> commands, bool is_subcommand) {
    for (auto cmd : commands) {
        std::vector<string> args;

        if (skip_next) {
            skip_next = false;
            continue;
        }

        for (int j = 0; j < cmd.args.size(); ++j) {
            Argument arg = cmd.args[j];
            string arg_str;

            for (auto arg_component : arg) {
                if (std::holds_alternative<string>(arg_component)) {
                    string val = std::get<string>(arg_component);

                    // Replace variables and expand tildes
                    if (val.length() >= 2 && val.front() == '\'' && val.back() == '\'') {
                        val = val.substr(1, val.length() - 2);
                    } else if (val.length() >= 2 && val.front() == '\"' && val.back() == '\"') {
                        val = val.substr(1, val.length() - 2);
                        val = escape_string(val);
                        val = replace_variables(val);
                    } else {
                        val = replace_variables(val);
                    }

                    arg_str += val;
                } else {
                    cmd_launch(std::get<CommandList>(arg_component), true);
                    arg_str += subc_out;
                }
            }

            // If we are looking at the command itself...
            if (j == 0) {
                // and the command has an alias...
                auto alias = alias_map.find(arg_str);
                if (alias != alias_map.end()) {
                    // substitute the alias
                    arg_str = alias->second;

                    // Tokenize the alias
                    std::vector<command> alias_tokens = tokenize(arg_str);
                    std::vector<string> tokens = flatten_alias(alias_tokens);
                    arg_str = tokens.front();

                    // Insert any args the alias may include
                    for (int k = 1; k < tokens.size(); ++k) {
                        cmd.args.insert(cmd.args.begin() + k, {tokens[k]});
                    }
                }

                // and the command isn't a builtin...
                if (builtins_map.find(arg_str) == builtins_map.end()) {
                    // and it's on PATH...
                    auto executable = executable_map.find(arg_str);
                    if (executable != executable_map.end()) {
                        // expand it to its full path
                        arg_str = executable->second;
                    }
                }
            }

            args.push_back(arg_str);
        }

        if (cmd.pipe_output)
            pipe_output = true;

        char **tokens = vec_to_charptr(args);

        // Find if this is a builtin command
        auto it = builtins_map.find(tokens[0]);
        if (it != builtins_map.end()) {
            auto builtin_fn = it->second;
            int result = builtin_fn(args.size(), tokens);

            if (result >= 0)
                last_status = result;
            else
                exit(-result - 1);

            if (is_subcommand)
                subc_out = std::to_string(last_status);
        } else {
            cmd_execute(tokens, is_subcommand, cmd.bg_command);

            if (with_var) {
                cmd_enter("without");
                with_var = false;
            }
        }

        if (cmd.and_output) {
            skip_next = last_status != 0;
            and_output = false;
        }

        if (cmd.or_output) {
            skip_next = last_status == 0;
            or_output = false;
        }
    }
}

void cmd_enter(string input) {
    trim(input);

    // No point in running an empty line
    if (input.empty())
        return;

    // Comments are easy enough to handle
    if (input[0] == '#')
        return;

    std::vector<command> commands = tokenize(input);

    cmd_launch(commands, false);
}

void print_completions(int index) {
    struct winsize size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);

    int row, col;
    int chars_per_col = size.ws_col / COMPLETION_COLUMNS - 1;
    int rows_added = matches.size() / COMPLETION_COLUMNS + 1;

    get_cursor_pos(&row, &col); // Save our cursor position

    sout() << "\e[J"     // Clear the rest of the screen
           << std::endl;

    for (int i = 0; i < matches.size(); ++i) {
        if (i > 0 && i % COMPLETION_COLUMNS == 0)
            sout() << std::endl;

        string entry = matches[i];

        if (entry.length() < chars_per_col)
            entry.append(chars_per_col - entry.length(), ' ');

        if (i == index)
            sout() << "\e[7m"  // Invert the characters
                   << entry.substr(0, chars_per_col)
                   << "\e[m "; // Return to normal
        else
            sout() << entry.substr(0, chars_per_col) << " ";
    }

    if (row + rows_added > size.ws_row)
        row = size.ws_row - rows_added;

    sout() << "\e[" << row << ";" << col << "H"; // Move back to our saved cursor position
}

void select_completions(int direction) {
    // It's necessary to cast the output of size to a
    // signed integer for comparing to completion_idx
    int size = (int) matches.size();

    if (direction == DIRECTION_DOWN && (completion_idx < size - COMPLETION_COLUMNS))
        completion_idx += COMPLETION_COLUMNS;
    else if (direction == DIRECTION_UP && (completion_idx >= COMPLETION_COLUMNS))
        completion_idx -= COMPLETION_COLUMNS;
    else if (direction == DIRECTION_RIGHT)
        completion_idx = (completion_idx + 1) % size;
    else if (direction == DIRECTION_LEFT)
        completion_idx = (completion_idx - 1) % size;
}

void cmd_complete(string input) {
    trim(input);

    // No point in trying to complete an empty line
    if (input.empty())
        return;

    // Comments are easy enough to handle
    if (input[0] == '#')
        return;

    std::vector<command> commands = tokenize(input);

    if (commands.empty())
        return;

    auto last_command = commands.back();
    auto last_arg = last_command.args.back();
    auto last_arg_component = last_arg.back();

    if (!std::holds_alternative<string>(last_arg_component))
        return;

    arg = std::get<string>(last_arg_component);

    if (last_command.args.size() == 1) {
        // Suggesting a command
        matches = filter_prefix(executable_map, arg);
    } else {
        // Suggesting an argument
        matches = complete_path(replace_variables(arg));
    }

    if (completing) {
        select_completions(DIRECTION_RIGHT);
    } else {
        completing = true;
        completion_idx = -1;
    }

    print_completions(completion_idx);
}

void process_keypress(char ch) {
    if (in_esc_seq) {
        // Add our newest character
        esc_seq += ch;

        // If the sequence is complete...
        if (process_esc_seq()) {
            // clear the buffer
            esc_seq.clear();
            in_esc_seq = false;
        }
    } else {
        switch (ch) {
            case 0x1b: // ESCAPE
                in_esc_seq = true;
                break;
            case 0x7f: // BACKSPACE (DELETE)
                if (!cmd_str.empty()) {
                    if (input_idx == INSERT_END) {
                        cmd_str.pop_back();
                        sout() << "\x08 \x08";
                    } else if (input_idx != 0) {
                        --input_idx;
                        cmd_str.erase(input_idx, 1);
                        sout() << "\x08\e[K" // Backspace and clear the rest of the line
                               << cmd_str.substr(input_idx, string::npos) // Print the second part of the command
                               << "\e[" << (cmd_str.length() - input_idx) << "D"; // Move the cursor back
                    }
                }
                break;
            case 0x20: // SPACE
                sout() << "\e[J";

                completing = false;

                if (input_idx == INSERT_END) {
                    cmd_str += ch;
                    sout() << ch;
                } else {
                    cmd_str.insert(input_idx, 1, ch);
                    sout() << cmd_str.substr(input_idx, string::npos)
                           << "\e[" << (cmd_str.length() - 1 - input_idx) << "D";
                    ++input_idx;
                }

                break;
            case 0x0a: // NEWLINE
                if (completing && completion_idx > -1) {
                    int len = arg.length();
                    sout() << "\e[" << len << "D"
                           << matches[completion_idx];
                    cmd_str.replace(cmd_str.length() - arg.length(), len, matches[completion_idx]);
                    completing = false;
                    sout() << "\e[J";
                    break;
                }

                sout() << std::endl << "\e[J";

                cmd_enter(cmd_str);
                history_idx = 0;
                // If this command is running silently, we don't want it in our history.
                // We also don't want it in our history if this command is the same as the
                // last non-silent command we executed, or if the command is empty
                if (echo_input && !cmd_str.empty() && (history.empty() || history.front() != cmd_str))
                    history.insert(history.begin(), cmd_str);
                cmd_str.clear();
                suggesting = false;
                input_idx = INSERT_END;
                load_prompt();
                sout() << prompt;
                break;
            case 0x09: // TAB
                cmd_complete(cmd_str);
                break;
            case 0x0c: // Ctrl+L
                cmd_enter("clear");
                sout() << prompt << cmd_str;
                break;
            case EOF:
                break;
            default:
                if (input_idx == INSERT_END) {
                    cmd_str += ch;
                    sout() << ch;
                } else {
                    cmd_str.insert(input_idx, 1, ch);
                    sout() << cmd_str.substr(input_idx, string::npos)
                           << "\e[" << (cmd_str.length() - 1 - input_idx) << "D";
                    ++input_idx;
                }

                break;
        }
    }
}

void sig_int_callback(int s) {
    cmd_str.clear();
    sout() << "\e[J" << std::endl << prompt;
    control_c = true;
}

int main(int argc, char **argv) {
    // Register our SIGINT handler
    struct sigaction sig_int_handler;
    sig_int_handler.sa_handler = sig_int_callback;
    sigemptyset(&sig_int_handler.sa_mask);
    sig_int_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_int_handler, NULL);

    // Setup our pipes
    pipe(pipefd_input);
    pipe(pipefd_output);
    pipe(pipefd_subc);

    // Setup our environment variables
    setenv("SHELL", SHELL_NAME, true);

    builtins_map = {
        { "exit",     builtins::bexit },
        { "cd",       builtins::bcd },
        { "about",    builtins::babout },
        { "and",      builtins::band },
        { "or",       builtins::bor },
        { "redirect", builtins::bredirect },
        { "silence",  builtins::bsilence },
        { "set",      builtins::bset },
        { "unset",    builtins::bunset },
        { "ladd",     builtins::bladd },
        { "radd",     builtins::bradd },
        { "reload",   builtins::breload },
        { "alias",    builtins::balias },
        { "unalias",  builtins::bunalias },
        { "exists",   builtins::bexists },
        { "equals",   builtins::bequals },
        { "with",     builtins::bwith },
        { "without",  builtins::bwithout }
    };

    initialize_path(); // This actually initializes the PATH variable using /etc/paths
    load_path();       // This reads the PATH variable to determine full paths to commands
    load_rc();         // Here, edits could potentially be made to PATH...
    load_path();       // hence we reload the PATH

    load_prompt();

    // Loading from script
    if (argc > 1) {
        execute_script(string(argv[1]));
        return 0;
    }

    sout() << prompt;

    while ((c = getch()) != EOF || control_c) {
        process_keypress(c);
        control_c = false;
    }

    return 0;
}


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
#include <sys/mman.h>
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
void cmd_launch(std::vector<Command>, bool);
int cmd_execute(int, char**, bool, bool);
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
bool getch_skip  = false;
bool subcommand  = false;
bool completing  = false;
int pipefd_input[2];
int pipefd_output[2];
int pipefd_subc[2];
char subc_buf[1024];

std::map<string, string> executable_map;
std::map<string, string> alias_map;
std::map<string, int (*)(int, char**, unsigned int*, char*, char*)> builtins_map;
std::map<string, string> set_globals;
std::vector<string> unset_globals;
std::vector<string> history;
std::vector<string> matches;
string esc_seq;
string cmd_str;
string prompt;
string subc_out;
string arg;
string prev_dir;
pid_t pid = 0;
pid_t active_pid = 0;
pid_t last_pid = 0;
pid_t suspended_pid = 0;

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

void load_history() {
    struct passwd *pw = getpwuid(getuid());
    string history_path(pw->pw_dir);
    history_path += '/';
    history_path += HIST_FILENAME;

    if (file_exists(history_path)) {
        string str;
        std::fstream fin(history_path, std::fstream::in);

        while (getline(fin, str))
            history.push_back(str);
    }
}

void save_history() {
    struct passwd *pw = getpwuid(getuid());
    string history_path(pw->pw_dir);
    history_path += '/';
    history_path += HIST_FILENAME;

    std::ofstream fout(history_path);

    for (auto it = history.begin(); it != history.end(); ++it) {
        fout << *it << std::endl;
    }
}

int cmd_execute(int argc, char **args, bool is_subcommand, bool is_background) {
    pid_t wpid;
    int status;
    with_var = false;

    if (pipe_input || pipe_output) {
        int tmp_a = pipefd_input[0], tmp_b = pipefd_input[1];

        pipefd_input[READ_END] = pipefd_output[READ_END];
        pipefd_input[WRITE_END] = pipefd_output[WRITE_END];

        pipefd_output[READ_END] = tmp_a;
        pipefd_output[WRITE_END] = tmp_b;
    }

    auto flags = (unsigned int*) mmap(NULL, sizeof(unsigned int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    auto flag_arg_a = (char*) mmap(NULL, sizeof(char) * 1024, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    auto flag_arg_b = (char*) mmap(NULL, sizeof(char) * 1024, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

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

        // Find if this is a builtin command
        auto it = builtins_map.find(args[0]);
        if (it != builtins_map.end()) {
            auto builtin_fn = it->second;
            int result = builtin_fn(argc, args, flags, flag_arg_a, flag_arg_b);

            exit(result);
        } else {
            if (execv(args[0], args) == -1) {
                // We would update some sort of status here or something
                perror(args[0]);
            }

            exit(EXIT_FAILURE);
        }
    } else if (pid < 0) {
        perror("Error when forking child process");
    } else {
        // Parent process

        active_pid = pid;

        if (!is_background) {
            // We want to run waitpid before checking the conditions, hence the do {} while
            do {
                wpid = waitpid(pid, &status, WUNTRACED);
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));

            last_status = WEXITSTATUS(status);
            last_pid = active_pid;
            active_pid = 0;

            // Check for flags set by child process
            if (*flags & FLAG_EXIT)
                exit(last_status);

            if (*flags & FLAG_CD) {
                std::filesystem::current_path(flag_arg_a);
                prev_dir = string(flag_arg_b);
            }

            if (*flags & FLAG_SKIP)
                skip_next = true;

            if (*flags & FLAG_SILENCE)
                echo_input = *flag_arg_a;

            if (*flags & FLAG_SET)
                setenv(flag_arg_a, flag_arg_b, true);

            if (*flags & FLAG_UNSET)
                unsetenv(flag_arg_a);

            if (*flags & FLAG_RELOAD) {
                load_path();
                load_prompt();
            }

            if (*flags & FLAG_ALIAS) {
                if (*flag_arg_b == '\0')
                    alias_map.erase(string(flag_arg_a));
                else
                    alias_map.emplace(string(flag_arg_a), string(flag_arg_b));
            }

            if (*flags & FLAG_WITH_S) {
                set_globals[string(flag_arg_a)] = string(std::getenv(flag_arg_a));
                setenv(flag_arg_a, flag_arg_b, true);
                with_var = true;
            }

            if (*flags & FLAG_WITH_U) {
                unset_globals.push_back(string(flag_arg_a));
                setenv(flag_arg_a, flag_arg_b, true);
                with_var = true;
            }

            if (*flags & FLAG_WITHOUT) {
                for (auto it = set_globals.begin(); it != set_globals.end(); ++it) {
                    setenv(it->first.c_str(), it->second.c_str(), true);
                }

                for (string name : unset_globals) {
                    unsetenv(name.c_str());
                }

                set_globals.clear();
                unset_globals.clear();
            }

            if (*flags & FLAG_RESUME) {
                kill(suspended_pid, SIGCONT);
                active_pid = suspended_pid;

                do {
                    wpid = waitpid(active_pid, &status, WUNTRACED);
                } while (!WIFEXITED(status) && !WIFSIGNALED(status));

                last_status = WEXITSTATUS(status);
                last_pid = active_pid;
                active_pid = 0;
            }

            if (*flags & FLAG_KILL) {
                pid_t this_pid = atoi(flag_arg_a);
                kill(this_pid, SIGTERM);
                sout() << "\e[1m" << "Killed PID " << this_pid << "\e[m" << std::endl;
            }

            if (*flags & FLAG_RUN) {
                string cmd = "wsh ";
                cmd += flag_arg_a;
                cmd_enter(string("wsh ") + flag_arg_a);
            }

            if (*flags & FLAG_SOURCE) {
                bool echo_before = echo_input;
                echo_input = false;
                execute_script(string(flag_arg_a));
                echo_input = echo_before;
            }

            munmap(flags, sizeof(unsigned int));
            munmap(flag_arg_a, sizeof(char) * 1024);
            munmap(flag_arg_b, sizeof(char) * 1024);

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
                if (subc_out.back() == '\n')
                    subc_out.pop_back();

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
    } else if (std::regex_match(esc_seq, match, std::regex("\\[(?:(\\d)~|(H))"))) {
        if (match[1] == "1" || match[1] == "7" || match[1] == "H") {
            if (input_idx > 0) {
                sout() << "\e[" << input_idx << "D";
                input_idx = 0;
            } else if (input_idx == INSERT_END && !cmd_str.empty()) {
                sout() << "\e[" << cmd_str.length() << "D";
                input_idx = 0;
            }
        } else if (match[1] == "3") {
            std::cout << "DELETE" << std::endl;
        } else if (match[1] == "4" || match[1] == "8") {
            if (input_idx != INSERT_END) {
                sout() << "\e[" << (cmd_str.length() - input_idx) << "C";
                input_idx = INSERT_END;
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
        string arg = vec_tokens[i];

        tokens[i] = (char*) malloc((arg.length() + 1) * sizeof(char));
        if (!tokens[i]) {
            perror("Error when allocating argument buffer");
            exit(EXIT_FAILURE);
        }

        strcpy(tokens[i], arg.c_str());
    }

    tokens[vec_tokens.size()] = nullptr;

    // This fixes some weird behavior with `which` for some reason
    sout().flush();

    return tokens;
}

void cmd_launch(std::vector<Command> commands, bool is_subcommand) {
    bool aliased = false;

    int c = 0;
    while (c < commands.size()) {
        Command cmd = commands[c];
        std::vector<string> args;

        if (skip_next) {
            skip_next = false;
            ++c;
            continue;
        }

        for (int i = 0; i < cmd.args.size(); ++i) {
            Argument arg = cmd.args[i];
            std::vector<Argument> new_args = expand_argument(arg);

            if (!new_args.empty()) {
                cmd.args.erase(cmd.args.begin() + i);

                for (int j = 0; j < new_args.size(); ++j) {
                    cmd.args.insert(cmd.args.begin() + i + j, new_args[j]);
                }
            }
        }

        for (int i = 0; i < cmd.args.size(); ++i) {
            Argument arg = cmd.args[i];
            string arg_str;

            for (auto arg_component : arg) {
                if (std::holds_alternative<string>(arg_component)) {
                    string val = std::get<string>(arg_component);

                    // Replace variables and expand tildes
                    if (val.length() >= 2 && val.front() == '\'' && val.back() == '\'') {
                        val = val.substr(1, val.length() - 2);
                    } else if (val.length() >= 2 && val.front() == '\"' && val.back() == '\"') {
                        val = val.substr(1, val.length() - 2);
                        val = replace_variables(val);
                        val = escape_string(val);
                    } else {
                        val = replace_variables(val);
                        val = escape_string(val);
                    }

                    arg_str += val;
                } else {
                    cmd_launch(std::get<CommandList>(arg_component), true);
                    arg_str += subc_out;
                }
            }

            // If we are looking at the command itself...
            if (i == 0) {
                // and the command has an alias...

                if (aliased) {
                    aliased = false;
                } else {
                    auto alias = alias_map.find(arg_str);
                    if (alias != alias_map.end()) {
                        // Substitute the alias
                        arg_str = alias->second;

                        // Tokenize the alias
                        std::vector<Command> alias_tokens = tokenize(arg_str);

                        // Erase the current command
                        commands.erase(commands.begin() + c);

                        // Replace it with the new alias
                        for (int j = 0; j < alias_tokens.size(); ++j) {
                            Command ncmd = alias_tokens[j];

                            if (j == alias_tokens.size() - 1 && cmd.args.size() > 1)
                                ncmd.args.insert(ncmd.args.end(), cmd.args.begin() + 1, cmd.args.end());

                            commands.insert(commands.begin() + c + j, ncmd);
                        }

                        aliased = true;
                        break;
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

        if (aliased)
            continue;

        bool needs_without = with_var;
        char **tokens = vec_to_charptr(args);
        cmd_execute(args.size(), tokens, is_subcommand, cmd.bg_command);
        needs_without &= !with_var;

        if (needs_without)
            cmd_enter("without");

        if (cmd.and_output) {
            skip_next = last_status != 0;
            and_output = false;
        }

        if (cmd.or_output) {
            skip_next = last_status == 0;
            or_output = false;
        }

        ++c;
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

    std::vector<Command> commands = tokenize(input);

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

    std::vector<Command> commands = tokenize(input);

    if (commands.empty())
        return;

    Command last_command = commands.back();
    Argument last_arg = last_command.args.back();
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
            case 0x12: // Ctrl+R
                sout() << "ctrl-r" << std::endl;
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
    in_esc_seq = false;
    sout() << "\e[J" << std::endl << prompt;
    getch_skip = true;
}

void sig_tstp_callback(int s) {
    if (active_pid != 0) {
        kill(active_pid, SIGSTOP);
        suspended_pid = active_pid;
        sout() << std::endl << "\e[1m" << "Suspended PID " << suspended_pid << "\e[m" << std::endl;
    }

    getch_skip = true;
}

void cleanup() {
    if (pid != 0)
        save_history();
}

int main(int argc, char **argv) {
    // Register our SIGINT handler
    struct sigaction sig_int_handler;
    sig_int_handler.sa_handler = sig_int_callback;
    sigemptyset(&sig_int_handler.sa_mask);
    sig_int_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_int_handler, NULL);

    // Register our SIGTSTP handler
    struct sigaction sig_tstp_handler;
    sig_tstp_handler.sa_handler = sig_tstp_callback;
    sigemptyset(&sig_tstp_handler.sa_mask);
    sig_tstp_handler.sa_flags = 0;
    sigaction(SIGTSTP, &sig_tstp_handler, NULL);

    std::atexit(cleanup);

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
        { "without",  builtins::bwithout },
        { "which",    builtins::bwhich },
        { "fg",       builtins::bfg },
        { "kill",     builtins::bkill },
        { "run",      builtins::brun },
        { "source",   builtins::bsource },
        { "history",  builtins::bhistory },
        { "debug",    builtins::bdebug }
    };

    if (argc < 2) {
        initialize_path(); // This actually initializes the PATH variable using /etc/paths
    }

    load_path(); // This reads the PATH variable to determine full paths to commands

    // Loading from script
    if (argc > 1) {
        execute_script(string(argv[1]));
        return 0;
    }

    load_rc(); // Here, edits could potentially be made to PATH...
    load_path(); // hence we reload the PATH
    load_prompt();
    load_history();

    sout() << prompt;

    while ((c = getch()) != EOF || getch_skip) {
        process_keypress(c);
        getch_skip = false;
    }

    return 0;
}


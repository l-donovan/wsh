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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using std::string;

string escape_string(string);
string parse_path_file(string);
void initialize_path();
void load_path();
void load_prompt();
void load_rc();
void process_keypress(char);
bool process_esc_seq();
void files_in_dir(string);
void execute_script(string);
void suggest(int);
void replace_variables(string&);
void cmd_enter(string, bool);
int cmd_execute(char**, bool, bool);
std::vector<string> cmd_tokenize(string);

char c;
unsigned int last_status = 0;
unsigned int history_idx = 0;
int input_idx = INSERT_END;
bool echo_input  = true;
bool in_esc_seq  = false;
bool skip_next   = false;
bool pipe_input  = false;
bool pipe_output = false;
bool or_output   = false;
bool and_output  = false;
bool bg_command   = false;
bool suggesting  = false;
bool control_c   = false;
bool subcommand  = false;
int pipefd_input[2];
int pipefd_output[2];
int pipefd_subc[2];
char subc_buf[1024];

struct passwd *pw = getpwuid(getuid());
const char *homedir = pw->pw_dir;

std::map<string, string> executable_map;
std::map<string, string> alias_map;
std::map<string, int (*)(char**)> builtins_map;
std::vector<string> history;
string esc_seq;
string cmd_str;
string prompt;
string subc_out;

// This horrible lookahead assertion makes sure we aren't inside of a string
// Because std::regex supports neither named backreferences nor relative numbered backreferences,
// the reference \4 is hardcoded and expects exactly one capturing group before this snippet
string not_in_quotes("(?=([^\"'\\\\]*(\\\\.|([\"'])([^\"'\\\\]*\\\\.)*[^\"'\\\\]*\\4))*[^\"']*$)");

void files_in_dir(string path) {
    struct stat info;

    if (dir_exists(path.c_str())) {
        for (const auto & entry : std::filesystem::directory_iterator(path))
            executable_map.emplace(entry.path().filename(), entry.path());
    } else {
        std::cerr << path << " is not a valid directory" << std::endl;
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

    string home_path(homedir);
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
        }

        if (is_subcommand) {
            close(pipefd_subc[READ_END]);

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
            }

            if (is_subcommand) {
                close(pipefd_subc[WRITE_END]);

                subc_out.clear();

                while (int nread = read(pipefd_subc[READ_END], subc_buf, 1023)) {
                    subc_buf[nread] = '\0';
                    subc_out += subc_buf;
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

string escape_string(string str) {
    string output = str;

    std::regex escapes("\\\\([\\\\\"'\\$adehHjlnrstT@uvVwW])");
    string::const_iterator search_start(str.cbegin());
    std::smatch match;
    int offset = 0;

    while (regex_search(search_start, str.cend(), match, escapes)) {
        std::string replacement;

        if (match[1] == "\\" || match[1] == "\"" || match[1] == "\'") {
            // Replacement of literal backslashes and quotes
            replacement = match[1];
        } else if (match[1] == "n") {
            replacement = "\n";
        } else if (match[1] == "r") {
            replacement = "\r";
        } else if (match[1] == "e") {
            replacement = "\e";
        } else if (match[1] == "a") {
            replacement = "\x07";
        } else if (match[1] == "h" || match[1] == "H") {
            char *name = (char*) malloc(_POSIX_HOST_NAME_MAX * sizeof(char));
            gethostname(name, _POSIX_HOST_NAME_MAX);
            replacement = string(name);
        } else if (match[1] == "u") {
            replacement = getlogin() ?: "";
        } else if (match[1] == "s" ) {
            replacement = SHELL_NAME;
        } else if (match[1] == "w") {
            replacement = std::filesystem::current_path();
        } else if (match[1] == "W") {
            replacement = std::filesystem::current_path().stem();
        } else if (match[1] == "$") {
            replacement = geteuid() == 0 ? "#" : "$";
        } else if (match[1] == "t") {
            std::time_t t = std::time(0);
            std::ostringstream os;
            os << std::put_time(std::localtime(&t), "%H:%M:%S");
            replacement = os.str();
        } else if (match[1] == "T") {
            std::time_t t = std::time(0);
            std::ostringstream os;
            os << std::put_time(std::localtime(&t), "%I:%M:%S");
            replacement = os.str();
        } else if (match[1] == "@") {
            std::time_t t = std::time(0);
            std::ostringstream os;
            os << std::put_time(std::localtime(&t), "%I:%M:%S %p");
            replacement = os.str();
        } else if (match[1] == "d") {
            std::time_t t = std::time(0);
            std::ostringstream os;
            os << std::put_time(std::localtime(&t), "%a %b %d");
            replacement = os.str();
        } else if (match[1] == "v") {
            std::ostringstream os;
            os << VERSION_MAJOR << "." << VERSION_MINOR;
            replacement = os.str();
        } else if (match[1] == "V") {
            std::ostringstream os;
            os << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH;
            replacement = os.str();
        }

        output.replace(match.position() + offset, match.length(), replacement);
        offset += match.position() + replacement.length();
        search_start = match.suffix().first;
    }

    return output;
}

int whitespace_separator(string input) {
    bool inside_squotes = false;
    bool inside_dquotes = false;
    bool whitespace_found = false;
    bool escaping = false;
    int whitespace_idx = NO_MORE_WHITESPACE;

    for (int i = 0; i < input.size(); ++i) {
        if (whitespace_found) {
            if (input[i] == ' ')
                whitespace_idx = i;
            else
                return whitespace_idx;
        } else if (input[i] == ' ' && !inside_squotes && !inside_dquotes) {
            whitespace_found = true;
            whitespace_idx = i;
        } else if (escaping) {
            escaping = false;
        } else if (inside_squotes) {
            if (input[i] == '\'')
                inside_squotes = false;
            else if (input[i] == '\\')
                escaping = true;
        } else if (inside_dquotes) {
            if (input[i] == '\"')
                inside_dquotes = false;
            else if (input[i] == '\\')
                escaping = true;
        } else {
            if (input[i] == '\'')
                inside_squotes = true;
            else if (input[i] == '\"')
                inside_dquotes = true;
        }
    }

    if (inside_squotes)
        return INSIDE_SINGLE_QUOTES;
    else if (inside_dquotes)
        return INSIDE_DOUBLE_QUOTES;

    return whitespace_idx;
}

std::vector<string> cmd_tokenize(string input) {
    string arg;
    input += ' ';
    unsigned int argc = 0;

    std::vector<string> tokens;

    int ws;
    while ((ws = whitespace_separator(input)) >= 0) {
        arg = input.substr(0, ws);
        input = input.substr(ws + 1, string::npos);

        trim(arg);

        if (arg.empty()) {
            continue;
        }

        if (arg[0] == '"' && arg[arg.length() - 1] == '"') {
            // Trim the double quotes off of a string argument and evaluate substitutions
            arg = arg.substr(1, arg.length() - 2);
            arg = escape_string(arg);
        } else if (arg[0] == '\'' && arg[arg.length() - 1] == '\'') {
            // Trim the single quotes off of a string argument
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

        ++argc;

        tokens.push_back(arg);
    }

    if (ws == INSIDE_SINGLE_QUOTES || ws == INSIDE_DOUBLE_QUOTES) {
        std::cerr << "Dropped argument inside unclosed quotes" << std::endl;
    }

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

    string suggestion = history.at(history_idx);

    // Move back to right after the prompt, then clear the line and print our suggestion
    if (!cmd_str.empty()) {
        std::cout << "\e[" << cmd_str.length() << "D";
        std::cout << "\e[K";
    }

    cmd_str = suggestion;
    std::cout << cmd_str;

    suggesting = true;
}

bool process_esc_seq() {
    std::smatch match;

    if (std::regex_match(esc_seq, match, std::regex("\\[([ABCD])"))) {
        if (match[1] == "A") { // UP
            suggest(DIRECTION_UP);
        } else if (match[1] == "B") { // DOWN
            suggest(DIRECTION_DOWN);
        } else if (match[1] == "C") { // RIGHT
            if (input_idx != INSERT_END) {
                if (++input_idx > cmd_str.length() - 1)
                    input_idx = INSERT_END;

                if (echo_input)
                    std::cout << "\e[C";
            }
        } else if (match[1] == "D") { // LEFT
            if (input_idx != 0)
                std::cout << "\e[D";

            if (input_idx == INSERT_END)
                input_idx = cmd_str.length() - 1;
            else if (input_idx > 0)
                --input_idx;
        }

        return true;
    }

    return false;
}

void replace_variables(string &input) {
    string output(input);
    int offset = 0;

    std::smatch match;
    std::regex variable("\\{(\\w+)\\}");
    string::const_iterator search_start(input.cbegin());

    while (regex_search(search_start, input.cend(), match, variable)) {
        const char* c_var = std::getenv(match[1].str().c_str());
        string var(c_var ?: "");
        output.replace(match.position() + offset, match.length(), var);
        offset += match.position() + var.length();
        search_start = match.suffix().first;
    }

    std::regex expr("\\{\\[(.+?)\\]\\}");
    search_start = input.cbegin();
    offset = 0;

    while (regex_search(search_start, input.cend(), match, expr)) {
        cmd_enter(match[1], true);
        output.replace(match.position() + offset, match.length(), subc_out);
        offset += match.position() + subc_out.length();
        search_start = match.suffix().first;
    }

    std::regex tilde("~");
    search_start = input.cbegin();
    string home(homedir);
    offset = 0;

    while (regex_search(search_start, input.cend(), match, tilde)) {
        output.replace(match.position() + offset, match.length(), home);
        offset += match.position() + home.length();
        search_start = match.suffix().first;
    }

    input = output;
}

void cmd_enter(string input, bool is_subcommand) {
    // No point in running an empty line
    if (input.empty())
        return;

    // Comments are easy enough to handle
    if (input[0] == '#')
        return;

    // We don't want the subcommand to affect the parent command
    bool _or_output = or_output;
    bool _pipe_output = pipe_output;
    bool _and_output = and_output;
    bool _bg_command = bg_command;

    // Commands are delimited by semicolons, double pipes, one pipe, double ampersands, or one ampersand
    std::smatch match;
    std::regex cmd_separator("(;|\\|\\||\\||&&|&)" + not_in_quotes);
    input += ';';
    string cmd;
    string::const_iterator search_start(input.cbegin());

    while (regex_search(search_start, input.cend(), match, cmd_separator)) {
        search_start = match.suffix().first;

        if (skip_next) {
            skip_next = false;
            continue;
        }

        or_output = match.str() == "||";
        pipe_output = match.str() == "|";
        and_output = match.str() == "&&";
        bg_command = match.str() == "&"; // TODO This will probably take a bit of restructuring

        cmd = match.prefix();
        trim(cmd);

        // No point in running an empty command
        if (cmd.empty())
            continue;

        replace_variables(cmd);

        // Lookup our command
        std::vector<string> vec_tokens = cmd_tokenize(cmd);

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

        // Find if this is a builtin command
        auto it = builtins_map.find(tokens[0]);
        if (it != builtins_map.end()) {
            auto builtin_fn = it->second;
            int result = builtin_fn(tokens);
            if (result >= 0) {
                last_status = result;
            } else {
                exit(-result - 1);
            }
        } else {
            cmd_execute(tokens, is_subcommand, bg_command);
        }

        if (and_output) {
            skip_next = last_status != 0;
            and_output = false;
        }

        if (or_output) {
            skip_next = last_status == 0;
            or_output = false;
        }
    }

    if (is_subcommand) {
        or_output = _or_output;
        pipe_output = _pipe_output;
        and_output = _and_output;
        bg_command = _bg_command;
    }
}

void cmd_complete(string input) {
    int argc = 0;
    int ws;

    // No point in completing empty lines or comments
    if (input.empty() || input[0] == '#')
        return;

    // Commands are delimited by semicolons, double pipes, one pipe, double ampersands, or one ampersand
    std::smatch match;
    std::regex cmd_separator("(;|\\|\\||\\||&&|&)" + not_in_quotes);
    input += ';';
    string cmd;
    string::const_iterator search_start(input.cbegin());

    while (regex_search(search_start, input.cend(), match, cmd_separator)) {
        search_start = match.suffix().first;
        cmd = match.prefix();
        trim(cmd);
    }

    if (cmd.empty())
        return;

    while ((ws = whitespace_separator(cmd)) >= 0) {
        cmd = cmd.substr(ws + 1, string::npos);
        ++argc;
    }

    if (ws == INSIDE_SINGLE_QUOTES || ws == INSIDE_DOUBLE_QUOTES)
        cmd = cmd.substr(1, string::npos);

    if (cmd.empty())
        return;

    std::cout << std::endl;

    if (argc == 0) {
        // Suggesting a command
        auto matches = filter_prefix(executable_map, cmd);
        for (string match : matches) {
            std::cout << match << std::endl;
        }
    } else {
        // Suggesting an argument
    }
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
                        if (echo_input)
                            std::cout << "\x08 \x08";
                    } else if (input_idx != 0) {
                        --input_idx;
                        cmd_str.erase(input_idx, 1);
                        if (echo_input) {
                            std::cout << "\x08\e[K"; // Backspace and clear the rest of the line
                            std::cout << cmd_str.substr(input_idx, string::npos); // Print the second part of the command
                            std::cout << "\e[" << (cmd_str.length() - input_idx) << "D"; // Move the cursor back
                        }
                    }
                }
                break;
            case 0x0a: // NEWLINE
                if (echo_input)
                    std::cout << std::endl;
                cmd_enter(cmd_str, false);
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
                if (echo_input)
                    std::cout << prompt;
                break;
            case 0x09: // TAB
                cmd_complete(cmd_str);
                break;
            case EOF:
                break;
            default:
                if (input_idx == INSERT_END) {
                    cmd_str += ch;
                    if (echo_input)
                        std::cout << ch;
                } else {
                    cmd_str.insert(input_idx, 1, ch);
                    if (echo_input) {
                        std::cout << cmd_str.substr(input_idx, string::npos);
                        std::cout << "\e[" << (cmd_str.length() - 1 - input_idx) << "D";
                    }
                    ++input_idx;
                }

                break;
        }
    }
}

void sig_int_callback(int s) {
    cmd_str.clear();
    if (echo_input)
        std::cout << std::endl << prompt;
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
        { "equals",   builtins::bequals }
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

    if (echo_input)
        std::cout << prompt;

    while ((c = getch()) != EOF || control_c) {
        process_keypress(c);
        control_c = false;
    }

    return 0;
}


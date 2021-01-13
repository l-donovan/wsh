#include "config.h"
#include "control.h"
#include "global.h"
#include "utils.h"

#include <cstdio>
#include <filesystem>
#include <limits.h>
#include <map>
#include <regex>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

using std::string;

static struct termios old, current;
static const struct command empty_command;

// Initialize new terminal i/o settings
void initTermios() {
    tcgetattr(0, &old); // grab old terminal i/o settings
    current = old; // make new settings same as old settings
    current.c_lflag &= ~ICANON; // disable buffered i/o
    current.c_lflag &= ~ECHO; // set no echo mode
    tcsetattr(0, TCSANOW, &current); // use these new terminal i/o settings now
}

// Restore old terminal i/o settings
void resetTermios(void) {
    tcsetattr(0, TCSANOW, &old);
}

// Read 1 character - echo defines echo mode
char getch() {
    char ch;
    initTermios();
    ch = getchar();
    resetTermios();
    return ch;
}

// Uses a DSR escape to retrieve cursor row and column
// Beware: The row and column are 1-indexed!
void get_cursor_pos(int *row, int *col) {
    string row_str;
    string col_str;
    char ch;

    std::cout << "\e[6n";

    getch(); // ESC
    getch(); // [

    while ((ch = getch()) != ';')
        row_str += ch;

    while ((ch = getch()) != 'R')
        col_str += ch;

    *row = std::stoi(row_str);
    *col = std::stoi(col_str);
}

void trim(string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));

    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

bool dir_exists(const string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0) && (buffer.st_mode & S_IFDIR);
}

bool file_exists(const string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0) && (buffer.st_mode & S_IFREG);
}

bool any_exists(const string& name) {
    struct stat buffer;
    return stat(name.c_str(), &buffer) == 0;
}

std::vector<string> filter_prefix(const std::map<string, string>& map, const string& search_for) {
    std::vector<string> output;

    auto it = map.begin();
    while (it != map.end()) {
        string name = it->first;
        if (name.rfind(search_for, 0) == 0)
            output.push_back(name);
        ++it;
    }

    return output;
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

void print_commands(std::vector<command> commands) {
    for (int i = 0; i < commands.size(); ++i) {
        std::cout << "New command" << std::endl;
        auto cmd = commands[i];
        for (int j = 0; j < cmd.args.size(); ++j) {
            auto arg = cmd.args[j];
            if (std::holds_alternative<string>(arg)) {
                std::cout << std::get<string>(arg) << std::endl;
            } else if (std::holds_alternative<CommandList>(arg)) {
                std::cout << "Subcommand" << std::endl;
                print_commands(std::get<CommandList>(arg));
            }
        }
    }
}

std::vector<command> tokenize(string input) {
    bool inside_quotes[input.length() + 1];
    bool inside_squotes = false;
    bool inside_dquotes = false;
    bool inside_backticks = false;
    bool escaping = false;
    int offset = 0;
    int pos = 0;
    int last = 0;
    std::vector<command> commands;
    string token;
    std::smatch match;

    inside_quotes[input.length()] = false;

    for (int i = 0; i < input.size(); ++i) {
        inside_quotes[i] = inside_squotes || inside_dquotes || inside_backticks;

        if (escaping) {
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
        } else if (inside_backticks) {
            if (input[i] == '`')
                inside_backticks = false;
            else if (input[i] == '\\')
                escaping = true;
        } else {
            if (input[i] == '\'')
                inside_squotes = true;
            else if (input[i] == '\"')
                inside_dquotes = true;
            else if (input[i] == '`')
                inside_backticks = true;
        }
    }

    struct command cmd = empty_command;
    std::vector<Value> args;
    std::regex token_separator("((?:\\s*(?:;|\\|\\||\\||&&|&)\\s*)|\\s+)");
    input += ';';
    string::const_iterator search_start(input.cbegin());
    string text;
    string separator;

    while (regex_search(search_start, input.cend(), match, token_separator)) {
        search_start = match.suffix().first;
        pos = match.position() + offset;
        offset += match.position() + match.length();

        if (inside_quotes[pos])
            continue;

        text = input.substr(last, pos - last);
        separator = match[1];
        trim(separator);
        last = pos + match.length();

        if (text.empty())
            continue;

        if (text.front() == '\'' && text.back() == '\'') {
            // Single-quoted string
            text = text.substr(1, text.length() - 2);
            args.push_back(text);
        } else if (text.front() == '"' && text.back() == '"') {
            // Double-quoted string
            text = text.substr(1, text.length() - 2);
            text = escape_string(text);
            args.push_back(text);
        } else if (text.front() == '`' && text.back() == '`') {
            // Subcommand
            text = text.substr(1, text.length() - 2);
            args.push_back(tokenize(text));
        } else {
            // Plain un-quoted text
            args.push_back(text);
        }

        if (separator == ";") {
            // Nothing special, sequential command separator
        } else if (separator == "||") {
            // Conditional OR separator
            cmd.or_output = true;
        } else if (separator == "|") {
            // Pipe output
            cmd.pipe_output = true;
        } else if (separator == "&&") {
            // Conditional AND separator
            cmd.and_output = true;
        } else if (separator == "&") {
            // Send to background
            cmd.bg_command = true;
        } else {
            // Only a whitespace separator, our command isn't done yet
            continue;
        }

        cmd.args = args;
        commands.push_back(cmd);
        cmd = empty_command;
        args.clear();
    }

    return commands;
}

std::vector<string> complete_path(string path) {
    auto idx = path.find_last_of('/');
    string head;
    std::vector<string> paths;

    if (idx != std::string::npos) {
        head = path.substr(0, idx);
    } else {
        head = "./";
        path = head + path;
    }

    for (const auto & entry : std::filesystem::directory_iterator(head)) {
        string option = entry.path().string();
        if (option.rfind(path, 0) == 0) {
            paths.push_back(option);
        }
    }

    return paths;
}

std::ostream& operator<<(std::ostream& out, utf8c ch) {
    for (int i = 0; i < ch.size; ++i) {
        out << static_cast<char>((ch.bytes >> (i * 8)) & 0b11111111);
    }

    return out;
}

std::istream& operator>>(std::istream& in, utf8c ch) {
    return in;
}

bool operator==(utf8c a, utf8c b) {
    return a.bytes == b.bytes;
}

utf8c getuch() {
    uint32_t ch = getch();
    uint8_t nbytes = 0;
    uint8_t i = 7;

    while (i > 0 && (ch & (1 << i--)) != 0)
        ++nbytes;

    nbytes = nbytes ?: 1;

    struct utf8c out = { .size = nbytes, .bytes = ch & 0b11111111 };

    for (int i = 1; i < nbytes; ++i) {
        ch = getch();
        out.bytes |= (ch & 0b11111111) << (i * 8);
    }

    return out;
}

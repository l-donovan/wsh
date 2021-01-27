#pragma once

#include <iostream>
#include <map>
#include <string>
#include <variant>
#include <vector>

struct Command;

struct utf8c {
    uint8_t size;
    uint32_t bytes;
};

template <typename T> struct recursive_wrapper {
  // construct from an existing object
  recursive_wrapper(T t_) { t.emplace_back(std::move(t_)); }
  // cast back to wrapped type
  operator const T &() const { return t.front(); }
  // store the value
  std::vector<T> t;
};

using CommandList = recursive_wrapper<std::vector<Command>>;
using Value = std::variant<std::monostate, std::string, CommandList>;
using Argument = std::vector<Value>;

char getch(void);
void trim(std::string&);
bool dir_exists(const std::string&);
bool file_exists(const std::string&);
bool any_exists(const std::string&);
std::vector<std::string> filter_prefix(const std::map<std::string, std::string>&, const std::string&);
int token_separator(std::string);
std::vector<Command> tokenize(std::string);
std::string escape_string(std::string);
std::string replace_variables(std::string&);
std::vector<std::string> expand_brackets(std::string);
std::vector<Argument> expand_argument(Argument);
void print_commands(std::vector<Command> commands);
std::vector<std::string> complete_path(std::string path);
void get_cursor_pos(int*, int*);
utf8c getuch();

std::ostream& operator<<(std::ostream& out, utf8c ch);
std::istream& operator>>(std::istream& in, utf8c ch);
bool operator==(utf8c a, utf8c b);

class NullStream : public std::ostream {
public:
    NullStream() : std::ostream(nullptr) {}
    NullStream(const NullStream &) : std::ostream(nullptr) {}
};

template <class T>
const NullStream &operator<<(NullStream &&os, const T &value) {
    return os;
}

struct Command {
    std::vector<Argument> args;
    bool or_output = false;
    bool pipe_output = false;
    bool and_output = false;
    bool bg_command = false;
};

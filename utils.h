#pragma once

#include <iostream>
#include <map>
#include <string>
#include <variant>
#include <vector>

struct command;

char getch(void);
char getche(void);
void trim(std::string&);
bool dir_exists(const std::string&);
bool file_exists(const std::string&);
bool any_exists(const std::string&);
std::vector<std::string> filter_prefix(const std::map<std::string, std::string>&, const std::string&);
int token_separator(std::string);
std::vector<command> tokenize(std::string);
std::string escape_string(std::string);
void print_commands(std::vector<command> commands);
std::vector<std::string> complete_path(std::string path);

class NullStream : public std::ostream {
public:
    NullStream() : std::ostream(nullptr) {}
    NullStream(const NullStream &) : std::ostream(nullptr) {}
};

template <class T>
const NullStream &operator<<(NullStream &&os, const T &value) {
    return os;
}

template <typename T> struct recursive_wrapper {
  // construct from an existing object
  recursive_wrapper(T t_) { t.emplace_back(std::move(t_)); }
  // cast back to wrapped type
  operator const T &() const { return t.front(); }
  // store the value
  std::vector<T> t;
};

using CommandList = recursive_wrapper<std::vector<command>>;
using Value = std::variant<std::monostate, std::string, CommandList>;

struct command {
    std::vector<Value> args;
    bool or_output = false;
    bool pipe_output = false;
    bool and_output = false;
    bool bg_command = false;
};

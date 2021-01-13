#pragma once

#include <map>
#include <string>

extern unsigned int last_status;
extern bool skip_next;
extern bool echo_input;
extern std::map<std::string, std::string> alias_map;
extern std::map<std::string, int (*)(int, char**)> builtins_map;


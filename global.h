#pragma once

#include <map>
#include <string>
#include <vector>

extern unsigned int last_status;
extern std::string prev_dir;
extern bool skip_next;
extern bool echo_input;
extern bool with_var;
extern std::map<std::string, std::string> executable_map;
extern std::map<std::string, std::string> alias_map;
extern std::map<std::string, int (*)(int, char**, unsigned int*, char*, char*)> builtins_map;
extern std::vector<std::string> history;
extern std::vector<pid_t> suspended_pids;


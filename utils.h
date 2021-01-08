#pragma once

#include <map>
#include <string>
#include <vector>


char getch(void);
char getche(void);
void trim(std::string&);
bool dir_exists(const std::string&);
bool file_exists(const std::string&);
bool any_exists(const std::string&);
std::vector<std::string> filter_prefix(const std::map<std::string, std::string>&, const std::string&);

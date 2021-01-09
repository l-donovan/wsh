#pragma once

#include <iostream>
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

class NullStream : public std::ostream {
public:
    NullStream() : std::ostream(nullptr) {}
    NullStream(const NullStream &) : std::ostream(nullptr) {}
};

template <class T>
const NullStream &operator<<(NullStream &&os, const T &value) {
    return os;
}


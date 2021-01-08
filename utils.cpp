#include "global.h"
#include "utils.h"

#include <map>
#include <regex>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <vector>

using std::string;

static struct termios old, current;

/* Initialize new terminal i/o settings */
void initTermios(int echo) {
  tcgetattr(0, &old); /* grab old terminal i/o settings */
  current = old; /* make new settings same as old settings */
  current.c_lflag &= ~ICANON; /* disable buffered i/o */
  if (echo) {
      current.c_lflag |= ECHO; /* set echo mode */
  } else {
      current.c_lflag &= ~ECHO; /* set no echo mode */
  }
  tcsetattr(0, TCSANOW, &current); /* use these new terminal i/o settings now */
}

/* Restore old terminal i/o settings */
void resetTermios(void) {
    tcsetattr(0, TCSANOW, &old);
}

/* Read 1 character - echo defines echo mode */
char getch_(int echo) {
    char ch;
    initTermios(echo);
    ch = getchar();
    resetTermios();
    return ch;
}

/* Read 1 character without echo */
char getch(void) {
    return getch_(0);
}

/* Read 1 character with echo */
char getche(void) {
    return getch_(1);
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


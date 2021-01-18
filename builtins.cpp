#include "builtins.h"
#include "config.h"
#include "control.h"
#include "global.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using std::string;

string twd;
string lwd;

// Defined in main.cpp
void load_path();
void load_prompt();
bool dir_exists(const std::string&);
bool file_exists(const std::string&);
bool any_exists(const std::string&);

std::map<string, string> set_globals;
std::vector<string> unset_globals;

namespace builtins {
    int bexit(int argc, char **argv) {
        std::cout << "Goodbye!" << std::endl;

        if (argc < 2)
            return CODE_EXIT_OK;
        else
            return -(atoi(argv[1]) + 1);
    }

    int bcd(int argc, char **argv) {
        if (argc < 2)
            return CODE_FAIL;

        twd = std::filesystem::current_path();

        if (strcmp(argv[1], "-") == 0) {
            std::filesystem::current_path(lwd);
            std::cout << lwd << std::endl;
        } else {
            std::filesystem::current_path(argv[1]);
        }

        lwd = twd;

        return CODE_CONTINUE;
    }

    int babout(int argc, char **argv) {
        std::cout << "WebShell (wsh) ";
        std::cout << "v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << std::endl;
        std::cout << "Created by Luke Donovan" << std::endl << std::endl;
        std::cout << "List of builtin commands:" << std::endl;

        for (auto it = builtins_map.begin(); it != builtins_map.end(); ++it) {
            std::cout << "  " << it->first << std::endl;
        }

        return CODE_CONTINUE;
    }

    int band(int argc, char **argv) {
        if (last_status > 0)
            skip_next = true;

        return last_status;
    }

    int bor(int argc, char **argv) {
        if (last_status == 0)
            skip_next = true;

        return last_status;
    }

    int bredirect(int argc, char **argv) {
        return CODE_CONTINUE;
    }

    int bsilence(int argc, char **argv) {
        if (argc < 2)
            return CODE_FAIL;

        echo_input = strcmp(argv[1], "true");

        return CODE_CONTINUE;
    }

    int bset(int argc, char **argv) {
        if (argc < 3)
            return CODE_FAIL;

        setenv(argv[1], argv[2], true);

        return CODE_CONTINUE;
    }

    int bunset(int argc, char **argv) {
        if (argc < 2)
            return CODE_FAIL;

        unsetenv(argv[1]);

        return CODE_CONTINUE;
    }

    int bladd(int argc, char **argv) {
        if (argc < 3)
            return CODE_FAIL;

        const char* c_var = std::getenv(argv[1]);

        std::string var(c_var ?: "");
        std::string result(argv[2]);

        result += var;

        setenv(argv[1], result.c_str(), true);

        return CODE_CONTINUE;
    }

    int bradd(int argc, char **argv) {
        if (argc < 3)
            return CODE_FAIL;

        const char* c_var = std::getenv(argv[1]);

        std::string var(c_var ?: "");
        std::string result(argv[2]);

        var += result;

        setenv(argv[1], var.c_str(), true);

        return CODE_CONTINUE;
    }

    int breload(int argc, char **argv) {
        load_path();
        load_prompt();

        return CODE_CONTINUE;
    }

    int balias(int argc, char **argv) {
        if (argc == 1) {
            std::cout << "Aliases:" << std::endl;

            for (auto it = alias_map.begin(); it != alias_map.end(); ++it) {
                std::cout << "  " << it->first << " -> " << it->second << std::endl;
            }
        } else if (argc == 2) {
            std::string name(argv[1]);
            std::cout << alias_map[name] << std::endl;
        } else {
            std::string name(argv[1]);
            std::string val(argv[2]);

            alias_map.emplace(name, val);
        }

        return CODE_CONTINUE;
    }

    int bunalias(int argc, char **argv) {
        if (argc < 2)
            return CODE_FAIL;

        std::string name(argv[1]);

        alias_map.erase(name);

        return CODE_CONTINUE;
    }

    int bexists(int argc, char **argv) {
        if (argc < 3)
            return CODE_FAIL;

        if (strcmp(argv[1], "file") == 0)
            return !file_exists(argv[2]);

        if (strcmp(argv[1], "dir") == 0)
            return !dir_exists(argv[2]);

        return !any_exists(argv[1]);
    }

    int bequals(int argc, char **argv) {
        if (argc < 3)
            return CODE_FAIL;

        return strcmp(argv[1], argv[2]) != 0;
    }

    int bwith(int argc, char **argv) {
        if (argc < 2)
            return CODE_FAIL;

        for (int i = 1; i < (argc - 1); i += 2) {
            string name(argv[i]);

            const char* c_var = std::getenv(argv[i]);

            if (c_var == nullptr)
                unset_globals.push_back(name);
            else
                set_globals[name] = string(c_var);

            setenv(argv[i], argv[i + 1], true);
        }

        with_var = true;

        return CODE_CONTINUE;
    }

    int bwithout(int argc, char **argv) {
        for (auto it = set_globals.begin(); it != set_globals.end(); ++it) {
            setenv(it->first.c_str(), it->second.c_str(), true);
        }

        for (string name : unset_globals) {
            unsetenv(name.c_str());
        }

        set_globals.clear();
        unset_globals.clear();

        return CODE_CONTINUE;
    }
}

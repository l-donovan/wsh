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

using std::string;

string twd;
string lwd;

// Defined in main.cpp
void load_path();
void load_prompt();
bool dir_exists(const std::string&);
bool file_exists(const std::string&);
bool any_exists(const std::string&);

namespace builtins {
    int bexit(int argc, char **argv) {
        std::cout << "Goodbye!" << std::endl;

        if (argv[1] == nullptr)
            return CODE_EXIT_OK;
        else
            return -(atoi(argv[1]) + 1);
    }

    int bcd(int argc, char **argv) {
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

        auto it = builtins_map.begin();
        while (it != builtins_map.end()) {
            std::cout << "  " << it->first << std::endl;
            ++it;
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
        echo_input = strcmp(argv[1], "true");

        return CODE_CONTINUE;
    }

    int bset(int argc, char **argv) {
        setenv(argv[1], argv[2], true);

        return CODE_CONTINUE;
    }

    int bunset(int argc, char **argv) {
        unsetenv(argv[1]);

        return CODE_CONTINUE;
    }

    int bladd(int argc, char **argv) {
        const char* c_var = std::getenv(argv[1]);

        std::string var(c_var ?: "");
        std::string result(argv[2]);

        result += var;

        setenv(argv[1], result.c_str(), true);

        return CODE_CONTINUE;
    }

    int bradd(int argc, char **argv) {
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

            auto it = alias_map.begin();
            while (it != alias_map.end()) {
                std::cout << "  " << it->first << " -> " << it->second << std::endl;
                ++it;
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
        std::string name(argv[1]);

        alias_map.erase(name);

        return CODE_CONTINUE;
    }

    int bexists(int argc, char **argv) {
        if (strcmp(argv[1], "file") == 0)
            return !file_exists(argv[2]);

        if (strcmp(argv[1], "dir") == 0)
            return !dir_exists(argv[2]);

        return !any_exists(argv[1]);
    }

    int bequals(int argc, char **argv) {
        return strcmp(argv[1], argv[2]) != 0;
    }
}

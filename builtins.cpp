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
    int bexit(char **args) {
        std::cout << "Goodbye!" << std::endl;

        if (args[1] == nullptr)
            return CODE_EXIT_OK;
        else
            return -(atoi(args[1]) + 1);
    }

    int bcd(char **args) {
        twd = std::filesystem::current_path();

        if (strcmp(args[1], "-") == 0) {
            std::filesystem::current_path(lwd);
            std::cout << lwd << std::endl;
        } else {
            std::filesystem::current_path(args[1]);
        }

        lwd = twd;

        return CODE_CONTINUE;
    }

    int babout(char **args) {
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

    int band(char **args) {
        if (last_status > 0)
            skip_next = true;

        return last_status;
    }

    int bor(char **args) {
        if (last_status == 0)
            skip_next = true;

        return last_status;
    }

    int bredirect(char **args) {
        return CODE_CONTINUE;
    }

    int bsilence(char **args) {
        echo_input = strcmp(args[1], "true");

        return CODE_CONTINUE;
    }

    int bset(char **args) {
        setenv(args[1], args[2], true);

        return CODE_CONTINUE;
    }

    int bunset(char **args) {
        unsetenv(args[1]);

        return CODE_CONTINUE;
    }

    int bladd(char **args) {
        const char* c_var = std::getenv(args[1]);

        std::string var(c_var ?: "");
        std::string result(args[2]);

        result += var;

        setenv(args[1], result.c_str(), true);

        return CODE_CONTINUE;
    }

    int bradd(char **args) {
        const char* c_var = std::getenv(args[1]);

        std::string var(c_var ?: "");
        std::string result(args[2]);

        var += result;

        setenv(args[1], var.c_str(), true);

        return CODE_CONTINUE;
    }

    int breload(char **args) {
        load_path();
        load_prompt();

        return CODE_CONTINUE;
    }

    int balias(char **args) {
        std::string name(args[1]);
        std::string val(args[2]);

        alias_map.emplace(name, val);

        return CODE_CONTINUE;
    }

    int bunalias(char **args) {
        std::string name(args[1]);

        alias_map.erase(name);

        return CODE_CONTINUE;
    }

    int bexists(char **args) {
        if (strcmp(args[1], "file") == 0)
            return !file_exists(args[2]);

        if (strcmp(args[1], "dir") == 0)
            return !dir_exists(args[2]);

        return !any_exists(args[1]);
    }

    int bequals(char **args) {
        return strcmp(args[1], args[2]) != 0;
    }
}

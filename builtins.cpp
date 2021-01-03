#include "builtins.h"
#include "config.h"
#include "control.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

char lwd[PATH_SIZE];
char twd[PATH_SIZE];

namespace builtins {
    int bexit(char **args) {
        std::cout << "Goodbye!" << std::endl;

        if (args[1] == nullptr) {
            return CODE_EXIT_OK;
        } else {
            return atoi(args[1]) + 1;
        }
    }

    int bcd(char **args) {
        getcwd(twd, PATH_SIZE);

        if (strcmp(args[1], "-") == 0) {
            std::cout << lwd << std::endl;
            chdir(lwd);
        } else {
            chdir(args[1]);
        }

        strcpy(lwd, twd);

        return CODE_CONTINUE;
    }

    int babout(char **args) {
        std::cout << "Welcome to WebShell (wsh)!" << std::endl;
        std::cout << "Created by Luke Donovan" << std::endl;

        return CODE_CONTINUE;
    }

    int band(char **args) {
        if (last_status > 0) {
            skip_next = true;
        }

        return CODE_CONTINUE;
    }

    int bor(char **args) {
        if (last_status == 0) {
            skip_next = true;
        }

        return CODE_CONTINUE;
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

    int breload(char **args) {
        // TODO Implement PATH reloading

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
}

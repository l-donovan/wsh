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
    int exit(char **args) {
        if (args[1] == nullptr) {
            return CODE_EXIT_OK;
        } else {
            return atoi(args[1]) + 1;
        }
    }

    int cd(char **args) {
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

    int about(char **args) {
        std::cout << "Welcome to WebShell (wsh)!" << std::endl;
        std::cout << "Created by Luke Donovan" << std::endl;

        return CODE_CONTINUE;
    }

    int cand(char **args) {
        if (last_status > 0) {
            skip_next = true;
        }

        return CODE_CONTINUE;
    }

    int cor(char **args) {
        if (last_status == 0) {
            skip_next = true;
        }

        return CODE_CONTINUE;
    }

    int out(char **args) {
        std::cout << args[0] << std::endl;
        std::cout << args[1] << std::endl;
        std::cout << args[2] << std::endl;

        std::cout << last_status << std::endl;

        return CODE_CONTINUE;
    }

    int err(char **args) {
        return CODE_CONTINUE;
    }

    int in(char **args) {
        return CODE_CONTINUE;
    }
}

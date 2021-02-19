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

// Defined in main.cpp
void load_path();
void load_prompt();
bool dir_exists(const string&);
bool file_exists(const string&);
bool any_exists(const string&);

namespace builtins {
    int bexit(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        std::cout << "Goodbye!" << std::endl;

        *flags |= FLAG_EXIT;

        if (argc < 2)
            return 0;
        else
            return atoi(argv[1]);
    }

    int bcd(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 2)
            return CODE_FAIL;

        *flags |= FLAG_CD;

        string last_dir = std::filesystem::current_path();

        if (strcmp(argv[1], "-") == 0) {
            std::cout << prev_dir << std::endl;
            strcpy(flag_arg_a, prev_dir.c_str());
        } else {
            strcpy(flag_arg_a, argv[1]);
        }

        strcpy(flag_arg_b, last_dir.c_str());

        return CODE_CONTINUE;
    }

    int babout(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        std::cout << "WebShell (\e[38;5;166mw\e[38;5;167ms\e[38;5;168mh\e[m) ";
        std::cout << "v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << std::endl;
        std::cout << "Created by Luke Donovan" << std::endl << std::endl;
        std::cout << "List of builtin commands:" << std::endl;

        for (auto it = builtins_map.begin(); it != builtins_map.end(); ++it) {
            std::cout << "  " << it->first << std::endl;
        }

        return CODE_CONTINUE;
    }

    int band(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (last_status != 0)
            *flags |= FLAG_SKIP;

        return last_status;
    }

    int bor(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (last_status == 0)
            *flags |= FLAG_SKIP;

        return last_status;
    }

    int bredirect(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        return CODE_CONTINUE;
    }

    int bsilence(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 2)
            return CODE_FAIL;

        *flags |= FLAG_SILENCE;
        *flag_arg_a = strcmp(argv[1], "true") != 0;

        return CODE_CONTINUE;
    }

    int bset(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 3)
            return CODE_FAIL;

        *flags |= FLAG_SET;
        strcpy(flag_arg_a, argv[1]);
        strcpy(flag_arg_b, argv[2]);

        return CODE_CONTINUE;
    }

    int bunset(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 2)
            return CODE_FAIL;

        *flags |= FLAG_UNSET;
        strcpy(flag_arg_a, argv[1]);

        return CODE_CONTINUE;
    }

    int bladd(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 3)
            return CODE_FAIL;

        const char* c_var = std::getenv(argv[1]);

        std::string var(c_var ?: "");
        std::string result(argv[2]);

        result += var;

        *flags |= FLAG_SET;
        strcpy(flag_arg_a, argv[1]);
        strcpy(flag_arg_b, result.c_str());

        return CODE_CONTINUE;
    }

    int bradd(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 3)
            return CODE_FAIL;

        const char* c_var = std::getenv(argv[1]);

        std::string var(c_var ?: "");
        std::string result(argv[2]);

        var += result;

        *flags |= FLAG_SET;
        strcpy(flag_arg_a, argv[1]);
        strcpy(flag_arg_b, var.c_str());

        return CODE_CONTINUE;
    }

    int breload(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        *flags |= FLAG_RELOAD;

        return CODE_CONTINUE;
    }

    int balias(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc == 1) {
            std::cout << "Aliases:" << std::endl;

            for (auto it = alias_map.begin(); it != alias_map.end(); ++it) {
                std::cout << "  " << it->first << " -> " << it->second << std::endl;
            }
        } else if (argc == 2) {
            std::string name(argv[1]);
            std::cout << alias_map[name] << std::endl;
        } else {
            *flags |= FLAG_ALIAS;

            strcpy(flag_arg_a, argv[1]);
            strcpy(flag_arg_b, argv[2]);
        }

        return CODE_CONTINUE;
    }

    int bunalias(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 2)
            return CODE_FAIL;

        *flags |= FLAG_ALIAS;
        strcpy(flag_arg_a, argv[1]);
        *flag_arg_b = '\0';

        return CODE_CONTINUE;
    }

    int bexists(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 3)
            return CODE_FAIL;

        if (strcmp(argv[1], "file") == 0)
            return !file_exists(argv[2]);

        if (strcmp(argv[1], "dir") == 0)
            return !dir_exists(argv[2]);

        return !any_exists(argv[1]);
    }

    int bequals(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 3)
            return CODE_FAIL;

        return strcmp(argv[1], argv[2]) != 0;
    }

    int bwith(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 3)
            return CODE_FAIL;

        const char* c_var = std::getenv(argv[1]);

        if (c_var == nullptr)
            *flags |= FLAG_WITH_U;
        else
            *flags |= FLAG_WITH_S;

        strcpy(flag_arg_a, argv[1]);
        strcpy(flag_arg_b, argv[2]);

        return CODE_CONTINUE;
    }

    int bwithout(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        *flags |= FLAG_WITHOUT;

        return CODE_CONTINUE;
    }

    int bwhich(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        string cmd(argv[1]);

        auto alias = alias_map.find(cmd);
        if (alias != alias_map.end()) {
            std::cout << cmd << ": aliased to " << alias->second << std::endl;
            return CODE_CONTINUE;
        }

        auto builtin = builtins_map.find(cmd);
        if (builtin != builtins_map.end()) {
            std::cout << cmd << ": builtin command" << std::endl;
            return CODE_CONTINUE;
        }

        auto executable = executable_map.find(cmd);
        if (executable != executable_map.end()) {
            std::cout << executable->second << std::endl;
            return CODE_CONTINUE;
        }

        std::cout << cmd << " not found" << std::endl;

        return CODE_CONTINUE;
    }

    int bdebug(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        *flags = atoi(argv[1]);
        strcpy(flag_arg_a, argv[2]);
        strcpy(flag_arg_b, argv[3]);

        return atoi(argv[4]);
    }

    int bfg(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        *flags |= FLAG_RESUME;

        return CODE_CONTINUE;
    }

    int bkill(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 2)
            return CODE_FAIL;

        pid_t pid;

        int idx = -1;

        if (strlen(argv[1]) > 1 && argv[1][0] == '%') {
            idx = atoi(argv[1] + 1);
            pid = suspended_pids[idx];

            if (pid == -1) {
                std::cerr << "Job is not running!" << std::endl;
                return CODE_FAIL;
            } else {
                std::cout << "\e[1m" << "Killed PID " << pid << "\e[m" << std::endl;
            }
        } else {
            pid = atoi(argv[1]);
            std::cout << "\e[1m" << "Killed PID " << pid << "\e[m" << std::endl;
        }

        *flags |= FLAG_KILL;
        snprintf(flag_arg_a, sizeof(flag_arg_a), "%d", pid);
        snprintf(flag_arg_b, sizeof(flag_arg_b), "%d", idx);

        return CODE_CONTINUE;
    }

    int brun(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 2)
            return CODE_FAIL;

        *flags |= FLAG_RUN;
        strcpy(flag_arg_a, argv[1]);

        return CODE_CONTINUE;
    }

    int bsource(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        if (argc < 2)
            return CODE_FAIL;

        *flags |= FLAG_SOURCE;
        strcpy(flag_arg_a, argv[1]);

        return CODE_CONTINUE;
    }

    int bhistory(int argc, char **argv, unsigned int *flags, char *flag_arg_a, char *flag_arg_b) {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            std::cout << *it << std::endl;
        }

        return CODE_CONTINUE;
    }
}

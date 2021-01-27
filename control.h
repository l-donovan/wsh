#pragma once

#define SEQ_PROMPT_START  "\e]1004;0s"
#define SEQ_PROMPT_STOP   "\e]1004;1s"
#define SEQ_COMMAND_START "\e]1004;2s"
#define SEQ_COMMAND_STOP  "\e]1004;3s"
#define SEQ_OUTPUT_START  "\e]1004;4s"
#define SEQ_OUTPUT_STOP   "\e]1004;5s"

#define CODE_CONTINUE  0
#define CODE_FAIL      1
#define CODE_EXIT_OK  -1
#define CODE_EXIT_BAD -2

#define READ_END  0
#define WRITE_END 1

#define DIRECTION_UP    0
#define DIRECTION_DOWN  1
#define DIRECTION_RIGHT 2
#define DIRECTION_LEFT  3

#define INSERT_END   -1

#define FLAG_EXIT    1 <<  0
#define FLAG_CD      1 <<  1
#define FLAG_SKIP    1 <<  2
#define FLAG_SILENCE 1 <<  3
#define FLAG_SET     1 <<  4
#define FLAG_UNSET   1 <<  5
#define FLAG_RELOAD  1 <<  6
#define FLAG_ALIAS   1 <<  7
#define FLAG_WITH_S  1 <<  8
#define FLAG_WITH_U  1 <<  9
#define FLAG_WITHOUT 1 << 10
#define FLAG_RESUME  1 << 11
#define FLAG_KILL    1 << 12
#define FLAG_RUN     1 << 13
#define FLAG_SOURCE  1 << 14

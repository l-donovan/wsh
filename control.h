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


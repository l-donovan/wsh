#pragma once

#define SEQ_PROMPT_START  "]1004;0s"
#define SEQ_PROMPT_STOP   "]1004;1s"
#define SEQ_COMMAND_START "]1004;2s"
#define SEQ_COMMAND_STOP  "]1004;3s"
#define SEQ_OUTPUT_START  "]1004;4s"
#define SEQ_OUTPUT_STOP   "]1004;5s"

#define CODE_CONTINUE 0
#define CODE_EXIT_OK  1
#define CODE_EXIT_BAD 2

extern unsigned int last_status;
extern bool skip_next;

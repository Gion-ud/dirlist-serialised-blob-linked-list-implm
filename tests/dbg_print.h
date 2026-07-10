#pragma once

#include <stdio.h>

#if defined(_DEBUG) && !defined(_NO_DBG_PRINT)
#define dbg_print(...) \
    do { \
        fprintf(stderr, __VA_ARGS__);putc('\n', stderr);fflush(stderr); \
    } while(0)

#define dbg_log_msg(msg) \
    do {\
        fprintf(stderr, "<%s@file '%s' line%d> %s\n", __func__, __FILE__, __LINE__, msg);\
    } while(0)

#else
#define dbg_print(...)
#define dbg_log_msg(msg)
#endif


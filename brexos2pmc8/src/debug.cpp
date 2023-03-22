#pragma once

#ifdef DEBUG

#define dprintf(...) printf(__VA_ARGS__)
#define dputs(s) puts(s)

#else

#define dprintf(...)
#define dputs(s)

#endif


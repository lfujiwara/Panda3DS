#pragma once
#include <cstdio>
#include <cstdlib>

#define ASSERT(cond) ((void)0)
#define DEBUG_ASSERT(cond) ((void)0)
#define ASSERT_MSG(cond, ...)                                       \
	do {                                                            \
		if (!(cond)) {                                              \
			std::fprintf(stderr, "dyncom assert failed: %s\n", #cond); \
			std::abort();                                           \
		}                                                           \
	} while (0)
#define UNREACHABLE() __builtin_unreachable()
#define UNIMPLEMENTED() ((void)0)

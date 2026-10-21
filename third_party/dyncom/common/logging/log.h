#pragma once
#include <cstdio>

// dyncom logs use fmt-style format strings; we only print the raw format so they cost nothing
#define LOG_TRACE(cls, ...) ((void)0)
#define LOG_DEBUG(cls, ...) ((void)0)
#define LOG_INFO(cls, fmt, ...) ((void)0)
#define LOG_WARNING(cls, fmt, ...) std::fprintf(stderr, "[dyncom] " fmt "\n")
#define LOG_ERROR(cls, fmt, ...) std::fprintf(stderr, "[dyncom] " fmt "\n")
#define LOG_CRITICAL(cls, fmt, ...) std::fprintf(stderr, "[dyncom] " fmt "\n")

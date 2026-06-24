#pragma once

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(__has_include)
#if __has_include(<version>)
#include <version>
#endif
#endif

#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 0
#endif

#ifndef METRICS_ENABLED
#define METRICS_ENABLED DEBUG_ENABLED
#endif

#if defined(__cpp_lib_source_location) && (__cpp_lib_source_location >= 201907L) && defined(__has_include) &&          \
    __has_include(<source_location>)
#include <source_location>
#define DBG_HAS_SOURCE_LOCATION 1
#else
#define DBG_HAS_SOURCE_LOCATION 0
#endif

#if DEBUG_ENABLED
#if DBG_HAS_SOURCE_LOCATION
#define DBG_ASSERT(cond, msg)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            const auto  _dbg_loc     = std::source_location::current();                                                \
            std::string _dbg_message = std::string("[HdRestir] assertion failed: ") + (msg) + " (" +                   \
                                       _dbg_loc.file_name() + ":" + std::to_string(_dbg_loc.line()) + " in " +         \
                                       _dbg_loc.function_name() + ")";                                                 \
            throw std::runtime_error(_dbg_message);                                                                    \
        }                                                                                                              \
    } while (0)
#else
#define DBG_ASSERT(cond, msg)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::string _dbg_message = std::string("[HdRestir] assertion failed: ") + (msg) + " (" + __FILE__ + ":" +  \
                                       std::to_string(__LINE__) + " in " + __func__ + ")";                             \
            throw std::runtime_error(_dbg_message);                                                                    \
        }                                                                                                              \
    } while (0)
#endif
#else
#define DBG_ASSERT(cond, msg)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            (void)sizeof(msg);                                                                                         \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
#endif

#if DEBUG_ENABLED
#define DBG_LOG(fmt, ...) std::printf("[HdRestir] " fmt "\n", ##__VA_ARGS__)
#else
#define DBG_LOG(fmt, ...) ((void)0)
#endif

#if METRICS_ENABLED
#define METRICS_LOG(fmt, ...) std::printf("[HdRestir] " fmt "\n", ##__VA_ARGS__)
#else
#define METRICS_LOG(fmt, ...) ((void)0)
#endif

#if DEBUG_ENABLED
#define DBG_UNREACHABLE(msg) DBG_ASSERT(false, msg)
#else
#if defined(__cpp_lib_unreachable) && (__cpp_lib_unreachable >= 202202L)
#define DBG_UNREACHABLE(msg)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(msg);                                                                                             \
        std::unreachable();                                                                                            \
    } while (0)
#elif defined(_MSC_VER)
#define DBG_UNREACHABLE(msg)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(msg);                                                                                             \
        __assume(false);                                                                                               \
    } while (0)
#elif defined(__clang__) || defined(__GNUC__)
#define DBG_UNREACHABLE(msg)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(msg);                                                                                             \
        __builtin_unreachable();                                                                                       \
    } while (0)
#else
#define DBG_UNREACHABLE(msg)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(msg);                                                                                             \
        std::abort();                                                                                                  \
    } while (0)
#endif
#endif

#if DEBUG_ENABLED
#define DBG_ONLY(expr)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        expr;                                                                                                          \
    } while (0)
#else
#define DBG_ONLY(expr) ((void)0)
#endif

#if DEBUG_ENABLED
#define DBG_NAMED(str) , std::string_view(str)
#define DBG_NAME_PARAM , std::string_view _dbgName = {}
#define DBG_NAME_INIT(name) , _debugName(std::string(name))
#define DBG_NAME_MEMBER std::string _debugName;
#else
#define DBG_NAMED(str)
#define DBG_NAME_PARAM
#define DBG_NAME_INIT(name)
#define DBG_NAME_MEMBER
#endif

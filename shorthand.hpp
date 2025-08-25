#pragma once
#include <float.h>
#include <stdint.h>

#include <type_traits>
// Shorthand for standard numeric types
// Unsigned integers
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// Signed integers
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

// Floating point numbers
typedef float f32;
typedef double f64;

typedef s32 b32;

// Limit values
#define u8Max  UINT8_MAX
#define u16Max UINT16_MAX
#define u32Max UINT32_MAX
#define u64Max UINT64_MAX

#define s8Max  INT8_MAX
#define s8Min  INT8_MIN
#define s16Max INT16_MAX
#define s16Min INT16_MIN
#define s32Max INT32_MAX
#define s32Min INT32_MIN
#define s64Max INT64_MAX
#define s64Min INT64_MIN

#define f32Max FLT_MAX
#define f32Min FLT_MIN

// Utils
typedef unsigned char byte;
typedef uintptr_t uptr;
typedef uintptr_t sptr;

static_assert(sizeof(byte) == 1);
static_assert(sizeof(s32) == 4);
static_assert(sizeof(s64) == 8);
static_assert(sizeof(f32) == 4);
static_assert(sizeof(f64) == 8);

#define CONCATH(x, y) x##y
#define CONCAT(x, y)  CONCATH(x, y)
#define ANNON_VAR     CONCAT(Annon__, __LINE__)

#define STRINGIFY(x)  STRINGIFY2(x)
#define STRINGIFY2(x) #x

#define STRINGIFYU8(x)  STRINGIFY2U8(x)
#define STRINGIFY2U8(x) u8#x

template <typename Fn>
struct DeferImpl {
    Fn fn;
    DeferImpl(Fn &&f)
        : fn(f)
    {}
    ~DeferImpl() { fn(); }
};
#define _defer DeferImpl ANNON_VAR = [&]

#if INTERNAL_BUILD
void assert_impl(const char *line, const char *func, const char *file, const char *cond);
#define FORCE_BREAK(cond) assert_impl(STRINGIFY(__LINE__), __FUNCTION__, __FILE__, cond);

#define ASSERT(expr)            \
    do {                        \
        if (!(expr)) {          \
            FORCE_BREAK(#expr); \
        }                       \
    } while (0);

#define VALIDATE(expr) ASSERT(expr)
#else
#define ASSERT(expr)
#define VALIDATE(expr) expr
#endif

constexpr u64 operator""_KB(u64 i)
{
    return i << 10;
}

constexpr u64 operator""_MB(u64 i)
{
    return i << 20;
}

#define PAD_BYTES(x) byte CONCAT(_pad, ANNON_VAR)[x] = {0};

#ifdef _MSC_VER
#define READ_ONLY __declspec(allocate(".roglob"))
#else
#define READ_ONLY
#endif

#ifdef _MSC_VER
#define MSVC_WARNING_ENABLE(w)  _Pragma(STRINGIFY(warning(default : w)))
#define MSVC_WARNING_DISABLE(w) _Pragma(STRINGIFY(warning(disable : w)))
#define MSVC_WARNING_PUSH       _Pragma(STRINGIFY(warning(push)))
#define MSVC_WARNING_PUSH_N(n)  _Pragma(STRINGIFY(warning(push, n)))
#define MSVC_WARNING_POP        _Pragma(STRINGIFY(warning(pop)))
#else
#define MSVC_WARNING_ENABLE(w)
#define MSVC_WARNING_DISABLE(w)
#define MSVC_WARNING_PUSH
#define MSVC_WARNING_PUSH_N(n)
#define MSVC_WARNING_POP
#endif

struct MemoryBlock {
    void *ptr = nullptr;
    s64 size = 0;

    operator bool() { return ptr != nullptr; }

    template <typename T>
    T as()
    {
        static_assert(std::is_pointer_v<T>);
        ASSERT(ptr);
        return static_cast<T>(ptr);
    }
};

template <typename T>
constexpr auto to_underlying(T n)
{
    return static_cast<std::underlying_type_t<T>>(n);
}

#define UNREF(x) (void)(x);

template <typename T, s64 N>
auto array_count(T (&)[N])
{
    return N;
}

#pragma once

#include <functional>
#include <vector>

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__CYGWIN__)
#    define SPHR_EXPORT __declspec(dllexport)
#    define SPHR_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#    define SPHR_EXPORT [[gnu::visibility("default")]]
#    define SPHR_IMPORT [[gnu::visibility("default")]]
#else
#    define SPHR_EXPORT
#    define SPHR_IMPORT
#endif

#ifdef SPHR_DLLEXPORT
#    define SPHR_API SPHR_EXPORT
#else
#    define SPHR_API SPHR_IMPORT
#endif

#ifdef SRBL_DLLEXPORT
#    define SRBL_API SPHR_EXPORT
#else
#    define SRBL_API SPHR_IMPORT
#endif

#define SDK_API SPHR_IMPORT

#define SPHR_DECL_API(...) \
    [[clang::annotate("sapphire::bind", __VA_ARGS__)]]

#define SPHR_CTOR_ALIAS \
    [[clang::annotate("sapphire::alias", 0)]]
#define SPHR_DTOR_ALIAS \
    [[clang::annotate("sapphire::alias", 1)]]

class Server {
public:
    SDK_API Server(std::vector<int> &, std::function<void()>);

    SPHR_DECL_API("1.21.2", "\x22")
    SPHR_CTOR_ALIAS SDK_API Server *ctor(std::vector<int> &, std::function<void()>);

    SDK_API ~Server();

    SPHR_DECL_API("1.21.2", "\x33")
    SPHR_DTOR_ALIAS SDK_API void dtor() noexcept;

    SPHR_DECL_API("1.21.2", "\x48\x89\x5C\x00\x00\x55")
    SPHR_DECL_API("v1_21_50,v1_21_60", "\x48\x89\x5C\x00\x00\x55")
    void init();

    SPHR_DECL_API("1.21.2", "disp:+1,deref", "\xE8\x00\x00\x00\x00\x48")
    void tick(float a);
};
#ifndef WFX_HTTP_ROUTE_MACROS_HPP
#define WFX_HTTP_ROUTE_MACROS_HPP

// Macros to tell compiler (when compiling user code into dll) that a specific-
// -part of code is used and it must not be optimized out
#if defined(_MSC_VER)
    #define WFX_USED __declspec(selectany)
#elif defined(__MINGW32__) || defined(__GNUC__) || defined(__clang__)
    #define WFX_USED __attribute__((used))
#else
    #define WFX_USED
#endif



#endif // WFX_HTTP_ROUTE_MACROS_HPP
#pragma once

#include <exception>
#include <execinfo.h>
#include <stdio.h>
#include <string>

#include "lux/log.hpp"


namespace lux {

    class Exception : public std::exception {
        std::string msg;

	void* callstack[128];
	char ** strs;
	int frames;

    public:
        Exception(const std::string &what) : std::exception(), msg(what) {
	    frames = backtrace(callstack, 128);
	    strs = backtrace_symbols(callstack, frames);
	}

        const char *what() const noexcept override { return msg.c_str(); }

	void printStackTrace() {
	    for (int i = 0; i < frames; ++i) {
		LUX_LOG(strs[i]);
		//printf("%s\n", strs[i]);
	    }
	    free(strs);
	}
    };

}  // namespace lux


#ifdef DEBUG_BUILD
#    define LUX_ASSERT_M(expr, message)      \
    if (!(expr)) {                           \
        throw ::lux::Exception(message);     \
    }
#    define LUX_ASSERT(expr)                                            \
    if (!(expr)) {                                                      \
        throw ::lux::Exception(std::string(__FILE__) + ":" + std::to_string(__LINE__)); \
    }
#else
#    define LUX_ASSERT_M(expr, message) (void)(expr);
#    define LUX_ASSERT(expr) (void)(expr);
#endif

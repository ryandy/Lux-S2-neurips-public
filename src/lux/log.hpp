#pragma once

#include <iostream>
#include <string>

#include "lux/json.hpp"

/**
 * \brief Simple macro to write logs in debug mode.
 *
 * Will only generate log statements if built in debug mode.
 *
 * \note Since logs can only be written to stderr, they will be
 * included in the error log of the agent. It is for debugging
 * purposes only.
 *
 * Example usage: LUX_LOG("this should be 5: " << aValue);
 */

extern bool LUX_LOG_ON;
extern bool LUX_LOG_DEBUG_ON;
#ifdef DEBUG_BUILD
#    define LUX_LOG(...) if (LUX_LOG_ON) { std::cerr << log_prefix() << __VA_ARGS__ << std::endl; }
#    define LUX_LOGF(...) if (LUX_LOG_ON) { std::cerr << log_prefix(); printf(__VA_ARGS__); std::cerr << std::endl; }
#    define LUX_LOG_DEBUG(...) if(LUX_LOG_DEBUG_ON) LUX_LOG(__VA_ARGS__)
#else
#    define LUX_LOG(...)
#    define LUX_LOGF(...)
#    define LUX_LOG_DEBUG(...)
#endif

std::string log_prefix();

namespace lux {
    /**
     * \brief Dumps contents of json to a file.
     *
     * Will only create file and dump contents in debug mode.
     * The destination will always be truncated before writing.
     */
    void dumpJsonToFile(const char *path, json data);
}  // namespace lux

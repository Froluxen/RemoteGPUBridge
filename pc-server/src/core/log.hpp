#pragma once

#include <iostream>
#include <string_view>

namespace argb::log {

enum class Level { Info, Warning, Error };

inline void write(Level level, std::string_view message) {
    std::ostream& out = (level == Level::Error) ? std::cerr : std::cout;
    const char* tag = "INFO";
    switch (level) {
        case Level::Info: tag = "INFO"; break;
        case Level::Warning: tag = "WARN"; break;
        case Level::Error: tag = "ERROR"; break;
    }
    out << '[' << tag << "] " << message << '\n';
}

inline void info(std::string_view message) { write(Level::Info, message); }
inline void warn(std::string_view message) { write(Level::Warning, message); }
inline void error(std::string_view message) { write(Level::Error, message); }

} // namespace argb::log

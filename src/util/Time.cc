#include "util/Time.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace acadia::util {

std::string utc_now_iso8601() {
    return to_iso8601(Clock::now());
}

int64_t utc_now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now().time_since_epoch()).count();
}

std::string to_iso8601(TimePoint tp) {
    auto t = Clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

TimePoint parse_iso8601(const std::string& s) {
    std::tm tm{};
    std::istringstream ss{s};
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (ss.fail()) return Clock::from_time_t(0);
    return Clock::from_time_t(std::mktime(&tm));
}

}  // namespace acadia::util

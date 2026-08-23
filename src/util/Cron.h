#pragma once
// util/Cron.h — compute the next fire time of a standard 5-field cron
// expression (minute hour day-of-month month day-of-week), evaluated in UTC.
#include <string>

namespace acadia::util {

// Returns the next occurrence strictly after `from_iso8601`, formatted as an
// ISO-8601 UTC string ("2026-01-02T15:04:00Z"). Returns an empty string when
// the expression is invalid or yields no occurrence within the next ~2 years.
std::string cron_next_occurrence(const std::string& expression,
                                 const std::string& from_iso8601);

}  // namespace acadia::util

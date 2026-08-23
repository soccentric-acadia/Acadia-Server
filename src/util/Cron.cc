// util/Cron.cc — next-occurrence computation for 5-field cron expressions.
// Supports: `*`, `*/step`, ranges `a-b`, `a-b/step`, lists `a,b,c`, and bare
// values for each field. Day-of-week uses Sunday=0. Day-of-month/day-of-week
// follow the standard cron rule: if BOTH are restricted, a day matches when
// EITHER matches; otherwise the unrestricted field is ignored.
#include "util/Cron.h"
#include "util/Time.h"

#include <ctime>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace acadia::util {

namespace {

// Expand a single comma-free segment (e.g. "*/15", "1-5/2", "7", "*") into
// allowed values in [min,max].
bool expand_segment(const std::string& seg, int min, int max, std::set<int>& out) {
    if (seg.empty()) return false;

    int lo = min, hi = max, step = 1;
    std::string base = seg;
    std::string step_str;
    auto slash = seg.find('/');
    if (slash != std::string::npos) {
        base = seg.substr(0, slash);
        step_str = seg.substr(slash + 1);
        if (step_str.empty()) return false;
        for (char c : step_str)
            if (c < '0' || c > '9') return false;
        step = std::stoi(step_str);
        if (step < 1) return false;
    }

    if (base == "*") {
        // base stays [min,max]; step applied below.
    } else {
        auto dash = base.find('-');
        if (dash == std::string::npos) {
            // Single value.
            bool ok = !base.empty();
            for (char c : base)
                if (c < '0' || c > '9') ok = false;
            if (!ok) return false;
            int v = std::stoi(base);
            if (v < min || v > max) return false;
            out.insert(v);
            return true;
        }
        std::string ls = base.substr(0, dash);
        std::string hs = base.substr(dash + 1);
        bool ok = !ls.empty() && !hs.empty();
        for (char c : ls) if (c < '0' || c > '9') ok = false;
        for (char c : hs) if (c < '0' || c > '9') ok = false;
        if (!ok) return false;
        lo = std::stoi(ls);
        hi = std::stoi(hs);
        if (lo < min || hi > max || lo > hi) return false;
    }

    // Apply step starting at lo (or min when base=="*").
    int start = (base == "*") ? min : lo;
    for (int v = start; v <= (base == "*" ? max : hi); v += step) out.insert(v);
    return true;
}

// Parse a single cron field into a set of allowed values.
bool parse_field(const std::string& field, int min, int max, std::set<int>& out) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : field) {
        if (c == ',') { parts.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    parts.push_back(cur);
    for (const auto& p : parts)
        if (!expand_segment(p, min, max, out)) return false;
    return true;
}

static bool is_leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap(y)) return 29;
    return d[m - 1];
}

// Sakamoto's algorithm: day of week (0=Sunday) for a given y/m/d.
static int day_of_week(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 3};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static bool chrono_before(int y1, int mo1, int d1, int h1, int mi1,
                          int y2, int mo2, int d2, int h2, int mi2) {
    if (y1 != y2) return y1 < y2;
    if (mo1 != mo2) return mo1 < mo2;
    if (d1 != d2) return d1 < d2;
    if (h1 != h2) return h1 < h2;
    return mi1 < mi2;
}

static std::string fmt(int y, int mo, int d, int h, int mi) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:00Z",
                  y, mo, d, h, mi);
    return buf;
}

}  // namespace (anon)

std::string cron_next_occurrence(const std::string& expression,
                                 const std::string& from_iso8601) {    std::vector<std::string> fields;
    std::string cur;
    for (char c : expression) {
        if (c == ' ') { if (!cur.empty()) { fields.push_back(cur); cur.clear(); } }
        else cur.push_back(c);
    }
    if (!cur.empty()) fields.push_back(cur);
    if (fields.size() != 5) return {};

    std::set<int> min_set, hour_set, dom_set, month_set, dow_set;
    if (!parse_field(fields[0], 0, 59, min_set)) return {};
    if (!parse_field(fields[1], 0, 23, hour_set)) return {};
    if (!parse_field(fields[2], 1, 31, dom_set)) return {};
    if (!parse_field(fields[3], 1, 12, month_set)) return {};
    if (!parse_field(fields[4], 0, 6, dow_set)) return {};

    const bool dom_star = (dom_set.size() == 31);
    const bool dow_star = (dow_set.size() == 7);

    // Starting point: the minute after `from`. `from_iso8601` is UTC (Z);
    // parse it directly with timegm so results are timezone-independent.
    struct tm from_tm{};
    int r = std::sscanf(from_iso8601.c_str(), "%d-%d-%dT%d:%d",
                        &from_tm.tm_year, &from_tm.tm_mon, &from_tm.tm_mday,
                        &from_tm.tm_hour, &from_tm.tm_min);
    if (r != 5) return {};
    from_tm.tm_year -= 1900;
    from_tm.tm_mon -= 1;
    const std::time_t t0 = timegm(&from_tm);
    gmtime_r(&t0, &from_tm);
    int sy = from_tm.tm_year + 1900;
    int smo = from_tm.tm_mon + 1;
    int sd = from_tm.tm_mday;
    int sh = from_tm.tm_hour;
    int smi = from_tm.tm_min;
    // advance to the next minute boundary
    smi += 1;
    if (smi >= 60) { smi = 0; sh += 1; if (sh >= 24) { sh = 0; sd += 1; } }
    // normalize day overflow simply (rare; loop handles up to 2 years)
    if (sd > days_in_month(sy, smo)) { sd = 1; smo += 1; if (smo > 12) { smo = 1; sy += 1; } }

    // Scan ~2 years forward. cron always repeats within a year.
    for (int y = sy; y <= sy + 1; ++y) {
        for (int mo = 1; mo <= 12; ++mo) {
            if (!month_set.count(mo)) continue;
            int dim = days_in_month(y, mo);
            for (int d = 1; d <= dim; ++d) {
                int dow = day_of_week(y, mo, d);
                bool day_ok;
                if (dom_star && dow_star) day_ok = true;
                else if (dom_star) day_ok = dow_set.count(dow) != 0;
                else if (dow_star) day_ok = dom_set.count(d) != 0;
                else day_ok = dom_set.count(d) != 0 || dow_set.count(dow) != 0;
                if (!day_ok) continue;
                for (int h : hour_set) {
                    for (int mi : min_set) {
                        if (chrono_before(y, mo, d, h, mi, sy, smo, sd, sh, smi))
                            continue;
                        return fmt(y, mo, d, h, mi);
                    }
                }
            }
        }
    }
    return {};  // no occurrence within ~2 years
}

}  // namespace acadia::util

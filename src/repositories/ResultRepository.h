#pragma once
// repositories/ResultRepository.h
#include <optional>
#include <string>
#include <vector>

namespace acadia {

struct Result {
    std::string id;
    std::string run_id;
    std::string module_id;
    std::string module_version;
    std::string timestamp;
    std::string mode;
    std::string status;  // pass | fail | error | unknown
    std::string error_message;
    std::vector<char> payload_bytes;
    std::string       payload_type_url;
};

struct ResultFilter {
    std::string run_id;
    std::string module_id;
    std::string status;
    std::string since;
    std::string until;
    int limit  = 100;
    int offset = 0;
};

class ResultRepository {
public:
    Result insert(const Result& r);
    std::optional<Result> find_by_id(const std::string& id);
    std::vector<Result>   list(const ResultFilter& f);
    // Returns result count for a run (for pagination metadata)
    int64_t count_for_run(const std::string& run_id);
};

}  // namespace acadia

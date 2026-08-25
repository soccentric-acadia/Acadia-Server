#pragma once
// repositories/BoardRepository.h — durable board lifecycle state (task 91).
#include <optional>
#include <string>

namespace acadia {

struct BoardRecord {
    std::string id;
    std::string client_version;
    std::string first_seen_at;
    std::string last_seen_at;
    int         overall_state      = 0;
    int64_t     last_spool_bytes   = 0;
    int64_t     last_spool_cap     = 0;
    double      last_spool_pressure = 0;
    std::string active_run_id;
    int         active_run_state   = 0;
    int64_t     last_active_seq    = 0;
};

class BoardRepository {
public:
    // Register a board on HelloReport (upsert, idempotent).
    void upsert_board(const std::string& id, const std::string& client_version,
                      const std::string& now_iso);

    // Update state from a StateReport (heartbeat).
    void update_state(const std::string& id, int overall_state,
                      int64_t spool_bytes, int64_t spool_cap, double pressure,
                      const std::string& now_iso);

    // Mark a run active for a board (or clear when run_id empty).
    void set_active_run(const std::string& board_id, const std::string& run_id,
                        int run_state, int64_t last_seq);

    std::optional<BoardRecord> get(const std::string& id) const;
};

}  // namespace acadia

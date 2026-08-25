// repositories/BoardRepository.cc — task 91: durable board lifecycle state.
#include "repositories/BoardRepository.h"
#include "db/DbClient.h"

#include <spdlog/spdlog.h>

namespace acadia {

void BoardRepository::upsert_board(const std::string& id,
                                   const std::string& client_version,
                                   const std::string& now_iso) {
    auto db = db::get_db_client();
    try {
        db->execSqlSync(
            "INSERT INTO boards(id, client_version, first_seen_at, last_seen_at) "
            "VALUES ($1, $2, $3, $3) "
            "ON CONFLICT (id) DO UPDATE SET client_version = EXCLUDED.client_version, "
            "                            last_seen_at   = EXCLUDED.last_seen_at",
            id, client_version, now_iso);
    } catch (const std::exception& e) {
        spdlog::error("board_repo: upsert_board error: {}", e.what());
    }
}

void BoardRepository::update_state(const std::string& id, int overall_state,
                                   int64_t spool_bytes, int64_t spool_cap,
                                   double pressure, const std::string& now_iso) {
    auto db = db::get_db_client();
    try {
        db->execSqlSync(
            "UPDATE boards SET last_seen_at=$1, overall_state=$2, "
            "last_spool_bytes=$3, last_spool_cap=$4, last_spool_pressure=$5 "
            "WHERE id=$6",
            now_iso, std::to_string(overall_state),
            std::to_string(spool_bytes), std::to_string(spool_cap),
            std::to_string(pressure), id);
    } catch (const std::exception& e) {
        spdlog::error("board_repo: update_state error: {}", e.what());
    }
}

void BoardRepository::set_active_run(const std::string& board_id,
                                     const std::string& run_id, int run_state,
                                     int64_t last_seq) {
    auto db = db::get_db_client();
    try {
        db->execSqlSync(
            "UPDATE boards SET active_run_id=$1, active_run_state=$2, last_active_seq=$3 "
            "WHERE id=$4",
            run_id, std::to_string(run_state), std::to_string(last_seq), board_id);
    } catch (const std::exception& e) {
        spdlog::error("board_repo: set_active_run error: {}", e.what());
    }
}

std::optional<BoardRecord> BoardRepository::get(const std::string& id) const {
    auto db = db::get_db_client();
    try {
        auto rows = db->execSqlSync(
            "SELECT id, client_version, first_seen_at, last_seen_at, overall_state, "
            "       last_spool_bytes, last_spool_cap, last_spool_pressure, "
            "       active_run_id, active_run_state, last_active_seq "
            "FROM boards WHERE id=$1",
            id);
        if (rows.empty()) return std::nullopt;
        const auto& r = rows[0];
        BoardRecord b;
        b.id                 = r["id"].as<std::string>();
        b.client_version     = r["client_version"].as<std::string>();
        b.first_seen_at      = r["first_seen_at"].as<std::string>();
        b.last_seen_at       = r["last_seen_at"].as<std::string>();
        b.overall_state      = r["overall_state"].as<int>();
        b.last_spool_bytes   = r["last_spool_bytes"].as<int64_t>();
        b.last_spool_cap     = r["last_spool_cap"].as<int64_t>();
        b.last_spool_pressure = r["last_spool_pressure"].as<double>();
        b.active_run_id      = r["active_run_id"].as<std::string>();
        b.active_run_state   = r["active_run_state"].as<int>();
        b.last_active_seq    = r["last_active_seq"].as<int64_t>();
        return b;
    } catch (const std::exception& e) {
        spdlog::error("board_repo: get error: {}", e.what());
        return std::nullopt;
    }
}

}  // namespace acadia

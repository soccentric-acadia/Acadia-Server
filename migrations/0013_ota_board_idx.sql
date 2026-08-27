-- migrations/0013_ota_board_idx.sql — SRV-021: index the OTA board dispatch path.
-- 0012_ota_board.sql added ota_upgrades.board_id without an index; per-board
-- queries (list upgrades for a board, in-flight check for a board) would
-- sequence-scan ota_upgrades as it grows.

CREATE INDEX IF NOT EXISTS idx_ota_upgrades_board
    ON ota_upgrades (board_id);
CREATE INDEX IF NOT EXISTS idx_ota_upgrades_board_status
    ON ota_upgrades (board_id, status);
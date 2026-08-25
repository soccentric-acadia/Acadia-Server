-- migrations/0012_ota_board.sql — task 97: target board for OTA dispatch.
ALTER TABLE ota_upgrades ADD COLUMN IF NOT EXISTS board_id TEXT NOT NULL DEFAULT '';

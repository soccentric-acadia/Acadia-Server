-- 0014_users_role.sql — add an RBAC role to users (SRV-003).
-- Admin-only operations (e.g. POST /api/v1/backup) require role = 'admin'.

BEGIN;

ALTER TABLE users ADD COLUMN IF NOT EXISTS role TEXT NOT NULL DEFAULT 'operator';

-- Promote the bootstrap admin account if it exists (AuthService::ensure_admin
-- now creates new admins with role='admin' going forward).
UPDATE users SET role = 'admin' WHERE username = 'admin' AND role <> 'admin';

COMMIT;
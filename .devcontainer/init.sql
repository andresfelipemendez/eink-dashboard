-- Database initialization for eink-dashboard

CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(64) UNIQUE NOT NULL,
    password_hash VARCHAR(128) NOT NULL,
    created_at TIMESTAMP DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);

CREATE TABLE IF NOT EXISTS sessions (
    id SERIAL PRIMARY KEY,
    token VARCHAR(64) UNIQUE NOT NULL,
    user_id INTEGER REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMP DEFAULT NOW(),
    expires_at TIMESTAMP NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_sessions_token ON sessions(token);

-- Dev user: admin / admin
-- Hash generated with: echo -n "admin" | argon2 deadbeef -id -e
INSERT INTO users (username, password_hash) VALUES
    ('admin', '$argon2id$v=19$m=65536,t=3,p=4$ZGVhZGJlZWY$K8H+L6TJzU5P5v5nQP5bRA')
ON CONFLICT (username) DO NOTHING;

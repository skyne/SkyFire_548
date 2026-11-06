#!/usr/bin/env bash
set -euo pipefail

MYSQL_HOST="${MYSQL_HOST:-sf-database}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_PWD="${MYSQL_PWD:-${MYSQL_ROOT_PASSWORD:-password}}"
export MYSQL_PWD

AUTH_DB="${SF_AUTH_DB:-auth}"
WORLD_DB="${SF_WORLD_DB:-world}"
CHARS_DB="${SF_CHARS_DB:-characters}"
SQL_ROOT="${SQL_ROOT:-/sql}"
SFDB_CACHE="${SFDB_CACHE:-/sfdb}"
SFDB_URL="${SFDB_URL:-https://github.com/ProjectSkyfire/SkyFire_548/releases/download/sf_db_27/SFDB_full_548_27.001_2026_011_03_Release.zip}"
SFDB_SHA256="${SFDB_SHA256:-7b2d59d25bac679b5f4e7a0e95c48ac08b4bdd4077fb4496b6cb3837f87b824f}"

mysql_cli() {
    mysql --protocol=TCP -h"$MYSQL_HOST" -P"$MYSQL_PORT" -u"$MYSQL_USER" "$@"
}

echo "Waiting for MySQL at ${MYSQL_HOST}:${MYSQL_PORT}..."
for i in $(seq 1 90); do
    if mysql_cli -e "SELECT 1" >/dev/null 2>&1; then
        break
    fi
    if [[ "$i" -eq 90 ]]; then
        echo "MySQL did not become ready"
        exit 1
    fi
    sleep 2
done

mysql_cli <<SQL
CREATE DATABASE IF NOT EXISTS \`${AUTH_DB}\` DEFAULT CHARACTER SET utf8 COLLATE utf8_general_ci;
CREATE DATABASE IF NOT EXISTS \`${WORLD_DB}\` DEFAULT CHARACTER SET utf8 COLLATE utf8_general_ci;
CREATE DATABASE IF NOT EXISTS \`${CHARS_DB}\` DEFAULT CHARACTER SET utf8 COLLATE utf8_general_ci;
CREATE USER IF NOT EXISTS 'skyfire'@'%' IDENTIFIED BY 'skyfire';
GRANT ALL PRIVILEGES ON \`${AUTH_DB}\`.* TO 'skyfire'@'%';
GRANT ALL PRIVILEGES ON \`${WORLD_DB}\`.* TO 'skyfire'@'%';
GRANT ALL PRIVILEGES ON \`${CHARS_DB}\`.* TO 'skyfire'@'%';
FLUSH PRIVILEGES;
SQL

table_count() {
    local db="$1"
    mysql_cli -N -e "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='${db}'"
}

import_sql() {
    local db="$1"
    local file="$2"
    echo "Importing $(basename "$file") -> ${db}"
    mysql_cli --binary-mode "$db" < "$file"
}

apply_updates() {
    local db="$1"
    local dir="$2"
    local f
    shopt -s nullglob
    for f in "$dir"/*.sql; do
        echo "Update $(basename "$f") -> ${db}"
        mysql_cli --force "$db" < "$f" || echo "warning: $(basename "$f") returned $?"
    done
}

mkdir -p "$SFDB_CACHE"
ZIP="${SFDB_CACHE}/SFDB_full_548_27.001.zip"
if [[ ! -f "$ZIP" ]]; then
    echo "Downloading SFDB 27.001..."
    curl -L --fail --retry 3 -o "$ZIP" "$SFDB_URL"
fi
echo "${SFDB_SHA256}  ${ZIP}" | sha256sum -c -

EXTRACT="${SFDB_CACHE}/extract"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
unzip -o "$ZIP" -d "$EXTRACT"
echo "SFDB archive contents:"
find "$EXTRACT" -type f | sort

pick_sql() {
    local needle="$1"
    find "$EXTRACT" -type f \( -iname "*.sql" -o -iname "*.sql.gz" \) \
        | grep -i "$needle" | head -n 1 || true
}

AUTH_DUMP=$(pick_sql 'auth')
CHARS_DUMP=$(pick_sql 'character')
WORLD_DUMP=$(pick_sql 'world')
if [[ -z "$WORLD_DUMP" ]]; then
    WORLD_DUMP=$(find "$EXTRACT" -type f \( -iname "*.sql" -o -iname "*.sql.gz" \) | head -n 1 || true)
fi

import_maybe_gz() {
    local db="$1"
    local file="$2"
    echo "Importing $(basename "$file") -> ${db}"
    if [[ "$file" == *.gz ]]; then
        gzip -dc "$file" | mysql_cli --binary-mode "$db"
    else
        mysql_cli --binary-mode "$db" < "$file"
    fi
}

if [[ "$(table_count "$AUTH_DB")" -eq 0 ]]; then
    if [[ -n "$AUTH_DUMP" ]]; then
        import_maybe_gz "$AUTH_DB" "$AUTH_DUMP"
    else
        import_sql "$AUTH_DB" "${SQL_ROOT}/base/auth_database.sql"
    fi
else
    echo "${AUTH_DB} already has tables, skipping base import"
fi

if [[ "$(table_count "$CHARS_DB")" -eq 0 ]]; then
    if [[ -n "$CHARS_DUMP" && "$CHARS_DUMP" != "$AUTH_DUMP" ]]; then
        import_maybe_gz "$CHARS_DB" "$CHARS_DUMP"
    else
        import_sql "$CHARS_DB" "${SQL_ROOT}/base/characters_database.sql"
    fi
else
    echo "${CHARS_DB} already has tables, skipping base import"
fi

if [[ "$(table_count "$WORLD_DB")" -eq 0 ]]; then
    if [[ -z "$WORLD_DUMP" ]]; then
        echo "No world dump found in SFDB zip"
        exit 1
    fi
    import_maybe_gz "$WORLD_DB" "$WORLD_DUMP"
else
    echo "${WORLD_DB} already has tables, skipping base import"
fi

if [[ -f "${SQL_ROOT}/base/stored_procs.sql" ]]; then
    mysql_cli --force "$WORLD_DB" < "${SQL_ROOT}/base/stored_procs.sql" || true
fi

apply_updates "$AUTH_DB" "${SQL_ROOT}/updates/auth"
apply_updates "$CHARS_DB" "${SQL_ROOT}/updates/characters"
apply_updates "$WORLD_DB" "${SQL_ROOT}/updates/world"

echo "SFDB import finished"
mysql_cli -e "SHOW DATABASES;"
mysql_cli -N -e "SELECT table_schema, COUNT(*) FROM information_schema.tables WHERE table_schema IN ('${AUTH_DB}','${WORLD_DB}','${CHARS_DB}') GROUP BY table_schema;"

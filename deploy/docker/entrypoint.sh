#!/usr/bin/env bash
set -euo pipefail

CONF_DIR="${CONF_DIR:-/skyfire/etc}"
LOGS_DIR="${LOGS_DIR:-/skyfire/logs}"
REF_DIR="${REF_DIR:-/skyfire/ref/etc}"
COMPONENT="${SF_COMPONENT:-${1:-worldserver}}"

# The process exits unless the legacy provider loads. The image ships its own
# OpenSSL under /opt, and the loader does not look there unless told.
if [[ -z "${OPENSSL_MODULES:-}" ]]; then
    for modules in /opt/openssl-4.0.0/lib64/ossl-modules /opt/openssl-4.0.0/lib/ossl-modules; do
        if [[ -d "$modules" ]]; then
            export OPENSSL_MODULES="$modules"
            break
        fi
    done
fi

mkdir -p "$CONF_DIR" "$LOGS_DIR"

if ! touch "$CONF_DIR/.write-test" || ! touch "$LOGS_DIR/.write-test"; then
    cat <<EOF
===== WARNING =====
The current user cannot write $CONF_DIR or $LOGS_DIR.
Fix ownership on the host, or set DOCKER_USER=root.
====================
EOF
fi
rm -f "$CONF_DIR/.write-test" "$LOGS_DIR/.write-test" || true

if [[ -d "$REF_DIR" ]]; then
    cp -rnv "$REF_DIR"/. "$CONF_DIR"/ 2>/dev/null || true
fi

CONF="$CONF_DIR/${COMPONENT}.conf"
CONF_DIST="$CONF_DIR/${COMPONENT}.conf.dist"
if [[ -f "$CONF_DIST" && ! -f "$CONF" ]]; then
    cp -v "$CONF_DIST" "$CONF"
elif [[ ! -f "$CONF" ]]; then
    touch "$CONF"
fi

conf_set() {
    local file="$1"
    local key="$2"
    local value="${3-}"
    [[ -z "$value" ]] && return 0
    if [[ "$value" != \"*\" ]] && [[ "$value" == *";"* || "$value" == *" "* || "$value" == *"/"* ]]; then
        value="\"${value//\"/}\""
    fi
    local escaped
    escaped=$(printf '%s' "$value" | sed -e 's/[&|]/\\&/g')
    if grep -qE "^[[:space:]]*${key}[[:space:]]*=" "$file"; then
        sed -i -E "s|^[[:space:]]*${key}[[:space:]]*=.*|${key} = ${escaped}|" "$file"
    else
        printf '%s = %s\n' "$key" "$value" >> "$file"
    fi
}

conf_set "$CONF" "LogsDir" "$LOGS_DIR"
conf_set "$CONF" "DataDir" "${SF_DATA_DIR:-}"
conf_set "$CONF" "LoginDatabaseInfo" "${SF_LOGIN_DATABASE_INFO:-}"
conf_set "$CONF" "WorldDatabaseInfo" "${SF_WORLD_DATABASE_INFO:-}"
conf_set "$CONF" "CharacterDatabaseInfo" "${SF_CHARACTER_DATABASE_INFO:-}"
conf_set "$CONF" "BindIP" "${SF_BIND_IP:-}"
conf_set "$CONF" "WorldServerPort" "${SF_WORLD_SERVER_PORT:-}"
conf_set "$CONF" "RealmServerPort" "${SF_REALM_SERVER_PORT:-}"
conf_set "$CONF" "Cluster.Enabled" "${SF_CLUSTER_ENABLED:-}"
conf_set "$CONF" "Cluster.NodeId" "${SF_CLUSTER_NODE_ID:-}"
conf_set "$CONF" "Cluster.AvailableMaps" "${SF_CLUSTER_AVAILABLE_MAPS:-}"
conf_set "$CONF" "Cluster.LinkPort" "${SF_CLUSTER_LINK_PORT:-}"
conf_set "$CONF" "Cluster.LinkBindIP" "${SF_CLUSTER_LINK_BIND_IP:-}"
conf_set "$CONF" "Cluster.AdvertisedHost" "${SF_CLUSTER_ADVERTISED_HOST:-}"
conf_set "$CONF" "Cluster.SharedToken" "${SF_CLUSTER_SHARED_TOKEN:-}"
conf_set "$CONF" "Cluster.HideWorldPort" "${SF_CLUSTER_HIDE_WORLD_PORT:-}"
conf_set "$CONF" "Cluster.RealmID" "${SF_CLUSTER_REALM_ID:-}"
conf_set "$CONF" "Cluster.StaleSeconds" "${SF_CLUSTER_STALE_SECONDS:-}"
conf_set "$CONF" "Cluster.MigrationTimeout" "${SF_CLUSTER_MIGRATION_TIMEOUT:-}"
conf_set "$CONF" "Cluster.NatsUrl" "${SF_CLUSTER_NATS_URL:-}"
conf_set "$CONF" "Cluster.AdminBindIP" "${SF_CLUSTER_ADMIN_BIND_IP:-}"
conf_set "$CONF" "Cluster.AdminPort" "${SF_CLUSTER_ADMIN_PORT:-}"
conf_set "$CONF" "Gateway.ProxyProtocol" "${SF_GATEWAY_PROXY_PROTOCOL:-}"
conf_set "$CONF" "RealmID" "${SF_REALM_ID:-}"

export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/opt/openssl-4.0.0/lib64}"
export PATH="/skyfire/bin:${PATH}"

echo "Starting ${COMPONENT}..."
if [[ "${1:-}" == "worldserver" || "${1:-}" == "authserver" || "${1:-}" == "clustergateway" ]]; then
    exec "$1" -c "$CONF"
fi
if [[ "$#" -gt 0 ]]; then
    exec "$@"
fi
exec "$COMPONENT" -c "$CONF"

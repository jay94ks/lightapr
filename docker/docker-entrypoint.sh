#!/bin/sh
set -e

# Default environment variables if not set
CELL_ID="${CELL_ID:-default_cell}"
ACCESS_KEY="${ACCESS_KEY:-lightapr_secret_key}"
MQTT_PORT="${MQTT_PORT:-1883}"
WS_PORT="${WS_PORT:-8083}"
HTTP_PORT="${HTTP_PORT:-8080}"
LOG_FILE="${LOG_FILE:-/var/log/lightapr.log}"
LOG_LEVEL="${LOG_LEVEL:-info}"
WORKER_THREADS="${WORKER_THREADS:-0}"
STANDALONE="${STANDALONE:-true}"
CONFIG_FILE="${CONFIG_FILE:-}"
IDLE_TIMEOUT="${IDLE_TIMEOUT:-90}"
MAX_BUFFER_BYTES="${MAX_BUFFER_BYTES:-262144}"
MAX_CONNECTIONS="${MAX_CONNECTIONS:-10000}"
MAX_CONNECTIONS_PER_IP="${MAX_CONNECTIONS_PER_IP:-100}"
MAX_NEW_CONNECTIONS_PER_IP="${MAX_NEW_CONNECTIONS_PER_IP:-20}"
CONNECTION_RATE_WINDOW_SEC="${CONNECTION_RATE_WINDOW_SEC:-10}"
MAX_REQUESTS_PER_CONNECTION="${MAX_REQUESTS_PER_CONNECTION:-10000}"

# Build argument array
ARGS=""

if [ -n "$CONFIG_FILE" ]; then
    ARGS="$ARGS --config $CONFIG_FILE"
fi

if [ "$STANDALONE" = "true" ] || [ "$STANDALONE" = "1" ]; then
    ARGS="$ARGS --standalone"
fi

if [ -n "$CELL_ID" ]; then
    ARGS="$ARGS --cell-id $CELL_ID"
fi

if [ -n "$ACCESS_KEY" ]; then
    ARGS="$ARGS --access-key $ACCESS_KEY"
fi

if [ -n "$MQTT_PORT" ]; then
    ARGS="$ARGS --port $MQTT_PORT"
fi

if [ -n "$WS_PORT" ]; then
    ARGS="$ARGS --ws-port $WS_PORT"
fi

if [ -n "$HTTP_PORT" ]; then
    ARGS="$ARGS --http-port $HTTP_PORT"
fi

if [ -n "$WORKER_THREADS" ] && [ "$WORKER_THREADS" -gt 0 ]; then
    ARGS="$ARGS --threads $WORKER_THREADS"
fi

if [ -n "$LOG_FILE" ]; then
    ARGS="$ARGS --log-file $LOG_FILE"
fi

if [ -n "$LOG_LEVEL" ]; then
    ARGS="$ARGS --log-level $LOG_LEVEL"
fi

if [ -n "$IDLE_TIMEOUT" ]; then
    ARGS="$ARGS --idle-timeout $IDLE_TIMEOUT"
fi

if [ -n "$MAX_BUFFER_BYTES" ]; then
    ARGS="$ARGS --max-buffer-bytes $MAX_BUFFER_BYTES"
fi

if [ -n "$MAX_CONNECTIONS" ]; then
    ARGS="$ARGS --max-connections $MAX_CONNECTIONS"
fi

if [ -n "$MAX_CONNECTIONS_PER_IP" ]; then
    ARGS="$ARGS --max-connections-per-ip $MAX_CONNECTIONS_PER_IP"
fi

if [ -n "$MAX_NEW_CONNECTIONS_PER_IP" ]; then
    ARGS="$ARGS --max-new-connections-per-ip $MAX_NEW_CONNECTIONS_PER_IP"
fi

if [ -n "$CONNECTION_RATE_WINDOW_SEC" ]; then
    ARGS="$ARGS --connection-rate-window-sec $CONNECTION_RATE_WINDOW_SEC"
fi

if [ -n "$MAX_REQUESTS_PER_CONNECTION" ]; then
    ARGS="$ARGS --max-requests-per-connection $MAX_REQUESTS_PER_CONNECTION"
fi

echo "[LightAPR Docker Entrypoint] Executing lightapr with arguments:$ARGS"
exec /usr/local/bin/lightapr $ARGS "$@"

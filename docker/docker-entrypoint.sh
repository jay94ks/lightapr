#!/bin/sh
set -e

# Default environment variables if not set
CELL_ID="${CELL_ID:-default_cell}"
ACCESS_KEY="${ACCESS_KEY:-lightapr_secret_key}"
MQTT_PORT="${MQTT_PORT:-1883}"
WS_PORT="${WS_PORT:-8083}"
HTTP_PORT="${HTTP_PORT:-8080}"
LOG_FILE="${LOG_FILE:-/var/log/lightapr.log}"
WORKER_THREADS="${WORKER_THREADS:-0}"
STANDALONE="${STANDALONE:-true}"
CONFIG_FILE="${CONFIG_FILE:-}"

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

echo "[LightAPR Docker Entrypoint] Executing lightapr with arguments:$ARGS"
exec /usr/local/bin/lightapr $ARGS "$@"

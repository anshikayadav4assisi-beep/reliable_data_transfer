#!/usr/bin/env bash
set -u

ROOT="$(pwd)"
CHANNEL="$ROOT/emulator/udp_channel.exe"
SENDER="$ROOT/udp_sender.exe"
RECEIVER="$ROOT/udp_receiver.exe"
CSV="$ROOT/results/channel_stats.csv"
LOG_DIR="$ROOT/results/experiment_logs"
TRIALS=20

mkdir -p "$LOG_DIR" "$ROOT/results"

for file in "$CHANNEL" "$SENDER" "$RECEIVER"; do
    if [[ ! -f "$file" ]]; then
        echo "ERROR: Missing executable: $file"
        exit 1
    fi
done

if [[ ! -f "$CSV" ]]; then
    echo "ERROR: CSV file not found: $CSV"
    exit 1
fi

# Keep the original CSV unchanged. Store seed-to-trial mapping separately.
SEED_LOG="$ROOT/results/experiment_seeds.csv"
if [[ ! -f "$SEED_LOG" ]]; then
    printf 'trial,loss_probability,seed,timestamp\n' > "$SEED_LOG"
fi

run_trial() {
    local name="$1"
    local loss="$2"
    local seed="$3"

    local receiver_log="$LOG_DIR/${name}_receiver.log"
    local channel_log="$LOG_DIR/${name}_channel.log"
    local sender_log="$LOG_DIR/${name}_sender.log"

    local before
    before="$(wc -l < "$CSV")"

    echo
    echo "=== $name | loss=$loss | seed=$seed ==="

    "$RECEIVER" >"$receiver_log" 2>&1 &
    local receiver_pid=$!
    sleep 1

    if ! kill -0 "$receiver_pid" 2>/dev/null; then
        echo "ERROR: Receiver failed to start."
        cat "$receiver_log"
        return 1
    fi

    timeout 20s "$CHANNEL" "$loss" 0 0 0 0 0 --auto --seed "$seed" \
        >"$channel_log" 2>&1 &
    local channel_pid=$!
    sleep 2

    if ! kill -0 "$channel_pid" 2>/dev/null; then
        echo "ERROR: Channel emulator exited during startup."
        cat "$channel_log"
        kill "$receiver_pid" 2>/dev/null || true
        wait "$receiver_pid" 2>/dev/null || true
        return 1
    fi

    timeout 10s "$SENDER" >"$sender_log" 2>&1
    local sender_status=$?

    wait "$channel_pid"
    local channel_status=$?

    # Receiver normally exits on a successfully delivered END packet.
    # If END is dropped, stop the receiver after the channel has finished.
    if kill -0 "$receiver_pid" 2>/dev/null; then
        kill "$receiver_pid" 2>/dev/null || true
    fi
    wait "$receiver_pid" 2>/dev/null || true

    local after
    after="$(wc -l < "$CSV")"

    if (( sender_status != 0 )); then
        echo "ERROR: Sender failed (status $sender_status)."
        cat "$sender_log"
        return 1
    fi

    if (( channel_status != 0 )); then
        echo "ERROR: Channel process failed (status $channel_status)."
        cat "$channel_log"
        return 1
    fi

    if (( after != before + 1 )); then
        echo "ERROR: Expected exactly one new CSV row."
        echo "Before=$before After=$after"
        cat "$channel_log"
        return 1
    fi

    printf '%s,%s,%s,"%s"\n' "$name" "$loss" "$seed" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$SEED_LOG"

    echo "PASS: One CSV row appended."
    echo "Seed recorded in: $SEED_LOG"
    tail -n 1 "$CSV"
}

run_loss_series() {
    local label="$1"
    local probability="$2"

    for ((trial = 1; trial <= TRIALS; trial++)); do
        printf -v trial_number "%02d" "$trial"
        # Deterministic, distinct, nonzero seed per trial for reproducibility.
        local seed=$((100000 + RANDOM * 32768 + RANDOM + trial))
        if (( seed > 4294967295 )); then
            seed=$((seed % 4294967295 + 1))
        fi
        run_trial "${label}_trial_${trial_number}" "$probability" "$seed" || return 1
    done
}

echo "Repeated packet-loss experiment"
echo "Trials per setting: $TRIALS"
echo "Settings: 0%, 10%, 30%, 50%"
echo "Total planned trials: $((TRIALS * 4))"
echo "Original CSV will be appended to, not overwritten."
echo "Seed mapping: $SEED_LOG"

run_loss_series "loss_00" 0 || exit 1
run_loss_series "loss_10" 0.1 || exit 1
run_loss_series "loss_30" 0.3 || exit 1
run_loss_series "loss_50" 0.5 || exit 1

echo
echo "All 80 repeated trials completed successfully."
echo "CSV: $CSV"
echo "Seed mapping: $SEED_LOG"
echo "Logs: $LOG_DIR"

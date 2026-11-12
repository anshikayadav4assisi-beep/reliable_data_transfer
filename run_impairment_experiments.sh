#!/usr/bin/env bash
set -u

ROOT="$(pwd)"
CHANNEL="$ROOT/emulator/udp_channel.exe"
SENDER="$ROOT/udp_sender.exe"
RECEIVER="$ROOT/udp_receiver.exe"
CSV="$ROOT/results/channel_stats.csv"
LOG_DIR="$ROOT/results/experiment_logs"
SEED_LOG="$ROOT/results/experiment_seeds.csv"

# Run only the additional impairment experiments. The previous 80 packet-loss
# trials and all existing CSV rows are preserved.
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

if [[ ! -f "$SEED_LOG" ]]; then
    printf 'trial,loss_probability,seed,timestamp\n' > "$SEED_LOG"
fi

# Avoid accidentally launching the 80-trial loss series again.
run_trial() {
    local name="$1"
    local loss="$2"
    local duplication="$3"
    local reordering="$4"
    local corruption="$5"
    local min_delay="$6"
    local max_delay="$7"
    local seed="$8"

    local receiver_log="$LOG_DIR/${name}_receiver.log"
    local channel_log="$LOG_DIR/${name}_channel.log"
    local sender_log="$LOG_DIR/${name}_sender.log"

    local before
    before="$(wc -l < "$CSV")"

    echo
    echo "=== $name | loss=$loss duplication=$duplication reordering=$reordering corruption=$corruption delay=${min_delay}-${max_delay}ms seed=$seed ==="

    "$RECEIVER" >"$receiver_log" 2>&1 &
    local receiver_pid=$!
    sleep 1

    if ! kill -0 "$receiver_pid" 2>/dev/null; then
        echo "ERROR: Receiver failed to start."
        cat "$receiver_log"
        return 1
    fi

    timeout 20s "$CHANNEL" "$loss" "$duplication" "$reordering" "$corruption" "$min_delay" "$max_delay" --auto --seed "$seed" \
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

    # Keep the existing seed-log schema intact. The trial name encodes the
    # impairment settings, while the seed is stored in the same mapping file.
    printf '%s,%s,%s,"%s"\n' "$name" "$loss" "$seed" "$(date '+%Y-%m-%d %H:%M:%S')" >> "$SEED_LOG"

    echo "PASS: One CSV row appended."
    tail -n 1 "$CSV"
}

run_series() {
    local label="$1"
    local loss="$2"
    local duplication="$3"
    local reordering="$4"
    local corruption="$5"
    local min_delay="$6"
    local max_delay="$7"
    local seed_base="$8"

    for ((trial = 1; trial <= TRIALS; trial++)); do
        printf -v trial_number "%02d" "$trial"
        local seed=$((seed_base + trial))
        run_trial "${label}_trial_${trial_number}" "$loss" "$duplication" "$reordering" "$corruption" "$min_delay" "$max_delay" "$seed" || return 1
    done
}

echo "Additional channel impairment experiments"
echo "Trials per setting: $TRIALS"
echo "The existing 80 packet-loss trials and CSV rows will be preserved."
echo "New results will be appended to: $CSV"
echo "Seed mapping: $SEED_LOG"

# Duplication series: vary duplication probability while keeping other
# impairment settings disabled. Fixed seeds make this series reproducible.
run_series "duplication_00" 0 0.0 0 0 0 0 210000 || exit 1
run_series "duplication_25" 0 0.25 0 0 0 0 220000 || exit 1
run_series "duplication_50" 0 0.5 0 0 0 0 230000 || exit 1
run_series "duplication_75" 0 0.75 0 0 0 0 240000 || exit 1

# Delay series: vary the configured delay range with other impairments off.
run_series "delay_000_000" 0 0 0 0 0 0 310000 || exit 1
run_series "delay_050_100" 0 0 0 0 50 100 320000 || exit 1
run_series "delay_100_300" 0 0 0 0 100 300 330000 || exit 1

echo
echo "All additional duplication and delay trials completed successfully."
echo "Additional trials: $((TRIALS * 7))"
echo "CSV: $CSV"
echo "Seed mapping: $SEED_LOG"
echo "Logs: $LOG_DIR"

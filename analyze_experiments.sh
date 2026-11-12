#!/usr/bin/env bash
set -euo pipefail

CSV="results/channel_stats.csv"
SEEDS="results/experiment_seeds.csv"

if [[ ! -f "$CSV" ]]; then
  echo "ERROR: Missing $CSV"
  exit 1
fi

if [[ ! -f "$SEEDS" ]]; then
  echo "ERROR: Missing $SEEDS"
  exit 1
fi

echo "=== Latest 80 seeded trials only ==="
tail -n 80 "$CSV" | awk -F, '
  {
    loss = $2 + 0
    key = sprintf("%.1f", loss)
    count[key]++
    received[key] += $8
    dropped[key] += $10
    forwarded[key] += $9
    if ($8 > 0) observed[key] += ($10 / $8) * 100
  }
  END {
    printf "%-12s %8s %16s %18s %20s\n", "Configured", "Trials", "Mean dropped", "Mean forwarded", "Mean observed loss"
    for (i = 1; i <= 4; i++) {
      if (i == 1) key = "0.0"
      else if (i == 2) key = "0.1"
      else if (i == 3) key = "0.3"
      else key = "0.5"
      if (count[key] > 0) {
        printf "%-11s %8d %16.2f %18.2f %19.2f%%\n", key, count[key], dropped[key]/count[key], forwarded[key]/count[key], observed[key]/count[key]
      }
    }
  }
'

echo
echo "=== Seed mapping verification ==="
awk -F, 'NR > 1 { count++ } END { printf "Seed mapping rows: %d (expected 80)\n", count; if (count != 80) exit 1 }' "$SEEDS"

echo
echo "=== Most recent 5 seed mappings ==="
tail -n 5 "$SEEDS"

echo
echo "=== CSV row counts ==="
printf "Channel CSV lines (including header): "
wc -l < "$CSV"
printf "Seed mapping lines (including header): "
wc -l < "$SEEDS"

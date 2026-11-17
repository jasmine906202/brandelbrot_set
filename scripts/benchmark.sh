#!/usr/bin/env bash
# Runs the full benchmark and writes CSV files into results/.
#   results/results.csv  - every strategy, several image sizes, several thread counts
#   results/chunks.csv   - effect of the chunk size on the two dynamic strategies
set -euo pipefail
cd "$(dirname "$0")/.."

make -s
mkdir -p results
rm -f results/results.csv results/chunks.csv

BIN=./build/mandelbrot
RUNS=5
ITER=1000

echo "== Scaling study (three image sizes) =="
for size in "800 600" "1600 1200" "3200 2400"; do
  read -r W H <<<"$size"
  "$BIN" --width "$W" --height "$H" --iter "$ITER" --runs "$RUNS" --chunk 4 --csv results/results.csv
  echo
done

echo "== Chunk-size study (dynamic strategies, 1600x1200) =="
for chunk in 1 2 4 8 16 32 64 128; do
  "$BIN" --width 1600 --height 1200 --iter "$ITER" --runs "$RUNS" --chunk "$chunk" \
         --strategy dynamic-atomic,dynamic-mutex --csv results/chunks.csv
  echo
done

echo "Done. CSV files are in results/. Run: make plot"
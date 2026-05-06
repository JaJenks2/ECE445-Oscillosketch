#!/bin/bash

# Resolve script directory (so it works regardless of where you click from)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Activate virtual environment
source "$SCRIPT_DIR/venv/bin/activate"

# Run your Python script
python "$SCRIPT_DIR/stream_audio_serial.py"

# Optional: keep terminal open if double-clicked
echo "Press any key to exit..."
read -n 1
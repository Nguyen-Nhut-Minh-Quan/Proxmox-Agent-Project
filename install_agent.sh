#!/bin/bash

echo "🚀 Proxmox Agent Auto Installer Starting..."

# Define base working directory
BASE_DIR="/opt/Proxmox-Agent-Project"
cd "$BASE_DIR" || { echo "❌ Failed to access $BASE_DIR"; exit 1; }
echo "📂 Working directory: $(pwd)"

# Load environment variables
if [ ! -f "$BASE_DIR/src/.env" ]; then
  echo "❌ .env file missing in src/ — aborting install."
  exit 1
fi
source "$BASE_DIR/src/.env"
echo "✅ Environment variables loaded."

# Install dependencies
echo "🔍 Checking and installing dependencies..."
apt-get update

# Git check
if ! command -v git &> /dev/null; then
  echo "📦 Installing Git..."
  apt-get install git -y
else
  echo "✅ Git is already installed."
fi

# libusbtc08 check
if ! ldconfig -p | grep -q libusbtc08; then
  echo "📦 Installing libusbtc08 (with PicoScope repo)..."
  wget -O- https://labs.picotech.com/Release.gpg.key | gpg --dearmor > /usr/share/keyrings/picotech-archive-keyring.gpg
  echo "deb [signed-by=/usr/share/keyrings/picotech-archive-keyring.gpg] https://labs.picotech.com/rc/picoscope7/debian/ picoscope main" > /etc/apt/sources.list.d/picoscope7.list
  apt-get update
  apt-get install picoscope libusbtc08 -y
else
  echo "✅ libusbtc08 already present."
fi

# Compile C agents
echo "🛠️ Compiling test.c..."
gcc -g -o "$BASE_DIR/src/test" "$BASE_DIR/src/test.c" $(pkg-config --cflags --libs libmongoc-1.0) || { echo "❌ Failed to compile test.c"; exit 1; }

echo "🛠️ Compiling Temp_Read.c..."
gcc -o "$BASE_DIR/src/main" "$BASE_DIR/src/Temp_Read.c" -I/opt/picoscope/include -L/opt/picoscope/lib -lusbtc08 $(pkg-config --cflags --libs libmongoc-1.0) || { echo "❌ Failed to compile Temp_Read.c"; exit 1; }

# Make Bash helper executable
chmod +x "$BASE_DIR/src/VirtualServerStat.sh"
echo "🔓 VirtualServerStat.sh is now executable."

# Copy systemd unit files
echo "📦 Installing systemd service units..."
cp "$BASE_DIR/systemd/sensor_reader.service" /etc/systemd/system/
cp "$BASE_DIR/systemd/sensor_reader.timer" /etc/systemd/system/
cp "$BASE_DIR/systemd/sensor.service" /etc/systemd/system/
cp "$BASE_DIR/systemd/sensor.timer" /etc/systemd/system/

# Reload systemd and enable services
echo "🔁 Reloading systemd..."
systemctl daemon-reload

echo "✅ Enabling timers..."
systemctl enable sensor_reader.timer --now
systemctl enable sensor.timer --now

echo "🎉 All systems go! Agents are compiled, installed, and services are running."
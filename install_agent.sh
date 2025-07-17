#!/bin/bash
# Step 0: Get the script's current directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Step 1: Move the folder into /opt if it's not already there
INSTALL_DIR="/opt/Proxmox-Agent-Project"

if [ "$SCRIPT_DIR" != "$INSTALL_DIR" ]; then
  echo "📦 Moving agent folder to $INSTALL_DIR..."
  rm -rf "$INSTALL_DIR"           # Remove any existing copy
  mv "$SCRIPT_DIR" "$INSTALL_DIR" || { echo "❌ Move failed!"; exit 1; }
fi

cd "$INSTALL_DIR" || { echo "❌ Failed to access $INSTALL_DIR"; exit 1; }
echo "📂 Working directory: $(pwd)"

# Step 2: Load environment variables
if [ ! -f "$INSTALL_DIR/src/.env" ]; then
  echo "❌ .env file missing in src/ — aborting install."
  exit 1
fi
# Step 2.1: Convert Bash-style env to systemd-compatible env
if [ ! -f "$INSTALL_DIR/src/.env" ]; then
  echo "❌ .env.bash not found in src/ — aborting install."
  exit 1
fi

echo "🔁 Converting .env to .env.systemd..."
grep -oP '^export \K.*' "$INSTALL_DIR/src/.env" > "$INSTALL_DIR/src/.env.systemd"
echo "✅ .env.systemd updated successfully."

source "$INSTALL_DIR/src/.env"
echo "✅ Environment variables loaded."

# Step 3: Install dependencies
echo "🔍 Checking and installing dependencies..."
apt-get update

# Git
if ! command -v git &> /dev/null; then
  echo "📦 Installing Git..."
  apt-get install git -y
else
  echo "✅ Git is already installed."
fi

# libusbtc08
if ! ldconfig -p | grep -q libusbtc08; then
  echo "📦 Installing libusbtc08 (with PicoScope repo)..."
  wget -O- https://labs.picotech.com/Release.gpg.key | gpg --dearmor > /usr/share/keyrings/picotech-archive-keyring.gpg
  echo "deb [signed-by=/usr/share/keyrings/picotech-archive-keyring.gpg] https://labs.picotech.com/rc/picoscope7/debian/ picoscope main" > /etc/apt/sources.list.d/picoscope7.list
  apt-get update
  apt-get install picoscope libusbtc08 -y
else
  echo "✅ libusbtc08 already present."
fi

# Step 4: Compile C agents
echo "🛠️ Compiling test.c..."
gcc -g -o "$INSTALL_DIR/src/test" "$INSTALL_DIR/src/test.c" $(pkg-config --cflags --libs libmongoc-1.0) || { echo "❌ Failed to compile test.c"; exit 1; }

echo "🛠️ Compiling Temp_Read.c..."
gcc -o "$INSTALL_DIR/src/main" "$INSTALL_DIR/src/Temp_Read.c" -I/opt/picoscope/include -L/opt/picoscope/lib -lusbtc08 $(pkg-config --cflags --libs libmongoc-1.0) || { echo "❌ Failed to compile Temp_Read.c"; exit 1; }

# Step 5: Bash helper
chmod +x "$INSTALL_DIR/src/VirtualServerStat.sh"
echo "🔓 VirtualServerStat.sh is now executable."

# Step 6: Systemd service/timer setup
echo "📦 Installing systemd service units..."
cp "$INSTALL_DIR/systemd/sensor_reader.service" /etc/systemd/system/
cp "$INSTALL_DIR/systemd/sensor_reader.timer" /etc/systemd/system/
cp "$INSTALL_DIR/systemd/sensor.service" /etc/systemd/system/
cp "$INSTALL_DIR/systemd/sensor.timer" /etc/systemd/system/

echo "🔁 Reloading systemd..."
systemctl daemon-reload

echo "✅ Enabling timers..."
systemctl enable sensor_reader.timer --now
systemctl enable sensor.timer --now

echo "🎉 Installation complete! Your agents are compiled, services running, and timers active."
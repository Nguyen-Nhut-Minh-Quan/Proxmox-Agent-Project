#!/bin/bash
# Step 0: Get the script's current directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Step 1: Move the folder into /opt if it's not already there
INSTALL_DIR="/opt/Tank-Agent"
if [ "$SCRIPT_DIR" != "$INSTALL_DIR" ]; then
  echo "📦 Moving agent folder to $INSTALL_DIR..."
  rm -rf "$INSTALL_DIR"           # Remove any existing copy
  mv "$SCRIPT_DIR" "$INSTALL_DIR" || { echo "❌ Move failed!"; exit 1; }
fi
cd "$INSTALL_DIR" || { echo "❌ Failed to access $INSTALL_DIR"; exit 1; }
echo "📂 Working directory: $(pwd)"
# Step 2: Load environment variables
if [ ! -f "$INSTALL_DIR/.tank_agent_env" ]; then
  echo "❌ .env file missing  — aborting install."
  exit 1
fi
# Step 3: Install dependencies
echo "🔍 Checking and installing dependencies..."
apt-get update
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
if ! dpkg -s liblust-dev &> /dev/null; then
  echo "📦 Installing Lust library..."
  apt-get install -y liblust-dev
fi
#step 4 : Compiling file 
echo "🛠️ Compiling tank_agent.c..."
gcc -o "$INSTALL_DIR/tank_agent" "$INSTALL_DIR/tank_agent.c" -I/opt/picoscope/include -L/opt/picoscope/lib -lusbtc08 -lcurl || { echo "❌ Failed to compile tank_agent.c"; exit 1; }
# Step 5: Systemd service/timer setup
echo "📦 Installing systemd service units..."
cp "$INSTALL_DIR/systemd/tank_agent.service" /etc/systemd/system/
cp "$INSTALL_DIR/systemd/tank_agent.timer" /etc/systemd/system/

echo "🔁 Reloading systemd..."
systemctl daemon-reload

echo "✅ Enabling timers..."
systemctl enable tank_agent.timer --now

echo "🎉 Installation complete! Your agents are compiled, services running, and timers active."

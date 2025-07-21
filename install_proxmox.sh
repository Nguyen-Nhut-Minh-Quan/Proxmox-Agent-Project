#!/bin/bash
# Step 0: Get the script's current directory
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Step 1: Move the folder into /opt if it's not already there
INSTALL_DIR="/opt/Proxmox-Agent"

if [ "$SCRIPT_DIR" != "$INSTALL_DIR" ]; then
  echo "📦 Moving agent folder to $INSTALL_DIR..."
  rm -rf "$INSTALL_DIR"           # Remove any existing copy
  mv "$SCRIPT_DIR" "$INSTALL_DIR" || { echo "❌ Move failed!"; exit 1; }
fi

cd "$INSTALL_DIR" || { echo "❌ Failed to access $INSTALL_DIR"; exit 1; }
echo "📂 Working directory: $(pwd)"

# Step 2: Load environment variables
if [ ! -f "$INSTALL_DIR/src/.proxmox_agent_env" ]; then
  echo "❌ .env file missing in src/ — aborting install."
  exit 1
fi

# Step 3: Install dependencies
echo "🔍 Checking and installing dependencies..."
apt-get update
# Check if lust library is installed
if ! dpkg -s liblust-dev &> /dev/null; then
  echo "📦 Installing Lust library..."
  apt-get install -y liblust-dev
fi

# Step 4: Compile C agents
echo "🛠️ Compiling test.c..."
gcc -o "$INSTALL_DIR/src/proxmox_agent" "$INSTALL_DIR/src/proxmox_agent.c" -lcurl || { echo "❌ Failed to compile test.c"; exit 1; }

# Step 5: Bash helper
chmod +x "$INSTALL_DIR/src/VirtualServerStat.sh"
echo "🔓 VirtualServerStat.sh is now executable."

# Step 6: Systemd service/timer setup
echo "📦 Installing systemd service units..."
cp "$INSTALL_DIR/systemd/proxmox_agent.service" /etc/systemd/system/
cp "$INSTALL_DIR/systemd/proxmox_agent.timer" /etc/systemd/system/

echo "🔁 Reloading systemd..."
systemctl daemon-reload

echo "✅ Enabling timers..."
systemctl enable proxmox_agent.timer --now

echo "🎉 Installation complete! Your agents are compiled, services running, and timers active."
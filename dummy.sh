#!/bin/bash

echo "🚀 Starting Agent_For_Server Installation..."

INSTALL_DIR="/opt/Proxmox-Agent"
REPO_DIR="/root/Proxmox-Agent-Project"

# Ensure dependencies

echo "🔍 Checking for 'sudo'..."
if ! command -v sudo &>/dev/null; then
  echo "🔧 'sudo' not found — installing..."
  apt update && apt install sudo -y || {
    echo "❌ Failed to install 'sudo'. Aborting."; exit 1;
  }
else
  echo "✅ 'sudo' is already installed."
fi

echo "🔍 Checking for 'git'..."
if ! command -v git &>/dev/null; then
  echo "📦 Git not found — installing..."
  sudo apt update && sudo apt install git -y || {
    echo "❌ Failed to install 'git'. Aborting."; exit 1;
  }
else
  echo "✅ 'git' is already installed."
fi

# Clean up and clone
echo "📁 Fetching Agent_For_Server files..."
sudo rm -rf "$INSTALL_DIR"
rm -rf "$REPO_DIR"
git clone --branch master https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$REPO_DIR" || {
  echo "❌ Git clone failed. Aborting."; exit 1;
}

if ! command -v gcc &>/dev/null; then
  echo "🧰 GCC compiler not found — installing build-essential..."
  sudo apt update && sudo apt install build-essential -y || {
    echo "❌ GCC installation failed. Aborting setup."
    exit 1
  }
else
  echo "✅ GCC is already installed."
fi

if ! dpkg -s liblust-dev &> /dev/null; then
  echo "📦 Installing Lust library..."
  sudo apt pdate && apt-get install -y liblust-dev
fi

# Move only Agent_For_Server folder
sudo mv "$REPO_DIR/Agent_For_Server" "$INSTALL_DIR"

# Compile C agent
echo "🛠️ Compiling C agent..."
cd "$INSTALL_DIR" || { echo "❌ Couldn't enter $INSTALL_DIR"; exit 1; }

# Make helper script executable
if [ -f "VirtualServerStat.sh" ]; then
  chmod +x VirtualServerStat.sh
  echo "🔓 VirtualServerStat.sh made executable."
else
  echo "⚠️ VirtualServerStat.sh not found — skipping chmod."
fi

gcc -o proxmox_agent proxmox_agent.c -lcurl || {
  echo "❌ Compilation failed."; exit 1;
}


# Verify .env exists
if [ ! -f ".proxmox_agent_env" ]; then
  echo "❌ .proxmox_agent_env file is missing. Abort."
  exit 1
fi

# Systemd setup
echo "📦 Setting up systemd service units..."
sudo cp "$REPO_DIR/systemd/proxmox_agent.service" /etc/systemd/system/
sudo cp "$REPO_DIR/systemd/proxmox_agent.timer" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable proxmox_agent.timer --now

echo "🎉 Installation complete! Agent is compiled, active, and scheduled via systemd."
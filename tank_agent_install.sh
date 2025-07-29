#!/bin/bash
set -euo pipefail

echo "🚀 Starting Tank-Agent Installation..."

INSTALL_DIR="/opt/Tank-Agent"
REPO_TEMP="$(pwd)/Tank-Agent-Temp"
REPO_URL="https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git"
REPO_BRANCH="master"

# 1. Check Dependencies
echo "🔍 Checking 'sudo' and 'git'..."
command -v sudo >/dev/null || { echo "❌ sudo not found. Aborting."; exit 1; }
command -v git >/dev/null || {
  echo "📦 'git' not found. Installing..."
  sudo apt update && sudo apt install -y git
}

# 2. Clone Repository (shallow clone of target branch)
echo "📁 Cloning Tank-Agent repo..."
rm -rf "$REPO_TEMP"
if ! git clone --depth 1 --branch "$REPO_BRANCH" "$REPO_URL" "$REPO_TEMP"; then
  echo "❌ Git clone failed. Aborting."; exit 1;
fi

# 3. Move Core Files to Installation Directory
echo "📦 Installing to $INSTALL_DIR..."
sudo rm -rf "$INSTALL_DIR"
sudo mkdir -p "$INSTALL_DIR"
sudo cp "$REPO_TEMP/Agent_For_Tank/tank_agent" "$INSTALL_DIR/"
sudo cp "$REPO_TEMP/Agent_For_Tank/.env_example" "$INSTALL_DIR/"
sudo chmod +x "$INSTALL_DIR/tank_agent"

# 4. Initialize Git Repo for Update Tracking
echo "🔗 Initializing Git tracking for updates..."
cd "$INSTALL_DIR"
sudo git init
sudo git remote add origin "$REPO_URL"

# 5. Set Up systemd Service and Timer
echo "🛠️ Configuring systemd..."
sudo cp "$REPO_TEMP/systemd/tank_agent.service" /etc/systemd/system/
sudo cp "$REPO_TEMP/systemd/tank_agent.timer" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable tank_agent.timer --now

# 6. Clean Up Temporary Files
echo "🧹 Cleaning up temporary files..."
rm -rf "$REPO_TEMP"

echo "🎉 Installation complete! Tank-Agent is ready and monitored via Git."
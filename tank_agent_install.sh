#!/bin/bash

echo "🚀 Starting Tank-Agent Installation..."

INSTALL_DIR="/opt/Tank-Agent"
REPO_TEMP="$(pwd)/Tank-Agent-Temp"

# 1. Check Dependencies
echo "🔍 Checking 'sudo' and 'git'..."
command -v sudo >/dev/null || { echo "❌ sudo not found. Aborting."; exit 1; }
command -v git >/dev/null || { 
  echo "📦 Installing git..."
  sudo apt update && sudo apt install -y git
}

# 2. Clone only necessary folder
echo "📁 Cloning Tank-Agent repo..."
rm -rf "$REPO_TEMP"
git clone --depth 1 --branch master https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$REPO_TEMP" || {
  echo "❌ Git clone failed. Aborting."
  exit 1
}

# 3. Move core files to /opt
echo "📦 Installing to $INSTALL_DIR..."
sudo rm -rf "$INSTALL_DIR"
sudo mkdir -p "$INSTALL_DIR"
sudo cp "$REPO_TEMP/Agent_For_Tank/tank_agent" "$INSTALL_DIR/"
sudo cp "$REPO_TEMP/Agent_For_Tank/.env_example" "$INSTALL_DIR/"
sudo chmod +x "$INSTALL_DIR/tank_agent"

# 4. Set up git repo inside /opt (linked to GitHub)
echo "🔗 Initializing Git tracking for updates..."
cd "$INSTALL_DIR"
sudo git init
sudo git remote add origin https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git

# 5. Set up systemd service + timer
echo "🛠️ Configuring systemd..."
sudo cp "$REPO_TEMP/systemd/tank_agent.service" /etc/systemd/system/
sudo cp "$REPO_TEMP/systemd/tank_agent.timer" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable tank_agent.timer --now

# 6. Clean up clone repo and temp files
echo "🧹 Cleaning up unnecessary files..."
rm -rf "$REPO_TEMP"

echo "🎉 Installation complete! Agent is ready and tracked via Git."

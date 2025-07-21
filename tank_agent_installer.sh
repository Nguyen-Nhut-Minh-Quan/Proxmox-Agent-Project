#!/bin/bash

echo "🚀 Proxmox Agent Remote Installer Starting..."

# Check and install Git
if ! command -v git &> /dev/null; then
  echo "📦 Git not found — installing it now..."
  apt-get update
  apt-get install git -y || { echo "❌ Git install failed — aborting."; exit 1; }
else
  echo "✅ Git is already installed."
fi

# Define install path
INSTALL_DIR="$HOME/Tank-Agent"

# Clean install path
if [ -d "$INSTALL_DIR" ]; then
  echo "🧹 Removing existing directory at $INSTALL_DIR..."
  rm -rf "$INSTALL_DIR"
fi
mkdir -p "$INSTALL_DIR"

# Clone repo into temp directory
TMP_DIR="/tmp/proxmox_install"
rm -rf "$TMP_DIR"
git clone https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$TMP_DIR"

# Move desired components
echo "📁 Copying required files..."
cp -r "$TMP_DIR/Agent_For_Tank" "$INSTALL_DIR/"
cp "$TMP_DIR/install_tank.sh" "$INSTALL_DIR/"
mkdir -p "$INSTALL_DIR/system"
cp "$TMP_DIR/system/tank_agent.service" "$INSTALL_DIR/system/"
cp "$TMP_DIR/system/tank_agent.timer" "$INSTALL_DIR/system/"

# Cleanup temp folder
rm -rf "$TMP_DIR"

# Launch installer
cd "$INSTALL_DIR" || { echo "❌ Failed to enter $INSTALL_DIR"; exit 1; }

echo "🛠 Setting executable permissions..."
chmod +x install_tank.sh

echo "🚦 Launching installer..."
./install_tank.sh | tee ~/install_log.txt

echo "✅ Installer complete. Log saved to ~/install_log.txt"

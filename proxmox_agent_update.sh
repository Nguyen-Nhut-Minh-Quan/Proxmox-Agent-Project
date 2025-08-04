#!/bin/bash

echo "🔄 Starting Proxmox-Agent update..."

INSTALL_DIR="/opt/Proxmox-Agent"
TEMP_CLONE="/tmp/proxmox-agent-update-temp"

# 1. Clone fresh copy to temp directory
echo "📥 Fetching latest version from GitHub..."
rm -rf "$TEMP_CLONE"
git clone --depth 1 --branch master https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$TEMP_CLONE" || {
  echo "❌ Failed to clone repo. Aborting update."; exit 1;
}

# 2. Copy updated files to install dir (excluding .env)
echo "📦 Updating files (preserving .env)..."
sudo cp "$TEMP_CLONE/Agent_For_Server/proxmox_agent" "$INSTALL_DIR/"
sudo cp "$TEMP_CLONE/Agent_For_Server/VirtualServerStat.sh" "$INSTALL_DIR/"
sudo chmod +x "$INSTALL_DIR/VirtualServerStat.sh"
sudo cp "$TEMP_CLONE/Agent_For_Server/.env_example" "$INSTALL_DIR/"

# 3. Refresh systemd files if changed
echo "🔧 Updating systemd service and timer..."
sudo cp "$TEMP_CLONE/systemd/proxmox_agent.service" /etc/systemd/system/
sudo cp "$TEMP_CLONE/systemd/proxmox_agent.timer" /etc/systemd/system/
sudo systemctl daemon-reload

# 4. Clean up temp folder
echo "🧹 Cleaning up temporary files..."
rm -rf "$TEMP_CLONE"

echo "✅ Update complete! Your .env file remains untouched."
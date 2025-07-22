#!/bin/bash

INSTALL_DIR="/opt/Proxmox-Agent"
REPO_DIR="$(pwd)/Proxmox-Agent-Project"
REPO_URL="https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git"

echo "🔄 Updating Proxmox-Agent..."

# Always re-clone fresh
rm -rf "$REPO_DIR"
git clone --branch master "$REPO_URL" "$REPO_DIR" || {
  echo "❌ Git clone failed. Aborting."; exit 1;
}

# Move agent folder into install dir
sudo rm -rf "$INSTALL_DIR"
sudo mv "$REPO_DIR/Agent_For_Server" "$INSTALL_DIR"

# Compile the agent binary
echo "🛠️ Building proxmox_agent..."
cd "$INSTALL_DIR" || { echo "❌ Install dir missing. Abort."; exit 1; }
gcc -o proxmox_agent proxmox_agent.c -lcurl || {
  echo "❌ Compilation failed."; exit 1;
}

# Add build timestamp check
echo "✅ Build complete. Binary timestamp: $(date)"

# Create global symlink so one-liners always use latest binary
sudo ln -sf "$INSTALL_DIR/proxmox_agent" /usr/local/bin/proxmox_agent

# Reload and restart systemd units
echo "🔁 Restarting systemd timer..."
sudo systemctl daemon-reload
sudo systemctl restart proxmox_agent.timer

# Clean up cloned repo
rm -rf "$REPO_DIR"
rm -rf "/opt/Proxmox-Agent-Project"
echo "🎉 Update complete! Agent is synced, compiled, linked, and live."
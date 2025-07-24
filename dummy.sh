#!/bin/bash

INSTALL_DIR="/opt/Proxmox-Agent"
REPO_BRANCH="master"

echo "🔄 Starting Proxmox-Agent update..."

# Check if install dir is a git repo
if [ ! -d "$INSTALL_DIR/.git" ]; then
  echo "❌ ERROR: $INSTALL_DIR is not a valid Git repository."
  echo "💡 Please reinstall using the updated install.sh script."
  exit 1
fi

cd "$INSTALL_DIR" || { echo "❌ Failed to access $INSTALL_DIR"; exit 1; }

# Backup .env if it exists
if [ -f ".env" ]; then
  echo "💾 Backing up .env file..."
  cp .env /tmp/proxmox_env_backup
fi

# Pull latest changes
echo "🔁 Pulling latest updates from GitHub..."
git fetch origin
git reset --hard "origin/$REPO_BRANCH" || { echo "❌ Git reset failed. Abort."; exit 1; }

# Restore .env after reset
if [ -f /tmp/proxmox_env_backup ]; then
  echo "♻️ Restoring user .env file..."
  mv /tmp/proxmox_env_backup .env
fi

# Rebuild the agent binary
echo "🛠️ Recompiling proxmox_agent..."
gcc -o proxmox_agent proxmox_agent.c -lcurl || { echo "❌ Compilation failed."; exit 1; }

# Make script executable if present
if [ -f "VirtualServerStat.sh" ]; then
  chmod +x VirtualServerStat.sh
  echo "🔓 VirtualServerStat.sh is executable."
fi

# Refresh systemd timer
echo "🔁 Restarting systemd timer..."
sudo systemctl daemon-reload
sudo systemctl restart proxmox_agent.timer

# Update global symlink
sudo ln -sf "$INSTALL_DIR/proxmox_agent" /usr/local/bin/proxmox_agent

# Optional diff check with .env_example
if ! diff -q .env .env_example &>/dev/null; then
  echo "⚠️ .env and .env_example differ — consider updating missing variables."
fi

echo "🎉 Update complete! Agent is rebuilt, config preserved, and timer reloaded."
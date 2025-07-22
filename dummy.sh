#!/bin/bash

REPO_DIR="$(pwd)/Proxmox-Agent-Project"
INSTALL_DIR="/opt/Proxmox-Agent"

echo "🔄 Checking for updates in Proxmox-Agent-Project..."

cd "$REPO_DIR" || { echo "❌ Repo directory not found. Abort."; exit 1; }
git fetch origin master

LOCAL_COMMIT=$(git rev-parse HEAD)
REMOTE_COMMIT=$(git rev-parse origin/master)

if [ "$LOCAL_COMMIT" != "$REMOTE_COMMIT" ]; then
  echo "🚀 New update detected! Pulling changes..."
  git pull origin master || { echo "❌ Git pull failed. Abort."; exit 1; }

  echo "🔄 Syncing Agent_For_Server folder to $INSTALL_DIR..."
  sudo rm -rf "$INSTALL_DIR"
  sudo mv "$REPO_DIR/Agent_For_Server" "$INSTALL_DIR"

  echo "🛠️ Recompiling C agent..."
  cd "$INSTALL_DIR" && gcc -o proxmox_agent proxmox_agent.c -lcurl

  echo "📦 Reloading systemd units..."
  sudo systemctl daemon-reload
  sudo systemctl restart proxmox_agent.timer
  echo "✅ Update complete and service restarted!"
else
  echo "✅ No updates found. Everything is up-to-date."
fi
#!/bin/bash

INSTALL_DIR="/opt/Proxmox-Agent"
REPO_DIR="$(pwd)/Proxmox-Agent-Project"
REPO_URL="https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git"

echo "🔄 Checking for updates to Proxmox-Agent-Project..."

# Clone fresh repo if it doesn't exist
if [ ! -d "$REPO_DIR" ]; then
  echo "📥 Local repo missing — cloning fresh copy..."
  git clone --branch master "$REPO_URL" "$REPO_DIR" || {
    echo "❌ Git clone failed. Abort."
    exit 1
  }
else
  echo "✅ Repo found. Fetching latest..."
  cd "$REPO_DIR" && git fetch origin master
fi

cd "$REPO_DIR" || { echo "❌ Couldn't enter repo directory. Abort."; exit 1; }

LOCAL_COMMIT=$(git rev-parse HEAD)
REMOTE_COMMIT=$(git rev-parse origin/master)

if [ "$LOCAL_COMMIT" != "$REMOTE_COMMIT" ]; then
  echo "🚀 Update detected! Syncing latest files..."
  git pull origin master || { echo "❌ Git pull failed. Abort."; exit 1; }

  echo "🔄 Moving updated Agent_For_Server to $INSTALL_DIR..."
  sudo rm -rf "$INSTALL_DIR"
  sudo mv "$REPO_DIR/Agent_For_Server" "$INSTALL_DIR"

  echo "🛠️ Recompiling agent..."
  cd "$INSTALL_DIR" && gcc -o proxmox_agent proxmox_agent.c -lcurl || {
    echo "❌ Compilation failed."; exit 1;
  }

  echo "📦 Restarting systemd service..."
  sudo systemctl daemon-reload
  sudo systemctl restart proxmox_agent.timer

  echo "✅ Update applied successfully!"
else
  echo "📡 No new updates. Agent is up-to-date."
fi

sudo rm -rf "$REPO_DIR"
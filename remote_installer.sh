#!/bin/bash

echo "📦 Cloning project to home directory..."

# Clean up previous install if it exists
if [ -d "$HOME/Proxmox-Agent-Project" ]; then
  echo "🧹 Removing old project folder..."
  rm -rf "$HOME/Proxmox-Agent-Project"
fi

# Clone the repo into ~/Proxmox-Agent-Project
git clone https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$HOME/Proxmox-Agent-Project"

# Move into the folder
cd "$HOME/Proxmox-Agent-Project" || {
  echo "❌ Failed to enter project directory"
  exit 1
}

# Make sure the installer script exists
if [ ! -f install_agent.sh ]; then
  echo "❌ install_agent.sh not found in $(pwd)"
  ls -la
  exit 1
fi

# Run the installer
chmod +x install_agent.sh
./install_agent.sh
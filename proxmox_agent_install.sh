#!/bin/bash

echo "🚀 Starting Proxmox Agent Setup..."
# Check for Git and install if missing
if ! command -v git &>/dev/null; then
  echo "📦 Git not found — installing..."
  sudo apt update && sudo apt install git -y || {
    echo "❌ Git installation failed. Aborting setup."
    exit 1
  }
else
  echo "✅ Git is already installed."
fi
# Clone the repo
git clone https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git ~/Proxmox-Agent-Project

# Navigate into the repo
cd ~/Proxmox-Agent-Project || { echo "❌ Failed to enter directory"; exit 1; }

# Make the script executable and run it
chmod +x install_proxmox.sh
./install_proxmox.sh

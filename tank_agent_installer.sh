#!/bin/bash

echo "🚀 Starting Proxmox Agent Setup..."

# Clone the repo
git clone https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git ~/Proxmox-Agent-Project

# Navigate into the repo
cd ~/Proxmox-Agent-Project || { echo "❌ Failed to enter directory"; exit 1; }

# Make the script executable and run it
chmod +x install_tank.sh
./install_tank.sh

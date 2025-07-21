#!/bin/bash

echo "🚀 Starting Proxmox Agent Setup..."

# Clone the repo
git clone https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git ~/Tank-Agent

# Navigate into the repo
cd ~/Tank-Agent || { echo "❌ Failed to enter directory"; exit 1; }

# Make the script executable and run it
chmod +x proxmox_agent_install.sh
./proxmox_agent_install.sh

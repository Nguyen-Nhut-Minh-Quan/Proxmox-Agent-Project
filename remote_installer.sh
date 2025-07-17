#!/bin/bash

echo "📦 Cloning the Proxmox Agent Project..."

git clone https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git /opt/Proxmox-Agent-Project

cd /opt/Proxmox-Agent-Project || exit 1

chmod +x install_agent.sh
./install_agent.sh
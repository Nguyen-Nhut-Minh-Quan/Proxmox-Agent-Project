#!/bin/bash
<<<<<<< HEAD
echo " Proxmox Agent Remote Installer Starting..."

if ! command -v git &> /dev/null; then
  echo "📦 Git not found — installing it now..."
  apt-get update
  apt-get install git -y || { echo "❌ Git install failed — aborting."; exit 1; }
=======

echo "🚀 Starting Proxmox Agent Setup..."
# Check for sudo and install if missing
if ! command -v sudo &>/dev/null; then
  echo "🔧 'sudo' not found — installing it now..."
  apt update && apt install sudo -y || {
    echo "❌ Failed to install 'sudo'. Aborting setup."
    exit 1
  }
else
  echo "✅ 'sudo' is already installed."
fi
# Check for Git and install if missing
if ! command -v git &>/dev/null; then
  echo "📦 Git not found — installing..."
  sudo apt update && sudo apt install git -y || {
    echo "❌ Git installation failed. Aborting setup."
    exit 1
  }
>>>>>>> edc514073ffa846eea1653b470ffc6fda57db715
else
  echo "✅ Git is already installed."
fi
# Clone the repo
git clone https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git ~/Proxmox-Agent-Project

# Navigate into the repo
cd ~/Proxmox-Agent-Project || { echo "❌ Failed to enter directory"; exit 1; }

# Make the script executable and run it
chmod +x install_tank.sh
./install_tank.sh

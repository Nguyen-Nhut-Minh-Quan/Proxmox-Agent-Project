#!/bin/bash

echo " Proxmox Agent Remote Installer Starting..."

if ! command -v git &> /dev/null; then
  echo "📦 Git not found — installing it now..."
  apt-get update
  apt-get install git -y || { echo "❌ Git install failed — aborting."; exit 1; }
else
  echo "✅ Git is already installed."
fi
# Define install path
INSTALL_DIR="$HOME/Tank-Agent"

# Step 1: Remove old project folder if it exists
if [ -d "$INSTALL_DIR" ]; then
  echo " Removing existing directory at $INSTALL_DIR..."
  rm -rf "$INSTALL_DIR"
fi

# Step 2: Clone the repository
echo " Cloning repository into $INSTALL_DIR..."
git clone https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$INSTALL_DIR"

#  Step 3: Move into project folder
cd "$INSTALL_DIR" || {
  echo " Failed to enter project directory"
  exit 1
}

# Step 4: Ensure working files are checked out
echo " Checking out master branch..."
git checkout master

#  Step 5: Verify install_agent.sh exists
if [ ! -f install_proxmox.sh ]; then
  echo " install_agent.sh not found in $(pwd)"
  echo " Contents of folder:"
  ls -la
  exit 1
fi

#  Step 6: Run install_agent.sh with logging
echo "✅ install_tank.sh found — launching installer..."
chmod +x install_tank.sh
./install_tank.sh | tee ~/install_log.txt

echo " Installer complete. Check ~/install_log.txt for full output."
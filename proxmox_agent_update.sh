#!/bin/bash

echo "🔄 Starting Proxmox-Agent update..."

INSTALL_DIR="/opt/Proxmox-Agent"
TEMP_CLONE="/tmp/proxmox-agent-update-temp"
check_and_install() {
    local command_name=$1
    local package_name=$2

    if ! command -v "$command_name" &> /dev/null
    then
        echo "[INFO] '$command_name' not found. Installing package '$package_name'..."
        sudo apt-get update
        sudo apt-get install -y "$package_name"
    else
        echo "[INFO] '$command_name' is already installed."
    fi
}

# Check for `pvesm` (Proxmox-specific command)
if ! command -v pvesm &> /dev/null
then
    echo "[WARN] 'pvesm' command not found. This script should be run on a Proxmox host."
else
    echo "[INFO] 'pvesm' is installed (Proxmox host confirmed)."
fi

# Check for other monitoring tools
check_and_install "mpstat" "sysstat"
check_and_install "free" "procps"
check_and_install "uptime" "procps"
check_and_install "nproc" "coreutils"
check_and_install "ip" "iproute2"
check_and_install "sensors" "lm-sensors"
check_and_install "awk" "gawk"
check_and_install "grep" "grep"
check_and_install "jq" "jq"
# 1. Clone fresh copy to temp directory
echo "📥 Fetching latest version from GitHub..."
rm -rf "$TEMP_CLONE"
git clone --depth 1 --branch master https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$TEMP_CLONE" || {
  echo "❌ Failed to clone repo. Aborting update."; exit 1;
}

# 2. Copy updated files to install dir (excluding .env)
echo "📦 Updating files (preserving .env)..."
sudo cp "$TEMP_CLONE/Agent_For_Server/proxmox_agent" "$INSTALL_DIR/"
sudo cp "$TEMP_CLONE/Agent_For_Server/VirtualServerStat.sh" "$INSTALL_DIR/"
sudo chmod +x "$INSTALL_DIR/VirtualServerStat.sh"
sudo cp "$TEMP_CLONE/Agent_For_Server/.env_example" "$INSTALL_DIR/"

# 3. Refresh systemd files if changed
echo "🔧 Updating systemd service and timer..."
sudo cp "$TEMP_CLONE/systemd/proxmox_agent.service" /etc/systemd/system/
sudo cp "$TEMP_CLONE/systemd/proxmox_agent.timer" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl restart proxmox_agent.service

# 4. Clean up temp folder
echo "🧹 Cleaning up temporary files..."
rm -rf "$TEMP_CLONE"

echo "✅ Update complete! Your .env file remains untouched."
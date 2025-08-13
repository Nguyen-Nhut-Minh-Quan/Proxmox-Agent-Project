#!/bin/bash

echo "🚀 Starting Proxmox-Agent Installation..."

INSTALL_DIR="/opt/Proxmox-Agent"
REPO_TEMP="$(pwd)/Proxmox-Agent-Temp"

# --- 1. Check and Install Sudo ---
echo "🔍 Checking for sudo..."
if ! command -v sudo &> /dev/null
then
    echo "❌ sudo not found. Attempting to install..."
    # Check if we are running as root to install sudo
    if [[ "$EUID" -ne 0 ]]; then
        echo "🚨 To install sudo, you must run this script as the root user. Aborting."
        exit 1
    fi
    apt update && apt install -y sudo
    echo "✅ sudo has been installed."
else
    echo "✅ sudo is already installed."
fi

# --- 2. Check and Install Git ---
echo "🔍 Checking for git..."
if ! command -v git &> /dev/null
then
    echo "📦 Installing git..."
    sudo apt update && sudo apt install -y git
else
    echo "✅ git is already installed."
fi

# --- 3. Check and Install Other Dependencies ---
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

echo "--- All dependency checks complete ---"

# --- 4. Clone repository ---
echo "📁 Cloning Proxmox-Agent repo..."
rm -rf "$REPO_TEMP"
git clone --depth 1 --branch master https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$REPO_TEMP" || {
    echo "❌ Git clone failed. Aborting."; exit 1;
}

# --- 5. Install files ---
echo "📦 Installing to $INSTALL_DIR..."
sudo rm -rf "$INSTALL_DIR"
sudo mkdir -p "$INSTALL_DIR"

sudo cp "$REPO_TEMP/Agent_For_Server/proxmox_agent" "$INSTALL_DIR/"
sudo cp "$REPO_TEMP/Agent_For_Server/VirtualServerStat.sh" "$INSTALL_DIR/"
sudo cp "$REPO_TEMP/Agent_For_Server/.env_example" "$INSTALL_DIR/"
sudo chmod +x "$INSTALL_DIR/./proxmox_agent"
sudo chmod +x "$INSTALL_DIR/VirtualServerStat.sh"

# --- 6. Set up git repo for updates ---
echo "🔗 Initializing Git tracking for updates..."
cd "$INSTALL_DIR"
sudo git init
sudo git remote add origin https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git

# --- 7. Set up systemd service and timer ---
echo "🛠️ Configuring systemd..."
sudo cp "$REPO_TEMP/systemd/proxmox_agent.service" /etc/systemd/system/
sudo cp "$REPO_TEMP/systemd/proxmox_agent.timer" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable proxmox_agent.timer --now

# --- 8. Cleanup and finish ---
echo "🧹 Cleaning up unnecessary files..."
rm -rf "$REPO_TEMP"

echo "🎉 Installation complete! Agent is ready and tracked via Git."
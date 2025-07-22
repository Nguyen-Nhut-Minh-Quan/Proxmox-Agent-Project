#!/bin/bash

echo "🚀 Starting Agent_For_Server Installation..."

INSTALL_DIR="/opt/Tank-Agent"
REPO_DIR="/root/Tank-Agent-Project"

# Ensure dependencies

echo "🔍 Checking for 'sudo'..."
if ! command -v sudo &>/dev/null; then
  echo "🔧 'sudo' not found — installing..."
  apt update && apt install sudo -y || {
    echo "❌ Failed to install 'sudo'. Aborting."; exit 1;
  }
else
  echo "✅ 'sudo' is already installed."
fi

echo "🔍 Checking for 'git'..."
if ! command -v git &>/dev/null; then
  echo "📦 Git not found — installing..."
  sudo apt update && sudo apt install git -y || {
    echo "❌ Failed to install 'git'. Aborting."; exit 1;
  }
else
  echo "✅ 'git' is already installed."
fi

# Clean up and clone
echo "📁 Fetching Agent_For_Server files..."
sudo rm -rf "$INSTALL_DIR"
rm -rf "$REPO_DIR"
git clone --branch master https://github.com/Nguyen-Nhut-Minh-Quan/Proxmox-Agent-Project.git "$REPO_DIR" || {
  echo "❌ Git clone failed. Aborting."; exit 1;
}
#!/bin/bash

# Check if 'sensors' command exists
if ! command -v sensors &> /dev/null; then
    echo "'sensors' not found. Installing lm-sensors..."
    sudo apt update && sudo apt install -y lm-sensors

    # Optional: run sensors-detect interactively
    echo "Running sensors-detect to configure available sensors..."
    sudo sensors-detect --auto
else
    echo "'sensors' is already installed."
fi

if ! command -v mpstat &> /dev/null; then
    echo "'mpstat' not found. Installing sysstat..."
    sudo apt update && sudo apt install -y sysstat
else
    echo "'mpstat' is already installed."
fi
if ! command -v gcc &>/dev/null; then
  echo "🧰 GCC compiler not found — installing build-essential..."
  sudo apt update && sudo apt install build-essential -y || {
    echo "❌ GCC installation failed. Aborting setup."
    exit 1
  }
else
  echo "✅ GCC is already installed."
fi

if ! dpkg -s libcurl4-openssl-dev &>/dev/null; then
  echo "📦 Installing libcurl development package..."
  sudo apt update && sudo apt install -y libcurl4-openssl-dev || {
    echo "❌ Failed to install libcurl headers. Aborting."
    exit 1
  }
else
  echo "✅ libcurl development package is already installed."
fi

# Move only Agent_For_Server folder
sudo mv "$REPO_DIR/Agent_For_Tank" "$INSTALL_DIR"

# Compile C agent
echo "🛠️ Compiling C agent..."
cd "$INSTALL_DIR" || { echo "❌ Couldn't enter $INSTALL_DIR"; exit 1; }

gcc tank_agent.c -o tank_agent -I/opt/picoscope/include -L/opt/picoscope/lib -lusbtc08 -lcurl || {
  echo "❌ Compilation failed."
  exit 1
}


# Verify .env exists
if [ ! -f ".env" ]; then
  echo "❌ .env file is missing. Abort."
  exit 1
fi

# Systemd setup
echo "📦 Setting up systemd service units..."
sudo cp "$REPO_DIR/systemd/tank_agent.service" /etc/systemd/system/
sudo cp "$REPO_DIR/systemd/tank_agent.timer" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable tank_agent.timer --now
sudo rm -rf "$REPO_DIR"

echo "🎉 Installation complete! Agent is compiled, active, and scheduled via systemd."
#!/bin/bash

echo "🔧 Proxmox Agent Auto Installer Starting..."

# Step 1: Ensure script runs from its own directory
cd "$(dirname "$0")" || exit 1
echo "📂 Current working directory: $(pwd)"

# Step 2: Load environment variables
if [ -f ./src/.env ]; then
    source ./src/.env
    echo "Environment variables loaded from .env"
else
    echo ".env file not found in ./src"
    exit 1
fi

# Step 3: Make bash helper executable
chmod +x ./src/VirtualServerStat.sh
echo " VirtualServerStat.sh made executable"

# Step 4: Compile C agents
echo " Compiling Test.c..."
gcc -g -o ./src/test ./src/Test.c $(pkg-config --cflags --libs libmongoc-1.0)

echo " Compiling Temp_Read.c..."
gcc -o ./src/main ./src/Temp_Read.c -I/opt/picoscope/include -L/opt/picoscope/lib -lusbtc08 $(pkg-config --cflags --libs libmongoc-1.0)

# Step 5: Install systemd service/timer units
echo " Copying systemd units..."
sudo cp ./systemd/sensor_reader.service /etc/systemd/system/
sudo cp ./systemd/sensor_reader.timer /etc/systemd/system/
sudo cp ./systemd/sensor.service /etc/systemd/system/
sudo cp ./systemd/sensor.timer /etc/systemd/system/

# Step 6: Reload systemd and activate timers
echo "🔄 Reloading systemd..."
sudo systemctl daemon-reload

echo "🚀 Enabling timers..."
sudo systemctl enable sensor_reader.timer --now
sudo systemctl enable sensor.timer --now

echo "🎉 All systems go. Proxmox agents are installed and running."
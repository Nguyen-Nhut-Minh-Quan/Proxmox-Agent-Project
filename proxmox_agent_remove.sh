#!/bin/bash
echo "remove service...."

SERVICE_NAME = "proxmox_agent.service"
TIMER_NAME ="proxmox_agent.timer"

sudo systemctl stop "$TIMER_NAME"
sudo systemctl stop "$SERVICE_NAME"

# Disable services to prevent auto-start
sudo systemctl disable "$TIMER_NAME"
sudo systemctl disable "$SERVICE_NAME"

sudo rm "/etc/systemd/system/$SERVICE_NAME"
sudo rm "/etc/systemd/system/$TIMER_NAME" 

echo "finishing remove"

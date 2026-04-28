#!/bin/bash
set -e

INSTALL_DIR="/home/pi/phone-call-light"
SERVICE_NAME="phone-call-light"

echo "=== Phone Call Light — Setup ==="

# Copy project files
sudo cp -r "$(dirname "$0")/.." "$INSTALL_DIR"

# Create virtualenv and install dependencies
python3 -m venv "$INSTALL_DIR/venv"
"$INSTALL_DIR/venv/bin/pip" install -r "$INSTALL_DIR/requirements.txt"

# Set auth token
echo ""
read -rp "Set auth token (press Enter to keep default 'change-me-please'): " TOKEN
TOKEN="${TOKEN:-change-me-please}"
sudo sed -i "s/change-me-please/$TOKEN/g" /etc/systemd/system/$SERVICE_NAME.service
sudo sed -i "s/change-me-please/$TOKEN/g" "$INSTALL_DIR/setup/$SERVICE_NAME.service"

# Install and enable systemd service
sudo cp "$INSTALL_DIR/setup/$SERVICE_NAME.service" /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_NAME"
sudo systemctl start "$SERVICE_NAME"

echo ""
echo "=== Done! ==="
echo "Server running on port 5000"
echo "Auth token: $TOKEN"
echo ""
echo "Check status: sudo systemctl status $SERVICE_NAME"
echo "View logs:    journalctl -u $SERVICE_NAME -f"

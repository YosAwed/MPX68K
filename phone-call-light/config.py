import os

# GPIO pin connected to relay module (BCM numbering)
RELAY_PIN = 17

# Flask server settings
HOST = "0.0.0.0"
PORT = 5000

# Simple auth token — change this to a random string
AUTH_TOKEN = os.environ.get("LAMP_AUTH_TOKEN", "change-me-please")

# How long to keep the lamp on after call ends (seconds)
LAMP_OFF_DELAY = 3

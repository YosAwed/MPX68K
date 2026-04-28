#!/usr/bin/env python3
"""
Phone Call Light Server
Receives webhooks from Tasker (Android) and controls a patrol lamp via GPIO relay.

Endpoints:
  POST /call/start  — incoming call detected, turn lamp on
  POST /call/end    — call ended, turn lamp off
  GET  /status      — health check
"""

import atexit
import logging
from flask import Flask, request, jsonify

import config
import lamp

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger(__name__)

app = Flask(__name__)


def _check_auth():
    token = request.headers.get("X-Auth-Token") or request.args.get("token")
    return token == config.AUTH_TOKEN


@app.before_request
def require_auth():
    if request.path == "/status":
        return  # health check is public
    if not _check_auth():
        log.warning("Unauthorized request from %s", request.remote_addr)
        return jsonify({"error": "unauthorized"}), 401


@app.post("/call/start")
def call_start():
    caller = request.json.get("caller", "unknown") if request.is_json else "unknown"
    log.info("Incoming call from %s — lamp ON", caller)
    lamp.turn_on()
    return jsonify({"status": "lamp_on"})


@app.post("/call/end")
def call_end():
    log.info("Call ended — lamp OFF (delay: %ss)", config.LAMP_OFF_DELAY)
    lamp.turn_off(delay=config.LAMP_OFF_DELAY)
    return jsonify({"status": "lamp_off"})


@app.get("/status")
def status():
    return jsonify({"status": "ok"})


if __name__ == "__main__":
    lamp.setup()
    atexit.register(lamp.cleanup)
    log.info("Starting server on %s:%s", config.HOST, config.PORT)
    app.run(host=config.HOST, port=config.PORT)

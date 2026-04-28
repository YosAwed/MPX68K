import threading
import time

try:
    import RPi.GPIO as GPIO
    _SIMULATION = False
except ImportError:
    # Allows development/testing on non-Raspberry Pi machines
    _SIMULATION = True
    print("[lamp] RPi.GPIO not found — running in simulation mode")

import config

_off_timer: threading.Timer | None = None
_lock = threading.Lock()


def setup():
    if _SIMULATION:
        return
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(config.RELAY_PIN, GPIO.OUT, initial=GPIO.HIGH)  # HIGH = relay off (active-low)


def cleanup():
    if _SIMULATION:
        return
    GPIO.cleanup()


def turn_on():
    global _off_timer
    with _lock:
        _cancel_off_timer()
        _set_relay(True)


def turn_off(delay: float = 0):
    global _off_timer
    with _lock:
        _cancel_off_timer()
        if delay > 0:
            _off_timer = threading.Timer(delay, _set_relay, args=(False,))
            _off_timer.daemon = True
            _off_timer.start()
        else:
            _set_relay(False)


def _cancel_off_timer():
    global _off_timer
    if _off_timer is not None:
        _off_timer.cancel()
        _off_timer = None


def _set_relay(on: bool):
    state = "ON" if on else "OFF"
    if _SIMULATION:
        print(f"[lamp] Relay {state}")
        return
    # Active-low relay: LOW = on, HIGH = off
    GPIO.output(config.RELAY_PIN, GPIO.LOW if on else GPIO.HIGH)

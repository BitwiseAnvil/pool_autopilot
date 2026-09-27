"""Validate Atlas MQTT timestamps before exposing live values to HA or dosing."""
import asyncio
import json
import logging
import time

import voluptuous as vol
from homeassistant.components import mqtt
from homeassistant.const import EVENT_HOMEASSISTANT_STOP
from homeassistant.core import callback
from homeassistant.helpers import config_validation as cv

from .validation import KEYS, Telemetry

DOMAIN = "pool_telemetry"
CONFIG_SCHEMA = vol.Schema({DOMAIN: vol.Schema({vol.Required("topic_prefix"): cv.string})}, extra=vol.ALLOW_EXTRA)
_LOGGER = logging.getLogger(__name__)


async def async_setup(hass, config):
    base = config[DOMAIN]["topic_prefix"].rstrip("/")
    model = Telemetry()
    pending = {}
    worker = None
    timer = None
    stopped = False

    async def publish():
        nonlocal worker
        try:
            while pending and not stopped:
                key = next(iter(pending))
                packet = pending.pop(key)
                # Recheck immediately before publishing, even if a prior send stalled.
                if packet and model.values.get(key, (None,))[0] != packet:
                    continue
                if packet and not -2000 <= time.time() * 1000 - packet["measured_at_ms"] < 20000:
                    packet = None
                try:
                    await mqtt.async_publish(hass, f"{base}/live/{key}",
                                             json.dumps(packet or {"value": None}), qos=0, retain=False)
                except Exception:  # transport failures must not retain readings for replay
                    _LOGGER.warning("Could not publish pool availability for %s", key)
        finally:
            worker = None

    def enqueue(changed):
        nonlocal worker
        pending.update(changed)  # one latest result per sensor, never a FIFO backlog
        # HA eagerly starts tasks: a publish with no suspension can finish before
        # async_create_task returns and assigns its already-completed Task here.
        if pending and (worker is None or worker.done()) and not stopped:
            worker = hass.async_create_task(publish())

    def schedule():
        nonlocal timer
        if timer:
            timer.cancel()
        if not stopped:
            timer = hass.loop.call_later(model.next_delay(time.monotonic() * 1000), expire)

    def expire():
        enqueue(model.expire(int(time.time() * 1000), time.monotonic() * 1000))
        schedule()

    @callback
    def received(message):
        try:
            packet = json.loads(message.payload)
        except (ValueError, TypeError):
            return
        key = message.topic.rsplit("/", 1)[-1]
        enqueue(model.receive(key, packet, message.retain,
                              int(time.time() * 1000), time.monotonic() * 1000))
        schedule()

    enqueue({key: None for key in KEYS})
    schedule()
    unsubscribers = [await mqtt.async_subscribe(hass, f"{base}/reading/+", received, qos=0),
                     await mqtt.async_subscribe(hass, f"{base}/diagnostics", received, qos=0)]

    async def stop(event):
        nonlocal stopped
        stopped = True
        if timer:
            timer.cancel()
        for unsubscribe in unsubscribers:
            unsubscribe()
        if worker:
            worker.cancel()
            try:
                await worker
            except asyncio.CancelledError:
                pass
        pending.clear()

    hass.bus.async_listen_once(EVENT_HOMEASSISTANT_STOP, stop)
    hass.data[DOMAIN] = model
    return True

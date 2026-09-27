"""Live measurement admission and expiry; no history or persistent replay queue."""
import math

KEYS = ("ph", "orp", "temperature", "conductivity", "salinity", "tds")
SOURCE = "time.nist.gov"


def timestamp(value):
    return type(value) is int and 1577836800000 <= value < 4102444800000


def fresh(value, now, limit=20000):
    return timestamp(value) and -2000 <= now - value < limit


class Telemetry:
    def __init__(self):
        self.diagnostic = None
        self.diagnostic_deadline = 0
        self.values = {}
        self.last_wall = self.last_mono = None

    def expire(self, now, mono):
        changed = {}
        jumped = self.last_wall is not None and abs(
            (now - self.last_wall) - (mono - self.last_mono)) > 2000
        self.last_wall, self.last_mono = now, mono
        if jumped or self.diagnostic is None or mono >= self.diagnostic_deadline or not fresh(
                self.diagnostic.get("timestamp_ms"), now, 10000):
            self.diagnostic = None
            changed = {key: None for key in self.values}
            self.values.clear()
        else:
            for key, (packet, deadline) in list(self.values.items()):
                if mono >= deadline or not fresh(packet["measured_at_ms"], now):
                    changed[key] = None
                    del self.values[key]
        return changed

    def receive(self, key, packet, retained, now, mono):
        changed = self.expire(now, mono)
        if retained or not isinstance(packet, dict):
            return changed
        if key == "diagnostics":
            valid = (packet.get("diagnostic_version") == 2 and packet.get("time_source") == SOURCE and packet.get("clock_healthy") is True
                     and isinstance(packet.get("boot"), str) and 0 < len(packet["boot"]) <= 32
                     and type(packet.get("clock_epoch")) is int
                     and 0 <= packet["clock_epoch"] <= 4294967295
                     and packet.get("maintenance") is False
                     and packet.get("recovery") is False and packet.get("configuration_ok") is True
                     and packet.get("status") == "Monitoring"
                     and fresh(packet.get("timestamp_ms"), now, 10000))
            if not valid:
                changed.update({name: None for name in self.values})
                self.values.clear()
                self.diagnostic = None
                return changed
            previous = self.diagnostic
            context = (packet["boot"], packet["clock_epoch"])
            if previous and context == (previous["boot"], previous["clock_epoch"]):
                if packet["timestamp_ms"] <= previous["timestamp_ms"]:
                    return changed  # a duplicate cannot renew the diagnostic deadline
            else:
                changed.update({name: None for name in self.values})
                self.values.clear()
            self.diagnostic = packet
            self.diagnostic_deadline = mono + min(10000, 10000 - (now - packet["timestamp_ms"]))
            return changed
        if key not in KEYS or self.diagnostic is None:
            return changed
        if (packet.get("boot") == self.diagnostic["boot"] and
                packet.get("clock_epoch") == self.diagnostic["clock_epoch"] and
                fresh(packet.get("timestamp_ms"), now, 10000) and packet.get("value") is None):
            if key in self.values:
                del self.values[key]
                changed[key] = None
            return changed
        valid = (packet.get("time_source") == SOURCE and packet.get("clock_healthy") is True
                 and packet.get("boot") == self.diagnostic["boot"]
                 and packet.get("clock_epoch") == self.diagnostic["clock_epoch"]
                 and fresh(packet.get("timestamp_ms"), now, 10000)
                 and fresh(packet.get("measured_at_ms"), now)
                 and type(packet.get("value")) in (float, int)
                 and -10000 <= packet["value"] <= 1e9 and math.isfinite(packet["value"]))
        if not valid:
            return changed
        previous = self.values.get(key)
        if previous and packet["measured_at_ms"] <= previous[0]["measured_at_ms"]:
            return changed
        # Small tolerated clock leads never buy additional lifetime.
        deadline = mono + min(20000, 20000 - (now - packet["measured_at_ms"]))
        self.values[key] = (dict(packet), deadline)
        changed[key] = dict(packet)
        return changed

    def next_delay(self, mono):
        deadlines = [deadline for _, deadline in self.values.values()]
        if self.diagnostic:
            deadlines.append(self.diagnostic_deadline)
        return max(0.001, min([1000] + [deadline - mono for deadline in deadlines]) / 1000)

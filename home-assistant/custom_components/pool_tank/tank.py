"""A display-only tank estimate based on accepted requested ounces."""
import math


def finite(value, minimum=0, maximum=4096):
    return type(value) in (int, float) and math.isfinite(value) and minimum <= value <= maximum


class Tank:
    def __init__(self, data=None):
        d = data if data is not None else dict(capacity_oz=1920.0, remaining_oz=None)
        if (not isinstance(d, dict) or not finite(d.get("capacity_oz")) or "remaining_oz" not in d or
                (d["remaining_oz"] is not None and not finite(d["remaining_oz"], 0, d["capacity_oz"]))):
            raise ValueError("Invalid stored tank estimate")
        self.data = dict(capacity_oz=d["capacity_oz"], remaining_oz=d["remaining_oz"])

    def observe(self, packet):
        """Consume only the immediate response to a manual or automatic dose call."""
        if not isinstance(packet, dict) or packet.get("response_version") != 3:
            raise ValueError("Unsupported controller response version")
        if packet.get("request_reply") != "accepted":
            return
        if not finite(packet.get("requested_oz"), 0.00001, 128):
            raise ValueError("Invalid requested dose")
        d = self.data
        if d["remaining_oz"] is not None:
            d["remaining_oz"] = max(0.0, d["remaining_oz"] - packet["requested_oz"])

    def baseline(self, level, capacity=None):
        capacity = self.data["capacity_oz"] if capacity is None else capacity
        if not finite(capacity) or not finite(level, 0, capacity):
            raise ValueError("Tank level must be within capacity")
        self.data.update(capacity_oz=capacity, remaining_oz=level)

    def capacity(self, value):
        if not finite(value):
            raise ValueError("Invalid tank capacity")
        self.data["capacity_oz"] = value
        if self.data["remaining_oz"] is not None:
            self.data["remaining_oz"] = min(value, self.data["remaining_oz"])

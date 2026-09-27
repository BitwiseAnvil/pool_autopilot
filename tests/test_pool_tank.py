"""Only immediate dose-call acceptance changes the display-only tank estimate."""
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("tank", ROOT / "home-assistant/custom_components/pool_tank/tank.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Tank = module.Tank


def response(**updates):
    return dict(dict(response_version=3, request_reply="accepted", result="accepted",
                     requested_oz=10, boot_id="a", accepted_sequence=1), **updates)


def restart(tank):
    return Tank(json.loads(json.dumps(tank.data)))


def main():
    tank = Tank()
    tank.baseline(100, 200)
    # A missed 10 oz acceptance followed by a busy rejection retains the old result.
    tank.observe(response(request_reply="busy"))
    assert tank.data["remaining_oz"] == 100
    for reply in (None, "none", "duplicate", "expired_or_unknown_request", "relay_fault"):
        tank.observe(response(request_reply=reply))
    missing = response()
    del missing["request_reply"]
    tank.observe(missing)
    assert tank.data == {"capacity_oz": 200, "remaining_oz": 100}

    tank.observe(response())
    assert tank.data["remaining_oz"] == 90
    before = dict(tank.data)
    for result in ("none", "accepted", "completed", "interrupted"):
        tank = restart(tank)
        tank.observe(response(request_reply=None, result=result, boot_id="new", accepted_sequence=0))
        assert tank.data == before  # No credit, catch-up or restart adjustment.
    tank.baseline(200)
    tank.observe(response(request_reply="busy"))
    assert tank.data["remaining_oz"] == 200
    # Separate accepted calls count independently; request markers are irrelevant.
    tank.observe(response(requested_oz=2))
    tank.observe(response(requested_oz=10))
    assert tank.data["remaining_oz"] == 188
    tank.capacity(100)
    assert tank.data["remaining_oz"] == 100
    tank.baseline(1)
    tank.observe(response())
    assert tank.data["remaining_oz"] == 0
    tank.observe(response())
    assert tank.data["remaining_oz"] == 0

    fresh = Tank()
    fresh.observe(response())
    assert fresh.data["remaining_oz"] is None
    fresh = restart(fresh)
    fresh.baseline(200)
    assert fresh.data["remaining_oz"] == 200  # The missed deduction stays missed.
    fresh.observe(response(requested_oz=2))
    assert fresh.data["remaining_oz"] == 198
    assert set(fresh.data) == {"capacity_oz", "remaining_oz"}
    # Loading an old dictionary must not retain or validate obsolete request markers.
    assert Tank(dict(fresh.data, version=1, boot="", sequence=-1)).data == fresh.data

    for updates in ({"response_version": 1}, {"response_version": 2},
                    {"requested_oz": float("nan")}, {"requested_oz": float("inf")},
                    {"requested_oz": -1}, {"requested_oz": 0}, {"requested_oz": True}):
        before = dict(tank.data)
        try:
            tank.observe(response(**updates))
        except ValueError:
            pass
        else:
            raise AssertionError(updates)
        assert tank.data == before
    for data in ([], {}, dict(capacity_oz=200), dict(capacity_oz=200, remaining_oz=201),
                 dict(capacity_oz=float("nan"), remaining_oz=0),
                 dict(capacity_oz=200, remaining_oz=True)):
        try:
            Tank(data)
        except ValueError:
            pass
        else:
            raise AssertionError(data)
    print("HA tank: immediate acceptance only, rejected/missing replies, two stored values, refill/correction and restarts passed")


if __name__ == "__main__":
    main()

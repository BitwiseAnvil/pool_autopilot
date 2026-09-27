"""Exercise the production receiver with simulated calendar and monotonic time."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("validation", ROOT / "home-assistant/custom_components/pool_telemetry/validation.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
Telemetry = module.Telemetry
EPOCH = 1790000000000


def packet(at=EPOCH, **changes):
    return dict(dict(value=7.8, measured_at_ms=at, timestamp_ms=at,
                     boot="atlas", clock_epoch=1, clock_healthy=True,
                     time_source="time.nist.gov", status="Monitoring",
                     maintenance=False, recovery=False, configuration_ok=True, diagnostic_version=2), **changes)


def diagnostic(model, at=EPOCH, mono=0, **changes):
    return model.receive("diagnostics", packet(at, **changes), False, at, mono)


def run():
    model = Telemetry()
    assert not model.receive("ph", packet(), False, EPOCH, 0)
    diagnostic(model)
    assert model.receive("ph", packet(), False, EPOCH, 0)["ph"]["value"] == 7.8
    assert not model.receive("ph", packet(), False, EPOCH+1000, 1000)
    for age in (20000,600000,30*86400000):
        assert not model.receive("ph", packet(EPOCH-age), False, EPOCH+1000, 1000)
    for changes in ({"measured_at_ms":None}, {"measured_at_ms":True},
                    {"measured_at_ms":EPOCH+2001}, {"clock_epoch":0},
                    {"clock_healthy":False}, {"time_source":"time.windows.com"},
                    {"value":float("nan")}, {"value":float("inf")}):
        assert not model.receive("ph", packet(**changes), False, EPOCH, 0)
    assert not model.receive("ph", packet(EPOCH+1000), True, EPOCH+1000,1000)
    # Diagnostics continue, but repeated readings never move their expiry.
    diagnostic(model,EPOCH+9000,9000)
    assert not model.receive("ph",packet(),False,EPOCH+9000,9000)
    diagnostic(model,EPOCH+18000,18000)
    assert not model.expire(EPOCH+19999,19999)
    assert model.expire(EPOCH+20000,20000)=={"ph":None}
    # New observers rebuild only from now, even after 30 or 50 days.
    for days in (30,50):
        now=EPOCH+days*86400000
        diagnostic(model,now,days*86400000)
        assert not model.receive("ph",packet(),False,now,days*86400000)
        assert "ph" in model.receive("ph",packet(now),False,now,days*86400000)
    # Host clock steps invalidate all previously observed data.
    assert model.expire(now-60000,days*86400000+1)=={"ph":None}
    now-=60000
    diagnostic(model,now,days*86400000+1,clock_epoch=2)
    assert "ph" in model.receive("ph",packet(now,clock_epoch=2),False,now,days*86400000+1)
    assert diagnostic(model,now+1,days*86400000+2,clock_epoch=3)=={"ph":None}
    # Duplicate diagnostics also expire; MQTT traffic is not proof of fresh monitoring.
    model=Telemetry(); diagnostic(model)
    model.receive("ph",packet(),False,EPOCH,0)
    assert not model.receive("diagnostics",packet(),False,EPOCH+9000,9000)
    assert model.expire(EPOCH+10000,10000)=={"ph":None}
    print("Production MQTT guard: stale/retained/duplicate/reordered packets, expiry, clock steps and long outages passed.")


if __name__ == "__main__":
    run()

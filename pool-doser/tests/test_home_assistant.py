"""Validate the package; --container executes its actions against fake services.

Container scenarios run in a throwaway Home Assistant image with no mounted
configuration. Set POOL_HA_IMAGE to test a specific image.
"""
import argparse
import os
from pathlib import Path
import subprocess

import yaml

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = yaml.safe_load((ROOT / "pool-doser-home-assistant.yaml").read_text())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--container", action="store_true")
    args = parser.parse_args()
    assert PACKAGE["input_text"]["doser_acid_session"]["initial"] == ""
    assert "initial" not in PACKAGE["input_boolean"]["doser_auto_enabled"]
    assert "switch.pool_doser_auto_enabled" not in str(PACKAGE)
    assert PACKAGE["script"]["doser_stop"]["sequence"][0]["action"] == "input_boolean.turn_off"
    assert PACKAGE["script"]["doser_dose"]["sequence"][0]["action"] == "esphome.pool_doser_dose"
    assert PACKAGE["template"][0]["triggers"][0]["entity_id"] == "sensor.pool_doser_last_delivered"
    assert len(PACKAGE["automation"]) == 2
    assert [item["mode"] for item in PACKAGE["automation"]] == ["single", "restart"]
    if args.container:
        source = "PACKAGE = " + repr(PACKAGE) + "\n"
        component = ROOT.parent / "home-assistant/custom_components/pool_tank"
        source += "TANK_SOURCE = " + repr((component / "tank.py").read_text()) + "\n"
        source += (ROOT / "tests/ha_acid_scenarios.py").read_text()
        image = os.environ.get("POOL_HA_IMAGE", "ghcr.io/home-assistant/home-assistant:stable")
        command = ["docker", "run", "--rm", "-i", "--entrypoint", "python", image, "-"]
        subprocess.run(command, input=source, text=True, check=True)
        restore = "PACKAGE = " + repr(PACKAGE) + "\n"
        restore += (ROOT / "tests/ha_auto_restore_scenarios.py").read_text()
        subprocess.run(command, input=restore, text=True, check=True)
        tank = "TANK_SOURCES = " + repr({p.name: p.read_text() for p in component.iterdir() if p.is_file()}) + "\n"
        tank += (ROOT / "tests/ha_tank_scenarios.py").read_text()
        subprocess.run(command, input=tank, text=True, check=True)
    else:
        print("HA package structure passed; use --container for schema and action scenarios.")


if __name__ == "__main__":
    main()

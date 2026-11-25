"""Parametric design model. All currents/durations below are assumptions, not measurements."""
import argparse
import json
import math

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--capacity-mah", type=float, default=1100)
parser.add_argument("--usable-fraction", type=float, default=0.8)
parser.add_argument("--standby-ma", type=float, default=5)
for name, current, minutes in [("interactive", 80, 20), ("thinking", 180, 5), ("speaking", 100, 5), ("storage", 50, 2)]:
    parser.add_argument(f"--{name}-ma", type=float, default=current)
    parser.add_argument(f"--{name}-minutes", type=float, default=minutes)
args = parser.parse_args()
if any(not math.isfinite(value) or value < 0 for value in vars(args).values()) or args.capacity_mah <= 0 or not 0 < args.usable_fraction <= 1:
    parser.error("All values must be finite/nonnegative; capacity >0; usable fraction in (0,1].")
durations = {name: getattr(args, f"{name}_minutes") for name in ["interactive", "thinking", "speaking", "storage"]}
if sum(durations.values()) > 1440:
    parser.error("Daily active durations exceed 24 hours.")
durations["standby"] = 1440 - sum(durations.values())
average = sum(getattr(args, f"{name}_ma") * minutes for name, minutes in durations.items()) / 1440
if average <= 0:
    parser.error("Average assumed current must be >0.")
usable = args.capacity_mah * args.usable_fraction
print(json.dumps({"evidence": "hypothetical design assumptions; no measured board currents",
    "capacity_mah": args.capacity_mah, "usable_mah": usable, "average_assumed_ma": average,
    "projected_days": usable / average / 24, "five_day_average_ceiling_ma": usable / 120,
    "three_day_average_ceiling_ma": usable / 72, "assumed_minutes": durations}, indent=2))

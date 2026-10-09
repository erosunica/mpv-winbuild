#!/usr/bin/env python3
"""Verify the presentation trace around actual mpv lifecycle events."""
import argparse
from pathlib import Path
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("log", type=Path)
p.add_argument("--ratio", type=float, required=True)
a = p.parse_args()
log = a.log.read_text()
assert "CRT_TEST_PASS" in log
assert "Failed rendering" not in log and "failed dispatching CRT" not in log
events = re.split(r"CRT_TEST_EVENT (\w+)\n", log)
sections = {"initial": events[0]}
sections.update(dict(zip(events[1::2], events[2::2])))
pattern = r"CRT present=(\d+) cycle=(\d+).*?phase=([\d.]+) ratio=([\d.]+) gain=([\d.]+)"

for label in ("initial", "resume", "seek", "single", "gain", "on", "resize"):
    samples = re.findall(pattern, sections[label])
    assert samples, f"No CRT render after {label}"
    # The VO thread may finish an in-flight render while the Lua/core thread
    # requests an option update. Verify the new epoch after its actual reset.
    resets = [i for i, sample in enumerate(samples) if sample[0] == "0"]
    assert resets, (label, samples[0])
    samples = samples[resets[0]:]
    expected = a.ratio if label in ("initial", "resume", "seek") else a.ratio * 2
    for present, cycle, phase, ratio, gain in samples:
        assert abs(float(ratio) - expected) < .001, (label, ratio, expected)
        assert 0 <= float(phase) < float(ratio)
    assert any(int(present) > 1 for present, *_ in samples)
for label in ("pause", "off"):
    assert len(re.findall(pattern, sections[label])) <= 1, f"Partial CRT renders during {label}"
print("Player lifecycle: pause/resume, seek, scan/gain changes, toggle, resize, original speed and bounded phase passed.")

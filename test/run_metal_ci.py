#!/usr/bin/env python3
# Copyright 2026 UAA Software. MIT licence; see ../LICENSE.

"""Run the headless Metal suite and require the release-blocker regressions."""

import re
import subprocess
import sys


REQUIRED_TESTS = {
    "Backend.OrderedAliasActivation",
    "Backend.AliasHandoffsResetEveryMip",
    "Backend.AliasHandoffsRetainDestroyedResourcesWhileGpuIsBlocked",
    "Bindless.MetalDescriptorTableReleasesDiscardedSlots",
    "Bindless.MetalDescriptorTableClearKeepsSlotAlive",
    "Bindless.MetalRejectsUpdateAfterBind",
    "Native.MetalRejectsVulkanCallback",
    "Window.MetalFrameWaitsForSlot",
}


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: run_metal_ci.py <vrhi_test>", file=sys.stderr)
        return 2

    try:
        result = subprocess.run(
            [sys.argv[1], "--backend=metal", "--headless"],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            errors="replace",
            timeout=1500,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        if error.stdout:
            print(error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else error.stdout)
        print("Metal suite timed out", file=sys.stderr)
        return 1

    print(result.stdout, end="")
    passed = set(re.findall(r"^\[\s*OK\s*\] ([^\s]+) \(", result.stdout, re.MULTILINE))
    missing = sorted(REQUIRED_TESTS - passed)
    if result.returncode != 0:
        print(f"Metal suite exited with {result.returncode}", file=sys.stderr)
    if not passed:
        print("Metal suite did not pass any tests", file=sys.stderr)
    if missing:
        print("Required Metal tests did not pass: " + ", ".join(missing), file=sys.stderr)
    return 1 if result.returncode != 0 or not passed or missing else 0


if __name__ == "__main__":
    sys.exit(main())

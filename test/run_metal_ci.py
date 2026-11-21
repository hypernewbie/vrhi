#!/usr/bin/env python3
# Copyright 2026 UAA Software. MIT licence; see ../LICENSE.

"""Run the headless Metal suite and reject unexpected skips."""

from collections import Counter
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
    "Buffer.PartialByteWrites_PreserveNeighbours",
    "Graphics.IndependentBlendUnequalTargets",
    "Graphics.IndependentBlend",
    "RHI.Shader16BitControl",
    "Native.MetalRejectsVulkanCallback",
    "ShaderInternal.IndependentBlendFactorsAndEquations",
    "ShaderInternal.IndependentBlendGlobalAndReset",
    "ShaderInternal.IndependentBlendLegacyFlags",
    "ShaderInternal.IndependentBlendPreservesState",
    "State.BlendTargetsRoundTrip",
    "Texture.Upload_MipTailRoundTrip",
    "Window.MetalFrameWaitsForSlot",
}

# Tests that exercise another backend or an optional feature.
ALLOWED_SKIPS_BY_REASON = {
    "Timestamp queries not producing results on this device/backend": {
        "Backend.TimerQueryBasic", "Backend.TimerQueryMultiple",
    },
    "VRS not supported on this device": {"Graphics.VRS_Smoke"},
    "Requires Metal Tier 2 argument buffers": {"Bindless.MetalLargeTableSparseSlots"},
    "No sampled-image update-after-bind support": {"Bindless.UpdateAfterBind_WriteAcrossFrames"},
    "UAB sampled image unsupported": {
        "Bindless.UAB_IntraFrame_WriteBetweenDispatches", "Bindless.UAB_CrossFrame_NoFence",
    },
    "Requires Vulkan device extensions": {"RHI.ExtraDeviceExtensions"},
    "Requires Vulkan drawIndirectCount feature queries": {"RHI.IndirectCountCapability"},
    "Requires Vulkan mapped buffer flushes": {
        "Buffer.Transient_PartialFlushFinalPaddedAtom",
        "Buffer.MappedFlushRejectsInvalidResourcesAndRanges",
        "Buffer.Transient_FailedPartialFlushCanRetry",
    },
    "Requires Null RHI mode": {"Buffer.Transient_NullModeMapFailure"},
    "Requires native Vulkan objects": {
        "Native.BracketAndRebaseline", "Native.DeviceLock", "Native.NativeCallbackWritesTexture",
    },
    "Requires Vulkan acquire semaphores": {"Swapchain.SemaphoreArraysCoverFrameSlots"},
    "Requires a Vulkan pipeline cache": {
        "RHI.PSOCache_RoundTrip", "RHI.PSOCache_StaleBlobIgnored", "RHI.PSOCache_NoFlushSizeMatchesBlob",
        "RHI.PSOCache_KeyStable", "RHI.PSOCache_ConcurrentQueryDuringPipelineCreation",
        "RHI.PSOCache_ValidateKeyMismatchDiscardsSeed", "Bench.PSOCache_ColdVsWarm",
    },
    "set VRHI_BENCH_GC_INTERVAL to compare": {"FrameBench.Bench_Frame_GCInterval"},
    "set VRHI_BENCH_HEAVY=1 to run the 500/1000-draw benchmarks": {
        "FrameBench.Bench_Frame_Heavy_500", "FrameBench.Bench_Frame_Heavy_1000",
    },
}


def check_output(output: str, returncode: int) -> list[str]:
    errors = []
    if returncode != 0:
        errors.append(f"Metal suite exited with {returncode}")

    runs = re.findall(r"^\[\s*RUN\s*\] (\S+)$", output, re.MULTILINE)
    outcomes = re.findall(r"^\[\s*(OK|SKIPPED|FAILED)\s*\] (\S+) \(", output, re.MULTILINE)
    passed = {name for status, name in outcomes if status == "OK"}
    skipped = {name for status, name in outcomes if status == "SKIPPED"}
    failed = {name for status, name in outcomes if status == "FAILED"}
    started_counts = Counter(runs)
    outcome_counts = Counter(name for _, name in outcomes)
    announced = re.findall(r"^\[==========\] Running (\d+) test cases\.$", output, re.MULTILINE)
    completed = re.findall(r"^\[==========\] (\d+) test cases ran\.$", output, re.MULTILINE)
    if len(announced) != 1 or len(completed) != 1 or int(announced[0]) != len(runs) or int(completed[0]) != len(runs):
        errors.append("Metal suite did not report a complete test run")
    if not passed:
        errors.append("Metal suite did not pass any tests")
    if any(count != 1 for count in started_counts.values()) or any(count != 1 for count in outcome_counts.values()):
        errors.append("Metal suite reported duplicate tests or outcomes")
    if set(runs) != set(outcome_counts):
        errors.append("Metal suite reported unfinished tests or outcomes without tests")
    if failed:
        errors.append("Failed Metal tests: " + ", ".join(sorted(failed)))

    allowed = {name: reason for reason, names in ALLOWED_SKIPS_BY_REASON.items() for name in names}
    for block in re.split(r"(?=^\[\s*RUN\s*\] \S+$)", output, flags=re.MULTILINE):
        match = re.match(r"\[\s*RUN\s*\] (\S+)$", block, re.MULTILINE)
        if not match or match.group(1) not in skipped:
            continue
        name = match.group(1)
        reasons = re.findall(r"^\s*Skipped : '(.*)'$", block, re.MULTILINE)
        expected = "Raytracing not supported on this device" if name.startswith("RT.") else allowed.get(name)
        if expected is None or reasons != [expected]:
            errors.append(f"Unexpected Metal skip: {name}: {reasons}")

    missing = sorted(REQUIRED_TESTS - passed)
    if missing:
        errors.append("Required Metal tests did not pass: " + ", ".join(missing))
    return errors


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: run_metal_ci.py <vrhi_test>", file=sys.stderr)
        return 2

    try:
        result = subprocess.run(
            [sys.argv[1], "--backend=metal", "--headless"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            timeout=1500,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        if error.stdout:
            print(error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else error.stdout)
        if error.stderr:
            print(error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else error.stderr, file=sys.stderr)
        print("Metal suite timed out", file=sys.stderr)
        return 1

    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    errors = check_output(result.stdout, result.returncode)
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

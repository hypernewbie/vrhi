#!/usr/bin/env python3
"""
Verify the vendored header trees stay in sync with the nvrhi submodule.

Vrhi keeps its own copy of the nvrhi and Vulkan-Headers public headers under
include/ so that consumers do not need the submodule checked out. Those copies
are duplicates of headers that nvrhi itself compiles against, and nothing in the
build system enforces that they agree.

When they silently drift, the failure is nasty and non-obvious. A version skew
between the vendored Vulkan-Hpp headers and the ones nvrhi was compiled with
trips the dispatcher assertion inside Vulkan-Hpp at runtime:

    Assertion failed: (d.getVkHeaderVersion() == VK_HEADER_VERSION)

which surfaces as a crash in an unrelated Vulkan call, far from the actual
mistake. This script turns that class of drift into an immediate, explicit CI
failure instead.

Checks performed:
  1. include/nvrhi/** is byte-identical to vdeps/nvrhi/include/nvrhi/**
     (same file set, same contents).
  2. The vendored VK_HEADER_VERSION matches the Vulkan-Headers tag that nvrhi
     fetches via NVRHI_VULKAN_HEADERS_GIT_TAG.

Exits 0 if every invariant holds, 1 otherwise.
"""

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent

VENDORED_NVRHI = REPO_ROOT / "include" / "nvrhi"
SUBMODULE_NVRHI = REPO_ROOT / "vdeps" / "nvrhi" / "include" / "nvrhi"
NVRHI_CMAKELISTS = REPO_ROOT / "vdeps" / "nvrhi" / "CMakeLists.txt"
VENDORED_VULKAN_CORE = REPO_ROOT / "include" / "vulkan" / "vulkan_core.h"


def fail(message):
    print(f"FAIL: {message}")
    return False


def relative_files(root):
    """All files under root, as paths relative to root."""
    return {p.relative_to(root) for p in root.rglob("*") if p.is_file()}


def check_nvrhi_headers_in_sync():
    """include/nvrhi must be a byte-identical copy of the submodule's headers."""
    if not SUBMODULE_NVRHI.is_dir():
        return fail(
            f"{SUBMODULE_NVRHI.relative_to(REPO_ROOT)} is missing. "
            "The nvrhi submodule is not checked out; run "
            "'git submodule update --init --recursive --depth 1'."
        )

    vendored = relative_files(VENDORED_NVRHI)
    upstream = relative_files(SUBMODULE_NVRHI)

    ok = True

    missing = sorted(upstream - vendored)
    if missing:
        ok = fail(
            "these headers exist in the nvrhi submodule but are missing from "
            f"include/nvrhi: {', '.join(str(p) for p in missing)}"
        )

    extra = sorted(vendored - upstream)
    if extra:
        ok = fail(
            "these headers exist in include/nvrhi but not in the nvrhi "
            f"submodule: {', '.join(str(p) for p in extra)}"
        )

    differing = sorted(
        rel
        for rel in (vendored & upstream)
        if (VENDORED_NVRHI / rel).read_bytes() != (SUBMODULE_NVRHI / rel).read_bytes()
    )
    if differing:
        ok = fail(
            "these vendored headers differ from the nvrhi submodule: "
            f"{', '.join(str(p) for p in differing)}"
        )

    if ok:
        print(f"OK: include/nvrhi matches the nvrhi submodule ({len(vendored)} files).")
    else:
        print(
            "\n  include/nvrhi is a verbatim copy of vdeps/nvrhi/include/nvrhi.\n"
            "  Re-sync it with:\n"
            "      cp -R vdeps/nvrhi/include/nvrhi/. include/nvrhi/\n"
            "  and commit the result as a separate mechanical commit."
        )

    return ok


def check_vulkan_header_version():
    """The vendored Vulkan headers must match the tag nvrhi fetches."""
    if not NVRHI_CMAKELISTS.is_file():
        return fail(
            f"{NVRHI_CMAKELISTS.relative_to(REPO_ROOT)} is missing. "
            "The nvrhi submodule is not checked out; run "
            "'git submodule update --init --recursive --depth 1'."
        )

    cmake_text = NVRHI_CMAKELISTS.read_text(encoding="utf-8", errors="replace")
    tag_match = re.search(
        r'set\(\s*NVRHI_VULKAN_HEADERS_GIT_TAG\s+"([^"]+)"', cmake_text
    )
    if not tag_match:
        return fail(
            "could not find NVRHI_VULKAN_HEADERS_GIT_TAG in "
            f"{NVRHI_CMAKELISTS.relative_to(REPO_ROOT)}. If upstream nvrhi renamed "
            "or removed this variable, update this script to match."
        )

    tag = tag_match.group(1)
    # Tags look like "v1.4.352"; the trailing component is the header version.
    version_match = re.fullmatch(r"v?\d+\.\d+\.(\d+)", tag)
    if not version_match:
        return fail(
            f"NVRHI_VULKAN_HEADERS_GIT_TAG is '{tag}', which is not a "
            "vMAJOR.MINOR.PATCH tag. It may be a raw commit hash, in which case "
            "this check needs updating to resolve it."
        )
    expected = int(version_match.group(1))

    core_text = VENDORED_VULKAN_CORE.read_text(encoding="utf-8", errors="replace")
    header_match = re.search(r"^#define\s+VK_HEADER_VERSION\s+(\d+)", core_text, re.M)
    if not header_match:
        return fail(
            "could not find VK_HEADER_VERSION in "
            f"{VENDORED_VULKAN_CORE.relative_to(REPO_ROOT)}."
        )
    actual = int(header_match.group(1))

    if actual != expected:
        return fail(
            f"vendored Vulkan headers are at VK_HEADER_VERSION {actual}, but nvrhi "
            f"builds against {tag} (version {expected}).\n"
            "  This skew trips the Vulkan-Hpp dispatcher assertion at runtime:\n"
            "      Assertion failed: (d.getVkHeaderVersion() == VK_HEADER_VERSION)\n"
            "  Re-vendor include/vulkan and include/vk_video from Vulkan-Headers "
            f"{tag} and commit as a separate mechanical commit."
        )

    print(f"OK: vendored Vulkan headers are at version {actual}, matching nvrhi's {tag}.")
    return True


def main():
    print("Checking vendored header invariants...")
    results = [
        check_nvrhi_headers_in_sync(),
        check_vulkan_header_version(),
    ]

    if not all(results):
        print("\nVendored header invariant check FAILED.")
        return 1

    print("\nAll vendored header invariants hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

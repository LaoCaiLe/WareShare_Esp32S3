"""
PlatformIO extra_script to enable ccache for faster incremental builds.
If ccache is not installed, this script does nothing (no error).
"""

Import("env")
import subprocess
import sys


def _has_ccache():
    """Check if ccache is available on the system."""
    try:
        if sys.platform == "win32":
            # On Windows, check for ccache.exe or sccache.exe
            for name in ["ccache.exe", "sccache.exe"]:
                result = subprocess.run(
                    ["where", name],
                    capture_output=True,
                    text=True,
                )
                if result.returncode == 0:
                    return name.replace(".exe", "")
        else:
            result = subprocess.run(
                ["which", "ccache"],
                capture_output=True,
                text=True,
            )
            if result.returncode == 0:
                return "ccache"
    except Exception:
        pass
    return None


def configure_ccache():
    ccache = _has_ccache()
    if ccache:
        # Prepend ccache to compiler commands
        env.Replace(CC=f"{ccache} {env['CC']}")
        env.Replace(CXX=f"{ccache} {env['CXX']}")
        print(f"[build_cache] Using {ccache} for compilation cache")
    else:
        print("[build_cache] ccache/sccache not found — skipping (install with: scoop install ccache)")


configure_ccache()

"""PlatformIO pre-build hook: weaken ieee80211_raw_frame_sanity_check.

The deauth module (src/modules/attack/deauth.cpp) provides its own strong
definition of ieee80211_raw_frame_sanity_check to allow management-frame TX.
For that override to link cleanly, the matching symbol inside the precompiled
libnet80211.a must be *weak*. A framework-libs reinstall/update can silently
restore it to strong, which breaks the link with a "multiple definition" error.

Running the (idempotent) weakening script as a pre-action guarantees the symbol
is weak on every build, so the patch can never be silently lost. See
scripts/weaken_deauth_symbol.sh for the actual objcopy logic.
"""
import os
import subprocess

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

script = os.path.join(env["PROJECT_DIR"], "scripts", "weaken_deauth_symbol.sh")

if not os.path.isfile(script):
    print("[weaken_deauth] %s not found — skipping" % script)
else:
    print("[weaken_deauth] ensuring ieee80211_raw_frame_sanity_check is weak...")
    try:
        subprocess.run(["bash", script], check=True)
    except FileNotFoundError:
        print("[weaken_deauth] 'bash' not available — skipping (run the script manually)")
    except subprocess.CalledProcessError as exc:
        # Surface the failure: a strong symbol will break the link anyway.
        raise SystemExit("[weaken_deauth] weakening failed (exit %d)" % exc.returncode)

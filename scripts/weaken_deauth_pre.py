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
import re
import subprocess

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

script = os.path.join(env["PROJECT_DIR"], "scripts", "weaken_deauth_symbol.sh")


def _derive_objcopy():
    """objcopy sibling of the compiler this build actually uses.

    The SCons env's $CC is the authoritative toolchain path; objcopy lives beside it with
    the same tool-prefix (…-elf-gcc -> …-elf-objcopy). Resolving from $CC avoids hardcoding
    a package path that PlatformIO renames across toolchain updates. Returns None if $CC is
    unset or the derived binary is absent, in which case the script's own PATH fallback runs.
    """
    cc = env.subst("$CC")  # noqa: F821
    if not cc:
        return None
    objcopy = re.sub(r"gcc(\.exe)?$", lambda m: "objcopy" + (m.group(1) or ""), cc)
    return objcopy if objcopy != cc and os.path.isfile(objcopy) else None


if not os.path.isfile(script):
    print("[weaken_deauth] %s not found — skipping" % script)
else:
    print("[weaken_deauth] ensuring ieee80211_raw_frame_sanity_check is weak...")
    child_env = dict(os.environ)
    objcopy = _derive_objcopy()
    if objcopy:
        child_env["OBJCOPY"] = objcopy
    try:
        subprocess.run(["bash", script], check=True, env=child_env)
    except FileNotFoundError:
        print("[weaken_deauth] 'bash' not available — skipping (run the script manually)")
    except subprocess.CalledProcessError as exc:
        # Surface the failure: a strong symbol will break the link anyway.
        raise SystemExit("[weaken_deauth] weakening failed (exit %d)" % exc.returncode)

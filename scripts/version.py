"""Injecte PROJECT_VERSION depuis le fichier VERSION a la racine.

Pourquoi ce script existe
-------------------------
Le fichier VERSION a la racine fait autorite. Ce script est appele par
PlatformIO avant la compilation (pre:scripts/version.py) pour injecter
le define PROJECT_VERSION de maniere centralisee et automatique.
"""

import os

try:
    Import("env")            # noqa: F821  (fourni par PlatformIO)
except NameError:
    raise SystemExit("Ce script doit etre execute par PlatformIO (pio run).")

_PROJECT_DIR = env["PROJECT_DIR"]           # noqa: F821
_VERSION_FILE = os.path.join(_PROJECT_DIR, "VERSION")

try:
    with open(_VERSION_FILE, encoding="utf-8") as handle:
        version = handle.read().strip()
except OSError as error:
    raise SystemExit(f"VERSION illisible ({_VERSION_FILE}) : {error}")

if not version:
    raise SystemExit(f"VERSION est vide : {_VERSION_FILE}")

env.Append(CPPDEFINES=[("PROJECT_VERSION", env.StringifyMacro(version))])  # noqa: F821
print(f"PROJECT_VERSION = {version}  (lu depuis VERSION)")

# SPDX-License-Identifier: GPL-3.0-or-later
"""Reicht die Version aus firmware/VERSION als Makro PROJECT_VER in den Build.

Frueher schrieb dieses Skript include/project_version.h. Das hatte eine stille Folge:
PlatformIO bildet eine Projekt-Pruefsumme ueber die Dateistruktur von include/, src/
und lib/. Ein waehrend des Builds entstehender Header aenderte sie, und der NAECHSTE
"pio run" loeschte das komplette Build-Verzeichnis samt fertigem Abbild. Lokal fiel das
nie auf (der Header existierte schon), in der CI war deshalb jeder Lauf rot
(gefunden 06.09.2026). Jetzt entsteht keine Datei mehr; include/project_version.h ist
fest und liest nur das Makro.

VERSION hat Vorrang vor git: Das Projekt lag zeitweise in einem fremden Arbeitsbaum, und
"git describe" stempelte die Firmware mit der Version eines anderen Projekts.
"""
import os
import subprocess

Import("env")  # noqa: F821 -- von PlatformIO bereitgestellt


def version(project_dir):
    pfad = os.path.join(project_dir, "VERSION")
    if os.path.exists(pfad):
        with open(pfad, encoding="utf-8") as f:
            v = f.read().strip()
        if v:
            return v
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty=-dev"],
            cwd=project_dir, stderr=subprocess.DEVNULL).decode().strip()
    except Exception:  # noqa: BLE001
        return "unknown"


v = version(env.get("PROJECT_DIR"))
env.Append(CPPDEFINES=[("PROJECT_VER", env.StringifyMacro(v))])
print("[git_version] Version: %s" % v)

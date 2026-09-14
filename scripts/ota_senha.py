"""Entrega ao espota a mesma senha de OTA que foi compilada no firmware.

Uso: extra_scripts = post:scripts/ota_senha.py  (nos ambientes *_ota)

A senha mora em include/secrets.h (fora do git) como LIMBIA_OTA_PASS. Na
falta dele, vale a do modelo include/secrets.example.h - exatamente a mesma
regra que include/segredos.h aplica ao firmware. Uma fonte so: a senha que a
placa espera e a senha que o PC manda nunca divergem.

O script so le a senha na hora do upload e a passa como argumento ao
espota. Ela nao e impressa nem gravada em lugar nenhum.
"""
from __future__ import annotations

import re
from pathlib import Path

Import("env")  # noqa: F821 - injetado pelo PlatformIO

RAIZ = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
PADRAO = re.compile(r'^\s*#define\s+LIMBIA_OTA_PASS\s+"([^"]*)"', re.MULTILINE)


def senha_ota() -> tuple[str, str]:
    for nome in ("secrets.h", "secrets.example.h"):
        arquivo = RAIZ / "include" / nome
        if arquivo.exists():
            achado = PADRAO.search(arquivo.read_text(encoding="utf-8", errors="ignore"))
            if achado:
                return achado.group(1), nome
    return "", ""


senha, origem = senha_ota()
if senha:
    env.Append(UPLOADERFLAGS=["--auth=" + senha])  # noqa: F821
    if origem == "secrets.example.h":
        print("[ota] AVISO: usando a senha de OTA do MODELO (include/secrets.example.h)")
else:
    print("[ota] ERRO: LIMBIA_OTA_PASS nao encontrado em include/secrets.h nem no modelo")

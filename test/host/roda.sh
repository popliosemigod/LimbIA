#!/bin/sh
# =====================================================================
#  Roda o autoteste NO PC, sem placa - para iterar rapido na logica.
#
#  Compilador: zig (clang empacotado no pip), num venv proprio:
#      python -m venv .venv-host
#      .venv-host/Scripts/python -m pip install ziglang
#
#  Uso (Git Bash, da raiz do repositorio):
#      sh test/host/roda.sh
#
#  Isto NAO substitui o autoteste na placa: no PC int e float se comportam
#  como no PC. O numero que vai para o diario e o medido no ESP32. O
#  tempo de execucao impresso aqui nao significa nada.
# =====================================================================
set -e
RAIZ="$(cd "$(dirname "$0")/../.." && pwd)"
PY="${PY:-$RAIZ/.venv-host/Scripts/python}"
SAIDA="$RAIZ/.pio/host"
mkdir -p "$SAIDA"

"$PY" -m ziglang c++ -std=c++11 -O2 -Wall -Wno-unused-function -Wno-date-time \
  -I "$RAIZ/test/host" -I "$RAIZ/lib/limbia_mao" -I "$RAIZ/include" \
  "$RAIZ/src/main_autoteste.cpp" \
  "$RAIZ/lib/limbia_mao/limbia_mao.cpp" \
  "$RAIZ/lib/limbia_mao/limbia_emg.cpp" \
  "$RAIZ/lib/limbia_mao/limbia_enlace.cpp" \
  "$RAIZ/test/host/main_host.cpp" \
  -o "$SAIDA/autoteste.exe" 2> "$SAIDA/compila.log" || {
  grep -E "error" "$SAIDA/compila.log" | head -30
  exit 1
}
"$SAIDA/autoteste.exe"

"""Espectro de 1 s de EMG cru, capturado pelo ambiente emg_teste.

Responde a pergunta que o envelope sozinho nao responde: o que o pino esta
vendo e musculo (energia espalhada de 20 a 450 Hz) ou rede eletrica (picos
em 60, 120, 180 Hz) - que e o que um pino solto ou um eletrodo sem contato
captam.

    python test/host/espectro_emg.py COM10            # manda 'd' e le
    python test/host/espectro_emg.py COM10 3          # 3 capturas seguidas

So pyserial; sem numpy (DFT direta, 1000 amostras cabem).
"""

import cmath
import math
import sys
import time

import serial


def captura(s):
    s.reset_input_buffer()
    s.write(b"d")
    valores, dentro, t0 = [], False, time.time()
    while time.time() - t0 < 8:
        linha = s.readline().decode("utf-8", "replace").strip()
        if linha.startswith("CAPTURA_INICIO"):
            dentro = True
        elif linha.startswith("CAPTURA_FIM"):
            return valores
        elif dentro and linha:
            valores += [int(v) for v in linha.split(",") if v.strip().isdigit()]
    raise SystemExit("sem captura: o firmware emg_teste esta gravado?")


def analisa(x, fs=1000):
    n = len(x)
    media = sum(x) / n
    y = [v - media for v in x]
    desvio = math.sqrt(sum(v * v for v in y) / n)
    # potencia por bin de 1 Hz (n = fs)
    pot = []
    for k in range(n // 2):
        c = sum(y[i] * cmath.exp(-2j * math.pi * k * i / n) for i in range(n))
        pot.append(abs(c) ** 2)
    total = sum(pot[1:]) or 1.0
    rede = sum(pot[h + d] for h in range(60, n // 2, 60) for d in (-1, 0, 1) if h + d < n // 2)
    musculo = sum(p for k, p in enumerate(pot) if 20 <= k <= 450) - sum(
        pot[h + d] for h in range(60, 451, 60) for d in (-1, 0, 1)
    )
    baixa = sum(pot[1:20])
    picos = sorted(range(1, n // 2), key=lambda k: pot[k], reverse=True)[:6]

    print(f"  amostras {n}  min {min(x)}  max {max(x)}  media {media:.0f}  desvio {desvio:.0f}")
    print(f"  rede (60 Hz e harmonicas) {100 * rede / total:5.1f}% da potencia")
    print(f"  banda do musculo (20-450, sem harmonicas) {100 * musculo / total:5.1f}%")
    print(f"  abaixo de 20 Hz (deriva, movimento) {100 * baixa / total:5.1f}%")
    print("  maiores picos: " + ", ".join(f"{k} Hz" for k in picos))
    if rede / total > 0.5:
        print("  => dominado pela REDE ELETRICA: pino solto ou eletrodo sem contato")
    elif baixa / total > 0.5:
        print("  => dominado por DERIVA lenta: eletrodo mexendo ou referencia solta")
    else:
        print("  => energia espalhada na banda do musculo")


def main():
    porta = sys.argv[1]
    vezes = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    with serial.Serial(porta, 115200, timeout=0.5) as s:
        time.sleep(1.5)
        for i in range(vezes):
            print(f"captura {i + 1}")
            analisa(captura(s))


if __name__ == "__main__":
    main()

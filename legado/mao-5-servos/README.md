# Mão de 5 servos — gestos por botão

Versão antiga da prótese: **um servo por dedo**, num **Arduino Mega 2560**. É um
projeto separado do firmware da prótese (a raiz do repositório); não compartilha
código com ele.

Hoje: um botão percorre uma lista de gestos. Depois: a câmera controla a mão pela
serial, com visão computacional (Python + `cvzone`).

## Ligação

| Dedo | Pino do Mega |
| --- | --- |
| Polegar | 2 |
| Indicador | 3 |
| Médio | 4 |
| Anelar | 5 |
| Mínimo | 6 |
| Botão | 7 (a outra perna no GND) |

**Os servos não podem ser alimentados pelo 5V do Mega.** Cinco servos partindo
juntos puxam mais corrente do que a placa fornece, e ela reinicia. Use uma fonte
externa de 5 a 6 V para os servos e ligue o **GND da fonte ao GND do Mega**.

## Gravar

De dentro desta pasta:

```powershell
pio run -t upload
pio device monitor
```

## Usar

A mão **liga fechada**: no nosso mecanismo o dedo sai do fechado para o aberto
com mais facilidade do que o contrário.

- **Toque curto no botão:** próximo gesto.
- **Segurar 1 s:** mão fechada (volta ao começo).
- **Pelo monitor serial:** `0` a `9` escolhem o gesto, `n` avança, `?` lista.

| # | Gesto | Polegar · Indicador · Médio · Anelar · Mínimo |
| --- | --- | --- |
| 0 | mão fechada | 0 0 0 0 0 |
| 1 | número 1 | 0 1 0 0 0 |
| 2 | número 2 (paz) | 0 1 1 0 0 |
| 3 | número 3 | 0 1 1 1 0 |
| 4 | número 4 | 0 1 1 1 1 |
| 5 | mão aberta | 1 1 1 1 1 |
| 6 | joinha | 1 0 0 0 0 |
| 7 | rock | 0 1 0 0 1 |
| 8 | hang loose | 1 0 0 0 1 |
| 9 | arminha | 1 1 0 0 0 |

`1` = dedo levantado. De 0 a 5, o número do gesto é o número de dedos levantados.

## Calibrar

No topo de [`src/main.cpp`](src/main.cpp), `ABERTO` e `FECHADO` guardam o ângulo
de cada dedo. Se um servo estiver montado ao contrário, troque os dois valores
daquele dedo. Ajuste até o dedo encostar **sem forçar**: servo parado contra o
batente esquenta e queima.

## Visão computacional (próximo passo)

A mão já entende o quadro que o `cvzone` envia: `$` seguido de cinco dígitos
0/1, na mesma ordem e no mesmo sentido do `fingersUp()`. O script do vídeo de
referência funciona sem mudar nada deste firmware:

```python
mySerial = cvzone.SerialObject("COM3", 9600, 1)   # a porta do Mega
...
mySerial.sendData(detector.fingersUp())           # envia "$01100", por exemplo
```

Referência: <https://www.youtube.com/watch?v=7KV5489rL3c>

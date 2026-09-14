# LimbIA

Prótese de mão movida por **EMG**, sobre o hardware do LAD Robotic Hand V3.0.
Dois eletrodos no antebraço, duas placas ESP32, seis sensores de corrente.

> **Estado:** compila, e a lógica foi medida nas duas placas.
> **Nenhum motor foi ligado e nenhum eletrodo tocou pele.**

## Objetivo

Uma prótese que qualquer pessoa possa vestir: o usuário calibra os próprios
eletrodos em um minuto, pela tela, e a mão passa a obedecer ao músculo dele.

1. **Calibrar por pessoa, não por prótese.** Antebraços são diferentes; a tela
   guia o posicionamento dos eletrodos até a barra ficar verde.
2. **Dois movimentos, bem feitos:** mão aberta e mão fechada — sem tremor, sem
   resposta errática, com velocidade estável.
3. **Fechar parando no contato**, em vez de esmagar o objeto até a pose gravada.
4. **Manutenção sem cabo:** as duas placas se gravam pela rede da própria prótese.

## Arquitetura

```
   ESP32-C3 SuperMini                 ESP32 DevKit V1
   placa do EMG                       placa da mão
   2 eletrodos a 1 kHz                4 motores DC (2× L293D)
   filtros, IA e decisor   ──UDP──►   2 servos no polegar
   rede, tela e OTA        ◄──────    6 ACS712
   192.168.4.1             telemetria 192.168.4.200
```

O PC ou o celular entra na rede da prótese e a tela de ajuste abre sozinha.

## Registros

Autoteste rodando **nas placas** — 89 verificações, 89 passaram nas duas
(13/09/2026):

| | DevKit V1 | ESP32-C3 |
| --- | --- | --- |
| Tempo do banco | 44,1 s | 209,5 s (banco reduzido) |
| Cadeia de filtros do EMG | 0,90 µs/amostra | 22,65 µs/amostra |
| Dois canais a 1 kHz | 0,2% de um núcleo | **4,5% de um núcleo** |
| Classificação LDA | 2,99 µs | 30,33 µs |
| Flash | 64,8% | 66,1% |

Com as duas placas ligadas:

| | |
| --- | --- |
| Janelas de EMG no C3, com rede e tela no ar | **1045, perdidas 0** |
| Enlace entre as placas | conecta nos dois sentidos |
| Comando EMG → rede → mão → preensão | funcionando |
| Pacotes com 1 bit trocado aceitos | 0 de 120 |
| Calibração de eletrodo (sinal sintético) | verde em 3 ciclos (~37 s) |
| Eletrodo no tendão, trocado, saturando, mau contato | recusados, com diagnóstico |
| Modelo recusado pela tela, usado à força | 0 de 30 intenções, nenhuma ação errada |

Cada iteração, com previsto ao lado de medido, está no [diário](diario.md).

## Gravar

```powershell
pio run                          # mão, EMG e autoteste

pio run -e esp32dev -t upload    # a mão      (sempre com -e)
pio run -e emg      -t upload    # o EMG
pio device monitor -b 115200     # console

pio run -e mao_ota -t upload     # pela rede, sem USB
pio run -e emg_ota -t upload
```

| Ambiente | Placa | |
| --- | --- | --- |
| `esp32dev` | DevKit V1 | a mão (motores DC do LAD) |
| `emg` | ESP32-C3 | EMG, IA, rede e tela |
| `mao_servo` | DevKit V1 | a mão na variante de servos + PCA9685 |
| `bancada` | DevKit V1 | a mão com log detalhado |
| `autoteste` / `autoteste_c3` | as duas | a lógica sem hardware |
| `mao_ota` / `emg_ota` | as duas | gravação pela rede |

## Documentação

| | |
| --- | --- |
| [01](docs/01-heranca-inovaweek-e-lad.md) | de onde o projeto veio: INOVAWEEK e LAD |
| [02](docs/02-hardware-e-pinagem.md) | hardware e pinagem |
| [03](docs/03-anatomia-gestos-e-calibracao.md) | juntas, gestos e calibração |
| [04](docs/04-propriocepcao.md) | como a mão descobre a forma do objeto |
| [05](docs/05-emg-e-a-ia.md) | o EMG, a calibração por pessoa e a IA |
| [06](docs/06-rede-tela-e-ota.md) | a rede, a tela e o OTA |

## Em aberto

1. Ligar um motor e medir: velocidade, limiares de corrente e arranque são chute
   educado.
2. Medir o tempo de curso de cada dedo — é a calibração desta mão.
3. Primeiro eletrodo na pele, com a placa do EMG na bateria.
4. Abrir a tela num navegador.
5. Primeiro ensaio de preensão com objeto real.
6. A órtese, que dá metade do nome ao projeto.

## Crédito

**LAD Robotic Hand V3.0 — ESP32 Control v1.1**, de **Adrian Duran** (LAD
Robotics): a mão, os motores, os sensores de corrente em série, a calibração por
endpoints medidos e o polegar com dois graus de liberdade.

Projeto do laboratório [Jaspy](https://github.com/popliosemigod/Jaspy).

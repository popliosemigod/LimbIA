# Hardware e pinagem

> **Duas placas, e duas variantes de mão.** A prótese tem hoje a **placa da mão**
> (ESP32 DevKit V1) e a **placa do EMG** (ESP32-C3 SuperMini) — ver
> [06-rede-tela-e-ota.md](06-rede-tela-e-ota.md). E o firmware da mão compila
> para dois hardwares diferentes:
>
> | | `LIMBIA_MAO_LAD=1` (padrão, na bancada) | `LIMBIA_MAO_LAD=0` |
> | --- | --- | --- |
> | Dedos longos | 4 motores DC em dois L293D | 4 servos |
> | Polegar | 2 servos (flexão e abdução) | 2 servos |
> | Punho | não tem | 1 servo |
> | Corrente | **6 ACS712** | 4 ACS712 |
> | Posição do dedo | **estimada pelo tempo** | pulso do servo |
>
> O que muda é só o backend de acionamento (`include/motores.h` ou
> `include/dedos.h`, os dois no mesmo `namespace Dedos`). Estado, console,
> preensão, enlace, rede e OTA são os mesmos.

## A mão do LAD: pinagem

Esta é a mão da bancada, com o hardware do **LAD Robotic Hand V3.0**.

| Função | GPIO | Observação |
| --- | --- | --- |
| MINDY — IN3/IN4 do 2º L293D | 23, 27 | PWM por LEDC |
| DONCARE — IN1/IN2 do 2º L293D | 21, 22 | |
| FEIO — IN3/IN4 do 1º L293D | 5, 15 | |
| JULGADOR — IN1/IN2 do 1º L293D | 18, 19 | |
| Servo do polegar (flexão) | 25 | LEDC 50 Hz, 16 bits |
| Servo do polegar (abdução) | 26 | |
| Corrente MINDY / DONCARE / FEIO / JULGADOR | 36, 39, 34, 35 | ADC1 |
| Corrente DEDÃO / DEDAO_ABD | 32, 33 | ADC1 |
| Botão BOOT (10 s = rede de fábrica) | 0 | |
| LED da placa | 2 | |

### A correção que o manual do LAD exige aqui

O esquema do manual põe os **sensores 5 e 6 nos GPIO 25 e 26 — que são ADC2**.
O projeto dele não usa Wi-Fi, e por isso funciona. Aqui o rádio fica ligado o
tempo todo (enlace entre as placas e OTA), e **leitura analógica no ADC2 com
Wi-Fi ligado devolve lixo**.

Os seis sensores foram remapeados para os **seis canais de ADC1** da DevKit — que
são exatamente seis, o número de sensores do LAD — e o 25/26, agora livres,
viraram as saídas dos dois servos do polegar. Nada se perdeu na troca.

### PWM nas entradas do L293D, e não `digitalWrite`

Motor DC ligado direto fecha o dedo rápido demais para a corrente ser lida no
meio do caminho — e ler no meio do caminho é o que faz o dedo **parar quando
encosta**, em vez de parar depois de ter empurrado. Como o EN dos módulos L293D
vem amarrado em nível alto, a velocidade é controlada pelas **entradas**: PWM
numa, zero na outra. O duty está em `VELOCIDADE_DEDO` (200 de 255) e é **chute
educado** — é o primeiro número que o ensaio corrige.

### Pull-down de 10 kΩ nas oito entradas do L293D

Enquanto o firmware não sobe, os GPIO ficam em alta impedância e as entradas do
driver flutuam. O pull-down é o equivalente ao pull-up do OE na mão de servos:
resolve **em hardware** o instante que o firmware não alcança.

### Posição estimada por tempo

Motor DC não tem posição, e é a posição de contato que forma a assinatura do
objeto ([04-propriocepcao.md](04-propriocepcao.md)). Mede-se uma vez o tempo de
ponta a ponta de cada dedo (comando `m` do console, gravado na flash), e a
posição passa a ser a integral do tempo de acionamento.

Estimativa por tempo escorrega — tensão da bateria cai, atrito muda, tendão
estica. Por isso ela se corrige sozinha: **toda abertura completa termina no fim
de curso, e ali a posição volta a ser zero por medida, não por conta.** Quem usa
a prótese abre a mão o tempo todo; o erro não acumula. **Isso não é encoder**, e
o número que sai dali é estimativa.

### O arranque cego

Motor DC parado puxa várias vezes a corrente de regime no instante em que parte.
Os primeiros `ARRANQUE_CEGO_MS` (250 ms) não contam como contato — sem isso, todo
dedo "encosta em alguma coisa" no primeiro instante de movimento.

---

## A mão de servos (variante `LIMBIA_MAO_LAD=0`)

Placa de controle: **ESP32 DevKit V1** (ESP32-D0WD-V3, 30 pinos, 4 MB de flash).
Driver de servo: **PCA9685**, 16 canais, I²C.

## Por que a DevKit V1, e não o C3 SuperMini

O caderno do INOVAWEEK registra a migração para "ESP32/ESP32-C3 SuperMini", e o
C3 é uma placa melhor em quase tudo — menor, USB nativo, mais barata. A escolha
aqui foi outra, e o motivo é contável.

**São os sensores de corrente que mandam.** Cada ACS712 ocupa um canal de ADC1, e
o ADC2 está fora de questão: ele é usado internamente pelo rádio, e leitura
analógica nele devolve lixo com Wi-Fi ligado.

| Placa | Canais de ADC1 utilizáveis |
| --- | --- |
| ESP32 DevKit V1 (30 pinos) | **6** — GPIO 32, 33, 34, 35, 36, 39 |
| ESP32-C3 SuperMini | 5 — GPIO 0 a 4 |

Seis canais são exatamente o que este projeto pede: **quatro correntes, um EMG e
uma reserva**. O C3 tem cinco, e o único C3 da bancada já está no FarmIO.

Contar os canais antes de escolher a placa é o que evita descobrir na solda que
falta um.

> GPIO 37 e 38 existem no chip mas não saem no conector da DevKit V1 de 30 pinos.
> Por isso são seis e não oito.

## Por que um driver PWM, e não os pinos da placa

Sete servos ligados direto exigiriam sete canais LEDC. O ESP32 tem oito de alta
velocidade — sobraria um para o resto do mundo. Pior: cada canal precisaria de
resolução alta em 50 Hz, que é onde o LEDC do ESP32 é mais desconfortável.

O PCA9685 resolve os sete servos com **dois pinos**, tem 12 bits dedicados a
50 Hz, e já está no estoque — duas unidades. E traz o pino OE, que vale sozinho a
escolha.

## O pino que substitui um procedimento manual

O manual do LAD manda desligar a fonte externa antes de gravar o código, "para
evitar movimentos erráticos dos servos". Procedimento manual é esquecido, e com a
mão montada um espasmo de sete servos arrebenta tendão.

O **OE (Output Enable) do PCA9685 é ativo em nível baixo**. Com pull-up de 10 kΩ,
ele fica alto durante todo o boot e toda a gravação, e as saídas de PWM ficam
desligadas **em hardware** — sem depender de o firmware ter subido.

O firmware só baixa esse pino depois de ter escrito uma posição válida em todos
os canais. A ordem no `Dedos::begin()` não é negociável:

```
1. OE alto            -> saídas mortas
2. mede o zero dos ACS712   (só faz sentido com tudo parado)
3. escreve posição válida em TODOS os canais
4. só então libera as saídas
```

Inverter 3 e 4 faria os servos saltarem para o último valor que estivesse nos
registradores do PCA9685 — que, depois de um reset por watchdog, é qualquer coisa.

E há um bônus: com o OE ainda alto no passo 2, **a corrente é zero por
construção**. É esse o instante em que o firmware mede o offset de cada ACS712,
em vez de supor o valor teórico de Vcc/2.

## Tabela de ligação

| Função | GPIO | Tipo | Observação |
| --- | --- | --- | --- |
| PCA9685 SDA | 21 | I²C | endereço 0x40 |
| PCA9685 SCL | 22 | I²C | |
| PCA9685 **OE** | 14 | saída | **pull-up de 10 kΩ** — saídas mortas no boot |
| Corrente MINDY | **36** | ADC1_CH0 | só entrada |
| Corrente DONCARE | **39** | ADC1_CH3 | só entrada |
| Corrente FEIO | **34** | ADC1_CH6 | só entrada |
| Corrente JULGADOR | **35** | ADC1_CH7 | só entrada |
| EMG (reservado) | 32 | ADC1_CH4 | sem sensor ainda |
| Reserva analógica | 33 | ADC1_CH5 | livre |
| Botão | 0 | entrada | BOOT, já tem pull-up |
| LED da placa | 2 | saída | aceso enquanto alguma junta se move |
| Buzzer (opcional) | 13 | saída | aviso de contato e de falha |

Pinos deliberadamente livres: **6–11** (ligados à flash interna, usá-los trava a
placa) e **12** (strapping MTDI — nível alto no boot faz a placa tentar 1,8 V na
flash e não subir).

### Canais do PCA9685

| Canal | Junta | Anatomia |
| --- | --- | --- |
| 0 | MINDY | mindinho |
| 1 | DONCARE | anelar |
| 2 | FEIO | médio |
| 3 | JULGADOR | indicador |
| 4 | DEDÃO | polegar — flexão |
| 5 | DEDAO_ABD | polegar — abdução |
| 6 | PULSO | punho |

Sobram nove canais. É onde entram os dedos da segunda mão, ou a órtese.

## Quatro sensores para sete juntas — e o firmware sabe disso

O estoque tem **quatro ACS712**. As quatro juntas instrumentadas são os dedos
longos (MINDY, DONCARE, FEIO, JULGADOR), porque são eles que formam a assinatura
do objeto. O polegar fica cego nesta versão.

Isso está declarado no firmware, não implícito:

```c
#define SENSORES_INSTALADOS                                      \
  (LIMBIA_BIT(limbia::MINDY) | LIMBIA_BIT(limbia::DONCARE) |     \
   LIMBIA_BIT(limbia::FEIO)  | LIMBIA_BIT(limbia::JULGADOR))
```

A máscara importa porque, sem ela, o classificador leria "junta sem sensor" como
"junta que não encostou em nada" — que é uma resposta **confiante e errada**, o
pior tipo. Com a máscara, junta sem sensor não vota: ela reduz a confiança do
resultado. O autoteste verifica isso explicitamente.

**O quinto ACS712 é a compra que mais rende neste projeto**, e vai no polegar:
sem ele, a mão não distingue "vazia" de "segurando algo fino contra a palma".

## A cadeia de medição de corrente

O ACS712 do estoque é a variante de **20 A**: 100 mV/A, com saída em repouso no
meio da alimentação (2,5 V em 5 V).

**2,5 V já é quase o teto do ADC do ESP32**, e um pico de corrente sobe disso. Por
isso a saída de cada sensor passa por um divisor **10 kΩ / 20 kΩ** (ganho 0,667):

```
ACS712 OUT ──[10k]──┬──> GPIO (ADC1)
                    │
                  [20k]
                    │
                   GND
```

| | Sem divisor | Com divisor |
| --- | --- | --- |
| Repouso | 2,50 V | 1,67 V |
| Sensibilidade | 100 mV/A | 66,7 mV/A |
| Resolução (12 bits, 3,3 V) | ~8 mA/contagem | **~12 mA/contagem** |
| Margem até o teto do ADC | 0,8 V (≈ 8 A) | 1,63 V (≈ 24 A) |

Doze miliampères por contagem para detectar um dedo encostando é folga larga: o
limiar de contato está em 400 mA.

> **Recomendação de compra:** a variante de **5 A** (185 mV/A) daria quase o
> triplo de resolução na faixa que realmente interessa — servo de mão puxa
> tipicamente 200 mA a 1 A. A de 20 A funciona; a de 5 A funciona melhor.

## Alimentação

```
Fonte 6 V (ou LM2596 a partir de 9 V) ──┬── PCA9685 V+ ── servos
                                        │
                                        └── ACS712 VCC (5 V)

USB ou LM2596 5 V ── ESP32 DevKit V1 (VIN)

GND comum obrigatório entre fonte dos servos, ESP32, PCA9685 e sensores.
```

Três pontos que quebram a montagem se passarem batido:

1. **Os servos não podem ser alimentados pelo regulador da placa.** Sete servos
   de 13 kgf em movimento simultâneo passam de 5 A de pico. O 3V3 da DevKit V1
   entrega dezenas de miliampères. A alimentação dos servos entra pelo V+ do
   PCA9685, de fonte própria.
2. **Terra comum.** Sem isso a leitura de corrente flutua e a mão "vê" contato
   onde não há.
3. **Divisor obrigatório na saída do ACS712.** Sem ele, um pico de corrente põe
   mais de 3,3 V no pino do ADC — e o pino morre calado, lendo valor fixo.

> O manual do LAD traz uma nota específica do arranjo dele, que não se aplica
> aqui mas vale registrar: com motores DC e L293D, a entrada do driver precisa de
> **+6,49 V** para que os motores recebam 6 V, por causa da queda interna do
> L293D. Como o LimbIA usa servos, não há esse desconto.

## Calibração — leia antes de confiar em qualquer movimento

Os limiares de corrente em [`include/config.h`](../include/config.h) são **ponto
de partida, não medida**:

```c
#define CORRENTE_CONTATO_MA  400   // acima disso: o dedo encostou
#define CORRENTE_LIMITE_MA   900   // acima disso: corta, protege o servo
#define CORRENTE_FOLGA_MA    150   // curso inteiro abaixo: tendão frouxo
```

Dependem do servo (MG90S e 13 kgf têm consumos muito diferentes), do atrito da
polia e de quanto o tendão já esticou.

O roteiro dos pulsos, e o do console que os grava, estão em
[03-anatomia-gestos-e-calibracao.md](03-anatomia-gestos-e-calibracao.md).

## O que o estoque cobre, e o que falta

| Função | Componente | Situação |
| --- | --- | --- |
| Driver de servo | PCA9685 ×2 | ✅ cobre com sobra |
| Corrente | ACS712 20 A ×4 | ⚠️ faltam 2 para instrumentar polegar e abdução |
| Servos | MG90S ×6 (metal), SG90 ×n | ⚠️ ver abaixo |
| Regulação | LM2596 ×3 | ✅ |
| Controle | ESP32 DevKit V1 | ✅ |
| EMG | — | ❌ não existe no estoque |

**Sobre os servos:** o MG90S tem engrenagem metálica e cerca de **2,2 kg·cm** de
torque. O caderno do INOVAWEEK registra a migração para servos de **13 kgf**
justamente porque o torque baixo não vencia a tensão dos tendões. Os MG90S servem
para montar, calibrar e validar toda a lógica — que é o que este repositório
precisa agora — mas não vão fechar a mão contra um objeto. A troca para os 13 kgf
é pré-requisito para o primeiro ensaio de preensão real.

# 08 — Amazing Hand com MG90S, PCA9685 e MyoWare

Decidido em 12/11/2026. Ambiente `amazing_c3`, firmware
[`src/main_amazing.cpp`](../src/main_amazing.cpp), lógica pura em
[`lib/limbia_mao/limbia_paralelo.h`](../lib/limbia_mao/limbia_paralelo.h),
parâmetros em [`include/config_amazing.h`](../include/config_amazing.h).

## De onde vem

Mão **Amazing Hand**, da Pollen Robotics
([github.com/pollen-robotics/AmazingHand](https://github.com/pollen-robotics/AmazingHand),
Apache-2.0). No original, oito servos seriais Feetech SCS0009, dois por dedo,
IDs 1/2 indicador, 3/4 médio, 5/6 anelar, 7/8 polegar.

Aqui trocamos o atuador: **MG90S** (servo PWM comum) num **PCA9685**, e o
comando vem de um **MyoWare** numa **ESP32-C3 SuperMini**. Uma placa só.

## A pegadinha: dois servos por dedo

Cada dedo é um mecanismo paralelo: dois servos lado a lado, cada um puxando
uma haste com rótula até a base do dedo.

| Os dois servos giram… | O dedo faz… |
| --- | --- |
| em sentidos **opostos** | flexão / extensão |
| no **mesmo** sentido | abdução / adução (abre para o lado) |

Mandar o mesmo ângulo para os dois **não fecha o dedo — torce ele para o lado**.

O demo oficial chama `Move_X(Angle_1, Angle_2)` e cada servo vai para
`MiddlePos + Angle`. Os números dos gestos dão a regra:

| Gesto oficial | (Angle_1, Angle_2) | soma | diferença |
| --- | --- | --- | --- |
| OpenHand | (−35, 35) | 0 | −70 |
| CloseHand | (90, −90) | 0 | 180 |
| Spread, indicador | (4, 90) | +94 | — |
| Spread, anelar | (−90, −4) | −94 | — |

```text
Angle_1 = +flexão + abdução
Angle_2 = −flexão + abdução
```

O repositório oficial **não escreve essa regra**. Ela foi deduzida dos números
acima e é conferida pelo autoteste (seção [12]). A prova definitiva é o dedo
montado.

## Do SCS0009 para o MG90S

| Amazing Hand | Aqui |
| --- | --- |
| `MiddlePos` por servo, em ticks (511 = meio de 300°) | `centro` por servo, em graus absolutos (90 = meio de 0–180°) |
| `write_goal_position` em radianos | pulso de 500–2400 µs a 50 Hz no PCA9685 |
| velocidade nativa do servo | rampa no firmware, um passo a cada 20 ms |

Os horns foram encaixados com o servo **no meio do curso (90°)**, como no guia
oficial (confirmado pelo Henrique em 12/11). Então o "zero" do dedo é A = 90°,
B = 90°, e flexão θ leva A para 90 + θ e B para 90 − θ.

Se um dedo **estender** quando deveria fletir, o MG90S ficou espelhado em
relação ao SCS0009: comando `inv <dedo>` no console, sem mexer em fio.

## Ligação (pinagem completa, 17/11/2026)

### ESP32-C3 SuperMini

| Pino do C3 | Vai para | Função |
| --- | --- | --- |
| **GPIO6** | PCA9685 **SDA** | dados I2C |
| **GPIO7** | PCA9685 **SCL** | relógio I2C |
| **GPIO3** (ADC1_CH3) | MyoWare **SIG** (ou RAW) | sinal do músculo |
| **3V3** | PCA9685 **VCC** e MyoWare **+Vs** | alimentação lógica |
| **GND** | PCA9685 **GND**, MyoWare **GND** e **GND da fonte de 5 V** | terra comum |
| USB-C | computador (opcional) | gravação, console e Serial Plotter |

### PCA9685: os 8 servos

| Canal | Servo | Dedo |
| --- | --- | --- |
| 0 | A | indicador |
| 1 | B | indicador |
| 2 | A | médio |
| 3 | B | médio |
| 4 | A | anelar |
| 5 | B | anelar |
| 6 | A | polegar |
| 7 | B | polegar |
| 8–15 | livres | — |

Cada MG90S tem três fios no borne do canal:

- **marrom = GND**;
- **vermelho = V+**;
- **laranja = sinal (PWM)**.

**Alimentação dos servos:** o borne **V+** do PCA9685 recebe **5 V de uma fonte
externa** de pelo menos **5 A**, com o GND dela ligado ao GND do C3. Um MG90S
travado puxa ~700 mA, e oito passam de 5 A. O 3V3 do C3 não alimenta nem um.
**Nunca ligar o V+ no 3V3 nem no 5V da USB do C3.**

O pino **OE** do PCA9685 fica sem ligar: a placa já o puxa para habilitar as
saídas. Quem garante que nada se move antes da calibração é o firmware, com as
saídas em "full off".

**A e B:** o servo A do dedo recebe o ângulo que soma e o B o que subtrai (a
regra do Amazing Hand, acima). Se um dedo **estender** quando deveria fletir, o
comando é `inv <dedo>` no console, sem mexer em fio. Se ele **torcer para o
lado**, os dois servos do par estão trocados de canal.

## Limites da fase 1

Pedido do Henrique: começar em 0 e ir no **máximo a 90**.

- Flexão de 0 a 90 (graus do Amazing Hand). O teto de 90 está no
  `config_amazing.h` e o console não passa dele.
- Extensão (o OpenHand oficial vai a −35) e abdução **travadas em zero** até o
  curso ser conhecido.
- `lim <graus>` encurta o curso sem regravar.

## Comando pelo EMG: alternado

Decidido em 13/11/2026, e só com a mão **liberada** (ver a trava, abaixo):
**contraiu uma vez, a mão fecha; contraiu de novo, abre.** A mão não precisa de contração sustentada para ficar fechada.

Só a **subida** da contração troca o estado. Segurar a contração por 4 s
troca uma vez só, e a próxima troca exige relaxar antes. Depois de cada troca
há **800 ms de período refratário**: contração nesse intervalo é ignorada e
contada na página como "contração ignorada".

Durante a calibração a mão não alterna. Senão cada contração pedida pelo
exercício abriria e fecharia a mão.

## O filtro, à prova de ruído sem mexer no hardware

Lógica pura em [`limbia_sinal.h`](../lib/limbia_mao/limbia_sinal.h), com
cada estágio contra um ruído que já apareceu nesta bancada:

| Estágio | Mata | Entrada |
| --- | --- | --- |
| **Hampel** de 5 amostras (k = 4 MAD, piso 40) | espigão de mau contato e de leitura do ADC: troca a amostra pela mediana só quando ela foge | SIG e RAW |
| Passa-altas 20 Hz | DC e artefato de movimento do cabo | só RAW |
| Notch **60 Hz** e **120 Hz** | rede elétrica, que chegou a 93–99% da potência em setembro | SIG e RAW |
| Retificação | — | só RAW |
| Passa-baixas 5 Hz **duas vezes** (4ª ordem) | ondulação que sobra; 80 dB por década | SIG e RAW |

Do mesmo sinal limpo saem **dois envelopes**:

- o **lento** (4ª ordem, 5 Hz) diz o **nível**. É liso e não treme no limiar;
- o **rápido** (2ª ordem, 25 Hz) diz a **duração**. O lento espalha o sinal no
  tempo: uma pancada de 40 ms vira envelope acima do limiar por mais de 80 ms
  e passava por contração. O autoteste pegou isso em 13/11.

Na decisão entram:

- a histerese (liga / desliga);
- 80 ms contínuos acima do limiar **nos dois envelopes** para valer, e 200 ms
  abaixo para soltar;
- o período refratário;
- o **teto de artefato**: depois de calibrar, um pico acima de 4× a faixa da
  contração (pancada no eletrodo) não conta.

RAW saturada congela a mão.

O filtro entra em regime pela **média dos primeiros 300 ms**, nunca por uma
amostra só. Pré-aquecer com a primeira amostra fazia um espigão no instante do
boot fechar a mão sozinha, o que também apareceu no autoteste de 13/11. Hoje
são duas proteções independentes contra isso: o pré-aquecimento pela média e a
regra de duração.

O recorte em 0 do ADC é limite físico, não do filtro. Rede de 200 de amplitude
em cima de um repouso de 86 recorta, e o nível médio sobe de verdade (~+17). O
repouso medido em 12/11 teve desvio < 1, longe disso.

Medido no autoteste [13], no C3: ver a entrada do diário de 13/11.

## A trava: nada se move sem calibração (17/11/2026)

**A prótese liga TRAVADA.** O PCA9685 nasce com as 16 saídas em "full off":
nenhum servo recebe pulso, então todos ficam soltos e parados. O PCA guarda
os registradores quando só o ESP reinicia, por isso o firmware força o "full
off" a cada boot. Sem isso, um reset no meio do uso deixaria os servos
segurando a última pose.

A calibração é exigida **a cada vez que liga**, porque o eletrodo nunca volta
exatamente ao mesmo lugar. Só ela destrava. Recalibrar, ou trocar a saída do
MyoWare (`raw`/`env`), trava de novo.

### A calibração, que insiste até ter certeza

1. **Preparo e repouso** (3 s + 4 s): braço solto. Mede média e desvio do
   repouso.
2. **Aprendizado:** "contraia" (2 s) e "relaxe" (3 s), **quantas vezes
   precisar**, até ter **3 contrações boas seguidas** e consistentes.
   - **Boa:** pico acima de 6 desvios do repouso e acima de 1,5× o repouso.
   - **E o braço voltou ao repouso** no fim do relaxe. Senão, a tela diz
     "não relaxou: solte o braço todo" e a sequência recomeça.
   - **Consistentes:** a maior das 3 no máximo 2,5× a menor. Senão, "diferente
     das anteriores: tente igual".
   - Uma contração fraca **zera a sequência**.
   - Desiste depois de 10 tentativas.
3. **Limiares** a partir da mediana das 3: disparo = repouso + 35% da faixa,
   solta = repouso + 15%.
4. **Prova:** mais 3 contrações pedidas, julgadas pelo **mesmo detector que vai
   comandar a mão**, já com o limiar novo. Passa só com **3 de 3 reconhecidas e
   nenhum disparo durante os relaxes**. Errou: volta ao aprendizado. Três
   provas erradas: recusa.
5. Aprovada: **a mão é liberada** e faz o **auto-home**.

O caminho mais curto leva **~37 s**. Na tela aparecem a instrução grande, a
contagem regressiva, as bolinhas de "boas seguidas" e da "prova", e o
julgamento da última contração.

## Auto-home = mão aberta

Definido em 17/11. Flexão 0: os dois servos de cada dedo no **centro**. É a
pose de montagem do Amazing Hand, e a segura:

- não belisca;
- não aperta objeto;
- cada servo fica com curso para os dois lados.

O auto-home roda **sozinho quando a calibração libera a mão** e também pelo
botão.

No primeiro auto-home os servos vêm de "sem pulso" e o firmware não sabe onde
eles estão. Eles vão ao centro **na velocidade própria do servo**, porque não
há rampa possível sem conhecer a posição de partida. Do segundo movimento em
diante, tudo anda em rampa.

## Servos um a um

Cada servo anda em **curso**: de 0 (casa, mão aberta) até o limite de flexão
(90 no máximo), **sempre para o lado que fecha o dedo**. O servo A soma ao
centro e o B subtrai. Assim nenhum comando, nem o de um servo sozinho, estende
o dedo além da casa. Mexer A e B do mesmo dedo em valores diferentes inclina o
dedo para o lado (abdução), dentro da faixa que o Amazing Hand usa nos gestos
dele.

## A página da mão

**Como entrar, sem internet e sem senha:**

1. Ligue a mão.
2. No celular, abra o Wi-Fi e toque em **`LimbIA-mao`** (rede aberta).
3. A página abre sozinha (portal cativo). Se não abrir, digite
   **192.168.4.1** no navegador; qualquer endereço digitado também cai nela.
4. Se o celular avisar que "a rede não tem internet", escolha **manter
   conectado**.

Não depende de roteador, de nuvem nem de aplicativo.

**O que tem nela, de cima para baixo:**

- **Faixa da trava:** vermelha "MÃO TRAVADA, calibre para liberar", com o botão
  "Calibrar agora", ou verde "MÃO LIBERADA".
- **Sinal do músculo:**
  - uma **lâmpada grande** que acende verde, escrito **CONTRAÇÃO**, enquanto
    uma contração está reconhecida;
  - o contador de contrações reconhecidas;
  - o gráfico dos últimos 5 s: envelope, linha de **disparo** (vermelha), linha
    de **solta** (laranja) e faixa verde onde houve contração reconhecida.

  Antes da calibração não há linha de disparo, e a lâmpada diz "calibre
  primeiro".
- **Calibração:** instrução grande, contagem regressiva, progresso e o resultado.
  Com a mão liberada, o botão vira "Recalibrar" e pede confirmação, porque
  recalibrar trava a mão.
- **Mão:** Abrir mão, Fechar mão, Auto-home.
- **Servos, um a um:** 8 sliders (indicador A/B, médio A/B, anelar A/B,
  polegar A/B, com o canal de cada um).
- **Capturar leitura:** Gravar / Baixar CSV, com as colunas
  `t_ms, adc, envelope, disparo, solta, contracao, mao_fechada, liberada`.

Com a mão travada, os botões de movimento e os sliders ficam desligados, e o
firmware recusa o comando mesmo se ele chegar.

**O preço da rede aberta:** qualquer pessoa no alcance do Wi-Fi pode abrir a
página e mover a mão, depois de liberada. Foi pedido assim. Se a mão sair do
laboratório, isso volta à mesa.

A potência do rádio fica em 8,5 dBm. O C3 SuperMini tem a antena mal casada e
perde a rede na potência máxima.

## Serial Plotter

Na USB, 50 linhas por segundo: `envelope`, `liga` (disparo), `desliga` (solta)
e `contracao`. A última salta até a linha de disparo enquanto há contração
reconhecida, para caber na mesma escala. O comando `t` liga e desliga essas
linhas.

## Console (115200, fim de linha LF)

| Comando | Faz |
| --- | --- |
| `?` | estado e ajuda |
| `cal` / `cancela` | calibração (trava até ser aprovada) |
| `abre` / `fecha` | mão inteira (só liberada) |
| `home` | auto-home = mão aberta (só liberada) |
| `servo <i> <c>` | servo `i` (0–7) no curso `c` (0–limite) (só liberada) |
| `lim <g>` | limite de flexão, 0–90 |
| `centro <d> <a> <b>` | centro dos servos do dedo `d` |
| `inv <d>` | inverte o sentido de flexão do dedo `d` |
| `dedos <máscara>` | quais dedos andam: `1` só indicador, `15` todos |
| `liga <l> <d>` | ajuste fino do limiar, depois de calibrado |
| `raw` / `env` / `auto` | tipo de saída do MyoWare (trava de novo) |
| `i2c` | diagnóstico do barramento e nova tentativa no PCA9685 |
| `t` | liga/desliga as linhas do Serial Plotter |

## Roteiro sugerido para o primeiro ensaio

1. **Antes de ligar os servos:**
   - MyoWare na pele;
   - conecte em `LimbIA-mao` e olhe o gráfico: o repouso precisa estar
     parado, e uma contração precisa subir bem acima dele;
   - faça a calibração até ela aprovar.
2. **Ainda no console, com a mão liberada:** `dedos 1` (só o indicador) e
   `lim 20`. Na página, suba "Indicador A" e "Indicador B" juntos até 10 e
   depois 20, e confira se o dedo **fecha**: não pode estender nem torcer.
3. Suba o limite de 10 em 10 até 90, anotando onde a haste ou o dedo encosta.
4. Ajuste `centro` até o dedo ficar reto no auto-home. Os valores entram em
   `config_amazing.h` num commit `calib`.
5. `dedos 15` (todos) e só então o comando pelo músculo: contrair fecha,
   contrair de novo abre.

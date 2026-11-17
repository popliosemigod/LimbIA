# Diário de bordo — LimbIA

Registro de cada iteração do ciclo de tentativa e erro. O formato é fixo para que
as entradas sejam comparáveis entre si e ao longo do tempo.

O campo que mais importa é a **divergência** entre previsto e medido: é ela que
corrige o modelo teórico da próxima iteração. Entrada sem divergência anotada é
entrada pela metade.

---

## Modelo de entrada

### AAAA-MM-DD — Título curto da iteração

**Alvo:** o que esta iteração precisava alcançar, de forma mensurável.

**Previsão:** o número calculado antes do ensaio.

**O que foi feito:** montagem, ajuste ou código alterado.

**Medido:** o resultado real.

**Divergência:** previsto × medido, e a hipótese para a diferença.

**Decisão:** o que muda na próxima iteração.

**Evidência:** caminho dos arquivos em `evidencias/`.

---

## Entradas

### 2026-09-09 — Fusão do INOVAWEEK com o LAD, e a lógica medida numa placa

**Alvo:** transformar dois projetos que existiam em documento — o braço do
INOVAWEEK e o manual do LAD Robotic Hand V3.0 — num repositório com firmware que
compila e com a lógica da mão verificada por número, não por opinião. Critério de
aceitação: os ambientes compilando, nenhum segredo versionado, e o classificador
de preensão medido numa placa real.

**Previsão, anotada antes de rodar:**

| | Previsto |
| --- | --- |
| Acerto do classificador, casos bem separados | 90–95% |
| Acerto nos casos de fronteira | 60–70% |
| Custo de uma classificação no ESP32 | 15–30 µs |
| Flash do firmware da mão | ~35% |

**O que foi feito:**

- Repositório criado com o padrão do laboratório — mesmos `cz.toml`,
  `.pre-commit-config.yaml`, `.clang-format`, `.gitattributes` e CI do Jaspy, com
  o escopo do `clang-format` apontado para `src/` e `include/`.
- **Biblioteca portável `lib/limbia_mao/`**, sem Arduino dentro: conversão
  posição↔pulso com endpoints medidos, tabela de gestos, classificação de
  preensão e diagnóstico de tendão frouxo. É ela que o autoteste exercita.
- **Firmware da mão** em cinco módulos: `config`, `corrente`, `dedos`, `preensao`,
  `memoria`, com console serial compatível com os comandos 1–7 do manual do LAD.
- **Ambiente `autoteste`**, que roda na placa sem nada ligado nela e imprime
  números.

**A fusão, em uma linha:** o LAD mede força e não sabe onde o dedo está; o
INOVAWEEK sabe onde o dedo está e não mede força. Servo (do INOVAWEEK) mais
sensor de corrente (do LAD) dão os dois sentidos, e é isso que permite o dedo
parar **quando encosta** em vez de parar num ângulo combinado.

**Medido** — ESP32-C3 na COM7, ambiente `autoteste_c3`:

| Ensaio | Previsto | Medido | |
| --- | --- | --- | --- |
| Verificações | todas passando | **35/35**, em 8 ms | ✅ |
| Classificador, casos bem separados | 90–95% | **100%** (240/240) | ✅ |
| Classificador, casos de fronteira | 60–70% | **85%** (34/40) | ✅ |
| Confiança no meio das classes | — | 94% | |
| Confiança na fronteira | — | **86%** | |
| Erro de ida e volta posição↔pulso | ≤ 3 por mil | **1 por mil** | ✅ |
| Custo de uma classificação | 15–30 µs | **1,7 µs** | ✅ |
| Confiança com 4 dedos / com 2 | — | 100% / 45% | |
| Flash da mão (`esp32dev`) | ~35% | **26,0%** (341 kB) | ✅ |
| RAM da mão | — | 6,8% (22,4 kB) | |

**Divergência 1 — classificação 10 a 18 vezes mais rápida que o previsto.** A
previsão de 15–30 µs supôs float em operações por dedo. A implementação usa
inteiro em quase tudo e só percorre quatro dedos, então a conta é trivial. A
folga é útil: dá para classificar a cada passo do fechamento, se um dia isso for
necessário, sem tocar no orçamento do tick de 20 ms.

**Divergência 2 — 100% no banco principal, e é ela que merece desconfiança.** As
quatro classes nasceram bem separadas no gerador que eu mesmo escrevi. 100% ali
mede o gerador tanto quanto o classificador, e foi por isso que os casos de
fronteira entraram no teste depois da primeira execução — a primeira versão do
autoteste dava 100% e não informava nada.

**O achado que valeu a sessão — a confiança está calibrada.** Nos casos de
fronteira, o acerto caiu para 85% e a confiança caiu para 86%. Os dois números
caem juntos e quase na mesma medida. Isso não estava previsto e não é sorte de um
caso: significa que o número de confiança **pode ser usado para decidir**. Uma
preensão relatada com 50% pode ser tratada como "reposicione e tente de novo", e
essa decisão vai estar certa na proporção anunciada.

**Um erro meu, corrigido durante a construção.** A primeira versão do
classificador tratava "junta sem sensor" como "junta que não encostou em nada".
Com quatro ACS712 para sete juntas, isso faria a mão responder **"nada na mão"
com 90% de confiança** enquanto o polegar segurava uma caneta contra a palma —
resposta confiante e errada, o pior tipo. A correção foi a máscara
`comSensor`: junta sem sensor não vota, e reduz a confiança do resultado. O
autoteste passou a verificar exatamente esse caso.

**Decisão:** a placa é a **DevKit V1**, não o C3 SuperMini que o caderno do
INOVAWEEK cita. O motivo é contável: cada ACS712 ocupa um canal de ADC1, e a
DevKit expõe seis (32, 33, 34, 35, 36, 39) contra cinco do C3 — quatro correntes,
um EMG e uma reserva. O único C3 da bancada já está no FarmIO.

**Decisão:** os servos são acionados por **PCA9685**, não pelos pinos da placa.
Sete servos exigiriam sete dos oito canais LEDC do ESP32. E o pino OE do módulo,
com pull-up, resolve em hardware o aviso do manual do LAD sobre movimentos
erráticos durante a gravação — procedimento manual é esquecido, e com a mão
montada um espasmo de sete servos arrebenta tendão.

**Pendências, em ordem de importância:**

1. **Nenhum servo foi ligado.** Tudo aqui é lógica exercitada em memória, numa
   placa nua. Contato real, corrente real e tendão real são ensaio de bancada.
2. **Os três limiares de corrente são chute educado** e dependem do servo, do
   atrito da polia e de quanto o tendão já esticou.
3. **Faltam dois ACS712** — polegar e abdução. O autoteste mostra a confiança
   caindo de 100% para 45% com metade dos dedos longos cegos; sem sensor no
   polegar, "mão vazia" e "segurando algo fino contra a palma" são a mesma
   leitura.
4. **Os MG90S do estoque não fecham a mão.** 2,2 kg·cm não vence a tensão dos
   tendões — o próprio caderno registra a migração para 13 kgf por esse motivo.
   Servem para montar e calibrar; não para agarrar.

**Nota de bancada:** o C3 da COM7 está agora com o autoteste do LimbIA gravado.
Para devolver o FarmIO à placa: `pio run -e c3 -t upload` no repositório do
FarmIO.

**Evidência:** saída de `pio run` nos quatro ambientes; saída serial do
`autoteste_c3` gravado na placa, nas duas execuções (antes e depois de os casos
de fronteira entrarem).

---

### 2026-09-13 — Duas placas, o EMG e a volta ao hardware do LAD

**Alvo:** transformar o pedido do Henrique — EMG movendo a mão, duas placas, OTA
e tela de ajuste — em firmware que compila e cuja lógica esteja medida; e
rodá-lo nas duas placas que estão na mesa (uma DevKit V1 e um C3 SuperMini).

**A mudança de escopo, tomada nesta sessão.** O hardware da bancada passou a ser
o do **LAD Robotic Hand V3.0**: quatro dedos longos com **motores DC** em dois
L293D, dois servos no polegar e **seis ACS712**. O firmware anterior era o
oposto — sete servos num PCA9685. Duas consequências:

1. **Motor DC não tem posição**, e é a posição de contato que forma a assinatura
   do objeto. A saída adotada foi **posição estimada por tempo**: mede-se uma vez
   o tempo de ponta a ponta de cada dedo, a posição passa a ser a integral do
   tempo de acionamento, e **toda abertura completa re-zera a estimativa** contra
   o fim de curso. Não é encoder, e o número que ela produz é estimativa.
2. **O polegar deixou de ser cego.** São seis sensores, um por motor. A dívida
   "faltam dois ACS712" do 09/09 está paga pelo próprio hardware do LAD.

**Correção obrigatória no esquema do manual do LAD.** Ele põe os sensores 5 e 6
nos **GPIO 25 e 26, que são ADC2**. Aquele projeto não usa Wi-Fi e por isso
funciona; aqui o rádio fica ligado o tempo todo e o ADC2 devolveria lixo. Os seis
sensores foram remapeados para os seis canais de ADC1 da DevKit (32, 33, 34, 35,
36 e 39 — exatamente seis), e o 25/26 viraram as saídas dos servos do polegar.

**Previsão, anotada antes de gravar as placas:**

| | Previsto |
| --- | --- |
| Custo da cadeia de filtros na DevKit (com FPU) | 2–6 µs por amostra |
| Custo da cadeia de filtros no C3 (sem FPU) | 20–60 µs por amostra |
| O C3 dá conta de 2 canais a 1 kHz servindo Wi-Fi? | sim, com folga |
| Janelas perdidas em um minuto de amostragem | 0 |
| Verificações do autoteste | 89/89 nas duas placas |

**Medido:**

| Ensaio | Previsto | Medido | |
| --- | --- | --- | --- |
| Autoteste na DevKit V1 | 89/89 | **89/89**, em 44,1 s | ✅ |
| Autoteste no C3 (banco reduzido) | 89/89 | **89/89**, em 209,5 s | ✅ |
| Cadeia de filtros, DevKit | 2–6 µs | **0,90 µs** por amostra | ✅ |
| Cadeia de filtros, C3 | 20–60 µs | **22,65 µs** → **4,5%** de um núcleo | ✅ |
| Classificação LDA, DevKit / C3 | — | 2,99 µs / 30,33 µs | |
| Janelas processadas no C3, com AP e enlace no ar | perdidas 0 | **1045, perdidas 0** | ✅ |
| Enlace entre as placas | conectar | **conectou nos dois sentidos** | ✅ |
| Flash: mão / EMG | — | 64,8% / 66,1% | |

**Divergência 1 — o filtro custa 25× mais no C3, e era esperado.** 0,90 µs na
DevKit contra 22,65 µs no C3: é exatamente o preço de não ter FPU, com cada
multiplicação virando rotina de biblioteca. O que importa é a conta final: dois
canais a 1 kHz custam **4,5% de um núcleo** do C3. Sobra folga de vinte vezes, e
a medida de campo confirma — **1045 janelas, nenhuma perdida**, com o ponto de
acesso, a tela e o enlace rodando na mesma placa.

**Divergência 2 — o banco completo do autoteste é impraticável no C3.** Levou
mais de dez minutos e não terminou; bancada parada esperando teste é teste que
ninguém roda. O banco passou a encolher quando compilado para o C3 (3 ciclos e
12 gestos em vez de 6 e 60). As verificações são as mesmas; o que muda é quantos
casos cada uma vê. O banco completo continua rodando na DevKit, em 44 s.

**Três defeitos que só a bancada mostrou, todos corrigidos na mesma sessão:**

1. **`endPacket: could not send data: 12`, vinte vezes por segundo.** A placa do
   EMG mandava comando para uma mão que ainda não tinha entrado na rede. Agora só
   fala quando há alguém na rede, e espaça as tentativas quando o envio falha.
2. **`SOBRECARGA em JULGADOR (1232 mA)` com o pino no ar.** Entrada de ADC solta
   flutua e o firmware lê isso como corrente — a proteção agiu certo, mas travou
   a bancada antes de existir sensor. Entrou o comando `i`, que ignora a corrente
   **só na bancada**: sem parada por contato e sem proteção, sobra a guarda de
   tempo. Numa prótese isso nunca fica desligado.
3. **O pior dos três: com a corrente ignorada, a mão ainda diagnosticou "tendão
   frouxo" em cinco juntas.** A máscara da assinatura vinha do `#define` de
   compilação, não de quem estava sentindo de verdade. É a regra 7 do projeto
   sendo violada por dentro — resposta confiante sobre o que não foi medido.
   Corrigido: a máscara passou a ser montada a partir de `Corr::temSensor()`, e
   a mesma preensão agora responde **"indefinido, confiança 0%"**, com "(sem
   sensor)" em cada junta. Conferido na placa depois da correção.

**O caminho inteiro, medido de ponta a ponta:** comando saindo da placa do EMG,
atravessando a rede da prótese por UDP e executado pela mão, com a telemetria
voltando — `[emg] fechar (modo ajuste)` de um lado, posições subindo de
`[468 468 468 468 296 4 500]` até `[1000 1000 1000 1000 1000 0 500]` do outro, e
a preensão classificando no fim. **Nenhum motor estava ligado:** o que se moveu
foi o estado, a estimativa de posição e o relatório.

**O que a fusão com o EMG tem de próprio.** A mão não vai à pose gravada e para:
ela fecha **parando quando encosta**. A pessoa contrai o flexor, a mão fecha em
volta do copo e para no contato, em vez de esmagar até a pose. É a propriocepção
do LimbIA encontrando o EMG.

**Decisão:** a "IA" é um **LDA de três classes** (repouso, fechar, abrir)
treinado por usuário, e não uma rede neural. Dois canais, três classes e um
minuto de calibração não sustentam mais que seis médias e uma covariância 2×2 —
uma rede decoraria o minuto de calibração. E o LDA devolve probabilidade, que é
o que o decisor usa para saber quando **não** agir.

**Decisão:** o enlace entre as placas é Wi-Fi, e não fio. Sem fio entre elas, o
terra ruidoso dos motores não tem caminho até o terra do amplificador de EMG, que
mede microvolts. O mesmo argumento vale para a tela: o ajuste é feito pela rede
da prótese, com a placa do EMG na bateria — **nunca com ela ligada ao USB do PC
enquanto há eletrodo na pele**.

**Pendências, em ordem:**

1. **Nenhum motor foi ligado, e nenhum eletrodo tocou pele.** Tudo aqui é lógica
   medida e estado trafegando entre duas placas.
2. **`VELOCIDADE_DEDO` (200 de 255) é chute** e é o primeiro número que o ensaio
   corrige: rápido demais não deixa a corrente ser lida no meio do caminho.
3. **Os limiares de corrente continuam chute**, agora com motor DC no lugar de
   servo — e motor DC tem pico de partida, tratado por `ARRANQUE_CEGO_MS` (250 ms
   sem contar contato), que também é chute.
4. **Os limiares de qualidade do eletrodo** (d' ≥ 2,5 e razão ≥ 3×) vieram de
   sinal sintético. Só um antebraço de verdade diz se separam bem-posicionado de
   mal-posicionado.
5. **A tela não foi aberta num navegador ainda** — falta um PC entrar na rede da
   prótese e conferir o portal cativo.

**Evidência:** saída serial do autoteste nas duas placas; saída de boot das duas
placas com o enlace subindo; o ensaio de ponta a ponta acima.

---

### 2026-09-14 — Abre a fase de ensaios

Daqui em diante o que entra no diário é medida de bancada, não de lógica. O
roteiro dos seis ensaios, **com as previsões escritas antes de qualquer
medição**, está em [`docs/07-roteiro-de-bancada.md`](docs/07-roteiro-de-bancada.md):

1. um dedo se movendo, com cadência controlada;
2. o mesmo dedo com ACS712 — de onde saem os três limiares de corrente;
3. a mão inteira, e o **primeiro acerto de campo** do classificador de preensão;
4. o primeiro eletrodo na pele, e se os limiares de qualidade separam bem de mal
   posicionado num antebraço de verdade;
5. a prótese em uso;
6. gravação pela rede.

A previsão que mais me interessa errar é a do ensaio 3: **3 de 4 objetos**. Todos
os 100% que este repositório mostra vieram de casos que eu mesmo gerei, e a
posição dos dedos longos agora é **estimada por tempo**, não medida — é onde o
erro de campo deve aparecer primeiro.

Antes dela, a do ensaio 2 é a que pode mudar firmware em vez de número: se a
diferença entre "motor girando livre" e "motor travado" for menor que ~150 mA, o
limiar absoluto não separa os dois casos, e a saída passa a ser medir a
**derivada** da corrente.

**Evidência:** nenhuma ainda. É esse o ponto da entrada.

---

### 2026-11-12 — Amazing Hand com MG90S, PCA9685 e MyoWare numa C3

**Alvo:** programar a ESP32-C3 SuperMini para mover os dedos da Amazing Hand
(Pollen Robotics) com MG90S num PCA9685 (SDA GPIO6, SCL GPIO7), comandados por
um MyoWare no GPIO3. Começar em 0 e ir no máximo a 90. Detalhes em
[docs/08](docs/08-amazing-hand.md).

**Previsão:** a regra dos dois servos por dedo, deduzida dos gestos oficiais
(CloseHand = (90, −90), OpenHand = (−35, 35), Spread = (4, 90) e (−90, −4)), é
Angle_1 = +flexão + abdução e Angle_2 = −flexão + abdução. Com o horn no meio do
curso, flexão 90 leva o servo A a 180° e o B a 0°.

**O que foi feito:** `limbia_paralelo` (lógica pura: regra dos dois servos,
limites, pulso, rampa, histerese, limiar pelo repouso), ambiente `amazing_c3` e
seção [12] no autoteste. Abdução e extensão travadas em 0; flexão de 0 a 90.

**Medido:**

- Autoteste na C3 (COM5): **110 de 110** verificações, seção [12] inclusa.
  Cadeia de filtros: 22,65 µs por amostra.
- Firmware gravado na C3. MyoWare detectado na saída **SIG/ENV**: repouso 86
  contagens (~70 mV), desvio 0,6–0,8; limiar liga 172, desliga 129. Não se sabe
  se o sensor estava na pele.
- **PCA9685 não responde em 0x40.** As duas linhas têm pull-up externo (leem 1
  com o pull-down interno ligado), mas a sonda deu timeout em 8 de 8
  tentativas, com recuperação de barramento entre elas. A varredura alterna NACK
  limpo e timeout de 1 s.
- Cada timeout trava o loop por **1 s**: o core ignora `Wire.setTimeOut(3)` no
  C3.

**Divergência:** a previsão era o PCA9685 responder e os dedos irem a flexão 0.
Pull-up presente sem ACK, com comportamento errático, aponta para defeito
físico. Hipótese principal: GND do PCA9685 sem contato. Os pull-ups vão ao VCC
dele, que está no 3V3, e o chip sem terra não responde. Também possíveis: jumper
com mau contato e barra de pinos sem solda.

**Decisão:** sem nova tentativa automática com o PCA ausente, porque o timeout
de 1 s picotaria o EMG. Ela só acontece pelo comando `i2c`, que também roda o
diagnóstico. Nenhum servo se moveu: a regra dos dois servos **compila e a lógica
foi medida**, mas o dedo montado ainda não a confirmou.

**Evidência:** log do autoteste e do boot no scratchpad da sessão; números
copiados acima.

---

### 2026-11-13 — Leitor de EMG na página da mão, filtro próprio e comando alternado

**Alvo:** ver o sinal do MyoWare sem depender de nada: página servida pelo Wi-Fi
da própria placa, aberto, e Serial Plotter na USB. Captura em arquivo.
Contração fecha a mão e a próxima abre. Filtro de software à prova de ruído.
Limiar padrão ao ligar e calibração opcional por exercício. Detalhes em
[docs/08](docs/08-amazing-hand.md).

**Previsão:**

- Rede de 120 pp e espigões de 2500 não tiram o envelope do repouso.
- Contração aparece em menos de 200 ms.
- Num roteiro de 6 contrações (abre/fecha, segurar 4 s, uma no refratário,
  pancada de 40 ms), a mão troca exatamente 4 vezes.
- Exercício sem contração é recusado.

**O que foi feito:**

- `limbia_sinal`: Hampel de 5 amostras, notch 60/120 Hz, envelope lento de
  4ª ordem (nível) e rápido de 25 Hz (duração), comando alternado com
  histerese, refratário, teto de artefato e calibração guiada com recusa.
- EMG numa tarefa FreeRTOS a 1 kHz.
- Rede aberta `LimbIA-mao` com portal cativo e página autocontida (gráfico,
  calibração, limiar manual, CSV).
- Limiar na NVS.
- Seção [13] do autoteste.

**Medido (C3, COM5):**

- Autoteste **130 de 130**. Repouso com rede de 120 pp e espigões:
  envelope 84,8–86,7 (real 86). Contração 86 → 900 cruza o limiar em
  **86 ms**. Roteiro: fechou 2, abriu 2, ignorou 2 (refratário e pancada).
  Calibração boa: repouso 85,9 ± 0,5, contração 950, liga 388,6, desliga
  215,6. Sem contração e com uma contração em três: recusadas. RAW com
  1000 pp de rede: contração 22× o repouso. Filtro: **50,7 µs por amostra**
  (5,1% do C3).
- Firmware: Serial Plotter a **50,4 linhas/s**. Calibração na placa, sem
  contrair: **recusada** ("separação 1,6"), limiar anterior mantido.
- Rede vista pelo PC: `LimbIA-mao`, 2437 MHz, −50 dBm, aberta.
- Página rodada no Edge headless contra um servidor que imita a placa:
  JavaScript executa, gráfico e estados aparecem e o layout cabe em 360 px.

**Divergência:** a primeira rodada do autoteste falhou em duas coisas, e as
duas eram defeitos reais:

1. **Mão fechando sozinha no boot.** O filtro pré-aquecia com a primeira
   amostra; um espigão nela virava envelope de 2500. Corrigido: regime pela
   média dos 300 ms da detecção.
2. **Pancada de 40 ms valendo como contração.** O envelope lento espalha o
   pulso no tempo. Corrigido medindo a duração no envelope rápido. Subir o
   tempo mínimo para 200 ms também resolveria, mas atrasaria toda resposta
   em ~120 ms.

Cada uma das duas proteções, sozinha, já segura o espigão no boot. Também
apareceu um viés de +17 no repouso: era o meu sinal sintético recortando em
0 no ADC (rede de 200 sobre repouso de 86), não o filtro.

**Decisão:** comando alternado com refratário de 800 ms. Limiar padrão liga
400 / desliga 250 (chute, para a saída SIG). Rede aberta por pedido: qualquer
pessoa no alcance pode comandar a mão. **Nada disso foi testado com músculo
nem com um aparelho conectado na rede.** A página foi verificada contra um
servidor falso, e o PCA9685 continua sem responder.

**Evidência:** logs do autoteste e screenshots da página no scratchpad da
sessão; números copiados acima.

---

### 2026-11-17 — Trava por calibração, 8 servos na página e auto-home

**Alvo:**

- A prótese não move nenhum servo antes de uma calibração que insista até ter
  certeza da contração.
- Página simples, sem internet, com um "plotter" que mostre sem dúvida quando
  há contração.
- Os 8 servos controláveis um a um.
- Abrir, fechar e auto-home.

Detalhes e pinagem completa em [docs/08](docs/08-amazing-hand.md).

**Previsão:** o usuário que faz certo é aprovado em ~37 s. Quem não contrai é
recusado. Quem erra no começo é aprovado depois, porque a calibração insiste.
Disparo no relaxe da prova reprova e reaprende. Contração irregular é recusada.

**O que foi feito:**

- **Calibração adaptativa com prova** em `limbia_sinal`: 3 contrações boas
  seguidas e consistentes (até 10 tentativas), depois a prova com o detector
  da mão (3 de 3 reconhecidas, nenhum disparo no relaxe; até 3 provas).
- PCA9685 em "full off" a cada boot: servos sem pulso.
- **A trava** mora na tarefa do EMG. Recalibrar ou trocar a entrada trava de
  novo.
- **Auto-home = mão aberta** (servos no centro), sozinho ao liberar.
- Cada servo anda em "curso", 0 até o limite, sempre para o lado que fecha.
- A página foi refeita: faixa da trava, lâmpada de contração, gráfico com
  linha de disparo, calibração guiada, mão e 8 sliders.
- O limiar na flash e o limiar manual saíram: a calibração passa a valer a
  cada boot.

**Medido (C3, COM5):**

- Autoteste **132 de 132**. Calibração:

  | Cenário | Resultado | Tempo |
  | --- | --- | --- |
  | Certo | ok, 3 tentativas, prova 3/3 | 37 s |
  | Sem contração | recusada, 10 tentativas | 57 s |
  | 1ª fraca | ok, 4 tentativas | 42 s |
  | Disparo no relaxe | ok, na 2ª prova | 67 s |
  | Irregular | recusada | 57 s |

- Firmware gravado. No boot: "MÃO TRAVADA" e servos sem pulso. `abre`,
  `servo 0 30` e `home` foram **recusados**. Calibração sem ninguém contraindo:
  10 tentativas "fracas" e **recusa**; a mão continua travada.
- Página rodada no Edge headless contra um servidor que imita a placa, nos três
  estados (travada, calibrando, liberada), a 380 px de largura.
- O GPIO3 agora lê **~1,86 V estável**, e o firmware detecta **RAW**. Em
  12/11 era SIG (~70 mV). O envelope RAW em repouso fica entre 8 e 174 (média
  35), mais perto do RAW ligado de setembro (49) que do pino solto (envelope
  fixo ~130).

**Divergência:** nenhuma no autoteste. O contador de tentativas mostrava uma a
menos durante o relaxe, só na tela; corrigido. A troca SIG → RAW não foi
pedida no firmware: alguém mudou o fio ou o sensor, a confirmar na bancada.

**Decisão:** calibração obrigatória a cada boot. A rede continua aberta.
**Nada disso foi testado com músculo, com servo nem com aparelho conectado na
rede.** O PCA9685 continua sem responder.

**Evidência:** logs e screenshots no scratchpad da sessão; números acima.

---

### Próxima entrada esperada — Ensaio 1: um dedo, um motor

**Alvo previsto:** montar um dedo com um motor DC no L293D e um ACS712 em série,
ver o dedo se mover com cadência controlada e medir o tempo de curso dele.

**Previsão a registrar antes do ensaio** — a tabela completa está no
[roteiro](docs/07-roteiro-de-bancada.md#ensaio-1--um-dedo-um-motor-sem-sensor):
tempo de ponta a ponta entre 0,8 e 2,0 s em duty 200; motor girando livre entre
60 e 150 mA; travado contra objeto entre 400 e 900 mA; e o pico de partida — o
que o arranque cego esconde — de 2 a 4 vezes a corrente livre, durando de 50 a
200 ms.

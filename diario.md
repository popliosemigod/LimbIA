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

### Próxima entrada esperada — Primeira junta montada e calibrada

**Alvo previsto:** montar um dedo com um servo e um ACS712, calibrar os dois
pulsos pelo console e medir os três limiares de corrente.

**Previsão a registrar antes do ensaio:** um MG90S sem carga deve consumir algo
entre 80 e 150 mA em movimento, e entre 400 e 700 mA quando o dedo encontra um
obstáculo. Se a diferença medida entre "movendo livre" e "encostou" for menor que
uns 150 mA, o limiar de contato não vai separar os dois casos de forma confiável —
e a saída, nesse caso, é medir a **derivada** da corrente em vez do valor
absoluto.

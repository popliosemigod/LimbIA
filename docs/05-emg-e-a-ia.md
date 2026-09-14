# O EMG, a calibração por pessoa e a IA

O LimbIA passou a ter **duas placas**. Esta parte é a do antebraço: dois
eletrodos, a calibração que adapta a prótese a quem vai usá-la, e a decisão que
vira "abrir" ou "fechar".

> **Estado: compila, e a lógica foi medida nas duas placas. Nenhum eletrodo
> tocou pele ainda.** Os limiares de qualidade vieram de sinal sintético.

## A frase que explica esta parte

O antebraço de João não é o de Maria. **O que se calibra não é a prótese: é o
par pessoa-eletrodo.** Por isso a calibração é feita por quem vai usar, dura um
minuto, e o resultado fica gravado na placa — mas o próximo usuário passa pelo
mesmo minuto.

## O caminho do sinal, do músculo ao dedo

```
eletrodo ──> ADC 1 kHz ──> passa-altas 20 Hz ──> notch 60 Hz ──> |x| ──>
envelope 4 Hz ──> janela de 50 ms ──> ln(1+x) ──> LDA ──> decisor ──> mão
```

Cada etapa existe por um motivo:

| Etapa | Por que |
| --- | --- |
| **passa-altas 20 Hz** | abaixo disso é artefato de movimento do cabo e deriva de eletrodo, não músculo |
| **notch 60 Hz** | a rede elétrica brasileira é 60 Hz, e o corpo é uma antena |
| **retificação + envelope 4 Hz** | a intenção de abrir ou fechar não muda mais rápido que isso; o que muda mais rápido é ruído |
| **janela de 50 ms** | 20 decisões por segundo, cada uma com 50 amostras |
| **ln(1 + envelope)** | a amplitude do EMG é aproximadamente log-normal; em escala linear o repouso vira um ponto colado no zero e nenhuma fronteira reta separa bem |

Medido: passa-altas **−24,1 dB em 5 Hz** e plano em 100 Hz; notch **−93 dB em
60 Hz** e intocado em 100 Hz; envelope **−47 dB em 60 Hz**.

## Os dois eletrodos

Esquema clássico de controle mioelétrico de dois sítios:

| Eletrodo | Onde | Gesto |
| --- | --- | --- |
| 1 | face de dentro do antebraço (lado da palma), a um terço do cotovelo ao punho | **fecha** a mão (flexores) |
| 2 | face de fora (lado das costas da mão), na mesma altura | **abre** a mão (extensores) |
| referência | sobre osso, longe dos dois — o cotovelo | — |

## A barra que vai de vermelho a verde

A tela de ajuste conduz: **"FECHE A MÃO"**, "relaxe", **"ABRA A MÃO"**, "relaxe".
Enquanto a pessoa segue o ritmo, cada eletrodo ganha uma nota. **Barra cheia =
verde = posicionado corretamente**, e o limiar é exatamente onde a barra enche.

Três grandezas decidem, e cada uma pega um erro diferente:

| Grandeza | O que mede | Limiar |
| --- | --- | --- |
| **d'** | separação entre gesto e repouso, em desvios | ≥ 2,5 |
| **razão** | quantas vezes o envelope da contração é maior que o do repouso | ≥ 3× |
| **seletividade** | responde mais ao gesto dele do que ao do outro eletrodo | > 1× |

**A seletividade não é redundante, e isso foi medido.** Na primeira versão do
autoteste, dois eletrodos **trocados de lugar** passaram como "posicionados
corretamente": a co-contração sozinha põe o músculo do outro gesto várias vezes
acima de um repouso quieto. Só a comparação entre os dois gestos pega o caso.

A estatística tem **esquecimento** de cerca de três ciclos. É isso que faz a
barra reagir quando a pessoa **move** o eletrodo, em vez de arrastar para sempre
a medida do lugar antigo. Medido: um eletrodo mal posto sai de 63% e chega a
verde **dois ciclos depois** de ser movido, sem recomeçar nada.

### Os diagnósticos, quando a barra não enche

| Diagnóstico | O que dizer a quem está vestindo |
| --- | --- |
| sinal fraco | mova o eletrodo para o centro do músculo |
| saturando | o sinal passou do limite do ADC: confira o ganho e a alimentação em 3,3 V |
| repouso ruidoso | contato ruim, ou eletrodo de referência solto |
| músculo trocado | responde mais ao gesto do outro: troque os eletrodos 1 e 2 |

## A "IA": um LDA de três classes, e por que não uma rede neural

O classificador é uma **análise discriminante linear** com três classes —
**repouso**, **fechar**, **abrir** — treinada com o antebraço de quem vai usar.

Dois canais, três classes e um minuto de calibração sustentam seis médias e uma
covariância 2×2. Uma rede neural, com esse tanto de dado, decora o minuto de
calibração e erra no minuto seguinte. E o LDA devolve **probabilidade** — que é
exatamente o que o decisor usa para saber quando **não** agir.

### O número que diz se está pronto é medido fora do treino

Cada janela rotulada é **classificada antes de entrar no treino**. O acerto
dessas previsões é medido em dado que o modelo ainda não viu — treino e teste
separados, janela a janela. A calibração só libera o modo padrão quando o **pior
acerto entre as três classes** passa de 90%.

Entender "fechar" muito bem e nunca entender "abrir" não serve, e por isso o
critério é o pior dos três, não a média.

### Candidato e modelo

Recalibrar não pode estragar o que já funcionava. A calibração treina um
**candidato**; o modelo em uso só é trocado quando o candidato fica verde e a
pessoa aperta o botão. Calibração abandonada no meio deixa o modelo antigo
intacto.

## O decisor: três travas contra mão errática

O pedido do projeto foi explícito — nada errático, nada rápido demais. Três
travas, em série:

1. **Probabilidade mínima (0,80).** Voto abaixo disso vira "incerto" e não conta.
2. **Maioria numa janela (5 de 6).** A intenção precisa durar ~250 ms.
3. **Período refratário (800 ms).** A mão termina o que começou.

E **repouso nunca muda nada**: relaxar o braço mantém a mão como está. É isso que
deixa segurar um copo sem manter o músculo contraído o tempo todo.

Medido, contra um usuário sintético: **60 de 60 intenções atendidas, nenhuma
troca errada, nenhuma troca espúria**, latência média de 314 ms; e **60 s de
braço relaxado sem uma única troca**.

### Quando o sinal não dá, a mão não age

Dois casos medidos:

- **Pessoa cansada** (35% da força da calibração): 40 de 40 atendidas, zero
  erradas. A degradação não inventa movimento.
- **O modelo que a tela recusou, usado à força**: **0 de 30** intenções
  atendidas, **nenhuma** ação errada. Quando o sinal não sustenta, a falha é
  "não age" — nunca "age errado".

- **Eletrodo solto em uso**: sinal batendo no trilho do ADC não decide nada, e os
  votos de antes da falha são esquecidos.

## O que isto tem do LimbIA, e não de um controle mioelétrico qualquer

O comando "fechar" **não vai à pose gravada e para**. Ele fecha **parando quando
encosta**: a pessoa contrai o flexor, a mão fecha em volta do copo e para no
contato, em vez de esmagar o copo até a pose. Depois, a mão relata **a forma do
que pegou** — é a propriocepção de [04-propriocepcao.md](04-propriocepcao.md)
encontrando o EMG.

## Segurança elétrica — ler antes do primeiro eletrodo

**Com os eletrodos na pele, a placa do EMG fica na bateria.** Nunca ligada ao PC
por cabo USB enquanto alguém está com os eletrodos: um notebook na tomada põe o
terra da rede elétrica a um passo do peito de quem está sendo medido, e é por
isso que equipamento de eletromiografia comercial é isolado.

A arquitetura já ajuda: **o ajuste é feito pela rede Wi-Fi da própria prótese**,
não por cabo. Não existe caminho elétrico entre o PC e a pessoa.

## O sensor

Não há sensor de EMG no estoque. O firmware aceita os dois tipos de módulo:

| `EMG_ENTRADA_BRUTA` | Módulo | O que o firmware faz |
| --- | --- | --- |
| **1** (escolhido) | MyoWare 2.0 na saída RAW, BioAmp EXG Pill, placas com AD8232 | filtra tudo: passa-altas, notch, retificação e envelope |
| 0 | MyoWare na saída ENV, "Muscle Sensor V3" | o módulo já retificou; aqui só se suaviza |

**Alimentar o módulo em 3,3 V.** Em 5 V a saída passa do teto do ADC e o pino
morre calado, lendo valor fixo — a mesma lição do ACS712.

## O que foi medido, e onde

Autoteste, seções [6] a [11], rodando **na placa**:

| Medida | DevKit V1 | ESP32-C3 |
| --- | --- | --- |
| Verificações | 89/89 (44,1 s) | 89/89 (209,5 s, banco reduzido) |
| Cadeia de filtros | **0,90 µs** por amostra | **22,65 µs** por amostra |
| Dois canais a 1 kHz | 0,2% de um núcleo | **4,5% de um núcleo** |
| Classificação LDA | 2,99 µs | 30,33 µs |

E a medida que vale mais que as três: com o ponto de acesso, a tela e o enlace
rodando na mesma placa, o C3 processou **1045 janelas sem perder nenhuma**.

O C3 não tem unidade de ponto flutuante — daí as 25 vezes de diferença. O banco
de casos do autoteste encolhe quando compilado para ele; as verificações são as
mesmas, o que muda é quantos casos cada uma vê.

## O que isto ainda não é

- **Nenhum eletrodo tocou pele.** Todos os números vieram de um usuário
  sintético: ruído gaussiano modulado por ativação, com co-contração, força
  variando a cada repetição, tônus de repouso, rede de 60 Hz e quantização de
  12 bits. Se esse gerador se parece com um antebraço de verdade é pergunta para
  o primeiro ensaio.
- **Os limiares de qualidade são chute educado** — d' ≥ 2,5 e razão ≥ 3×.
- **Dois gestos**, não cinco. Mão aberta e mão fechada, como foi pedido.

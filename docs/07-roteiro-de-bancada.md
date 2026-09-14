# Roteiro de bancada — a fase de ensaios

Tudo neste repositório foi medido em lógica: casos sintéticos, duas placas
conversando, estado trafegando. **Nenhum motor foi ligado e nenhum eletrodo
tocou pele.** Este documento abre a fase que muda isso.

A regra do laboratório vale aqui inteira: **a previsão é escrita antes do
ensaio**. Um número medido sem previsão ao lado não corrige modelo nenhum — só
vira anotação.

## Os números que esta fase precisa transformar de chute em medida

| Parâmetro | Onde | Hoje | Vira medida no |
| --- | --- | --- | --- |
| `VELOCIDADE_DEDO` | `config.h` | 200 de 255 | ensaio 1 |
| `TEMPO_CURSO_*` | flash, por dedo | 1500 ms (fábrica) | ensaio 1 |
| `ARRANQUE_CEGO_MS` | `config.h` | 250 ms | ensaio 1 |
| `CORRENTE_CONTATO_MA` | `config.h` | 400 mA | ensaio 2 |
| `CORRENTE_LIMITE_MA` | `config.h` | 900 mA | ensaio 2 |
| `CORRENTE_FOLGA_MA` | `config.h` | 150 mA | ensaio 2 |
| `QUAL_DPRIME_OK`, `QUAL_RAZAO_OK` | `config_emg.h` | 2,5 e 3× | ensaio 4 |
| `QUAL_REPOUSO_MAX` | `config_emg.h` | 4,0 (ln) | ensaio 4 |

Todo número medido entra com commit de tipo **`calib`**, com o valor no corpo.

---

## Ensaio 1 — um dedo, um motor, sem sensor

**Objetivo:** ver um dedo se mover com cadência controlada, e medir o tempo de
curso dele.

**Montagem**

- um motor DC no primeiro L293D (entradas nos GPIO 18 e 19 = JULGADOR);
- **pull-down de 10 kΩ em cada entrada do L293D** — enquanto o firmware não sobe,
  os pinos ficam em alta impedância e a entrada flutua;
- alimentação do motor pela fonte externa (6 V), **nunca pelo 3V3 da placa**;
- **terra comum** entre fonte, ESP32 e driver.

**Procedimento**

```
pio run -e esp32dev -t upload --upload-port COM8
pio device monitor -b 115200

i        <- ignora a corrente (ainda não há ACS712)
3        <- fechar com o polegar fora; o dedo deve andar
2        <- abrir
m 3      <- mede o curso do JULGADOR: abre até travar, fecha cronometrando
m        <- lista os tempos
```

**Previsão**

| | Previsto |
| --- | --- |
| Tempo de ponta a ponta, duty 200 | 0,8 a 2,0 s |
| O dedo anda em duty 200? | sim; abaixo de ~150 provavelmente não sai do lugar |
| Fim de curso detectado sem sensor | **não** — sem corrente, `m` vai falhar |

**Atenção:** `m` depende da corrente para saber que travou. Com `i` ligado
(corrente ignorada) ele vai bater na guarda de tempo e recusar a medida. Ou seja:
**o ensaio 1 mostra movimento; o tempo de curso só sai de verdade no ensaio 2.**
Se quiser o número antes do sensor, cronometre no relógio e grave à mão editando
`TEMPO_CURSO_PADRAO_MS`.

**Critério:** o dedo abre e fecha, e para quando mandado. Se fechar em menos de
0,6 s, **baixe `VELOCIDADE_DEDO`** — rápido demais não deixa a corrente ser lida
no meio do caminho, e é a leitura no meio do caminho que faz o dedo parar no
contato.

---

## Ensaio 2 — o mesmo dedo, agora com ACS712

**Objetivo:** os três limiares de corrente, medidos.

**Montagem:** o ACS712 **em série com o motor** (nota 2 do manual do LAD), saída
pelo divisor 10k/20k até o GPIO 35 (JULGADOR).

**Procedimento**

```
i        <- volta a usar a corrente (o console avisa)
s        <- status: a corrente em repouso deve ler perto de zero
3        <- fecha; acompanhe a corrente
m 3      <- agora o curso sai medido
```

Repita fechando **contra um objeto** (um lápis atravessado no caminho do dedo) e
anote a corrente no instante em que ele encosta.

**Previsão**

| | Previsto |
| --- | --- |
| Repouso (motor parado) | 0 a 30 mA |
| Motor girando livre | 60 a 150 mA |
| Pico de partida (o que `ARRANQUE_CEGO_MS` esconde) | 2 a 4× a corrente livre |
| Dedo travado contra objeto | 400 a 900 mA |
| Duração do pico de partida | 50 a 200 ms |

**Critério e o que fazer com ele**

- `CORRENTE_CONTATO_MA` = meio do caminho entre "girando livre" e "travado".
- `CORRENTE_LIMITE_MA` = travado + 30%, sem passar do que o L293D aguenta.
- `CORRENTE_FOLGA_MA` = metade de "girando livre".
- `ARRANQUE_CEGO_MS` = duração medida do pico + 50 ms.

**Se a diferença entre "girando livre" e "travado" for menor que ~150 mA**, o
limiar absoluto não separa os dois casos de forma confiável — nesse caso a saída
é medir a **derivada** da corrente, e isso é mudança de firmware, não de número.

---

## Ensaio 3 — a mão inteira

Repete o ensaio 2 para os quatro dedos e os dois servos do polegar, e então:

```
p        <- preensão: fecha até encostar e classifica
```

com a mão vazia, com um copo, com uma caneta e com um cartão.

**Previsão:** o classificador acerta a forma em **3 de 4** objetos. O caso mais
provável de erro é o cartão (plano) virar cilíndrico, porque depende do
espalhamento entre dedos e a posição aqui é **estimada por tempo**, não medida.

**Este é o primeiro número de acerto de campo do projeto.** Todos os 100% do
autoteste vieram de casos que eu mesmo gerei.

---

## Ensaio 4 — o primeiro eletrodo na pele

> **Segurança elétrica, antes de tudo:** com eletrodo na pele, a placa do EMG
> fica **na bateria**. Nunca ligada ao PC por USB. O ajuste é pela rede Wi-Fi da
> própria prótese, e é por isso que ele foi feito assim.

**Montagem:** eletrodo 1 na face de dentro do antebraço (flexores), eletrodo 2 na
face de fora (extensores), referência sobre o cotovelo. Módulo alimentado em
**3,3 V** — em 5 V a saída passa do teto do ADC e o pino lê valor fixo.

**Procedimento**

```
v        <- envelope no Serial Plotter (pela bateria isso não dá; use a tela)
```

Pela tela: entrar na rede da prótese, **Começar**, e seguir o ritmo.

**Previsão**

| | Previsto |
| --- | --- |
| Envelope em repouso | 5 a 40 contagens |
| Envelope em contração forte | 150 a 800 contagens |
| Razão contração/repouso, eletrodo bem posto | 5× a 30× |
| d' | acima de 3 |
| Ciclos até a barra ficar verde | 3 a 6 (~40 a 70 s) |

**O que o ensaio precisa responder, e é a pergunta que vale:** os limiares
(d' ≥ 2,5 e razão ≥ 3×) separam **bem posicionado** de **mal posicionado** num
antebraço de verdade? Teste de propósito o eletrodo no lugar errado — sobre o
osso, sobre o tendão do punho — e veja se a tela recusa.

---

## Ensaio 5 — a prótese em uso

Calibração pronta, botão único, modo padrão, e a mão obedecendo ao músculo.

**Previsão**

| | Previsto |
| --- | --- |
| Intenções atendidas | acima de 90% |
| Trocas espúrias em 1 min de braço relaxado | 0 a 2 |
| Latência da intenção ao movimento | 300 a 600 ms |

**Critério que importa mais que o acerto:** quando o sinal não sustentar, a mão
tem de **ficar parada** — nunca fazer o gesto errado. Isso foi medido em sinal
sintético (0 de 30 intenções atendidas, nenhuma ação errada); aqui se confirma
com músculo.

---

## Ensaio 6 — gravar pela rede

```
pio run -e mao_ota -t upload     # PC na rede da prótese, prótese em modo ajuste
pio run -e emg_ota -t upload
```

**Previsão:** grava em 20 a 60 s cada. Em modo padrão, **falha por tempo** — e
falhar ali é o comportamento certo.

---

## O que anotar, em qualquer ensaio

No `diario.md`, no formato de sempre: **alvo, previsão, o que foi feito, medido,
divergência, decisão, evidência**. A divergência é o campo que mais importa —
entrada sem ela é entrada pela metade.

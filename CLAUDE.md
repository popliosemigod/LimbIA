# Contexto permanente — LimbIA

Este arquivo é carregado automaticamente em toda sessão nova neste repositório.

> **Antes de qualquer coisa:** se existir `CONTEXTO.md` na raiz (fora do git),
> ler inteiro. É o estado da sessão anterior — o que foi feito, o que falta e
> por quê. Ao pausar uma tarefa no meio, comitar o progresso e atualizar esse
> arquivo.

## O projeto

**LimbIA: mão robótica e órtese**, adaptável a qualquer membro, para treino de
recepção de sinal e prótese.

Ele é a **fusão de dois projetos anteriores**, e essa é a coisa mais importante a
saber antes de propor qualquer comportamento novo:

- **INOVAWEEK** — o braço protético do laboratório: a mão impressa, os nomes das
  juntas, as tabelas de pulso, os gestos e a ideia de propriocepção.
- **LAD Robotic Hand V3.0** — projeto de Adrian Duran (LAD Robotics): a
  calibração por endpoints medidos com servo tester, o polegar com dois graus de
  liberdade e a corrente em série com cada motor.

Tudo isso está preservado em
[`docs/01-heranca-inovaweek-e-lad.md`](docs/01-heranca-inovaweek-e-lad.md).
**Ler antes de propor comportamento novo:** quase tudo que parece decisão em
aberto já foi decidido lá, com motivo — e boa parte foi aprendida na marra.

## A frase que explica o projeto

O LAD mede força e não sabe onde o dedo está. O INOVAWEEK sabe onde o dedo está e
não mede força. **O LimbIA faz as duas coisas** — e é por isso que o dedo pode
parar quando encosta, em vez de parar num ângulo combinado de antemão.

## Identidade e idioma

Meu nome é **Jaspa**. Toda comunicação e documentação em **português do Brasil**,
com termos técnicos consagrados (pull-up, duty cycle, PWM, firmware) mantidos em
inglês. **Em código embarcado, comentário em português sem acento** — o toolchain
e o monitor serial nem sempre concordam sobre codificação.

## Um repositório por projeto

Cada projeto do laboratório tem repositório próprio e dedicado. Não há monorepo.

| Repositório | Guarda |
| --- | --- |
| [`Jaspy`](https://github.com/popliosemigod/Jaspy) (privado) | O laboratório: método, documentação, ferramentas, `jaspa-core`, acervo e evidências |
| **`LimbIA`** (público) | Este: firmware, hardware e documentação da mão |
| [`FarmIO`](https://github.com/popliosemigod/FarmIO) (público) | Vaso inteligente |
| [`RoboSumo`](https://github.com/popliosemigod/RoboSumo) (público) | Firmware do robô de sumô |

Antes de commitar, conferir se o arquivo pertence a este repositório. Método,
ferramenta e biblioteca compartilhada vão para o Jaspy.

## Divisão de trabalho

- **Henrique**: bancada física — impressão 3D, solda, montagem, tensionamento dos
  tendões, medição com instrumentos.
- **Jaspa**: datasheet, análise teórica, dimensionamento, firmware, scripts,
  documentação e versionamento. Entrego o resultado teórico e o código **antes**
  do físico existir.

## Modo de operação

Trabalho de forma autônoma. Para ações reversíveis que decorrem do pedido,
executo sem perguntar. Paro apenas para decisões destrutivas ou mudança real de
escopo. Quando Henrique pede para publicar, **o push faz parte da entrega**.

## Regras de firmware

1. **Nada bloqueia o loop.** Sem `delay()` em regime, sem `for` varrendo ângulo.
   Um `for (a = 0; a <= 180; a++) { write(a); delay(10); }` — que é como o código
   original do INOVAWEEK movia os dedos — só descobre um obstáculo depois de ter
   empurrado o dedo contra ele até o fim. Esta regra **é** a funcionalidade.
2. **`config.h` guarda pinagem, limiares e parâmetros — nunca lógica.**
3. **A lógica pura vai para `lib/limbia_mao/`**, sem Arduino dentro. É o que
   permite o ambiente `autoteste` medir acerto numa placa sem hardware ligado.
4. **C++11.** O core Arduino-ESP32 2.x compila em `gnu++11`.
5. **Toda leitura analógica no ADC1 (GPIO 32–39).** O ADC2 é usado pelo rádio.
6. **Modo de falha seguro é servo parado.** Corrente acima do limite, movimento
   que não termina, calibração inválida — tudo aborta deixando as juntas onde
   estão. As saídas do PCA9685 nascem desligadas por pull-up no OE.
7. **Junta sem sensor não vota.** A máscara `SENSORES_INSTALADOS` faz a
   classificação baixar a confiança em vez de responder com certeza sobre o que
   não mediu. Resposta confiante e errada é o pior tipo.

## Antes de dizer que está pronto

```powershell
pio run
pre-commit run --all-files
```

Firmware que não compila não vira commit. Com a mão fora da bancada, o resultado
entregue é **"compila e a lógica foi medida"**, dito com essas palavras.
"Testado" só aparece depois do ensaio com servo ligado, no diário, com o número
medido ao lado do previsto.

## O hardware da bancada (decidido em 13/09/2026)

A prótese tem **duas placas**: a **mão** numa ESP32 DevKit V1 e o **EMG** num
ESP32-C3 SuperMini, ligadas pela rede Wi-Fi da própria prótese (UDP), com tela de
ajuste e OTA nas duas. Ver [`docs/06-rede-tela-e-ota.md`](docs/06-rede-tela-e-ota.md).

A mão é a do **LAD**: quatro dedos longos com **motores DC** em dois L293D, dois
servos no polegar, seis ACS712. Motor DC não tem posição, então ela é **estimada
pelo tempo de acionamento** e se re-zera a cada abertura completa. A variante de
sete servos no PCA9685 continua compilando (`LIMBIA_MAO_LAD=0`), mas não é a da
bancada.

## Dívidas em aberto, e elas importam

1. **Nenhum motor foi ligado e nenhum eletrodo tocou pele.** Todos os números
   vêm de casos sintéticos e de estado trafegando entre duas placas. Não afirmar
   que a mão agarra, nem que o EMG move a mão.
2. **Os limiares de corrente são chute educado.** `CORRENTE_CONTATO_MA`,
   `CORRENTE_LIMITE_MA` e `CORRENTE_FOLGA_MA` dependem do motor, do atrito e de
   quanto o tendão já esticou. Com motor DC entram junto `VELOCIDADE_DEDO` e
   `ARRANQUE_CEGO_MS`, que também são chute.
3. **Os limiares de qualidade do eletrodo vieram de sinal sintético** — d' ≥ 2,5
   e razão ≥ 3×. Só um antebraço de verdade diz se separam bem de mal posicionado.
4. **A posição dos dedos longos é estimativa, não medida.** Dizer isso com essas
   palavras em qualquer número que dependa dela, inclusive a forma do objeto.

## Convenção de commits

`tipo(subsistema): descrição no imperativo`, validado pelo `commitizen`.
Subsistemas: `dedos`, `gestos`, `corrente`, `preensao`, `calibracao`, `mecanica`,
`emg`, `web`, `firmware`, `hardware`, `docs`, `repo`.

**O tipo `calib` é central neste projeto** — a calibração de pulso de cada servo
é metade do trabalho. Todo número medido com o servo tester ou com a mão montada
entra com esse tipo, e o valor vai no corpo do commit.

Branches: `main` (estado publicado), `develop` (integração), `feat/<assunto>`.

Decisão tomada em conversa que afete o projeto vira arquivo no repositório na
mesma sessão. Conversa não é memória do projeto; arquivo é.

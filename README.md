# LimbIA

**Mão robótica e órtese.** Sete juntas, sensores de corrente nos dedos, e uma
mão que sabe a forma do que está segurando.

> **Estado: compila, e a lógica foi medida numa placa. Nenhum servo foi ligado
> ainda.** Os limiares de corrente e os pulsos de cada junta são ponto de
> partida, não medida — ver
> [calibração](docs/03-anatomia-gestos-e-calibracao.md#o-console-de-calibração).

Este é um projeto do laboratório [**Jaspy**](https://github.com/popliosemigod/Jaspy).
Cada projeto tem repositório próprio; este é o do LimbIA.

## A ideia em uma frase

O **LAD Robotic Hand V3.0** mede força e não sabe onde o dedo está. O
**INOVAWEEK** sabe onde o dedo está e não mede força. O LimbIA funde os dois — e
com os dois sentidos juntos o dedo pode parar **quando encosta**, em vez de parar
num ângulo combinado de antemão.

Isso responde à dor número um registrada no caderno do INOVAWEEK: *"é muito
difícil deixar os servos ajustados para mover as cordas de cada dedo — isso gera
um retrabalho desgramado"*. Um dedo que para por contato depende muito menos do
ajuste fino do tendão. E um tendão que esticou passa a ter assinatura detectável.

A herança completa dos dois projetos está em
[`docs/01-heranca-inovaweek-e-lad.md`](docs/01-heranca-inovaweek-e-lad.md).

## O que ele faz

- move **sete juntas** — cinco dedos, o punho e a abdução do polegar — por um
  PCA9685, com movimento gradual e sem `delay()`;
- **para o dedo no contato**, lendo a corrente entre os passos do movimento;
- classifica a **forma do objeto** pela posição em que cada dedo parou: fino,
  cilíndrico, plano, grande, ou nada na mão;
- **detecta tendão frouxo** — servo que percorre o curso inteiro sem a corrente
  subir está enrolando folga, não puxando dedo;
- guarda a calibração na flash, para o ajuste não se perder entre sessões;
- console serial compatível com os comandos do manual do LAD (1 a 7).

## Hardware

| Item | Componente |
| --- | --- |
| Controle | ESP32 DevKit V1 |
| Driver de servo | PCA9685, 16 canais, I²C |
| Juntas | 7 servos (MG90S hoje; 13 kgf para a versão de força) |
| Força | 4 × ACS712 20 A, um por dedo longo |
| Estrutura | PLA impresso + silicone, tendões em linha de pesca, polias |
| EMG | previsto, canal reservado — sem sensor ainda |

Pinagem, alimentação e a contagem de canais de ADC que definiu a placa estão em
[`docs/02-hardware-e-pinagem.md`](docs/02-hardware-e-pinagem.md).

## Compilar e gravar

```powershell
pio run                                  # todos os ambientes
pio run -e esp32dev -t upload            # grava a mão
pio device monitor -b 115200             # console

pio run -e bancada -t upload             # log detalhado, para calibrar
pio run -e autoteste_c3 -t upload        # exercita a lógica sem hardware
```

| Ambiente | Placa | Para que serve |
| --- | --- | --- |
| `esp32dev` | DevKit V1 | a mão |
| `bancada` | DevKit V1 | a mesma, com log detalhado |
| `autoteste` | DevKit V1 | exercita `limbia_mao` sem nada ligado |
| `autoteste_c3` | ESP32-C3 | o mesmo teste na placa que está na bancada hoje |

## Consumo de recursos

Compilação de 09/09/2026:

| Ambiente | RAM | Flash |
| --- | --- | --- |
| `esp32dev` | 6,8% (22,4 kB) | 26,0% (341 kB de 1,31 MB) |
| `bancada` | 6,8% (22,4 kB) | 26,4% (346 kB) |
| `autoteste_c3` | 4,2% (13,9 kB) | 19,1% (251 kB) |

Sobra folga larga — não há Wi-Fi nem servidor web nesta versão.

## O que já foi medido

O autoteste roda na placa e imprime números. Resultado de 09/09/2026, num
ESP32-C3:

| Medida | Valor |
| --- | --- |
| Verificações | **35/35 passaram**, em 8 ms |
| Classificador, casos bem separados | 240/240 = 100%, confiança 94% |
| Classificador, **casos de fronteira** | 34/40 = 85%, confiança **86%** |
| Erro de ida e volta posição↔pulso | 1 por mil |
| Custo de uma classificação | 1,7 µs |
| Confiança com 4 dedos / com 2 | 100% / 45% |

Os 100% não são o resultado — um banco que eu mesmo gerei mede o gerador tanto
quanto o classificador. **O resultado é a linha de fronteira**: acerto e confiança
caem juntos e quase na mesma medida, o que significa que o número de confiança
está calibrado e pode ser usado para decidir. Leitura completa em
[`docs/04-propriocepcao.md`](docs/04-propriocepcao.md).

## Estrutura

```
LimbIA/
├── platformio.ini      quatro ambientes, bibliotecas fixadas
├── include/
│   ├── config.h        pinagem, limiares, estado  ← só isso muda ao trocar de placa
│   ├── dedos.h         PCA9685, movimento gradual, parada por contato
│   ├── corrente.h      ACS712, zero medido no boot
│   ├── preensao.h      fecha até encostar e lê a forma
│   ├── memoria.h       calibração persistida na NVS
│   └── secrets.example.h
├── lib/limbia_mao/     lógica sem Arduino: pulso, gestos, classificação
├── src/
│   ├── main.cpp        console serial e loop
│   └── main_autoteste.cpp
├── docs/
└── diario.md           previsto × medido, a cada iteração
```

## Documentação

- [Herança do INOVAWEEK e do LAD](docs/01-heranca-inovaweek-e-lad.md) — o que
  cada projeto contribuiu, o que a fusão descartou e por quê
- [Hardware e pinagem](docs/02-hardware-e-pinagem.md) — ligação, alimentação, a
  contagem de ADC que escolheu a placa, o pino OE
- [Anatomia, gestos e calibração](docs/03-anatomia-gestos-e-calibracao.md) — as
  sete juntas, as tabelas de pulso, o console de ajuste
- [Propriocepção](docs/04-propriocepcao.md) — como a mão descobre a forma do
  objeto, e como ler os números do autoteste
- [Diário](diario.md) — cada iteração com previsto ao lado de medido

## Próximos passos

1. **Montar uma junta e calibrá-la.** Um dedo, um servo, um ACS712. É o ensaio
   que transforma todos os limiares de chute educado em número medido, e destrava
   tudo o mais.
2. **Comprar dois ACS712** para o polegar e a abdução. Sem sensor no polegar a
   mão não distingue "vazia" de "segurando algo fino contra a palma" — e o
   autoteste mostra a confiança caindo de 100% para 45% com metade dos dedos
   cegos.
3. **Trocar os MG90S pelos servos de 13 kgf.** Os MG90S servem para validar a
   lógica; não vão fechar a mão contra um objeto.
4. **Primeiro ensaio de preensão real** com um copo, uma caneta e um cartão —
   medir o acerto de campo, que hoje não existe.
5. **Reconhecimento de objeto por imagem**, o objetivo original do INOVAWEEK.
6. **A órtese**, que dá metade do nome ao projeto e não tem concepção escrita.

## Convenções

Commits seguem `tipo(subsistema): descrição`, validados pelo `commitizen`
([`cz.toml`](cz.toml)). Parâmetro medido na bancada usa o tipo `calib` e cita o
número no corpo. Branches: `main` guarda o estado coerente e publicado, `develop`
integra, `feat/<assunto>` para tarefa curta.

**Tag de versão só nasce de coisa medida.** A v0.1 está na `main` porque compila
e teve a lógica verificada numa placa, mas não recebeu tag — tag em firmware que
nunca moveu um servo transforma a linha do tempo em ficção.

```powershell
python -m pip install --user pre-commit commitizen
pre-commit install --install-hooks
pre-commit install --hook-type commit-msg
```

## Crédito

O **LAD Robotic Hand V3.0 — ESP32 Control v1.1** é projeto de **Adrian Duran**
(LAD Robotics). O método de calibração por endpoints medidos, o polegar com dois
graus de liberdade e a corrente em série com cada motor vieram do manual dele.

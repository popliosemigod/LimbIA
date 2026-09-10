# De onde o LimbIA veio: INOVAWEEK e LAD Robotic Hand V3.0

O LimbIA não começa do zero. Ele é a fusão de dois projetos que já existiam,
resolvendo problemas complementares da mesma mão.

- **INOVAWEEK** — o braço robótico protético do laboratório, com a mão impressa,
  os servos, os nomes das juntas, as tabelas de pulso e uma lista de dores
  aprendidas na marra.
- **LAD Robotic Hand V3.0 — ESP32 Control v1.1** — projeto de Adrian Duran
  (LAD Robotics), com um método de calibração maduro, o polegar resolvido de
  verdade e corrente medida em cada motor.

Este documento preserva os dois e registra o que cada um contribuiu.

> Originais no acervo do repositório [Jaspy](https://github.com/popliosemigod/Jaspy):
> `INOVAWEEK.pdf` e o manual do LAD, ambos catalogados em `acervo/`.

---

## A frase que resume a fusão

**O LAD mede força e não sabe onde o dedo está. O INOVAWEEK sabe onde o dedo
está e não mede força. O LimbIA faz as duas coisas.**

O LAD move os dedos F2–F5 com **motores DC** através de dois L293D, e põe um
sensor de corrente em série com cada motor. Motor DC não tem realimentação de
posição: o firmware sabe o esforço, não o ângulo.

O INOVAWEEK move tudo com **servos**. Servo tem posição por construção — você
manda 1,5 ms e ele vai para o meio do curso — mas não conta nada sobre o esforço.

O LimbIA mantém os servos do INOVAWEEK (posição de graça) e acrescenta os
sensores de corrente do LAD (força). Com os dois sentidos juntos, o dedo pode
parar **quando encosta**, em vez de parar num ângulo combinado de antemão.

Isso não é refinamento. É a resposta direta à dor número um registrada no
caderno do INOVAWEEK:

> É MUITO DIFÍCIL DEIXAR OS SERVOS AJUSTADOS PARA MOVER AS CORDAS DE CADA DEDO.
> Isso gera um retrabalho desgramado para fazer os ajustes.

E à dor número três:

> O braço com certeza vai ser falho, pois ele precisa de manutenções severas a
> todo tempo por ser feito de tendões que são cordas que se deformam a toda hora,
> ou seja, não tem como confiar 100% num sistema que precisa ser vigiado
> constantemente.

Um dedo que para por contato depende muito menos do ajuste fino do tendão. E um
tendão que esticou passa a ter assinatura detectável — o servo percorre o curso
inteiro sem a corrente subir, porque está enrolando folga em vez de puxar dedo.
O firmware avisa. Ninguém precisa vigiar.

---

## O que veio do INOVAWEEK

### As juntas, com os nomes da bancada

O caderno de pesquisa nomeia cada servo, e os nomes ficaram — é assim que a
bancada chama cada dedo, e trocar por `servo_2` só criaria a necessidade de
traduzir de cabeça toda vez.

| # | Nome | Anatomia |
| --- | --- | --- |
| 1 | MINDY | mindinho |
| 2 | DONCARE | anelar |
| 3 | FEIO | médio |
| 4 | JULGADOR | indicador |
| 5 | DEDÃO | polegar |
| 6 | PULSO | punho |

### A tabela de pulso

| Ângulo | Pulso |
| --- | --- |
| 0° | 1,000 ms |
| 30° | 1,167 ms |
| 60° | 1,333 ms |
| 90° | 1,500 ms |
| 120° | 1,667 ms |
| 150° | 1,833 ms |
| 180° | 2,000 ms |

Com a definição de estado: **relaxado** = dedo em 0° (estendido), **tensionado**
= dedo em 180° (fechado), punho sempre em 90° como neutro.

### Os gestos

O sinal da paz, como está no caderno — e como está implementado, conferido pelo
autoteste:

| Servo | Posição | Pulso | Estado |
| --- | --- | --- | --- |
| MINDY | 180° | 2 ms | tensionado 100% |
| DONCARE | 180° | 2 ms | tensionado 100% |
| FEIO | 0° | 1 ms | relaxado 100% |
| JULGADOR | 0° | 1 ms | relaxado 100% |
| DEDÃO | 180° | 2 ms | tensionado 100% |
| PULSO | 90° | 1,5 ms | 50% |

### A ideia de propriocepção

> **RECONHECIMENTO DE OBJETOS EM EVIDÊNCIA** — é um dos desafios principais da
> mão biônica e consiste basicamente em reconhecer o tipo de objeto que a mão
> está portando lendo apenas as informações de pulso e posição.
>
> Exemplo: se temos todos os servos meio tensionados, é porque é muito provável
> que estejamos segurando um objeto reto de estrutura circular cilíndrica.

Essa ideia é o núcleo do que diferencia o LimbIA de uma mão que só executa
gestos. Ela está implementada, com uma correção — ver
[04-propriocepcao.md](04-propriocepcao.md).

### A estrutura física

- Estrutura em PLA impresso, com silicone;
- **linha de pesca** no lugar de nylon elástico (melhor para tensão);
- **sistema de polias** para flexão e tensão;
- servos de **13 kgf** de alto torque;
- referência de modelagem: [Thingiverse thing:17773](https://www.thingiverse.com/thing:17773).

Configuração de impressão que já foi calibrada na Bambu Lab A1 mini, bico 0,4 mm,
e que existe para reduzir *strings* — os fios finos de filamento que atrapalham
o encaixe das peças móveis:

| Parâmetro | Valor |
| --- | --- |
| Comprimento de retração | 1 mm |
| Z hop ao retrair | 0,6 mm (auto) |
| Velocidade de retração / desretração | 45 mm/s |
| Limiar de distância de deslocamento | 1 mm |

Lição registrada junto: imprimir as peças **na horizontal com suporte** foi erro
— o suporte fica preso dentro dos encaixes e não sai. As peças dos dedos vão
todas numa leva, posicionadas para não deformar os eixos de encaixe.

---

## O que veio do LAD Robotic Hand V3.0

### O polegar tem dois graus de liberdade

Esta é a contribuição mais importante do LAD, e a que o INOVAWEEK não tinha.

O manual descreve **dois servos no polegar**: `F1_servo_Ext`, que controla a
flexão, e `F1_servo_Abd`, que controla a abdução. Sem o segundo, não existe
oposição do polegar — existe um dedo que dobra ao lado da palma. E é a oposição
que separa uma mão que agarra de uma mão que empurra.

O LimbIA tem sete juntas por causa disso: os cinco dedos, o punho, e a abdução
do polegar como junta própria (`DEDAO_ABD`).

### Calibração por endpoints medidos, não por ângulo suposto

O método do manual:

> O primeiro passo é determinar os limites de largura de pulso dos dois servos
> que movem o polegar. Use um **Servo Tester** e conecte cada motor
> individualmente, para avaliar a largura de pulso máxima e mínima permitida.

E os valores que ele publica:

```c
int extensionF1  = 2300;  // extensão total  -> normalizado para 0
int flexionF1_2  = 1026;  // flexão total    -> normalizado para 900
int add_F1       = 950;   // aduzido (junto à palma) -> 0
int abd_F1       = 1550;  // abduzido (longe da palma) -> 900
```

Repare que **o repouso é maior que o trabalho** no primeiro par: 2300 → 1026. É
servo montado espelhado, e é a regra, não a exceção. Por isso o LimbIA guarda
dois pulsos medidos por junta em vez de um mínimo e um máximo — a conversão
funciona nos dois sentidos, e o autoteste verifica exatamente esse caso.

A normalização do LAD ia de 0 a 900; aqui vai de **0 a 1000 (por mil)**, pelo
mesmo motivo dele: grau é ficção quando existe polia e tendão no meio. O que o
servo entrega é largura de pulso; o ângulo real do dedo depende do mecanismo.

### Corrente em série com cada motor

> NOTA 2: cada sensor de corrente deve ser conectado em série com cada um dos
> motores.

Seis sensores, um por motor. É daqui que vem o segundo sentido da mão.

### Os comandos de teste

O console serial do LimbIA mantém a numeração do LAD, para quem já conhece um
não precisar reaprender o outro:

| Comando | LAD | LimbIA |
| --- | --- | --- |
| 1 | fechar a mão | idem |
| 2 | abrir a mão | idem |
| 3 | fechar com o polegar afastado da palma | idem |
| 4 | sinal da paz | idem |
| 5 | flexionar o polegar | idem |
| 6 | estender o polegar | idem |
| 7 | oposição do polegar (varre a amplitude) | idem |

### O aviso de segurança, resolvido em hardware

> Ao carregar o código na placa ESP32, certifique-se de que a fonte externa
> esteja DESLIGADA, para evitar movimentos erráticos dos servos.

Procedimento manual é esquecido — e com a mão montada, um espasmo de sete servos
arrebenta tendão. O LimbIA usa o pino **OE do PCA9685**, ativo em nível baixo,
com pull-up de 10 kΩ: as saídas ficam mortas em hardware durante todo o boot e
toda a gravação, sem depender de o firmware ter subido nem de alguém lembrar de
nada. Detalhe em [02-hardware-e-pinagem.md](02-hardware-e-pinagem.md).

---

## O que a fusão descartou, e por quê

| Descartado | Origem | Motivo |
| --- | --- | --- |
| Motores DC + 2× L293D nos dedos longos | LAD | Motor DC não tem posição. O INOVAWEEK já havia migrado para servo, que resolve posição de graça; voltar atrás exigiria encoder em cada dedo |
| Acionar servo direto nos pinos da placa | INOVAWEEK | Sete servos exigiriam sete canais LEDC, e o ESP32 tem oito. O PCA9685 resolve com dois pinos e sobra placa |
| `for` varrendo ângulo com `delay(10)` | INOVAWEEK | Só descobre o obstáculo depois de ter empurrado o dedo contra ele até o fim do curso |
| Sensor ultrassônico disparando o gesto | INOVAWEEK | Era andaime de demonstração, não função da prótese. A detecção do que fazer virá da câmera ou do EMG |
| Ajuste de servo refeito a cada sessão | ambos | A calibração agora é gravada na NVS e sobrevive à regravação do firmware |

---

## O que ainda não existe

- **Reconhecimento de objeto por imagem.** O INOVAWEEK planeja treinar um
  classificador no Make.sense.AI para chaves, canetas, celulares e maçanetas, e
  ajustar a preensão conforme o objeto. Nada disso está escrito ainda. O que
  existe é a propriocepção — a mão sabe a *forma* do que segura, não *o que* é.
- **EMG.** O caderno já concluiu que "é bem complexo mexer com o sensor de EMG
  para leitura de sinais musculares (essa parte do projeto nem é tão prioridade
  por agora)". O firmware reserva um canal de ADC1 (GPIO32) e para por aí. Não há
  sensor de EMG no estoque.
- **A órtese.** O nome do projeto no laboratório é "mão robótica **+ órtese**
  adaptável a qualquer membro". A parte de órtese não tem nem concepção escrita.
- **A mão montada.** Nada neste repositório foi verificado com servo ligado.

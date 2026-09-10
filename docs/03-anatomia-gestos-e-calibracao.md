# Anatomia, gestos e calibração

## As sete juntas

| # | Nome | Anatomia | Canal PCA9685 | Corrente |
| --- | --- | --- | --- | --- |
| 0 | MINDY | mindinho | 0 | ✅ |
| 1 | DONCARE | anelar | 1 | ✅ |
| 2 | FEIO | médio | 2 | ✅ |
| 3 | JULGADOR | indicador | 3 | ✅ |
| 4 | DEDÃO | polegar — flexão | 4 | — |
| 5 | DEDAO_ABD | polegar — abdução | 5 | — |
| 6 | PULSO | punho | 6 | — |

Os nomes vêm do caderno do INOVAWEEK e ficaram de propósito: é assim que a
bancada chama cada dedo.

A sétima junta, `DEDAO_ABD`, veio do LAD Robotic Hand V3.0. Sem ela não existe
oposição do polegar — existe um dedo que dobra ao lado da palma. E é a oposição
que separa uma mão que agarra de uma mão que empurra.

## Posição em por mil, não em grau

Toda posição circula pelo firmware em **0..1000**:

- **0** = repouso — dedo estendido, polegar junto à palma;
- **1000** = trabalho — dedo fechado, polegar afastado.

Grau seria ficção. O que o servo entrega é largura de pulso; o ângulo real do
dedo depende da polia, do tendão e de quanto ele já esticou. Por mil é a fração
do curso **medido**, e essa é uma grandeza que continua verdadeira quando o
mecanismo muda.

## Calibração: dois pulsos medidos por junta

O método é o do manual do LAD: com um **servo tester**, achar o pulso que leva a
junta ao repouso e o que a leva ao fim de curso, e anotar os dois.

```c
struct Calibracao {
  uint16_t pulsoRepouso;   // us na posição 0
  uint16_t pulsoTrabalho;  // us na posição 1000
};
```

**Não se supõe qual é maior.** Os valores publicados no manual do LAD para o
polegar são `extensionF1 = 2300` (repouso) e `flexionF1_2 = 1026` (trabalho) —
invertidos. Servo montado espelhado é regra, não exceção, e a conversão funciona
nos dois sentidos:

```c
us = repouso + (trabalho - repouso) * perMil / 1000;   // (trabalho - repouso) pode ser negativo
```

Todo pulso é preso em **500..2500 µs** antes de sair. Fora dessa faixa o servo
bate no batente interno e trava puxando corrente até queimar.

### Padrão de fábrica

Enquanto não houver medida, o firmware usa os números dos dois documentos de
origem:

| Junta | Repouso | Trabalho | Origem |
| --- | --- | --- | --- |
| MINDY, DONCARE, FEIO, JULGADOR | 1000 µs | 2000 µs | tabela do INOVAWEEK (0° a 180°) |
| DEDÃO | 2300 µs | 1026 µs | `extensionF1` / `flexionF1_2` do LAD |
| DEDAO_ABD | 950 µs | 1550 µs | `add_F1` / `abd_F1` do LAD |
| PULSO | 1000 µs | 2000 µs | neutro em 500 = 1500 µs |

O firmware diz, no boot, de onde veio a calibração:

```
[calib] PADRAO DE FABRICA - calibre antes de montar
[calib] carregada da flash
```

## O console de calibração

Pela serial, a 115200. É a resposta direta ao "retrabalho desgramado": o ajuste
é feito uma vez e **gravado na flash**, sobrevivendo à regravação do firmware e
ao próximo dia de bancada.

```
c              lista a calibração de todas as juntas
c <j> +<n>     move a junta j em +n microssegundos
c <j> -<n>     move a junta j em -n microssegundos
c <j> r <us>   marca o pulso atual como REPOUSO da junta j
c <j> t <us>   marca o pulso atual como TRABALHO da junta j
w              grava na flash
f              volta ao padrão de fábrica
```

### Roteiro, uma junta de cada vez

1. `x` — desliga as saídas e conecte **só** o servo da junta a calibrar;
2. `e` — liga as saídas;
3. `c 3 +25` repetidas vezes, olhando o dedo, até ele chegar ao fim de curso
   estendido sem forçar. Passos de 25 µs; use 5 µs perto do fim;
4. `c 3 r 1480` — anota esse pulso como repouso (o valor é o que o console
   acabou de mostrar);
5. repete na direção oposta até o dedo fechar sem forçar;
6. `c 3 t 1980` — anota como trabalho;
7. `w` — grava;
8. `c` — confere: curso de pelo menos 150 µs, sinal positivo ou negativo, tanto
   faz.

> Durante o empurrão fino (`+`/`-`), a parada por contato fica **desligada** — ali
> o objetivo é justamente varrer o curso inteiro. Ela volta sozinha no próximo
> gesto ou preensão.

**Forçar o dedo contra o batente é o erro que estraga a calibração.** O ponto
certo é onde o movimento para de acontecer, não onde o servo começa a zumbir.
Anotar 40 µs além disso significa que o servo vai passar a vida empurrando o
mecanismo — quente, ruidoso e com o tendão esticando mais rápido.

### O que uma calibração inválida faz

Curso menor que 150 µs é recusado: ou a calibração ficou pela metade, ou os dois
pontos foram medidos na mesma posição por engano. O console avisa na hora, e o
`carrega()` da NVS recusa o bloco inteiro se qualquer junta estiver inválida —
melhor cair no padrão de fábrica, com aviso, do que mover dedo para posição que
não foi medida.

## Gestos

Nove poses estáticas, mais uma sequência.

| Comando | Gesto | Origem |
| --- | --- | --- |
| `1` | fechar a mão | LAD '1' |
| `2` | abrir a mão | LAD '2' |
| `3` | fechar com o polegar afastado da palma | LAD '3' |
| `4` | sinal da paz | LAD '4' / tabela do INOVAWEEK |
| `5` | flexionar o polegar | LAD '5' |
| `6` | estender o polegar | LAD '6' |
| `7` | oposição do polegar (sequência) | LAD '7' |
| `8` | positivo | INOVAWEEK |
| `9` | apontar | INOVAWEEK |
| `0` | pinça | — |

A tabela em `lib/limbia_mao/limbia_mao.cpp`, em por mil, na ordem MINDY,
DONCARE, FEIO, JULGADOR, DEDÃO, DEDAO_ABD, PULSO:

```c
G_ABRIR      {   0,    0,    0,    0,    0,  300, 500}
G_FECHAR     {1000, 1000, 1000, 1000, 1000,    0, 500}
G_FECHAR_V2  {1000, 1000, 1000, 1000, 1000, 1000, 500}
G_PAZ        {1000, 1000,    0,    0, 1000,    0, 500}
G_POSITIVO   {1000, 1000, 1000, 1000,    0, 1000, 500}
G_APONTAR    {1000, 1000, 1000,    0, 1000,    0, 500}
G_PINCA      {   0,    0,    0,  700,  700,  700, 500}
```

O sinal da paz é conferido pelo autoteste contra a tabela manuscrita do caderno,
linha por linha — mindinho e anelar tensionados 100%, médio e indicador
relaxados, polegar tensionado, punho em 50%.

### Sequências

Oposição do polegar é um caminho, não uma pose. Um passo só avança quando o
anterior termina de se mover — sem `delay()`:

```
DEDAO_ABD -> 1000   (afasta da palma)
DEDAO     -> 1000   (flexiona)
DEDAO_ABD -> 0      (traz para a palma, flexionado: é a oposição)
DEDAO     -> 0      (estende)
DEDAO_ABD -> 300    (repouso)
```

## Movimento

| Parâmetro | Valor | Efeito |
| --- | --- | --- |
| `PASSO_PERMIL` | 8 | avanço por tick |
| `INTERVALO_MOVIMENTO_MS` | 20 | 50 Hz |
| curso completo | | ~2,5 s |
| `MOVIMENTO_LIMITE_MS` | 6000 | guarda de tempo: aborta e deixa cada junta onde está |

Nada disso usa `delay()`. O movimento avança um passo por tick e a corrente é
lida **entre** os passos — é isso que permite o dedo parar quando encosta, em vez
de descobrir o obstáculo depois de tê-lo empurrado até o fim.

## Proteções

| Situação | O que acontece |
| --- | --- |
| Corrente acima de `CORRENTE_LIMITE_MA` | movimento abortado, junta informada na serial |
| Movimento passa de `MOVIMENTO_LIMITE_MS` | aborta, juntas param onde estão |
| Comando `x` | saídas desligadas em hardware (OE alto) — os servos ficam livres |
| Boot / gravação | saídas desligadas por pull-up, antes de o firmware existir |
| Calibração inválida na flash | recusa o bloco, cai no padrão de fábrica, avisa |

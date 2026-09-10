# Propriocepção: saber o que a mão está segurando

A ideia é do caderno de pesquisa do INOVAWEEK:

> **RECONHECIMENTO DE OBJETOS EM EVIDÊNCIA** — é um dos desafios principais da
> mão biônica e consiste basicamente em reconhecer o tipo de objeto que a mão
> está portando lendo apenas as informações de pulso e posição.
>
> Exemplo: se temos todos os servos meio tensionados, é porque é muito provável
> que estejamos segurando um objeto reto de estrutura circular cilíndrica.

Está implementada. Com uma correção que muda tudo.

## A correção: os dedos param quando encostam

Na formulação original, os dedos param num ângulo escolhido e a leitura desse
ângulo é a assinatura. O problema está no próprio caderno, três páginas adiante:

> mesmo que possamos trabalhar com linhas bem tensionadas e ajustadas, sempre
> teremos aquele desajuste pela deformação das linhas com o tempo.

Quando o tendão estica, a posição do servo deixa de corresponder à posição do
dedo. A assinatura medida na segunda-feira não vale na sexta — e nada no sistema
avisa. É uma medida que apodrece em silêncio.

**A corrente resolve isso porque mede do outro lado do problema.** Ela sobe
quando o dedo encontra resistência, esteja o tendão como estiver. Então o dedo
não para num ângulo combinado: para quando **encosta**. E o que sobra da leitura
— em que posição cada dedo encostou — é a forma do objeto.

```
fecha os quatro dedos longos + o polegar, devagar
        │
        ├─ corrente de um dedo passa de 400 mA, três leituras seguidas?
        │       └─> aquele dedo PARA. Anota a posição.
        │
        └─ chegou ao fim do curso sem a corrente subir?
                └─> anota "não tocou". Se a corrente ficou baixa o tempo todo,
                    isso é tendão frouxo, não objeto ausente.
```

Três leituras seguidas, e não uma: uma leitura isolada acima do limiar é ruído do
ADC; três seguidas são um dedo tocando. Leitura abaixo do limiar decrementa o
contador, então ruído esporádico nunca acumula.

## A assinatura

```c
struct AssinaturaPreensao {
  uint16_t contatoEm[N_JUNTAS];  // por mil onde a junta parou
  uint16_t forcaMa[N_JUNTAS];    // corrente no instante do contato
  bool     tocou[N_JUNTAS];
  uint8_t  comSensor;            // quais juntas têm sensor instalado
};
```

O campo `comSensor` existe porque a bancada tem **quatro** ACS712 e **sete**
juntas. Sem ele, o classificador leria "junta sem sensor" como "junta que não
encostou em nada" — uma resposta **confiante e errada**, que é o pior tipo. Com
ele, junta sem sensor não vota: ela reduz a confiança do resultado.

## A classificação

Só os quatro dedos longos formam a assinatura. O polegar se opõe a eles, então
mede outra coisa; o punho não agarra nada.

Duas grandezas bastam:

- **média** dos pontos de contato — quão fundo os dedos entraram;
- **espalhamento** (maior menos menor) — se pararam juntos ou em profundidades
  diferentes.

| Condição | Objeto | Exemplo |
| --- | --- | --- |
| média ≥ 800 | **fino** | caneta, chave, talher |
| média ≤ 250 | **grande** | os dedos mal saíram do lugar |
| espalhamento > 250 | **plano** | cartão, celular |
| resto | **cilíndrico** | copo, maçaneta |
| nenhum dedo tocou | **nada na mão** | |

O caso do caderno — "todos os servos meio tensionados → objeto cilíndrico" — cai
exatamente na última linha: média no meio, espalhamento baixo.

A segunda tabela do caderno (MINDY 120°, DONCARE 90°, FEIO 70°, JULGADOR 60°) é
uma escada: espalhamento alto, que aqui vira **plano**. É a mesma leitura que o
caderno fazia à mão, agora feita pelo firmware.

## Confiança

A classificação devolve um número de 0 a 100. Ele cai quando:

- poucos dedos encostaram;
- a média ficou perto de uma fronteira de decisão;
- **poucas juntas têm sensor** — a confiança é multiplicada pela fração de dedos
  longos instrumentados.

Confiança baixa não é erro: é a resposta honesta quando a assinatura está em cima
da fronteira. Um classificador que erra dizendo 90% é pior que um que erra
dizendo 55%, porque o primeiro engana quem lê.

## Diagnóstico de tendão frouxo

O caderno já tinha diagnosticado o problema em palavras. Com corrente, ele vira
detecção automática:

```c
bool tendaoFrouxo = temSensor(junta)
                 && !tocou[junta]
                 && contatoEm[junta] >= 1000      // percorreu o curso inteiro
                 && forcaMa[junta] < 150;         // e a corrente nunca subiu
```

Servo girando sem carga = está enrolando folga em vez de puxar dedo. Duas
condições precisam valer juntas, e a segunda é o que separa "tendão frouxo" de
"dedo que fechou no vazio": no segundo caso a corrente sobe, porque o servo ainda
empurra o próprio mecanismo.

Junta sem sensor **nunca** é diagnosticada. Melhor calado que errado.

## O autoteste

Nada disso pode ser verificado numa mão que não existe montada. O que dá para
verificar é a lógica — e isso é verificado, na placa, com números.

```powershell
pio run -e autoteste_c3 -t upload
pio device monitor -e autoteste_c3
```

Resultado medido em 09/09/2026, num ESP32-C3:

```
[1] conversao posicao <-> pulso
     pior erro de ida e volta: 1 por mil
[3] classificacao de preensao
     classe            n   acertos   confianca media
     objeto cilindrico  60    60/60    99%
     objeto fino        60    60/60    89%
     objeto plano       60    60/60   100%
     objeto grande      60    60/60    89%
     TOTAL: 240/240 = 100%  |  confianca media 94%
     --- casos de fronteira (ambiguos de proposito) ---
     fronteira: 34/40 = 85%  |  confianca media 86%
     confianca com 4 dedos: 100%  |  com 2 dedos: 45%
[5] custo de execucao
     classificacao: 1.7 us por chamada
     conversao de pulso: 0.58 us por chamada

  35 verificacoes: 35 passaram, 0 falharam  (8 ms)
```

### Como ler esses números

**Os 100% não são o resultado.** Um banco de casos que eu mesmo gerei, com as
quatro classes bem separadas, mede o gerador tanto quanto o classificador.
Acertar era o esperado.

**O resultado é a linha de fronteira.** Com assinaturas colocadas de propósito em
cima das fronteiras de decisão, o acerto cai para 85% — e a confiança cai para
86%. Os dois números caem juntos, e quase na mesma medida.

Isso significa que **o número de confiança está calibrado**: quando o
classificador diz 86%, ele acerta 85% das vezes. Não é sorte de um caso; é a
propriedade que torna o número utilizável para decidir. Uma preensão relatada com
50% de confiança pode ser tratada como "reposicione e tente de novo", e essa
decisão vai estar certa na proporção anunciada.

**A queda de 100% para 45% ao passar de quatro dedos instrumentados para dois** é
a medida do que os dois ACS712 que faltam custam.

O tempo — 1,7 µs por classificação — é irrelevante perto do tick de 20 ms, o que
significa que dá para classificar a cada passo do fechamento, se um dia isso for
útil, sem tocar no orçamento do loop.

## O que isto ainda não é

A mão sabe a **forma** do que segura. Não sabe **o que** é.

O objetivo do INOVAWEEK é outro e continua aberto: treinar um classificador de
imagem (Make.sense.AI) para reconhecer chaves, canetas, celulares e maçanetas, e
ajustar a preensão conforme o objeto reconhecido. A propriocepção é
complementar a isso, não substituta — e tem a vantagem de funcionar no escuro,
dentro do bolso e com a câmera suja.

E, principalmente: **nenhum destes números veio de um dedo tocando em alguma
coisa.** Vieram de casos sintéticos. Acerto de campo é outra medida e ainda não
existe.

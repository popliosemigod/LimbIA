# A rede da prótese, a tela de ajuste e o OTA

Duas placas, uma prótese, uma rede. Este documento é o de quem vai vestir e o de
quem vai dar manutenção.

## Quem é quem

```
       ESP32-C3 SuperMini                    ESP32 DevKit V1
   ┌────────────────────────┐          ┌────────────────────────┐
   │  placa do EMG          │          │  placa da mão          │
   │  2 eletrodos, 1 kHz    │   UDP    │  4 motores DC (L293D)  │
   │  a IA e o decisor      │ ───────► │  2 servos no polegar   │
   │  ponto de acesso       │ ◄─────── │  6 ACS712              │
   │  tela de ajuste + OTA  │telemetria│  OTA                   │
   │  192.168.4.1           │          │  192.168.4.200         │
   └────────────────────────┘          └────────────────────────┘
              ▲
              │ Wi-Fi (WPA2)
        PC ou celular ── a tela abre sozinha
```

| | Endereço |
| --- | --- |
| placa do EMG (ponto de acesso, tela, OTA) | 192.168.4.1 |
| placa da mão (cliente, OTA) | 192.168.4.200 |
| PC e celular | 192.168.4.2 em diante, por DHCP |

A mão fica em **.200**, e não em .2, porque o DHCP do ESP32 distribui a partir de
.2 e entregaria o mesmo endereço ao primeiro PC que entrasse na rede.

## Por que Wi-Fi entre as placas, e não um fio

As duas estão no mesmo braço; um fio seria mais simples. A escolha é outra por um
motivo elétrico: **sem fio entre elas, o terra ruidoso dos motores — que puxam
amperes em pico — não tem caminho até o terra do amplificador de EMG, que mede
microvolts.**

O mesmo vale para a tela: o ajuste é feito **pela rede**, com a placa do EMG na
bateria. Nunca com ela ligada ao USB do PC enquanto há eletrodo na pele
([05-emg-e-a-ia.md](05-emg-e-a-ia.md#segurança-elétrica--ler-antes-do-primeiro-eletrodo)).

## Por que UDP, e não TCP

O comando se repete 20 vezes por segundo e cada pacote carrega o **estado**
inteiro — modo, ação, número da ação — não um evento. Pacote perdido é
substituído pelo próximo 50 ms depois. O TCP retransmitiria o velho, atrasando o
novo: o oposto do que se quer.

Cada pacote leva **CRC-16**. Pacote com mágico, versão, tipo ou tamanho errado, ou
com CRC que não fecha, é descartado em silêncio. Medido: **nenhum** dos 120
pacotes com um bit trocado foi aceito.

## As três regras do enlace, e por que cada uma existe

1. **Ação só é executada quando o número dela muda.** O comando chega 20 vezes
   por segundo repetindo o mesmo estado; a mão não recomeça o movimento a cada
   repetição.
2. **O primeiro pacote depois de um silêncio só sincroniza — não move.** A mão
   que acabou de ligar não sabe há quanto tempo aquela ação foi decidida. Nada
   acontece sem uma decisão nova, tomada com a mão já ouvindo.
3. **Enlace perdido com a prótese em uso: cada junta para onde está.** Servo
   parado é o modo de falha seguro. A mão **não abre** — abrir derrubaria o que
   ela estivesse segurando.

## A tela que abre sozinha

Quando o PC ou o celular entra numa rede Wi-Fi, o sistema faz uma pergunta de
teste a um endereço conhecido: o Windows pede `msftconnecttest.com`, o Android
pede `generate_204`, o iPhone pede `captive.apple.com`. Na rede da prótese, o DNS
responde **qualquer** nome com o IP da placa, e o servidor manda qualquer caminho
desconhecido para a tela. O sistema conclui que a rede tem página de entrada e
**abre a tela sozinho**.

É isso que faz "conectei na prótese e a tela de ajuste apareceu". Se não abrir
sozinha, o endereço é **http://192.168.4.1/**.

## Os dois modos, e o botão único

| | Modo ajuste | Modo padrão |
| --- | --- | --- |
| Quem manda na mão | a tela (botões de teste) | **o músculo** |
| Calibração dos eletrodos | sim | não |
| Nome e senha da rede | sim | não |
| Poses "mão aberta" e "mão fechada" | sim | não |
| OTA | **liberado** | **bloqueado** |

Um botão só troca de um para o outro, no rodapé da tela. Para entrar em padrão,
uma de duas: a calibração em curso terminou com as três barras verdes (e o modelo
novo é gravado), ou não há calibração em curso e existe modelo gravado de antes.

**A prótese liga pronta para uso** se já houver calibração gravada — quem já
calibrou não calibra de novo a cada manhã.

## Nome e senha: escolhidos uma vez, guardados para sempre

O cliente escolhe o nome da rede (em geral o modelo da prótese) e a senha. Os dois
vão para a NVS, num namespace próprio, e sobrevivem a queda de energia, a
regravação do firmware por USB ou por OTA, e ao comando `f` da mão, que apaga a
calibração mas não a rede.

### A troca acontece em duas fases, e isso não é capricho

O nome e a senha são **das duas placas**. Se só a do EMG trocasse, a mão
reiniciaria procurando a rede velha e nunca mais voltaria — prótese com a mão
muda, e só um cabo USB para consertar. Então:

1. a senha nova vai para a mão, repetindo até ela confirmar;
2. a mão grava, confirma e reinicia;
3. **só então** a placa do EMG grava e reinicia.

Se a mão não confirmar em 3 s, **nada muda em nenhuma das duas**, e a tela diz
que a mão precisa estar ligada.

### Esqueci a senha

Segurar o **BOOT** da placa por **10 segundos** devolve o nome e a senha de
fábrica. Vale para as duas placas, e é longo de propósito: ninguém segura um
botão por dez segundos sem querer.

## OTA — gravar as duas placas sem USB

```powershell
pio run -e mao_ota -t upload     # a mão,  192.168.4.200
pio run -e emg_ota -t upload     # o EMG,  192.168.4.1
```

Três condições:

1. o PC está na rede da prótese;
2. a prótese está em **modo ajuste** — em modo padrão as duas placas recusam
   firmware novo, porque gravar desliga os atuadores;
3. a senha de OTA confere.

A senha sai de `include/secrets.h`, **a mesma que foi compilada no firmware**
(`scripts/ota_senha.py` a lê na hora do upload). Uma fonte só: a senha que a
placa espera e a senha que o PC manda nunca divergem.

> Sem `include/secrets.h`, o firmware compila com a senha do modelo — e avisa no
> boot. Serve para bancada; não se entrega prótese assim.

**Na mão, gravar desliga as saídas antes de começar.** É o aviso do manual do LAD
("desligue a fonte externa antes de gravar, para evitar movimentos erráticos")
virando comportamento do firmware, em vez de procedimento que alguém precisa
lembrar.

## O que a tela mostra

**Em ajuste**, três passos:

1. **Rede da prótese** — nome e senha. Se ainda estiver com os de fábrica, a tela
   avisa em vermelho.
2. **Posicione os eletrodos** — a instrução grande ("FECHE A MÃO", "relaxe",
   "ABRA A MÃO") com o relógio da fase, uma barra por eletrodo, uma barra de
   calibração geral, e o que a prótese está entendendo agora.
3. **Movimentos da mão** — quanto cada dedo vai em "mão aberta" e em "mão
   fechada", com botão de testar e de gravar.

**Em padrão**: o gesto atual em letra grande, o que a mão está segurando (quando
houve preensão), os níveis dos dois músculos e os avisos — eletrodo solto, mão
sem sinal, esforço alto, tendão frouxo.

## Console serial, para a bancada

Tudo o que a tela faz tem equivalente na serial, a 115200.

**Placa do EMG:** `s` status, `c`/`k` inicia e cancela calibração, `p`/`a` troca
de modo, `v` joga o envelope no Serial Plotter, `z` apaga o modelo, `n` rede, e
`1`/`2`/`0` mandam fechar, abrir e parar — o mesmo caminho que o decisor usa,
para testar o enlace sem eletrodo nem tela.

**Placa da mão:** os comandos 1 a 7 do manual do LAD, `p` preensão, `c` calibração
de pulso do polegar, `m` tempo de curso dos dedos, `n` rede, `x`/`e` saídas, e
`i`, que ignora a corrente — **só na bancada**, para mover motor antes de os
ACS712 estarem ligados.

## Consumo

| Firmware | RAM | Flash |
| --- | --- | --- |
| mão (DevKit V1) | 15,2% | 64,8% (849 kB de 1,31 MB) |
| placa do EMG (C3) | 14,4% | 66,1% (866 kB) |

Cabe nas duas partições de OTA com folga — é o que permite gravar pela rede.

// =====================================================================
//  LimbIA - config.h
//  Pinagem, limiares e parametros ajustaveis. Nenhuma logica mora aqui.
//  Placa alvo: ESP32 DevKit V1 (ESP32-D0WD-V3, 30 pinos)
//
//  DUAS MAOS, O MESMO FIRMWARE
//  ---------------------------
//  LIMBIA_MAO_LAD = 1 (padrao, e a mao que esta na bancada)
//      O hardware do LAD Robotic Hand V3.0: quatro dedos longos com
//      motores DC em dois L293D, dois servos no polegar e SEIS ACS712.
//
//  LIMBIA_MAO_LAD = 0
//      A mao do INOVAWEEK: sete servos num PCA9685, quatro ACS712.
//
//  O que muda e so o backend de acionamento (include/motores.h ou
//  include/dedos.h, os dois no namespace Dedos) e a pinagem daqui. Estado,
//  console, preensao, enlace, rede e OTA sao os mesmos para as duas.
// =====================================================================
#pragma once
#include <Arduino.h>

#include <limbia_mao.h>

#include "config_rede.h"

#ifndef LIMBIA_MAO_LAD
#define LIMBIA_MAO_LAD 1
#endif

#if LIMBIA_MAO_LAD
// =====================================================================
//  A MAO DO LAD: MOTORES DC + L293D + DOIS SERVOS NO POLEGAR
// =====================================================================

// ---------------------------------------------------------------------
//  Os dois L293D
//
//  Cada dedo longo usa duas entradas do driver: uma em nivel alto flexiona,
//  a outra estende, as duas em baixo deixam o dedo solto.
//
//  As entradas sao acionadas por PWM (LEDC), e nao por digitalWrite. O
//  motivo esta no pedido do projeto: movimento com cadencia controlada e
//  velocidade estavel. Motor DC ligado direto em 6 V fecha o dedo rapido
//  demais para a corrente ser lida no meio do caminho - e e a leitura no
//  meio do caminho que faz o dedo parar quando encosta.
//
//  O EN dos modulos L293D vem amarrado em nivel alto de fabrica; por isso
//  o PWM vai nas ENTRADAS, que e o jeito de controlar velocidade sem
//  mexer na placa.
// ---------------------------------------------------------------------
#define PIN_F_MINDY_A    23  // IN3 do segundo L293D
#define PIN_F_MINDY_B    27  // IN4 do segundo L293D
#define PIN_F_DONCARE_A  21  // IN1 do segundo L293D
#define PIN_F_DONCARE_B  22  // IN2 do segundo L293D
#define PIN_F_FEIO_A     5   // IN3 do primeiro L293D
#define PIN_F_FEIO_B     15  // IN4 do primeiro L293D
#define PIN_F_JULGADOR_A 18  // IN1 do primeiro L293D
#define PIN_F_JULGADOR_B 19  // IN2 do primeiro L293D

// ---------------------------------------------------------------------
//  Os dois servos do polegar
//
//  GPIO 25 e 26 sao os pinos que o manual do LAD usava para dois sensores
//  de corrente. Eles sao ADC2 - inutilizaveis para leitura analogica com
//  Wi-Fi ligado - mas continuam perfeitos como SAIDA. Os sensores mudaram
//  para o ADC1 (abaixo) e os servos herdaram estes dois pinos.
// ---------------------------------------------------------------------
#define PIN_SERVO_DEDAO     25  // F1_servo_Ext: flexao do polegar
#define PIN_SERVO_DEDAO_ABD 26  // F1_servo_Abd: abducao do polegar

// LEDC: os oito canais dos motores compartilham a mesma frequencia; os
// dois servos ficam no outro grupo de canais, para nao dividir timer com
// eles (um mudaria a frequencia do outro).
#define LEDC_MOTOR_HZ      1000
#define LEDC_MOTOR_BITS    8
#define LEDC_CANAL_MOTOR_0 0  // ...ate o 7: dois canais por dedo longo
#define LEDC_SERVO_HZ      50
#define LEDC_SERVO_BITS    16
#define LEDC_CANAL_SERVO_0 8   // DEDAO
#define LEDC_CANAL_SERVO_1 10  // DEDAO_ABD (canal 9 dividiria timer com o 8)

// Velocidade dos dedos longos, em duty de 0 a 255. Abaixo de ~150 o motor
// com carga nao sai do lugar; 255 e rapido demais para parar no contato.
// MEDIR na bancada e ajustar - e o primeiro numero que o ensaio corrige.
#define VELOCIDADE_DEDO 200

// Motor DC puxa um pico de corrente ao partir, com o rotor ainda parado.
// Durante este tempo a corrente NAO conta como contato - senao todo dedo
// "encosta em alguma coisa" no instante em que comeca a se mover.
#define ARRANQUE_CEGO_MS 250

// Tempo de ponta a ponta de cada dedo, medido uma vez (comando 'm' do
// console) e gravado na flash. E dele que sai a posicao estimada.
#define TEMPO_CURSO_PADRAO_MS 1500
#define TEMPO_CURSO_MIN_MS    200
#define TEMPO_CURSO_MAX_MS    8000

#else
// =====================================================================
//  A MAO DO INOVAWEEK: SETE SERVOS NUM PCA9685
// =====================================================================

// ---------------------------------------------------------------------
//  POR QUE UM DRIVER PWM, E NAO OS PINOS DA PLACA
//
//  Sete servos ligados direto exigiriam sete pinos com LEDC, e o ESP32
//  tem so oito canais LEDC de alta velocidade - sobrariam zero para
//  qualquer outra coisa. Pior: cada canal precisaria de resolucao alta
//  em 50 Hz, que e onde o LEDC do ESP32 e mais desconfortavel.
//
//  O PCA9685 resolve os sete servos com DOIS pinos (I2C), tem 12 bits
//  dedicados a 50 Hz, e ja esta no estoque - duas unidades. O modulo
//  tambem traz o pino OE, que vale sozinho a escolha: ver abaixo.
// ---------------------------------------------------------------------
#define PIN_SDA         21
#define PIN_SCL         22
#define PCA9685_ADDR    0x40
#define PCA9685_FREQ_HZ 50  // 20 ms de periodo: o que todo servo de hobby espera

// ---------------------------------------------------------------------
//  O PINO QUE RESOLVE O AVISO DO MANUAL DO LAD
//
//  O manual do LAD Robotic Hand manda desligar a fonte externa antes de
//  gravar o codigo, "para evitar movimentos erraticos dos servos". Isso
//  e um procedimento manual, e procedimento manual e esquecido - com a
//  mao montada, um espasmo de sete servos arrebenta tendao.
//
//  O OE (Output Enable) do PCA9685 e ativo em NIVEL BAIXO. Com pull-up
//  de 10k, ele fica ALTO durante todo o boot e toda a gravacao, e as
//  saidas de PWM ficam desligadas em hardware - sem depender de o
//  firmware ter subido, e sem depender de alguem lembrar de nada.
//
//  O firmware so baixa esse pino depois de ter escrito uma posicao valida
//  em todos os canais.
// ---------------------------------------------------------------------
#define PIN_PCA_OE      14

#endif  // LIMBIA_MAO_LAD

// ---------------------------------------------------------------------
//  SENSORES DE CORRENTE - a regra do ADC1
//
//  O ADC2 do ESP32 e usado internamente pelo radio: com Wi-Fi ligado,
//  analogRead() nele devolve lixo. Toda leitura analogica fica no ADC1.
//
//  Na DevKit V1 de 30 pinos, o ADC1 expoe exatamente SEIS canais - 32,
//  33, 34, 35, 36 e 39 (o 37 e o 38 existem no chip mas nao saem no
//  conector). Esta placa tem Wi-Fi ligado o tempo todo (cliente da rede da
//  protese, para o enlace e o OTA); e a regra do ADC1 que deixa isso sem
//  custo.
//
//  ATENCAO A QUEM SEGUE O ESQUEMA DO MANUAL DO LAD: la os sensores 5 e 6
//  estao nos GPIO 25 e 26, que sao ADC2. Aquele projeto nao usa Wi-Fi, e
//  por isso funciona; aqui devolveria lixo. Os seis sensores foram
//  remapeados para os seis canais de ADC1 - que sao exatamente seis, o
//  numero de sensores do LAD.
// ---------------------------------------------------------------------
#define PIN_CORR_MINDY    36  // ADC1_CH0 - so entrada
#define PIN_CORR_DONCARE  39  // ADC1_CH3 - so entrada
#define PIN_CORR_FEIO     34  // ADC1_CH6 - so entrada
#define PIN_CORR_JULGADOR 35  // ADC1_CH7 - so entrada

#if LIMBIA_MAO_LAD
// O LAD tem SEIS sensores: um em serie com cada motor, inclusive os dois
// do polegar. Todas as juntas que agarram passam a votar na classificacao
// do objeto - o polegar deixa de ser cego.
#define PIN_CORR_DEDAO     32  // ADC1_CH4 - F1_servo_Ext
#define PIN_CORR_DEDAO_ABD 33  // ADC1_CH5 - F1_servo_Abd

#define SENSORES_INSTALADOS                                                             \
  (LIMBIA_BIT(limbia::MINDY) | LIMBIA_BIT(limbia::DONCARE) | LIMBIA_BIT(limbia::FEIO) | \
   LIMBIA_BIT(limbia::JULGADOR) | LIMBIA_BIT(limbia::DEDAO) | LIMBIA_BIT(limbia::DEDAO_ABD))
#else
#define PIN_RESERVA_ADC_1 32  // ADC1_CH4 - livre
#define PIN_RESERVA_ADC_2 33  // ADC1_CH5 - livre

// Bit por junta. Mudou o hardware, muda aqui - e o firmware inteiro se
// ajusta, incluindo a confianca da classificacao.
#define SENSORES_INSTALADOS                                                             \
  (LIMBIA_BIT(limbia::MINDY) | LIMBIA_BIT(limbia::DONCARE) | LIMBIA_BIT(limbia::FEIO) | \
   LIMBIA_BIT(limbia::JULGADOR))
#endif

// Pino de corrente de cada junta, ou -1 se ela nao tem sensor. E a unica
// tabela; corrente.h le daqui.
inline int pinoDeCorrente(uint8_t junta) {
  switch (junta) {
    case limbia::MINDY: return PIN_CORR_MINDY;
    case limbia::DONCARE: return PIN_CORR_DONCARE;
    case limbia::FEIO: return PIN_CORR_FEIO;
    case limbia::JULGADOR: return PIN_CORR_JULGADOR;
#if LIMBIA_MAO_LAD
    case limbia::DEDAO: return PIN_CORR_DEDAO;
    case limbia::DEDAO_ABD: return PIN_CORR_DEDAO_ABD;
#endif
    default: return -1;
  }
}

// A mao de servos nao usa tempo de curso (a posicao dela e o pulso), mas o
// campo existe no estado das duas para o codigo comum nao precisar de #if.
#ifndef TEMPO_CURSO_PADRAO_MS
#define TEMPO_CURSO_PADRAO_MS 1500
#define TEMPO_CURSO_MIN_MS    200
#define TEMPO_CURSO_MAX_MS    8000
#endif

// A mao do LAD nao tem punho: sao seis juntas com atuador, nao sete.
#if LIMBIA_MAO_LAD
#define JUNTA_TEM_ATUADOR(j) ((j) != limbia::PULSO)
#else
#define JUNTA_TEM_ATUADOR(j) (true)
#endif

// ---------------------------------------------------------------------
//  Interface local
// ---------------------------------------------------------------------
#define PIN_BOTAO     0   // BOOT: segurar 10 s volta a rede ao de fabrica
#define PIN_LED_PLACA 2   // aceso enquanto alguma junta esta em movimento
#define PIN_BUZZER    13  // opcional - aviso de contato e de falha

// ---------------------------------------------------------------------
//  CADEIA DE MEDICAO DE CORRENTE
//
//  O ACS712 do estoque e a variante de 20 A: 100 mV/A, com saida em
//  repouso no meio da alimentacao (2,5 V em 5 V).
//
//  2,5 V ja e quase o teto do ADC do ESP32, e um pico de corrente sobe
//  disso. Por isso a saida de cada ACS712 passa por um divisor 10k/20k
//  (ganho 0,667): o repouso cai para ~1,67 V e sobra faixa para os dois
//  lados. O preco e sensibilidade: 100 mV/A viram 66,7 mV/A, ou cerca de
//  12 mA por contagem do ADC. Para detectar um dedo encostando, sobra.
//
//  A variante de 5 A (185 mV/A) daria quase o triplo de resolucao e e a
//  recomendacao para a proxima compra - ver docs/02-hardware-e-pinagem.md.
// ---------------------------------------------------------------------
#define ACS712_MV_POR_A 100.0f  // variante 20 A
#define DIVISOR_GANHO   0.667f  // 10k / 20k na saida de cada sensor
#define ADC_VREF_MV     3300.0f
#define ADC_MAX         4095.0f

// mV no ADC por ampere, ja com o divisor.
#define MV_POR_AMPERE (ACS712_MV_POR_A * DIVISOR_GANHO)

// ---------------------------------------------------------------------
//  LIMIARES DE CORRENTE
//
//  ATENCAO: os tres numeros abaixo sao PONTO DE PARTIDA, nao medida.
//  Dependem do servo (MG90S e 13 kgf tem consumos muito diferentes), do
//  atrito da polia e de quanto o tendao ja esticou. Calibrar com a mao
//  montada, pelo ambiente `bancada`, e commitar com tipo `calib`.
// ---------------------------------------------------------------------
#define CORRENTE_CONTATO_MA 400  // acima disso: o dedo encostou em algo
#define CORRENTE_LIMITE_MA  900  // acima disso: corta o movimento, protege o servo
#define CORRENTE_FOLGA_MA   150  // curso inteiro abaixo disso: tendao frouxo

// Leituras seguidas acima do limiar antes de aceitar como contato. Uma
// leitura isolada e ruido do ADC; tres seguidas sao um dedo tocando.
#define CONTATO_CONFIRMACOES 3

// ---------------------------------------------------------------------
//  MOVIMENTO
//
//  Nada de laco com delay(): o movimento avanca por passo a cada tick, e
//  o loop continua livre para ler corrente. E isso que permite PARAR no
//  contato - um for() com delay(10) so descobriria o obstaculo depois de
//  ter empurrado o dedo contra ele ate o fim.
// ---------------------------------------------------------------------
#define PASSO_PERMIL           8     // avanco por tick
#define INTERVALO_MOVIMENTO_MS 20    // 50 Hz -> curso completo em ~2,5 s
#define INTERVALO_CORRENTE_MS  10    // amostragem de corrente, mais rapida que o passo
#define INTERVALO_SERIAL_MS    2000  // telemetria pela serial

// Tempo maximo que uma junta pode ficar se movendo. Se estourar, algo
// travou e o movimento e abortado com a junta parada onde esta.
#define MOVIMENTO_LIMITE_MS 6000

// ---------------------------------------------------------------------
//  PERSISTENCIA
//
//  Mudou QUALQUER campo da struct salva na NVS? Suba o schema. O
//  flashAccess apaga a area antiga e volta ao padrao, em vez de ler o
//  layout novo em cima de bytes velhos.
// ---------------------------------------------------------------------
#define NVS_NAMESPACE "limbia"
#define CALIB_SCHEMA  1

// As duas poses que o EMG comanda ("mao aberta" e "mao fechada") moram
// na mesma area da calibracao, com versao propria dentro do bloco: o 'f'
// apaga as duas coisas juntas, que e o que "padrao de fabrica" quer dizer.
#define POSES_VERSAO 1

// ---------------------------------------------------------------------
//  Identidade
// ---------------------------------------------------------------------
#ifndef LIMBIA_VERSAO
#define LIMBIA_VERSAO "0.1.0-dev"
#endif

#ifndef LIMBIA_NOME
#define LIMBIA_NOME "limbia-01"
#endif

// ---------------------------------------------------------------------
//  ESTADO GLOBAL COMPARTILHADO
//  Fonte unica da verdade: serial, painel e diagnostico leem daqui.
// ---------------------------------------------------------------------
struct EstadoJunta {
  uint16_t posicao;     // por mil, onde a junta esta agora
  uint16_t destino;     // por mil, para onde esta indo
  uint16_t pulsoUs;     // ultimo pulso escrito no PCA9685
  uint16_t correnteMa;  // ultima leitura de corrente (0 se sem sensor)
  uint16_t picoMa;      // maior corrente do movimento atual
  bool emMovimento;
  bool contato;  // parou por ter encostado em algo
};

struct EstadoMao {
  EstadoJunta junta[limbia::N_JUNTAS];
  limbia::Calibracao calib[limbia::N_JUNTAS];

  bool saidasLigadas;  // OE em nivel baixo, servos podem se mover
  bool calibrada;      // a calibracao veio da NVS, nao do padrao de fabrica
  uint8_t gestoAtual;
  uint32_t movimentos;  // quantos gestos ja foram executados
  uint8_t folgas;       // juntas diagnosticadas com tendao frouxo

  uint16_t offsetAdc[limbia::N_JUNTAS];  // zero de cada ACS712, medido no boot

  // So na mao do LAD: tempo de ponta a ponta de cada dedo longo, medido na
  // bancada. E o que converte tempo de acionamento em posicao estimada -
  // motor DC nao tem posicao, e sem isto nao existe assinatura de objeto.
  uint16_t tempoCursoMs[limbia::N_JUNTAS];
  bool cursoMedido;

  // As duas poses comandadas pelo EMG. Editadas na tela de ajuste.
  limbia::Pose poseAberta;
  limbia::Pose poseFechada;
  bool posesDaFlash;
  bool sobrecarga;  // a ultima parada foi por corrente acima do limite

  // Corrente vale como sentido? Falso so na bancada, para mover motor
  // antes de os ACS712 estarem ligados: entrada de ADC solta flutua e o
  // firmware le isso como corrente - foi medido, 1232 mA num pino no ar.
  // Com isto falso, nao ha parada por contato NEM protecao de sobrecarga;
  // o que sobra e a guarda de tempo. Nunca fica falso numa protese.
  bool usaCorrente;
};

extern EstadoMao M;

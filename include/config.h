// =====================================================================
//  LimbIA - config.h
//  Pinagem, limiares e parametros ajustaveis. Nenhuma logica mora aqui.
//  Placa alvo: ESP32 DevKit V1 (ESP32-D0WD-V3, 30 pinos)
// =====================================================================
#pragma once
#include <Arduino.h>

#include <limbia_mao.h>

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
#define PIN_PCA_OE 14

// ---------------------------------------------------------------------
//  SENSORES DE CORRENTE - a regra do ADC1
//
//  O ADC2 do ESP32 e usado internamente pelo radio: com Wi-Fi ligado,
//  analogRead() nele devolve lixo. Toda leitura analogica fica no ADC1.
//
//  Na DevKit V1 de 30 pinos, o ADC1 expoe exatamente SEIS canais - 32,
//  33, 34, 35, 36 e 39 (o 37 e o 38 existem no chip mas nao saem no
//  conector). Seis canais para quatro correntes, o EMG e uma reserva.
//
//  Quatro, e nao sete, porque o estoque tem QUATRO ACS712. Os quatro vao
//  nos dedos longos, que sao os que formam a assinatura do objeto. O
//  polegar fica cego nesta versao, e o firmware sabe disso - a mascara
//  SENSORES_INSTALADOS faz a classificacao baixar a confianca em vez de
//  responder com certeza sobre o que nao mediu.
// ---------------------------------------------------------------------
#define PIN_CORR_MINDY    36  // ADC1_CH0 - so entrada
#define PIN_CORR_DONCARE  39  // ADC1_CH3 - so entrada
#define PIN_CORR_FEIO     34  // ADC1_CH6 - so entrada
#define PIN_CORR_JULGADOR 35  // ADC1_CH7 - so entrada
#define PIN_EMG           32  // ADC1_CH4 - reservado, sem sensor ainda
#define PIN_RESERVA_ADC   33  // ADC1_CH5 - livre

// Bit por junta. Mudou o hardware, muda aqui - e o firmware inteiro se
// ajusta, incluindo a confianca da classificacao.
#define SENSORES_INSTALADOS                                                             \
  (LIMBIA_BIT(limbia::MINDY) | LIMBIA_BIT(limbia::DONCARE) | LIMBIA_BIT(limbia::FEIO) | \
   LIMBIA_BIT(limbia::JULGADOR))

// ---------------------------------------------------------------------
//  Interface local
// ---------------------------------------------------------------------
#define PIN_BOTAO     0   // BOOT: ja tem pull-up, vai ao GND quando pressionado
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
};

extern EstadoMao M;

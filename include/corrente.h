// =====================================================================
//  LimbIA - corrente.h
//  Leitura dos ACS712, um por dedo longo.
//
//  A corrente e o sentido que a mao nao tinha. Posicao diz onde o SERVO
//  esta; corrente diz se o DEDO esta encontrando alguma coisa. Sao duas
//  perguntas diferentes, e o caderno do INOVAWEEK ja tinha descoberto,
//  pela via dolorosa, que a primeira sozinha nao basta: tendao que
//  estica desfaz a relacao entre angulo do servo e posicao do dedo.
// =====================================================================
#pragma once
#include <Arduino.h>

#include "config.h"

namespace Corr {

// Quantas amostras por leitura. O ADC do ESP32 e ruidoso: uma leitura
// isolada varia dezenas de contagens entre chamadas. Oito custam ~100 us.
static const uint8_t AMOSTRAS = 8;

inline bool temSensor(uint8_t junta) {
  return (SENSORES_INSTALADOS & LIMBIA_BIT(junta)) != 0;
}

inline int pinoDaJunta(uint8_t junta) {
  switch (junta) {
    case limbia::MINDY: return PIN_CORR_MINDY;
    case limbia::DONCARE: return PIN_CORR_DONCARE;
    case limbia::FEIO: return PIN_CORR_FEIO;
    case limbia::JULGADOR: return PIN_CORR_JULGADOR;
    default: return -1;
  }
}

inline uint16_t leAdcMedio(uint8_t pino) {
  uint32_t soma = 0;
  for (uint8_t i = 0; i < AMOSTRAS; i++) soma += analogRead(pino);
  return (uint16_t)(soma / AMOSTRAS);
}

// ---------------------------------------------------------------------
//  Zero de cada sensor
//
//  O ACS712 nao entrega exatamente Vcc/2 em repouso - varia por peca,
//  por temperatura e pela tolerancia do divisor. Assumir o valor teorico
//  faria a mao "ver" corrente parada e disparar contato no vazio.
//
//  O zero e medido no boot, e o PCA9685 e quem torna isso possivel: com
//  o OE ainda alto, nenhum servo esta energizado, entao o que o ADC le
//  naquele instante e, por construcao, corrente zero.
//
//  Chamar SEMPRE antes de ligar as saidas.
// ---------------------------------------------------------------------
inline void medeZero() {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    if (!temSensor(i)) {
      M.offsetAdc[i] = 0;
      continue;
    }
    // Media longa: o zero e medido uma vez e usado por horas, entao vale
    // gastar alguns milissegundos para ele nascer estavel.
    uint32_t soma = 0;
    for (uint8_t n = 0; n < 32; n++) soma += analogRead(pinoDaJunta(i));
    M.offsetAdc[i] = (uint16_t)(soma / 32);
  }
}

inline void begin() {
  analogReadResolution(12);
  // 11 dB estende a faixa util do ADC ate ~3,1 V, que e onde a saida
  // dividida do ACS712 trabalha.
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    const int p = pinoDaJunta(i);
    if (p >= 0) analogSetPinAttenuation(p, ADC_11db);
  }
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) M.offsetAdc[i] = 0;
}

// Corrente em mA. Sempre positiva: o sentido de giro nao interessa aqui,
// o que interessa e o esforco.
inline uint16_t leMa(uint8_t junta) {
  if (!temSensor(junta)) return 0;
  const int pino = pinoDaJunta(junta);
  if (pino < 0) return 0;

  const int32_t bruto = (int32_t)leAdcMedio((uint8_t)pino);
  int32_t delta       = bruto - (int32_t)M.offsetAdc[junta];
  if (delta < 0) delta = -delta;

  // contagens -> mV -> mA
  const float mv = ((float)delta * ADC_VREF_MV) / ADC_MAX;
  const float ma = (mv / MV_POR_AMPERE) * 1000.0f;
  return (uint16_t)(ma < 0 ? 0 : (ma > 65535.0f ? 65535.0f : ma));
}

// Chamar todo loop. Atualiza a corrente de todas as juntas com sensor e
// guarda o pico do movimento em curso.
inline void tick() {
  static uint32_t proximo = 0;
  const uint32_t agora    = millis();
  if ((int32_t)(agora - proximo) < 0) return;
  proximo = agora + INTERVALO_CORRENTE_MS;

  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    if (!temSensor(i)) continue;
    const uint16_t ma     = leMa(i);
    M.junta[i].correnteMa = ma;
    if (ma > M.junta[i].picoMa) M.junta[i].picoMa = ma;
  }
}

inline void zeraPicos() {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) M.junta[i].picoMa = 0;
}

// Alguma junta passou do limite de protecao?
inline bool sobrecarga(uint8_t* qual) {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    if (!temSensor(i)) continue;
    if (M.junta[i].correnteMa >= CORRENTE_LIMITE_MA) {
      if (qual) *qual = i;
      return true;
    }
  }
  return false;
}

}  // namespace Corr

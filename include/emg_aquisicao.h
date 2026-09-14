// =====================================================================
//  LimbIA - emg_aquisicao.h
//  Amostragem dos dois eletrodos a 1 kHz, numa tarefa propria.
//
//  Por que uma tarefa, e nao o loop
//  --------------------------------
//  O loop desta placa serve a tela de ajuste. Mandar a pagina ao celular
//  leva dezenas de milissegundos, e nesse tempo o loop nao volta. Se a
//  amostragem morasse nele, cada visita a tela abriria um buraco no sinal
//  - e um buraco no sinal, depois do passa-altas, parece uma contracao.
//
//  A tarefa roda no nucleo 1 com prioridade acima do loop, acorda a cada
//  1 ms pelo relogio do FreeRTOS (vTaskDelayUntil, que nao acumula
//  atraso) e entrega uma janela pronta a cada 50 ms numa fila. O loop
//  consome a fila quando puder; se atrasar, as janelas esperam - nao se
//  perdem.
//
//  Fixada num nucleo por exigencia do ESP-IDF 4.x: tarefa que usa FPU
//  precisa estar fixada, e a cadeia de filtros e toda em float.
// =====================================================================
#pragma once
#include <Arduino.h>

#include <limbia_emg.h>

#include "config_emg.h"

namespace Aquisicao {

struct Janela {
  float envelope[limbia::N_CANAIS_EMG];  // media do envelope na janela
  uint16_t saturadas[limbia::N_CANAIS_EMG];
  uint16_t amostras;
  uint32_t numero;
};

static const uint16_t AMOSTRAS_POR_JANELA = EMG_FS_HZ * EMG_JANELA_MS / 1000;

inline QueueHandle_t& fila() {
  static QueueHandle_t f = nullptr;
  return f;
}

inline limbia::CadeiaEmg* cadeias() {
  static limbia::CadeiaEmg c[limbia::N_CANAIS_EMG];
  return c;
}

inline uint32_t& descartadas() {
  static uint32_t n = 0;
  return n;
}

inline void tarefa(void*) {
  static const uint8_t PINOS[limbia::N_CANAIS_EMG] = {PIN_EMG_FLEXOR, PIN_EMG_EXTENSOR};
  Janela j;
  memset(&j, 0, sizeof(j));
  float soma[limbia::N_CANAIS_EMG] = {0, 0};
  uint32_t numero                  = 0;

  TickType_t ultimo = xTaskGetTickCount();
  for (;;) {
    // 1 tick = 1 ms no core Arduino-ESP32 (configTICK_RATE_HZ = 1000).
    vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(1000 / EMG_FS_HZ));

    for (uint8_t ch = 0; ch < limbia::N_CANAIS_EMG; ch++) {
      const uint16_t bruto = analogRead(PINOS[ch]);
      if (bruto <= EMG_TRILHO_BAIXO || bruto >= EMG_TRILHO_ALTO) j.saturadas[ch]++;
      soma[ch] += limbia::cadeiaProcessa(&cadeias()[ch], (float)bruto);
    }

    if (++j.amostras >= AMOSTRAS_POR_JANELA) {
      for (uint8_t ch = 0; ch < limbia::N_CANAIS_EMG; ch++) {
        j.envelope[ch] = soma[ch] / j.amostras;
        soma[ch]       = 0;
      }
      j.numero = numero++;
      // Timeout zero: a tarefa nunca espera pelo loop. Fila cheia (loop
      // travado por mais de 400 ms) descarta a janela e conta.
      if (xQueueSend(fila(), &j, 0) != pdTRUE) descartadas()++;
      memset(&j, 0, sizeof(j));
    }
  }
}

inline void begin() {
  analogReadResolution(12);
  // 11 dB: faixa util ate ~3,1 V. O modulo de EMG alimentado em 3,3 V
  // oscila em torno de 1,65 V e cabe inteiro.
  analogSetPinAttenuation(PIN_EMG_FLEXOR, ADC_11db);
  analogSetPinAttenuation(PIN_EMG_EXTENSOR, ADC_11db);

  const limbia::ParametrosCadeia p = parametrosCadeia();
  for (uint8_t ch = 0; ch < limbia::N_CANAIS_EMG; ch++) limbia::cadeiaConfigura(&cadeias()[ch], p);

  fila() = xQueueCreate(8, sizeof(Janela));
  xTaskCreatePinnedToCore(tarefa, "emg", 4096, nullptr, 5, nullptr, 1);
}

// Proxima janela, se houver. Nao bloqueia.
inline bool proxima(Janela* j) {
  return fila() && xQueueReceive(fila(), j, 0) == pdTRUE;
}

}  // namespace Aquisicao

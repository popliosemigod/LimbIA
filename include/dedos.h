// =====================================================================
//  LimbIA - dedos.h
//  Acionamento das sete juntas pelo PCA9685, com movimento gradual e
//  parada por contato.
//
//  A parada por contato e o coracao do projeto. Um laco
//  `for (a = 0; a <= 180; a++) { write(a); delay(10); }` - que e como o
//  codigo do INOVAWEEK movia os dedos - so descobre um obstaculo depois
//  de ter empurrado o dedo contra ele ate o fim do curso. Aqui o
//  movimento avanca um passo por tick e a corrente e lida entre os
//  passos, entao o dedo PARA onde encostou.
//
//  Isso ataca direto a dor numero um registrada no caderno: "e muito
//  dificil deixar os servos ajustados para mover as cordas de cada dedo".
//  Quando o dedo para por contato e nao por angulo combinado, o ajuste
//  fino do tendao deixa de ser pre-requisito para a mao agarrar.
// =====================================================================
#pragma once
#include <Adafruit_PWMServoDriver.h>
#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "corrente.h"

namespace Dedos {

inline Adafruit_PWMServoDriver& driver() {
  static Adafruit_PWMServoDriver instancia(PCA9685_ADDR);
  return instancia;
}

// Canal do PCA9685 por junta. Mapeamento direto: junta 0 no canal 0.
inline uint8_t canal(uint8_t junta) {
  return junta;
}

// Quando true, o movimento para assim que a corrente indicar contato.
// Fica desligado durante a calibracao - la o objetivo e justamente
// varrer o curso inteiro.
inline bool& pararNoContato() {
  static bool v = true;
  return v;
}

// ---------------------------------------------------------------------
//  Saidas
// ---------------------------------------------------------------------
inline void ligaSaidas(bool ligar) {
  // OE e ativo em nivel BAIXO.
  digitalWrite(PIN_PCA_OE, ligar ? LOW : HIGH);
  M.saidasLigadas = ligar;
}

inline void escrevePulso(uint8_t junta, uint16_t us) {
  if (junta >= limbia::N_JUNTAS) return;
  if (us < limbia::PULSO_MIN_US) us = limbia::PULSO_MIN_US;
  if (us > limbia::PULSO_MAX_US) us = limbia::PULSO_MAX_US;
  M.junta[junta].pulsoUs = us;
  driver().writeMicroseconds(canal(junta), us);
}

// Posiciona imediatamente, sem rampa. Usado no boot e na calibracao.
inline void escreve(uint8_t junta, uint16_t perMil) {
  if (junta >= limbia::N_JUNTAS) return;
  if (perMil > 1000) perMil = 1000;
  M.junta[junta].posicao     = perMil;
  M.junta[junta].destino     = perMil;
  M.junta[junta].emMovimento = false;
  escrevePulso(junta, limbia::pulsoDePerMil(M.calib[junta], perMil));
}

// ---------------------------------------------------------------------
//  Movimento gradual
// ---------------------------------------------------------------------
inline void vaiPara(uint8_t junta, uint16_t perMil) {
  if (junta >= limbia::N_JUNTAS) return;
  if (perMil > 1000) perMil = 1000;
  M.junta[junta].destino     = perMil;
  M.junta[junta].contato     = false;
  M.junta[junta].picoMa      = 0;
  M.junta[junta].emMovimento = (M.junta[junta].posicao != perMil);
}

inline void vaiParaPose(const limbia::Pose& p) {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) vaiPara(i, p.alvo[i]);
}

inline void para() {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    M.junta[i].destino     = M.junta[i].posicao;
    M.junta[i].emMovimento = false;
  }
}

inline bool emMovimento() {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    if (M.junta[i].emMovimento) return true;
  }
  return false;
}

// ---------------------------------------------------------------------
//  begin
//
//  A ordem aqui importa e nao e negociavel:
//    1. OE alto  -> saidas mortas, em hardware
//    2. mede o zero dos sensores (so faz sentido com tudo parado)
//    3. escreve uma posicao valida em TODOS os canais
//    4. so entao libera as saidas
//
//  Inverter 3 e 4 faria os servos saltarem para o ultimo valor que
//  estivesse nos registradores do PCA9685 - que, depois de um reset por
//  watchdog, e qualquer coisa.
// ---------------------------------------------------------------------
inline bool begin() {
  pinMode(PIN_PCA_OE, OUTPUT);
  ligaSaidas(false);  // antes de tudo

  Wire.begin(PIN_SDA, PIN_SCL);
  const bool ok = driver().begin();
  driver().setPWMFreq(PCA9685_FREQ_HZ);
  delay(10);  // o oscilador do PCA9685 precisa de um instante apos trocar a frequencia

  Corr::medeZero();  // com tudo desenergizado, o que se le e o zero

  const limbia::Pose& repouso = limbia::poseDoGesto(limbia::G_ABRIR);
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) escreve(i, repouso.alvo[i]);

  ligaSaidas(true);
  return ok;
}

// ---------------------------------------------------------------------
//  tick - chamar todo loop
// ---------------------------------------------------------------------
inline void tick() {
  static uint32_t proximo                       = 0;
  static uint8_t confirmacoes[limbia::N_JUNTAS] = {0};
  static uint32_t inicioMovimento               = 0;

  const uint32_t agora = millis();
  if ((int32_t)(agora - proximo) < 0) return;
  proximo = agora + INTERVALO_MOVIMENTO_MS;

  if (!emMovimento()) {
    inicioMovimento = 0;
    for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) confirmacoes[i] = 0;
    return;
  }
  if (inicioMovimento == 0) inicioMovimento = agora;

  // Guarda de tempo: movimento que nao termina e mecanismo travado.
  // Abortar deixando cada junta onde esta e mais seguro que insistir.
  if (agora - inicioMovimento > MOVIMENTO_LIMITE_MS) {
    para();
    return;
  }

  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    EstadoJunta& j = M.junta[i];
    if (!j.emMovimento) continue;

    // ---- contato ----
    if (pararNoContato() && Corr::temSensor(i)) {
      if (j.correnteMa >= CORRENTE_CONTATO_MA) {
        if (confirmacoes[i] < 255) confirmacoes[i]++;
        if (confirmacoes[i] >= CONTATO_CONFIRMACOES) {
          j.contato     = true;
          j.destino     = j.posicao;
          j.emMovimento = false;
          continue;
        }
      } else if (confirmacoes[i] > 0) {
        confirmacoes[i]--;  // ruido isolado nao acumula
      }
    }

    // ---- passo ----
    const int32_t delta = (int32_t)j.destino - (int32_t)j.posicao;
    if (delta == 0) {
      j.emMovimento = false;
      continue;
    }
    const int32_t passo = delta > 0 ? PASSO_PERMIL : -PASSO_PERMIL;
    int32_t nova        = (int32_t)j.posicao + passo;
    // Nao passar do destino
    if ((passo > 0 && nova > (int32_t)j.destino) || (passo < 0 && nova < (int32_t)j.destino)) {
      nova = (int32_t)j.destino;
    }
    j.posicao = (uint16_t)nova;
    escrevePulso(i, limbia::pulsoDePerMil(M.calib[i], j.posicao));
    if (j.posicao == j.destino) j.emMovimento = false;
  }
}

// Volta para a mao aberta e espera terminar nao e trabalho desta funcao:
// ela so define o destino, o tick faz o resto.
inline void relaxa() {
  vaiParaPose(limbia::poseDoGesto(limbia::G_ABRIR));
}

}  // namespace Dedos

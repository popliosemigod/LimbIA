// =====================================================================
//  LimbIA - memoria.h
//  Calibracao e poses persistidas na NVS.
//
//  Por que isso importa mais aqui do que em outro projeto: o caderno do
//  INOVAWEEK registra que ajustar os servos "gera um retrabalho
//  desgramado". Retrabalho so existe porque o ajuste se perde. Medido
//  uma vez e gravado na flash, ele sobrevive a queda de energia, a
//  regravacao do firmware e ao proximo dia de bancada.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>

#include <limbia_enlace.h>

#include "config.h"

namespace Memoria {

inline Preferences& prefs() {
  static Preferences p;
  return p;
}

inline bool& aberta() {
  static bool v = false;
  return v;
}

// Struct efetivamente gravada. Mudou um campo? Suba CALIB_SCHEMA.
struct Bloco {
  limbia::Calibracao calib[limbia::N_JUNTAS];
};

// As poses tem versao dentro do proprio bloco. Mudou o layout? Suba
// POSES_VERSAO - o bloco velho e recusado e as poses voltam ao padrao.
struct BlocoPoses {
  uint8_t versao;
  uint8_t reservado;
  limbia::Pose aberta;
  limbia::Pose fechada;
};

inline bool begin() {
  aberta() = prefs().begin(NVS_NAMESPACE, false);
  if (!aberta()) return false;

  const uint16_t gravado = prefs().getUShort("__schema", 0);
  if (gravado != CALIB_SCHEMA) {
    // Layout antigo ou area virgem: apaga em vez de ler bytes velhos com
    // o formato novo. Silencio aqui viraria dedo indo para o lugar errado.
    prefs().clear();
    prefs().putUShort("__schema", CALIB_SCHEMA);
  }
  return true;
}

inline bool salva() {
  if (!aberta()) return false;
  Bloco b;
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) b.calib[i] = M.calib[i];
  const bool ok = prefs().putBytes("calib", &b, sizeof(b)) == sizeof(b);
  if (ok) M.calibrada = true;
  return ok;
}

// Devolve true quando a calibracao veio da flash; false quando caiu no
// padrao de fabrica. Quem chama precisa saber a diferenca - firmware
// rodando com padrao de fabrica numa mao montada move dedo para posicao
// que nao foi medida.
inline bool carrega() {
  limbia::calibracaoPadrao(M.calib);
  M.calibrada = false;

  if (!aberta()) return false;
  if (prefs().getBytesLength("calib") != sizeof(Bloco)) return false;

  Bloco b;
  if (prefs().getBytes("calib", &b, sizeof(b)) != sizeof(b)) return false;

  // Confere antes de aceitar: bloco gravado pela metade, ou por uma
  // versao com bug, nao vira posicao de servo.
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    if (!limbia::calibracaoValida(b.calib[i])) return false;
  }
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) M.calib[i] = b.calib[i];
  M.calibrada = true;
  return true;
}

// ---------------------------------------------------------------------
//  Poses: "mao aberta" e "mao fechada"
//
//  Padrao: os gestos '2' (abrir) e '1' (fechar) do manual do LAD.
// ---------------------------------------------------------------------
inline void posesPadrao() {
  M.poseAberta   = limbia::poseDoGesto(limbia::G_ABRIR);
  M.poseFechada  = limbia::poseDoGesto(limbia::G_FECHAR);
  M.posesDaFlash = false;
}

inline bool salvaPoses() {
  if (!aberta()) return false;
  BlocoPoses b;
  b.versao      = POSES_VERSAO;
  b.reservado   = 0;
  b.aberta      = M.poseAberta;
  b.fechada     = M.poseFechada;
  const bool ok = prefs().putBytes("poses", &b, sizeof(b)) == sizeof(b);
  if (ok) M.posesDaFlash = true;
  return ok;
}

inline bool carregaPoses() {
  posesPadrao();
  if (!aberta()) return false;
  if (prefs().getBytesLength("poses") != sizeof(BlocoPoses)) return false;
  BlocoPoses b;
  if (prefs().getBytes("poses", &b, sizeof(b)) != sizeof(b)) return false;
  if (b.versao != POSES_VERSAO) return false;
  if (!limbia::poseValida(b.aberta.alvo) || !limbia::poseValida(b.fechada.alvo)) return false;
  M.poseAberta   = b.aberta;
  M.poseFechada  = b.fechada;
  M.posesDaFlash = true;
  return true;
}

// Calibracao E poses de volta ao padrao. A rede NAO: ela mora em outro
// namespace (NVS_REDE), justamente para sobreviver a este comando.
inline void apaga() {
  if (aberta()) prefs().clear();
  prefs().putUShort("__schema", CALIB_SCHEMA);
  limbia::calibracaoPadrao(M.calib);
  M.calibrada = false;
  posesPadrao();
}

}  // namespace Memoria

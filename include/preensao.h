// =====================================================================
//  LimbIA - preensao.h
//  Fecha a mao ate encostar e le a forma do que ficou dentro dela.
//
//  A ideia e do caderno de pesquisa do INOVAWEEK: "reconhecer o tipo de
//  objeto que a mao esta portando lendo apenas as informacoes de pulso e
//  posicao". O exemplo dado la e exatamente o que este modulo produz -
//  todos os dedos parados no meio do curso indicam objeto cilindrico.
//
//  A diferenca esta em COMO os dedos param. No caderno, eles param num
//  angulo escolhido; aqui, param quando a corrente diz que encostaram.
//  A distincao e o que separa a ideia de funcionar de a ideia funcionar:
//  com tendao que estica, angulo combinado deixa de corresponder a
//  posicao real do dedo, e a assinatura medida na segunda-feira nao vale
//  na sexta. Contato nao tem esse problema.
// =====================================================================
#pragma once
#include <Arduino.h>

#include "config.h"
#include "corrente.h"
#include "dedos.h"

namespace Preensao {

enum Fase : uint8_t {
  PARADO = 0,
  FECHANDO,
  PRONTA
};

inline Fase& fase() {
  static Fase f = PARADO;
  return f;
}

inline limbia::AssinaturaPreensao& assinatura() {
  static limbia::AssinaturaPreensao a;
  return a;
}

inline uint8_t& objeto() {
  static uint8_t o = limbia::OBJ_NENHUM;
  return o;
}

inline uint8_t& confianca() {
  static uint8_t c = 0;
  return c;
}

inline bool ocupado() {
  return fase() == FECHANDO;
}
inline bool pronta() {
  return fase() == PRONTA;
}

// ---------------------------------------------------------------------
//  Inicia a preensao
//
//  O punho fica fora: ele nao agarra, e move-lo durante a leitura so
//  acrescentaria corrente que nao diz nada sobre o objeto.
// ---------------------------------------------------------------------
inline void inicia() {
  limbia::AssinaturaPreensao& a = assinatura();
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    a.contatoEm[i] = 0;
    a.forcaMa[i]   = 0;
    a.tocou[i]     = false;
  }
  a.comSensor = SENSORES_INSTALADOS;

  Corr::zeraPicos();
  Dedos::pararNoContato() = true;

  // Fecha os cinco dedos; o polegar tambem cruza para a palma, que e o
  // que fecha a pinca contra os longos.
  for (uint8_t i = limbia::MINDY; i <= limbia::DEDAO; i++) Dedos::vaiPara(i, 1000);
  Dedos::vaiPara(limbia::DEDAO_ABD, 0);

  objeto()    = limbia::OBJ_NENHUM;
  confianca() = 0;
  fase()      = FECHANDO;
}

inline void solta() {
  Dedos::relaxa();
  fase() = PARADO;
}

// ---------------------------------------------------------------------
//  tick - chamar todo loop
// ---------------------------------------------------------------------
inline void tick() {
  if (fase() != FECHANDO) return;
  if (Dedos::emMovimento()) return;  // ainda fechando

  // Todas as juntas pararam: ou por contato, ou por fim de curso.
  limbia::AssinaturaPreensao& a = assinatura();
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    a.contatoEm[i] = M.junta[i].posicao;
    a.forcaMa[i]   = M.junta[i].picoMa;
    a.tocou[i]     = M.junta[i].contato;
  }

  uint8_t c   = 0;
  objeto()    = limbia::classificaPreensao(a, &c);
  confianca() = c;
  M.folgas    = limbia::juntasComFolga(a, CORRENTE_FOLGA_MA);
  fase()      = PRONTA;
}

// ---------------------------------------------------------------------
//  Relatorio para a serial
// ---------------------------------------------------------------------
inline void imprime(Stream& out) {
  const limbia::AssinaturaPreensao& a = assinatura();
  out.println(F("--- assinatura da preensao ---"));
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    if (i == limbia::PULSO) continue;
    const bool sensor = (a.comSensor & LIMBIA_BIT(i)) != 0;
    out.printf("  %-10s pos %4u  %s", limbia::nomeDaJunta(i), a.contatoEm[i],
               sensor ? "" : "(sem sensor)");
    if (sensor) {
      out.printf("%5u mA  %s", a.forcaMa[i], a.tocou[i] ? "CONTATO" : "sem contato");
      if (limbia::tendaoFrouxo(a, i, CORRENTE_FOLGA_MA)) out.print(F("  << TENDAO FROUXO"));
    }
    out.println();
  }
  out.printf("  -> %s (confianca %u%%)\n", limbia::nomeDoObjeto(objeto()), confianca());
  if (M.folgas) {
    out.printf("  ATENCAO: %u junta(s) com tendao frouxo - retensionar\n", M.folgas);
  }
}

}  // namespace Preensao

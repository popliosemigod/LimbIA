// =====================================================================
//  limbia_paralelo - dedo de mecanismo paralelo (Amazing Hand), sem
//  Arduino dentro
//
//  De onde vem
//  -----------
//  Amazing Hand, da Pollen Robotics (github.com/pollen-robotics/
//  AmazingHand, Apache-2.0). Cada dedo e movido por DOIS servos lado a
//  lado, cada um puxando uma haste com rotula ate a base do dedo:
//
//    - os dois girando em sentidos OPOSTOS -> flexao / extensao
//    - os dois girando no MESMO sentido    -> abducao / aducao
//
//  E essa a pegadinha: mandar o mesmo angulo para os dois servos nao
//  fecha o dedo, torce o dedo para o lado.
//
//  A regra, lida no codigo oficial
//  -------------------------------
//  O demo oficial manda, para cada dedo, Move_X(Angle_1, Angle_2), e
//  cada servo vai para MiddlePos + Angle. Os valores dos gestos:
//
//      OpenHand   (-35,  35)    soma 0, diferenca -70
//      CloseHand  ( 90, -90)    soma 0, diferenca 180
//      Spread ind (  4,  90)    soma +94  (indicador para um lado)
//      Spread anl (-90,  -4)    soma -94  (anelar para o outro)
//
//  Logo: flexao = (Angle_1 - Angle_2) / 2 e abducao = (Angle_1 +
//  Angle_2) / 2, ou, invertendo,
//
//      Angle_1 = +flexao + abducao
//      Angle_2 = -flexao + abducao
//
//  O repositorio oficial nao escreve essa regra em lugar nenhum; ela sai
//  dos numeros acima. A conferencia de verdade e o dedo montado.
//
//  O que muda do SCS0009 para o MG90S
//  ----------------------------------
//  O Amazing Hand usa servo serial (SCS0009, 300 graus, posicao em
//  ticks) e calibra MiddlePos por servo. Aqui sao MG90S num PCA9685: o
//  "MiddlePos" vira `centro`, em graus absolutos do servo (90 = meio do
//  curso de 0 a 180), e o angulo vira largura de pulso.
//
//  Se o MG90S estiver montado espelhado em relacao ao SCS0009, o dedo
//  ESTENDE quando deveria fletir. `sentido = -1` troca os papeis dos dois
//  servos sem mexer em fio.
// =====================================================================
#pragma once
#include <stdint.h>

namespace limbia {

// Um dedo: dois servos, cada um com seu centro calibrado.
struct DedoParalelo {
  uint8_t canalA;  // servo que recebe Angle_1 no Amazing Hand
  uint8_t canalB;  // servo que recebe Angle_2
  float centroA;   // graus absolutos do servo A com o dedo em "zero"
  float centroB;   // idem, servo B
  int8_t sentido;  // +1 como no Amazing Hand; -1 se o dedo fletir ao contrario
};

// Faixa permitida, em graus do Amazing Hand (relativos ao centro).
struct LimitesParalelo {
  float flexaoMin;
  float flexaoMax;
  float abducaoMax;  // simetrico: [-abducaoMax, +abducaoMax]
  float servoMin;    // curso absoluto do servo, ultima barreira
  float servoMax;
};

struct ParServos {
  float a;       // graus absolutos do servo A
  float b;       // graus absolutos do servo B
  bool saturou;  // algum dos dois bateu em servoMin/servoMax
};

float limita(float x, float lo, float hi);

// Flexao e abducao (graus do Amazing Hand) -> angulo absoluto de cada
// servo. Limita flexao e abducao ANTES de converter, para que um pedido
// fora da faixa nunca vire torcao de um servo so.
ParServos paraleloAngulos(const DedoParalelo& d, float flexao, float abducao,
                          const LimitesParalelo& lim);

// Angulo absoluto do servo (0..180) -> largura de pulso, linear.
float servoGrausParaUs(float graus, float usEm0, float usEm180);

// Largura de pulso -> contagem do PCA9685 (12 bits por periodo).
uint16_t usParaTicksPca(float us, float freqPwmHz);

// Aproxima `atual` de `alvo` em no maximo `passoMax`. Movimento gradual
// sem bloquear: chamado uma vez por tick.
float rampa(float atual, float alvo, float passoMax);

// ---------------------------------------------------------------------
//  Ativacao muscular com histerese e tempo minimo
//
//  Um limiar so faz a mao tremer na fronteira: o envelope cruza o
//  limiar dez vezes por segundo e o dedo segue. Dois limiares (liga
//  acima, desliga abaixo) e um tempo minimo em cada lado resolvem.
// ---------------------------------------------------------------------
struct ParametrosAtivacao {
  float liga;        // envelope acima disso conta como contracao
  float desliga;     // abaixo disso conta como repouso (< liga)
  uint32_t ligaMs;   // tempo continuo acima de `liga` para ativar
  uint32_t soltaMs;  // tempo continuo abaixo de `desliga` para soltar
};

struct Ativacao {
  bool ativo;
  bool candidato;    // o que o sinal esta pedindo agora
  uint32_t desdeMs;  // quando `candidato` mudou
};

void ativacaoZera(Ativacao* a);

// Devolve true quando `ativo` muda.
bool ativacaoAtualiza(Ativacao* a, float envelope, uint32_t agoraMs, const ParametrosAtivacao& p);

// ---------------------------------------------------------------------
//  Repouso medido -> limiares
//
//  liga = media + max(kSigma * desvio, (razao - 1) * media)
//  desliga = meio caminho entre a media e `liga`
//
//  O termo do desvio cobre repouso com ruido grande; o da razao cobre
//  repouso muito liso, em que kSigma * desvio daria um limiar colado na
//  media e qualquer respiracao ligaria a mao.
// ---------------------------------------------------------------------
struct EstatRepouso {
  double soma;
  double soma2;
  uint32_t n;
};

void repousoZera(EstatRepouso* e);
void repousoInclui(EstatRepouso* e, float x);
float repousoMedia(const EstatRepouso& e);
float repousoDesvio(const EstatRepouso& e);
void limiaresDoRepouso(const EstatRepouso& e, float kSigma, float razao, float pisoLiga,
                       float* liga, float* desliga);

}  // namespace limbia

// limbia_paralelo - ver o cabecalho para a origem da regra dos dois servos.
#include "limbia_paralelo.h"

#include <math.h>
#include <string.h>

namespace limbia {

float limita(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

ParServos paraleloAngulos(const DedoParalelo& d, float flexao, float abducao,
                          const LimitesParalelo& lim) {
  const float f = limita(flexao, lim.flexaoMin, lim.flexaoMax);
  const float b = limita(abducao, -lim.abducaoMax, lim.abducaoMax);

  // Angle_1 = +flexao + abducao ; Angle_2 = -flexao + abducao.
  // sentido = -1 inverte so a flexao: a abducao continua sendo "os dois
  // para o mesmo lado", que nao depende de como o servo foi montado.
  const float s = d.sentido < 0 ? -1.0f : 1.0f;
  float a       = d.centroA + s * f + b;
  float c       = d.centroB - s * f + b;

  ParServos r;
  r.saturou = (a < lim.servoMin || a > lim.servoMax || c < lim.servoMin || c > lim.servoMax);
  r.a       = limita(a, lim.servoMin, lim.servoMax);
  r.b       = limita(c, lim.servoMin, lim.servoMax);
  return r;
}

float servoGrausParaUs(float graus, float usEm0, float usEm180) {
  return usEm0 + (usEm180 - usEm0) * (limita(graus, 0.0f, 180.0f) / 180.0f);
}

uint16_t usParaTicksPca(float us, float freqPwmHz) {
  const float periodoUs = 1e6f / freqPwmHz;
  float t               = us / periodoUs * 4096.0f + 0.5f;
  if (t < 0) t = 0;
  if (t > 4095) t = 4095;
  return (uint16_t)t;
}

float rampa(float atual, float alvo, float passoMax) {
  const float d = alvo - atual;
  if (d > passoMax) return atual + passoMax;
  if (d < -passoMax) return atual - passoMax;
  return alvo;
}

// ---------------------------------------------------------------------
void ativacaoZera(Ativacao* a) {
  memset(a, 0, sizeof(*a));
}

bool ativacaoAtualiza(Ativacao* a, float envelope, uint32_t agoraMs, const ParametrosAtivacao& p) {
  // Entre `desliga` e `liga` o pedido nao muda: e a faixa de histerese.
  bool pedido = a->candidato;
  if (envelope >= p.liga) pedido = true;
  if (envelope <= p.desliga) pedido = false;

  if (pedido != a->candidato) {
    a->candidato = pedido;
    a->desdeMs   = agoraMs;
  }
  if (a->candidato == a->ativo) return false;

  const uint32_t espera = a->candidato ? p.ligaMs : p.soltaMs;
  if ((uint32_t)(agoraMs - a->desdeMs) >= espera) {
    a->ativo = a->candidato;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------
void repousoZera(EstatRepouso* e) {
  memset(e, 0, sizeof(*e));
}

void repousoInclui(EstatRepouso* e, float x) {
  e->soma += x;
  e->soma2 += (double)x * x;
  e->n++;
}

float repousoMedia(const EstatRepouso& e) {
  return e.n ? (float)(e.soma / e.n) : 0.0f;
}

float repousoDesvio(const EstatRepouso& e) {
  if (e.n < 2) return 0.0f;
  const double m = e.soma / e.n;
  double v       = e.soma2 / e.n - m * m;
  return v > 0 ? (float)sqrt(v) : 0.0f;
}

void limiaresDoRepouso(const EstatRepouso& e, float kSigma, float razao, float pisoLiga,
                       float* liga, float* desliga) {
  const float m = repousoMedia(e);
  const float s = repousoDesvio(e);
  float folga   = kSigma * s;
  const float r = (razao - 1.0f) * m;
  if (r > folga) folga = r;
  if (m + folga < pisoLiga) folga = pisoLiga - m;
  *liga    = m + folga;
  *desliga = m + 0.5f * folga;
}

}  // namespace limbia

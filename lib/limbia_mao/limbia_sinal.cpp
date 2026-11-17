// limbia_sinal - ver o cabecalho para o porque de cada estagio.
#include "limbia_sinal.h"

#include <math.h>
#include <string.h>

namespace limbia {

// ---------------------------------------------------------------------
//  Hampel
// ---------------------------------------------------------------------
static void ordena5(float* v, uint8_t n) {
  for (uint8_t i = 1; i < n; i++) {
    const float x = v[i];
    int8_t j      = (int8_t)i - 1;
    while (j >= 0 && v[j] > x) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = x;
  }
}

void hampelZera(Hampel5* h) {
  memset(h, 0, sizeof(*h));
}

float hampelPassa(Hampel5* h, float x, float k, float piso) {
  h->b[h->i] = x;
  h->i       = (uint8_t)((h->i + 1) % 5);
  if (h->n < 5) {
    h->n++;
    return x;
  }
  float s[5];
  memcpy(s, h->b, sizeof(s));
  ordena5(s, 5);
  const float med = s[2];
  float d[5];
  for (uint8_t i = 0; i < 5; i++) d[i] = fabsf(h->b[i] - med);
  ordena5(d, 5);
  // 1,4826 * MAD estima o desvio-padrao de um sinal gaussiano.
  float limite = k * 1.4826f * d[2];
  if (limite < piso) limite = piso;
  if (fabsf(x - med) > limite) {
    h->trocadas++;
    return med;
  }
  return x;
}

// ---------------------------------------------------------------------
//  Filtro
// ---------------------------------------------------------------------
void filtroConfigura(FiltroEmg* f, const ParametrosFiltro& p) {
  memset(f, 0, sizeof(*f));
  f->bruto   = p.entradaBruta;
  f->comRede = p.redeHz > 0;
  biquadPassaAltas(&f->pa, p.passaAltasHz, p.fsHz);
  if (f->comRede) {
    biquadNotch(&f->n1, p.redeHz, p.notchQ, p.fsHz);
    biquadNotch(&f->n2, 2.0f * p.redeHz, p.notchQ, p.fsHz);
  }
  biquadPassaBaixas(&f->pb1, p.envelopeHz, p.fsHz);
  biquadPassaBaixas(&f->pb2, p.envelopeHz, p.fsHz);
  biquadPassaBaixas(&f->rap, p.rapidoHz, p.fsHz);
  hampelZera(&f->hampel);
}

void filtroPreaquece(FiltroEmg* f, float x, const ParametrosFiltro& p) {
  // Regime para um nivel constante: sem isso o primeiro segundo vira um
  // degrau enorme, que a cadeia leria como contracao.
  hampelZera(&f->hampel);
  for (uint8_t i = 0; i < 5; i++) hampelPassa(&f->hampel, x, p.hampelK, p.hampelPiso);
  f->hampel.trocadas = 0;
  // Na RAW o passa-altas tira o nivel e o resto da cadeia nasce em zero;
  // na SIG/ENV o nivel atravessa tudo.
  const float depois = f->bruto ? 0.0f : x;
  if (f->bruto) biquadPreaquece(&f->pa, x);
  biquadPreaquece(&f->n1, depois);
  biquadPreaquece(&f->n2, depois);
  biquadPreaquece(&f->pb1, depois);
  biquadPreaquece(&f->pb2, depois);
  biquadPreaquece(&f->rap, depois);
  f->rapido   = depois;
  f->iniciado = true;
}

float filtroPassa(FiltroEmg* f, float x, const ParametrosFiltro& p) {
  if (!f->iniciado) filtroPreaquece(f, x, p);

  float y        = hampelPassa(&f->hampel, x, p.hampelK, p.hampelPiso);
  f->ultimoLimpo = y;
  if (f->bruto) y = biquadPassa(&f->pa, y);
  if (f->comRede) {
    y = biquadPassa(&f->n1, y);
    y = biquadPassa(&f->n2, y);
  }
  if (f->bruto) y = fabsf(y);
  const float r = biquadPassa(&f->rap, y);
  f->rapido     = r < 0 ? 0 : r;
  y             = biquadPassa(&f->pb1, y);
  y             = biquadPassa(&f->pb2, y);
  return y < 0 ? 0 : y;
}

// ---------------------------------------------------------------------
//  Comando alternado
// ---------------------------------------------------------------------
void alternaZera(DetectorAlterna* d, bool fechada) {
  memset(d, 0, sizeof(*d));
  ativacaoZera(&d->a);
  d->fechada = fechada;
}

EventoAlterna alternaAtualiza(DetectorAlterna* d, float envelope, float rapido, uint32_t agoraMs,
                              const ParametrosAlterna& p) {
  // Duracao medida no envelope rapido, que nao espalha a pancada no tempo.
  if (rapido >= p.ativ.liga) {
    if (!d->rapidoAcima) {
      d->rapidoAcima   = true;
      d->rapidoDesdeMs = agoraMs;
    }
  } else {
    d->rapidoAcima = false;
  }

  const bool mudou = ativacaoAtualiza(&d->a, envelope, agoraMs, p.ativ);

  // Pico desde que o sinal pediu contracao: e com ele que se reconhece a
  // pancada no eletrodo, que sobe muito mais que qualquer musculo.
  if (d->a.candidato || d->a.ativo) {
    if (envelope > d->picoCandidato) d->picoCandidato = envelope;
  } else {
    d->picoCandidato = 0;
  }

  if (!mudou || !d->a.ativo) return ALT_NADA;  // so a SUBIDA troca o estado

  const bool curta      = !d->rapidoAcima || (uint32_t)(agoraMs - d->rapidoDesdeMs) < p.ativ.ligaMs;
  const bool refratario = d->jaTrocou && (uint32_t)(agoraMs - d->ultimaTrocaMs) < p.refratarioMs;
  const bool artefato   = p.tetoArtefato > 0 && d->picoCandidato > p.tetoArtefato;
  if (curta || refratario || artefato) {
    d->motivo = artefato ? REJ_PANCADA : (curta ? REJ_CURTA : REJ_REFRATARIO);
    d->rejeitadas++;
    return ALT_REJEITOU;
  }
  d->fechada       = !d->fechada;
  d->ultimaTrocaMs = agoraMs;
  d->jaTrocou      = true;
  return d->fechada ? ALT_FECHOU : ALT_ABRIU;
}

// ---------------------------------------------------------------------
//  Calibracao guiada, adaptativa e com prova
// ---------------------------------------------------------------------
void calInicia(CalibracaoGuiada* c, uint32_t agoraMs) {
  memset(c, 0, sizeof(*c));
  repousoZera(&c->rep);
  c->etapa        = CAL_PREPARO;
  c->etapaDesdeMs = agoraMs;
  c->motivo       = "";
}

void calCancela(CalibracaoGuiada* c) {
  c->etapa  = CAL_FALHOU;
  c->motivo = "cancelada";
}

bool calEmAndamento(const CalibracaoGuiada& c) {
  return c.etapa >= CAL_PREPARO && c.etapa <= CAL_PROVA_RELAXE;
}

static uint32_t duracao(EtapaCal e, const TemposCal& t) {
  switch (e) {
    case CAL_PREPARO: return t.preparoMs;
    case CAL_REPOUSO: return t.repousoMs;
    case CAL_CONTRAIA:
    case CAL_PROVA_CONTRAIA: return t.contraiaMs;
    case CAL_RELAXE:
    case CAL_PROVA_RELAXE: return t.relaxeMs;
    default: return 0;
  }
}

uint32_t calRestanteMs(const CalibracaoGuiada& c, uint32_t agoraMs, const TemposCal& t) {
  const uint32_t d  = duracao(c.etapa, t);
  const uint32_t dt = agoraMs - c.etapaDesdeMs;
  return dt >= d ? 0 : d - dt;
}

const char* calNomeEtapa(EtapaCal e) {
  switch (e) {
    case CAL_PARADA: return "parada";
    case CAL_PREPARO: return "preparo";
    case CAL_REPOUSO: return "repouso";
    case CAL_CONTRAIA: return "contraia";
    case CAL_RELAXE: return "relaxe";
    case CAL_PROVA_CONTRAIA: return "prova_contraia";
    case CAL_PROVA_RELAXE: return "prova_relaxe";
    case CAL_OK: return "ok";
    case CAL_FALHOU: return "falhou";
  }
  return "?";
}

const char* calNomeAvaliacao(uint8_t a) {
  switch (a) {
    case AVAL_BOA: return "boa";
    case AVAL_FRACA: return "fraca";
    case AVAL_NAO_RELAXOU: return "nao_relaxou";
    case AVAL_INCONSISTENTE: return "inconsistente";
    case AVAL_DETECTOU: return "detectou";
    case AVAL_PERDEU: return "perdeu";
    case AVAL_FALSO: return "falso";
    default: return "";
  }
}

static float desvioEfetivo(const CalibracaoGuiada& c) {
  // Desvio minimo de 1 contagem: repouso liso demais daria separacao
  // infinita com qualquer contracao de mentira.
  const float s = repousoDesvio(c.rep);
  return s < 1.0f ? 1.0f : s;
}

static void guardaBoa(CalibracaoGuiada* c, float pico, uint8_t maxBoas) {
  if (maxBoas > CAL_MAX_BOAS) maxBoas = CAL_MAX_BOAS;
  if (c->nBoas >= maxBoas) {
    for (uint8_t i = 1; i < c->nBoas; i++) c->boas[i - 1] = c->boas[i];
    c->nBoas--;
  }
  c->boas[c->nBoas++] = pico;
}

static bool consistentes(const CalibracaoGuiada& c, float maxRazao) {
  float lo = c.boas[0], hi = c.boas[0];
  for (uint8_t i = 1; i < c.nBoas; i++) {
    if (c.boas[i] < lo) lo = c.boas[i];
    if (c.boas[i] > hi) hi = c.boas[i];
  }
  return lo > 0 && hi / lo <= maxRazao;
}

// Calcula os limiares a partir das contracoes boas e arma a prova.
static void aprende(CalibracaoGuiada* c, const TemposCal& t) {
  ResultadoCal& r = c->r;
  r.repouso       = repousoMedia(c->rep);
  r.desvio        = repousoDesvio(c->rep);
  const float s   = desvioEfetivo(*c);

  float ord[CAL_MAX_BOAS];
  memcpy(ord, c->boas, sizeof(ord));
  ordena5(ord, c->nBoas);
  r.contracao = ord[c->nBoas / 2];
  r.separacao = (r.contracao - r.repouso) / s;
  r.boas      = c->nBoas;

  const float faixa = r.contracao - r.repouso;
  r.liga            = r.repouso + t.fracaoLiga * faixa;
  r.desliga         = r.repouso + t.fracaoDesliga * faixa;
  if (r.liga < r.repouso + 4.0f * s) r.liga = r.repouso + 4.0f * s;
  if (r.desliga < r.repouso + 2.0f * s) r.desliga = r.repouso + 2.0f * s;
  if (r.desliga >= r.liga) r.desliga = r.repouso + 0.5f * (r.liga - r.repouso);
  r.teto = r.repouso + t.tetoFator * faixa;

  c->pDet.ativ.liga    = r.liga;
  c->pDet.ativ.desliga = r.desliga;
  c->pDet.ativ.ligaMs  = t.ativaMs;
  c->pDet.ativ.soltaMs = t.soltaMs;
  c->pDet.refratarioMs = 0;  // na prova cada contracao pedida conta
  c->pDet.tetoArtefato = r.teto;
  r.acertos            = 0;
  r.falsos             = 0;
  c->provaCiclo        = 0;
}

static void falha(CalibracaoGuiada* c, const char* motivo) {
  c->etapa  = CAL_FALHOU;
  c->motivo = motivo;
}

bool calAtualiza(CalibracaoGuiada* c, float env, float rapido, uint32_t agoraMs,
                 const TemposCal& t) {
  if (!calEmAndamento(*c)) return false;
  const uint32_t dt = agoraMs - c->etapaDesdeMs;

  // Na prova o detector da mao roda o tempo todo, com os limiares novos.
  if (c->etapa == CAL_PROVA_CONTRAIA || c->etapa == CAL_PROVA_RELAXE) {
    const EventoAlterna ev = alternaAtualiza(&c->det, env, rapido, agoraMs, c->pDet);
    if (ev == ALT_FECHOU || ev == ALT_ABRIU) {
      if (c->etapa == CAL_PROVA_CONTRAIA) {
        c->detectouNaJanela = true;
      } else if (dt >= t.descarteMs) {
        c->r.falsos++;  // contracao vista com o braco relaxado
        c->avaliacao = AVAL_FALSO;
      }
    }
  }

  switch (c->etapa) {
    case CAL_PREPARO:
      if (dt < t.preparoMs) return false;
      c->etapa = CAL_REPOUSO;
      break;

    case CAL_REPOUSO:
      if (dt >= t.descarteMs) repousoInclui(&c->rep, env);
      if (dt < t.repousoMs) return false;
      c->etapa     = CAL_CONTRAIA;
      c->picoAtual = 0;
      break;

    case CAL_CONTRAIA:
      if (env > c->picoAtual) c->picoAtual = env;
      if (dt < t.contraiaMs) return false;
      c->etapa = CAL_RELAXE;
      break;

    case CAL_RELAXE: {
      if (dt < t.relaxeMs) return false;
      // Fim do relaxe: julga a contracao que acabou de acontecer.
      const float m = repousoMedia(c->rep);
      const float s = desvioEfetivo(*c);
      const float p = c->picoAtual;
      c->ciclo++;
      if (p < m + t.separacaoMin * s || (m > 0 && p < t.razaoMin * m)) {
        c->avaliacao = AVAL_FRACA;
        c->nBoas     = 0;  // "seguidas": uma fraca zera a sequencia
      } else if (env > m + 0.3f * (p - m)) {
        c->avaliacao = AVAL_NAO_RELAXOU;
        c->nBoas     = 0;
      } else {
        guardaBoa(c, p, t.boasMin);
        c->avaliacao = AVAL_BOA;
        if (c->nBoas >= t.boasMin && !consistentes(*c, t.consistenciaMax)) {
          c->avaliacao = AVAL_INCONSISTENTE;
          // Mantem so a mais recente: a sequencia recomeca a partir dela.
          c->boas[0] = p;
          c->nBoas   = 1;
        }
      }

      if (c->nBoas >= t.boasMin) {
        aprende(c, t);
        alternaZera(&c->det, false);
        c->detectouNaJanela = false;
        c->etapa            = CAL_PROVA_CONTRAIA;
      } else if (c->ciclo >= t.ciclosMax) {
        falha(c, c->avaliacao == AVAL_FRACA         ? "contracao nao se separa do repouso"
                 : c->avaliacao == AVAL_NAO_RELAXOU ? "o braco nao volta ao repouso"
                                                    : "contracoes irregulares");
        return true;
      } else {
        c->etapa     = CAL_CONTRAIA;
        c->picoAtual = 0;
      }
      break;
    }

    case CAL_PROVA_CONTRAIA:
      if (dt < t.contraiaMs) return false;
      if (c->detectouNaJanela) {
        c->r.acertos++;
        c->avaliacao = AVAL_DETECTOU;
      } else {
        c->avaliacao = AVAL_PERDEU;
      }
      c->detectouNaJanela = false;
      c->etapa            = CAL_PROVA_RELAXE;
      break;

    case CAL_PROVA_RELAXE:
      if (dt < t.relaxeMs) return false;
      // Terminou o relaxe com o detector ainda "contraido": nao soltou.
      if (c->det.a.ativo) {
        c->r.falsos++;
        c->avaliacao = AVAL_FALSO;
      }
      c->provaCiclo++;
      if (c->provaCiclo < t.provaCiclos) {
        c->etapa = CAL_PROVA_CONTRAIA;
        break;
      }
      c->r.provas++;
      if (c->r.acertos == t.provaCiclos && c->r.falsos == 0) {
        c->etapa  = CAL_OK;
        c->motivo = "";
        return true;
      }
      if (c->r.provas >= t.provasMax) {
        falha(c, c->r.falsos ? "a prova viu contracao com o braco relaxado"
                             : "a prova perdeu contracoes pedidas");
        return true;
      }
      // Volta a aprender, com o mesmo repouso. Sequencia zerada.
      c->nBoas     = 0;
      c->ciclo     = 0;
      c->picoAtual = 0;
      c->etapa     = CAL_CONTRAIA;
      break;

    default: return false;
  }
  c->etapaDesdeMs = agoraMs;
  return true;
}

}  // namespace limbia

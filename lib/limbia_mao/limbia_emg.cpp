// =====================================================================
//  limbia_emg - implementacao
// =====================================================================
#include "limbia_emg.h"

#include <math.h>
#include <string.h>

namespace limbia {

static const float PI_F = 3.14159265358979f;

static float prendeF(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static const char* const NOME_CLASSE[N_CLASSES_EMG] = {"repouso", "fechar", "abrir"};

const char* nomeDaClasse(uint8_t classe) {
  return classe < N_CLASSES_EMG ? NOME_CLASSE[classe] : "incerto";
}

// ---------------------------------------------------------------------
//  Biquad
// ---------------------------------------------------------------------
static void normaliza(Biquad* f, float b0, float b1, float b2, float a0, float a1, float a2) {
  f->b0 = b0 / a0;
  f->b1 = b1 / a0;
  f->b2 = b2 / a0;
  f->a1 = a1 / a0;
  f->a2 = a2 / a0;
  f->z1 = 0;
  f->z2 = 0;
}

void biquadPassaAltas(Biquad* f, float fc, float fs) {
  const float w0 = 2.0f * PI_F * fc / fs;
  const float c  = cosf(w0);
  const float al = sinf(w0) / (2.0f * 0.70710678f);
  normaliza(f, (1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
}

void biquadPassaBaixas(Biquad* f, float fc, float fs) {
  const float w0 = 2.0f * PI_F * fc / fs;
  const float c  = cosf(w0);
  const float al = sinf(w0) / (2.0f * 0.70710678f);
  normaliza(f, (1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
}

void biquadNotch(Biquad* f, float f0, float q, float fs) {
  const float w0 = 2.0f * PI_F * f0 / fs;
  const float c  = cosf(w0);
  const float al = sinf(w0) / (2.0f * q);
  normaliza(f, 1, -2 * c, 1, 1 + al, -2 * c, 1 - al);
}

void biquadZera(Biquad* f) {
  f->z1 = 0;
  f->z2 = 0;
}

float biquadPassa(Biquad* f, float x) {
  const float y = f->b0 * x + f->z1;
  f->z1         = f->b1 * x - f->a1 * y + f->z2;
  f->z2         = f->b2 * x - f->a2 * y;
  return y;
}

void biquadPreaquece(Biquad* f, float x) {
  // Regime para entrada constante: a saida vale ganho_DC * x, e os dois
  // estados sao os que a forma II transposta teria depois de ver x para
  // sempre. No passa-altas o ganho DC e zero - a saida nasce parada.
  const float den = 1.0f + f->a1 + f->a2;
  const float g   = den != 0 ? (f->b0 + f->b1 + f->b2) / den : 0;
  const float y   = g * x;
  f->z2           = f->b2 * x - f->a2 * y;
  f->z1           = f->b1 * x - f->a1 * y + f->z2;
}

float biquadGanhoMedido(const Biquad& modelo, float freq, float fs) {
  Biquad f = modelo;
  biquadZera(&f);
  // Um segundo para o transitorio morrer, um segundo medindo.
  const uint32_t n   = (uint32_t)fs;
  double somaEntrada = 0, somaSaida = 0;
  for (uint32_t i = 0; i < 2 * n; i++) {
    const float x = sinf(2.0f * PI_F * freq * (float)i / fs);
    const float y = biquadPassa(&f, x);
    if (i >= n) {
      somaEntrada += (double)x * x;
      somaSaida += (double)y * y;
    }
  }
  return somaEntrada > 0 ? (float)sqrt(somaSaida / somaEntrada) : 0;
}

// ---------------------------------------------------------------------
//  Cadeia
// ---------------------------------------------------------------------
void cadeiaConfigura(CadeiaEmg* c, const ParametrosCadeia& p) {
  memset(c, 0, sizeof(*c));
  c->bruto    = p.entradaBruta;
  c->comNotch = p.entradaBruta && p.notchHz > 0;
  biquadPassaAltas(&c->pa, p.passaAltasHz, p.fsHz);
  if (c->comNotch) biquadNotch(&c->notch, p.notchHz, p.notchQ, p.fsHz);
  biquadPassaBaixas(&c->pb, p.envelopeHz, p.fsHz);
  c->iniciada = false;
}

float cadeiaProcessa(CadeiaEmg* c, float x) {
  if (!c->iniciada) {
    if (c->bruto) {
      biquadPreaquece(&c->pa, x);
    } else {
      biquadPreaquece(&c->pb, x);
    }
    c->iniciada = true;
  }
  float y = x;
  if (c->bruto) {
    y = biquadPassa(&c->pa, y);
    if (c->comNotch) y = biquadPassa(&c->notch, y);
    y = y < 0 ? -y : y;
  }
  y = biquadPassa(&c->pb, y);
  // O passa-baixas de segunda ordem tem um lobulo negativo na resposta ao
  // impulso: entrada sempre positiva ainda pode dar saida um pouco abaixo
  // de zero. Envelope negativo nao existe.
  return y < 0 ? 0 : y;
}

float caracteristicaEmg(float envelope) {
  return logf(1.0f + (envelope < 0 ? 0 : envelope));
}

// ---------------------------------------------------------------------
//  Estatistica com esquecimento
// ---------------------------------------------------------------------
void estatZera(EstatClasse* e) {
  memset(e, 0, sizeof(*e));
}

void estatInclui(EstatClasse* e, const float x[N_CANAIS_EMG], float memoria) {
  const float lambda = memoria > 1.0f ? 1.0f - 1.0f / memoria : 0.0f;
  e->peso            = lambda * e->peso + 1.0f;
  const float a      = 1.0f / e->peso;

  float d[N_CANAIS_EMG];
  for (uint8_t i = 0; i < N_CANAIS_EMG; i++) {
    d[i] = x[i] - e->media[i];
    e->media[i] += a * d[i];
  }
  // Covariancia exponencialmente ponderada. Com memoria infinita (lambda
  // = 1) reduz a formula recursiva de Welford para a covariancia
  // populacional - o autoteste confere.
  for (uint8_t i = 0; i < N_CANAIS_EMG; i++) {
    for (uint8_t j = 0; j < N_CANAIS_EMG; j++) {
      e->cov[i][j] = (1.0f - a) * (e->cov[i][j] + a * d[i] * d[j]);
    }
  }
}

// ---------------------------------------------------------------------
//  LDA
// ---------------------------------------------------------------------
bool treinaModelo(const EstatClasse est[N_CLASSES_EMG], float pesoMinimo, float regularizacao,
                  ModeloEmg* m) {
  m->valido = 0;

  float pesoTotal = 0;
  for (uint8_t k = 0; k < N_CLASSES_EMG; k++) {
    if (est[k].peso < pesoMinimo) return false;
    pesoTotal += est[k].peso;
  }
  if (pesoTotal <= 0) return false;

  // Covariancia agrupada: o LDA supoe que as tres classes tem o mesmo
  // espalhamento e so diferem na media. Em escala log isso vale bem.
  float s[2][2] = {{0, 0}, {0, 0}};
  for (uint8_t k = 0; k < N_CLASSES_EMG; k++) {
    for (uint8_t i = 0; i < 2; i++) {
      for (uint8_t j = 0; j < 2; j++) s[i][j] += est[k].peso * est[k].cov[i][j];
    }
  }
  for (uint8_t i = 0; i < 2; i++) {
    for (uint8_t j = 0; j < 2; j++) s[i][j] /= pesoTotal;
  }
  s[0][0] += regularizacao;
  s[1][1] += regularizacao;

  const float det = s[0][0] * s[1][1] - s[0][1] * s[1][0];
  if (!(det > 1e-9f)) return false;

  m->covInv[0][0] = s[1][1] / det;
  m->covInv[1][1] = s[0][0] / det;
  m->covInv[0][1] = -s[0][1] / det;
  m->covInv[1][0] = -s[1][0] / det;

  // Priores iguais, de proposito: a calibracao passa mais tempo em
  // repouso do que em cada gesto, e deixar isso virar prior faria o
  // modelo preferir "repouso" so porque viu mais dele.
  for (uint8_t k = 0; k < N_CLASSES_EMG; k++) {
    for (uint8_t i = 0; i < 2; i++) m->media[k][i] = est[k].media[i];
    float quad = 0;
    for (uint8_t i = 0; i < 2; i++) {
      m->w[k][i] = m->covInv[i][0] * est[k].media[0] + m->covInv[i][1] * est[k].media[1];
      quad += est[k].media[i] * m->w[k][i];
    }
    m->w0[k] = -0.5f * quad;
  }
  m->valido = 1;
  return true;
}

uint8_t classificaEmg(const ModeloEmg& m, const float x[N_CANAIS_EMG], float prob[N_CLASSES_EMG]) {
  if (!m.valido) {
    if (prob) {
      for (uint8_t k = 0; k < N_CLASSES_EMG; k++) prob[k] = 1.0f / N_CLASSES_EMG;
    }
    return C_REPOUSO;
  }
  float g[N_CLASSES_EMG];
  uint8_t melhor = 0;
  for (uint8_t k = 0; k < N_CLASSES_EMG; k++) {
    g[k] = m.w[k][0] * x[0] + m.w[k][1] * x[1] + m.w0[k];
    if (g[k] > g[melhor]) melhor = k;
  }
  if (prob) {
    // softmax, subtraindo o maior para exp() nunca estourar
    float soma = 0;
    for (uint8_t k = 0; k < N_CLASSES_EMG; k++) {
      prob[k] = expf(g[k] - g[melhor]);
      soma += prob[k];
    }
    for (uint8_t k = 0; k < N_CLASSES_EMG; k++) prob[k] /= soma;
  }
  return melhor;
}

float distanciaEntreClasses(const ModeloEmg& m, uint8_t a, uint8_t b) {
  if (!m.valido || a >= N_CLASSES_EMG || b >= N_CLASSES_EMG) return 0;
  const float d0 = m.media[a][0] - m.media[b][0];
  const float d1 = m.media[a][1] - m.media[b][1];
  const float q  = d0 * (m.covInv[0][0] * d0 + m.covInv[0][1] * d1) +
                  d1 * (m.covInv[1][0] * d0 + m.covInv[1][1] * d1);
  return q > 0 ? sqrtf(q) : 0;
}

float acertoPrevisto(float delta) {
  // Phi(delta/2) = 0,5 * erfc(-delta / (2 * raiz de 2))
  return 0.5f * erfcf(-delta / (2.0f * 1.41421356f));
}

// ---------------------------------------------------------------------
//  Qualidade do eletrodo
// ---------------------------------------------------------------------
static const char* const TEXTO_DIAG[N_DIAGNOSTICOS] = {
    "aguardando", "posicionado corretamente", "sinal fraco",
    "saturando",  "repouso ruidoso",          "musculo trocado"};

const char* textoDoDiagnostico(uint8_t diag) {
  return diag < N_DIAGNOSTICOS ? TEXTO_DIAG[diag] : "?";
}

static float dPrime(const EstatClasse& a, const EstatClasse& b, uint8_t canal) {
  const float s = sqrtf(0.5f * (a.cov[canal][canal] + b.cov[canal][canal]) + 1e-6f);
  return (a.media[canal] - b.media[canal]) / s;
}

void avaliaEletrodo(const EstatClasse est[N_CLASSES_EMG], uint8_t canal, uint8_t ativa,
                    uint8_t oposta, float saturacao, const LimiaresQualidade& l,
                    QualidadeEletrodo* q) {
  const float pesoMin =
      est[ativa].peso < est[C_REPOUSO].peso ? est[ativa].peso : est[C_REPOUSO].peso;
  const float progresso = l.pesoMinimo > 0 ? prendeF(pesoMin / l.pesoMinimo, 0, 1) : 1;
  const float dp        = dPrime(est[ativa], est[C_REPOUSO], canal);
  const bool temOposto  = est[oposta].peso >= l.pesoMinimo;

  // A caracteristica e ln(1 + envelope), entao a diferenca das medias ja
  // e o log da razao entre os envelopes.
  const float lnRazao = est[ativa].media[canal] - est[C_REPOUSO].media[canal];
  const float lnSelet = temOposto ? est[ativa].media[canal] - est[oposta].media[canal] : 0.0f;

  q->dPrime       = dp;
  q->razao        = expf(lnRazao);
  q->seletividade = expf(lnSelet);

  const bool saturando = saturacao > l.saturacaoMax;
  const bool ruidoso   = est[C_REPOUSO].peso >= 1 && est[C_REPOUSO].media[canal] > l.repousoMax;
  const bool trocado   = temOposto && lnSelet <= 0.0f;
  const bool forte     = dp >= l.dPrimeOk && q->razao >= l.razaoOk;

  uint8_t diag;
  if (progresso < 1.0f) {
    diag = D_AGUARDANDO;
  } else if (saturando) {
    diag = D_SATURADO;
  } else if (ruidoso) {
    diag = D_RUIDO_REPOUSO;
  } else if (trocado) {
    // Responde mais ao gesto do outro eletrodo do que ao proprio: os dois
    // estao trocados, ou este esta do lado errado do antebraco.
    diag = D_MUSCULO_TROCADO;
  } else if (!forte) {
    diag = D_SINAL_FRACO;
  } else {
    diag = D_OK;
  }

  // Histerese: ganha o ok nos limiares "Ok", so perde abaixo dos
  // limiares "Perde". A razao usa a mesma proporcao do d'.
  const float folga = l.dPrimeOk > 0 ? l.dPrimePerde / l.dPrimeOk : 1.0f;
  if (q->ok) {
    q->ok = (progresso >= 1.0f) && !saturando && !ruidoso && !trocado && dp >= l.dPrimePerde &&
            q->razao >= l.razaoOk * folga;
  } else {
    q->ok = (diag == D_OK);
  }
  if (q->ok) diag = D_OK;
  q->diagnostico = diag;

  // A barra enche pelo que estiver pior das duas grandezas, e enche
  // exatamente no limiar. Em log, para a razao: ir de 1x a 2x vale tanto
  // quanto ir de 2x a 4x.
  const float pelaSeparacao = l.dPrimeOk > 0 ? prendeF(dp / l.dPrimeOk, 0, 1) : 1;
  const float pelaRazao =
      l.razaoOk > 1.0f ? prendeF((lnRazao > 0 ? lnRazao : 0) / logf(l.razaoOk), 0, 1) : 1;
  float nota = 100.0f * (pelaSeparacao < pelaRazao ? pelaSeparacao : pelaRazao) * progresso;
  if (q->ok) nota = 100;
  if (saturando || ruidoso) nota = nota > 40 ? 40 : nota;
  if (diag == D_MUSCULO_TROCADO) nota = nota > 25 ? 25 : nota;
  q->nota = (uint8_t)(nota + 0.5f);
}

// ---------------------------------------------------------------------
//  Placar
// ---------------------------------------------------------------------
void placarZera(Placar* p) {
  memset(p, 0, sizeof(*p));
}

void placarInclui(Placar* p, uint8_t verdade, uint8_t previsto, float memoria) {
  if (verdade >= N_CLASSES_EMG) return;
  const float lambda = memoria > 1.0f ? 1.0f - 1.0f / memoria : 0.0f;
  p->peso[verdade]   = lambda * p->peso[verdade] + 1.0f;
  const float a      = 1.0f / p->peso[verdade];
  const float acerto = (previsto == verdade) ? 1.0f : 0.0f;
  p->acerto[verdade] += a * (acerto - p->acerto[verdade]);
}

float placarPior(const Placar& p, float pesoMinimo) {
  float pior = 1.0f;
  for (uint8_t k = 0; k < N_CLASSES_EMG; k++) {
    const float confianca = pesoMinimo > 0 ? prendeF(p.peso[k] / pesoMinimo, 0, 1) : 1;
    const float v         = p.acerto[k] * confianca;
    if (v < pior) pior = v;
  }
  return pior;
}

// ---------------------------------------------------------------------
//  Protocolo
// ---------------------------------------------------------------------
static const char* const NOME_INSTRUCAO[N_INSTRUCOES] = {"prepare", "feche", "relaxe", "abra"};

const char* nomeDaInstrucao(uint8_t instrucao) {
  return instrucao < N_INSTRUCOES ? NOME_INSTRUCAO[instrucao] : "?";
}

PassoProtocolo passoDoProtocolo(uint32_t t, const TemposProtocolo& tp) {
  PassoProtocolo p;
  uint32_t dentro = 0;

  if (t < tp.preparoMs) {
    // O preparo e repouso de verdade: a pessoa esta com a mao relaxada
    // lendo a tela. Ja serve de amostra de repouso.
    p.instrucao  = I_PREPARE;
    p.rotulo     = C_REPOUSO;
    p.duracaoMs  = tp.preparoMs;
    p.ciclo      = 0;
    dentro       = t;
    p.restanteMs = (uint16_t)(tp.preparoMs - t);
  } else {
    // Um ciclo: FECHE, RELAXE, ABRA, RELAXE.
    const uint32_t c     = tp.contracaoMs;
    const uint32_t r     = tp.repousoMs;
    const uint32_t ciclo = 2 * (c + r);
    const uint32_t u     = t - tp.preparoMs;
    const uint32_t f     = u % ciclo;
    p.ciclo              = (uint16_t)(u / ciclo + 1);

    uint32_t inicio;
    if (f < c) {
      p.instrucao = I_FECHE;
      p.rotulo    = C_FECHAR;
      inicio      = 0;
      p.duracaoMs = (uint16_t)c;
    } else if (f < c + r) {
      p.instrucao = I_RELAXE;
      p.rotulo    = C_REPOUSO;
      inicio      = c;
      p.duracaoMs = (uint16_t)r;
    } else if (f < 2 * c + r) {
      p.instrucao = I_ABRA;
      p.rotulo    = C_ABRIR;
      inicio      = c + r;
      p.duracaoMs = (uint16_t)c;
    } else {
      p.instrucao = I_RELAXE;
      p.rotulo    = C_REPOUSO;
      inicio      = 2 * c + r;
      p.duracaoMs = (uint16_t)r;
    }
    dentro       = f - inicio;
    p.restanteMs = (uint16_t)(p.duracaoMs - dentro);
  }

  // Descarta a transicao do comeco e os ultimos 100 ms - a pessoa que ve
  // o relogio chegando ao fim antecipa o proximo gesto.
  p.rotulavel = dentro >= tp.transicaoMs && p.restanteMs > 100;
  return p;
}

// ---------------------------------------------------------------------
//  Decisor
// ---------------------------------------------------------------------
void decisorZera(Decisor* d, uint8_t comandoInicial) {
  memset(d->hist, C_INCERTO, sizeof(d->hist));
  d->n             = 0;
  d->idx           = 0;
  d->comando       = comandoInicial;
  d->jaTrocou      = false;
  d->ultimaTrocaMs = 0;
  d->trocas        = 0;
}

void decisorEsquece(Decisor* d) {
  memset(d->hist, C_INCERTO, sizeof(d->hist));
  d->n   = 0;
  d->idx = 0;
}

bool decisorAtualiza(Decisor* d, uint8_t classe, float prob, uint32_t agora,
                     const ParametrosDecisor& p) {
  const uint8_t janela = p.janela == 0 ? 1 : (p.janela > 16 ? 16 : p.janela);
  const uint8_t voto   = (prob >= p.probMin) ? classe : C_INCERTO;

  d->hist[d->idx] = voto;
  d->idx          = (uint8_t)((d->idx + 1) % janela);
  if (d->n < janela) d->n++;
  if (d->n < janela) return false;

  uint8_t fechar = 0, abrir = 0;
  for (uint8_t i = 0; i < janela; i++) {
    if (d->hist[i] == C_FECHAR) fechar++;
    if (d->hist[i] == C_ABRIR) abrir++;
  }

  uint8_t alvo = C_INCERTO;
  if (fechar >= p.votosMin && fechar > abrir) alvo = C_FECHAR;
  if (abrir >= p.votosMin && abrir > fechar) alvo = C_ABRIR;
  if (alvo == C_INCERTO || alvo == d->comando) return false;

  if (d->jaTrocou && (uint32_t)(agora - d->ultimaTrocaMs) < p.refratarioMs) return false;

  d->comando       = alvo;
  d->ultimaTrocaMs = agora;
  d->jaTrocou      = true;
  d->trocas++;
  return true;
}

}  // namespace limbia

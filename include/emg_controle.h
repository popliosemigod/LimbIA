// =====================================================================
//  LimbIA - emg_controle.h
//  Calibracao guiada, modos, decisao e o modelo gravado na flash.
//
//  Os dois modos
//  -------------
//  MODO AJUSTE - a protese nao obedece ao musculo. A tela conduz a
//    calibracao ("feche", "relaxe", "abra"), mostra a barra de cada
//    eletrodo indo de vermelho a verde, deixa ajustar as duas poses e o
//    nome e a senha da rede. OTA so e aceito aqui.
//
//  MODO PADRAO - a protese em uso. Cada janela de 50 ms e classificada,
//    o decisor filtra, e so uma intencao firme vira "abrir" ou "fechar".
//
//  Um botao so troca de um para o outro. Para entrar em padrao, uma de
//  duas: a calibracao em curso terminou com as tres barras verdes (e o
//  modelo novo e gravado), ou nao ha calibracao em curso e existe um
//  modelo gravado de antes. A placa liga em padrao se ha modelo gravado -
//  quem ja calibrou nao precisa calibrar de novo a cada manha.
//
//  Por que "candidato" e "modelo"
//  ------------------------------
//  Recalibrar nao pode estragar o que ja funcionava. A calibracao treina
//  um CANDIDATO; o modelo em uso so e substituido quando o candidato fica
//  verde e o usuario aperta o botao. Calibracao abandonada no meio deixa
//  o modelo antigo intacto.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include <limbia_emg.h>
#include <limbia_enlace.h>

#include "config_emg.h"
#include "emg_aquisicao.h"

namespace Controle {

// ---------------------------------------------------------------------
//  O modelo na flash
// ---------------------------------------------------------------------
struct BlocoModelo {
  uint8_t schema;
  uint8_t reservado[3];
  limbia::ModeloEmg modelo;
};

inline bool modeloCoerente(const limbia::ModeloEmg& m) {
  if (m.valido != 1) return false;
  const float* f = (const float*)&m;
  const size_t n = offsetof(limbia::ModeloEmg, valido) / sizeof(float);
  for (size_t i = 0; i < n; i++) {
    if (!isfinite(f[i])) return false;
  }
  return true;
}

inline bool salvaModelo() {
  Preferences p;
  if (!p.begin(NVS_EMG, false)) return false;
  BlocoModelo b;
  memset(&b, 0, sizeof(b));
  b.schema      = MODELO_SCHEMA;
  b.modelo      = E.modelo;
  const bool ok = p.putBytes("modelo", &b, sizeof(b)) == sizeof(b);
  p.end();
  E.modeloSalvo = ok;
  return ok;
}

inline bool carregaModelo() {
  memset(&E.modelo, 0, sizeof(E.modelo));
  E.modeloSalvo = false;
  Preferences p;
  if (!p.begin(NVS_EMG, true)) return false;
  BlocoModelo b;
  const bool lido =
      p.getBytesLength("modelo") == sizeof(b) && p.getBytes("modelo", &b, sizeof(b)) == sizeof(b);
  p.end();
  // Bloco de outra versao, ou com numero que nao e numero: recusa. Modelo
  // quebrado classificando musculo move mao para o lado errado.
  if (!lido || b.schema != MODELO_SCHEMA || !modeloCoerente(b.modelo)) return false;
  E.modelo      = b.modelo;
  E.modeloSalvo = true;
  return true;
}

inline void apagaModelo() {
  Preferences p;
  if (p.begin(NVS_EMG, false)) {
    p.clear();
    p.end();
  }
  memset(&E.modelo, 0, sizeof(E.modelo));
  E.modeloSalvo = false;
}

// ---------------------------------------------------------------------
//  Calibracao
// ---------------------------------------------------------------------
inline void iniciaCalibracao() {
  for (uint8_t k = 0; k < limbia::N_CLASSES_EMG; k++) limbia::estatZera(&E.est[k]);
  memset(&E.candidato, 0, sizeof(E.candidato));
  limbia::placarZera(&E.placar);
  memset(E.qual, 0, sizeof(E.qual));
  memset(E.saturacaoCal, 0, sizeof(E.saturacaoCal));
  E.acertoGeral = 0;
  E.notaGeral   = 0;
  E.geralOk     = false;
  E.inicioCalMs = millis();
  E.passo       = limbia::passoDoProtocolo(0, temposProtocolo());
  E.calibrando  = true;
}

inline void cancelaCalibracao() {
  E.calibrando = false;
}

inline bool calibracaoPronta() {
  return E.calibrando && E.qual[0].ok && E.qual[1].ok && E.geralOk;
}

// O botao "usar a protese" esta liberado?
inline bool podeUsar() {
  return calibracaoPronta() || (!E.calibrando && E.modelo.valido);
}

// O modelo que classifica agora: em uso, o gravado; em ajuste, o
// candidato assim que existir (e o que a tela mostra como "o que a
// protese entendeu"), senao o gravado.
inline const limbia::ModeloEmg* modeloAtivo() {
  if (E.modo == limbia::MODO_PADRAO) return E.modelo.valido ? &E.modelo : nullptr;
  if (E.calibrando && E.candidato.valido) return &E.candidato;
  return E.modelo.valido ? &E.modelo : nullptr;
}

// ---------------------------------------------------------------------
//  Acao para a mao
// ---------------------------------------------------------------------
inline void novaAcao(uint8_t acao) {
  E.acao = acao;
  E.seqAcao++;
}

// ---------------------------------------------------------------------
//  Modos
// ---------------------------------------------------------------------

// Devolve nullptr se entrou; senao o motivo, para a tela mostrar.
inline const char* entraPadrao() {
  if (E.modo == limbia::MODO_PADRAO) return nullptr;
  if (E.calibrando) {
    if (!calibracaoPronta()) {
      return "Termine a calibração (as três barras verdes) ou cancele para usar a anterior.";
    }
    E.modelo     = E.candidato;
    E.calibrando = false;
    if (!salvaModelo()) Serial.println(F("[emg] FALHA ao gravar o modelo na flash"));
  } else if (!E.modelo.valido) {
    return "Calibre os eletrodos primeiro.";
  }
  E.modo = limbia::MODO_PADRAO;
  // O decisor parte do que a mao esta fazendo agora: se o ultimo teste
  // da tela a deixou fechada, "fechar" nao pode ser o comando que falta.
  limbia::decisorZera(&E.decisor,
                      E.acao == limbia::ACAO_FECHAR ? limbia::C_FECHAR : limbia::C_ABRIR);
  E.eletrodoSolto = false;
  Serial.println(F("[modo] PADRAO - a protese obedece ao musculo"));
  return nullptr;
}

inline void entraAjuste() {
  if (E.modo == limbia::MODO_AJUSTE) return;
  E.modo = limbia::MODO_AJUSTE;
  Serial.println(F("[modo] AJUSTE - a protese nao obedece ao musculo"));
}

// ---------------------------------------------------------------------
//  Uma janela de 50 ms
// ---------------------------------------------------------------------
inline void processaJanela(const Aquisicao::Janela& j) {
  E.janelas++;
  float x[limbia::N_CANAIS_EMG];
  for (uint8_t ch = 0; ch < limbia::N_CANAIS_EMG; ch++) {
    E.envelope[ch]       = j.envelope[ch];
    x[ch]                = limbia::caracteristicaEmg(j.envelope[ch]);
    E.caracteristica[ch] = x[ch];
    E.saturacao[ch]      = j.amostras ? (float)j.saturadas[ch] / j.amostras : 0;
  }

  // ---- calibracao guiada ----
  if (E.calibrando) {
    E.passo = limbia::passoDoProtocolo(millis() - E.inicioCalMs, temposProtocolo());
    if (E.passo.rotulavel) {
      const uint8_t r = E.passo.rotulo;

      // Saturacao de cada eletrodo medida durante o gesto DELE.
      if (r == limbia::C_FECHAR) E.saturacaoCal[0] += (E.saturacao[0] - E.saturacaoCal[0]) * 0.05f;
      if (r == limbia::C_ABRIR) E.saturacaoCal[1] += (E.saturacao[1] - E.saturacaoCal[1]) * 0.05f;

      // Testa ANTES de treinar: o placar mede o modelo em dado que ele
      // ainda nao viu.
      if (E.candidato.valido) {
        float p[limbia::N_CLASSES_EMG];
        limbia::placarInclui(&E.placar, r, limbia::classificaEmg(E.candidato, x, p),
                             PLACAR_MEMORIA);
      }
      limbia::estatInclui(&E.est[r], x, QUAL_MEMORIA);
      limbia::treinaModelo(E.est, LDA_PESO_MINIMO, LDA_REGULARIZACAO, &E.candidato);

      const limbia::LimiaresQualidade l = limiaresQualidade();
      limbia::avaliaEletrodo(E.est, limbia::EMG_FLEXOR, limbia::C_FECHAR, limbia::C_ABRIR,
                             E.saturacaoCal[0], l, &E.qual[0]);
      limbia::avaliaEletrodo(E.est, limbia::EMG_EXTENSOR, limbia::C_ABRIR, limbia::C_FECHAR,
                             E.saturacaoCal[1], l, &E.qual[1]);

      E.acertoGeral = limbia::placarPior(E.placar, PLACAR_PESO_MIN);
      E.geralOk = E.geralOk ? E.acertoGeral >= QUAL_ACERTO_PERDE : E.acertoGeral >= QUAL_ACERTO_OK;
      const float n = 100.0f * E.acertoGeral / QUAL_ACERTO_OK;
      E.notaGeral   = E.geralOk ? 100 : (uint8_t)(n > 99 ? 99 : n);
    }
  }

  // ---- classificacao ao vivo ----
  const limbia::ModeloEmg* m = modeloAtivo();
  if (m) {
    E.classe = limbia::classificaEmg(*m, x, E.prob);
  } else {
    E.classe = limbia::C_REPOUSO;
    for (uint8_t k = 0; k < limbia::N_CLASSES_EMG; k++) E.prob[k] = 1.0f / limbia::N_CLASSES_EMG;
  }

  // ---- decisao, so em uso ----
  if (E.modo != limbia::MODO_PADRAO || !m) return;

  // Eletrodo solto ou cabo arrancado: o sinal bate no trilho e parece
  // contracao maxima. Nada se decide - a mao fica como esta.
  const bool solto = E.saturacao[0] > USO_SATURACAO_MAX || E.saturacao[1] > USO_SATURACAO_MAX;
  if (solto != E.eletrodoSolto) {
    E.eletrodoSolto = solto;
    Serial.println(solto ? F("[emg] ELETRODO SOLTO? sinal no trilho - nenhuma decisao")
                         : F("[emg] sinal de volta"));
  }
  if (solto) {
    limbia::decisorEsquece(&E.decisor);
    return;
  }

  if (limbia::decisorAtualiza(&E.decisor, E.classe, E.prob[E.classe], millis(),
                              parametrosDecisor())) {
    const uint8_t acao =
        E.decisor.comando == limbia::C_FECHAR ? limbia::ACAO_FECHAR : limbia::ACAO_ABRIR;
    novaAcao(acao);
    Serial.printf("[emg] %s  (%.0f%%)\n", limbia::nomeDaAcao(acao), 100.0f * E.prob[E.classe]);
  }
}

// Nivel de cada canal para os medidores da tela: 0 no repouso, 1 no
// patamar do gesto daquele eletrodo, pela calibracao. Sem modelo, uma
// escala log fixa - o suficiente para ver o eletrodo respondendo.
inline float nivel(uint8_t ch) {
  const limbia::ModeloEmg* m = modeloAtivo();
  const float x              = E.caracteristica[ch];
  float n;
  if (m) {
    const uint8_t ativa = ch == limbia::EMG_FLEXOR ? limbia::C_FECHAR : limbia::C_ABRIR;
    const float r       = m->media[limbia::C_REPOUSO][ch];
    const float a       = m->media[ativa][ch];
    n                   = (a - r) > 0.05f ? (x - r) / (a - r) : 0;
  } else {
    n = x / 7.0f;  // ln(1 + ~1100 contagens)
  }
  return n < 0 ? 0 : (n > 1.25f ? 1.25f : n);
}

}  // namespace Controle

// =====================================================================
//  LimbIA - config_emg.h
//  Pinagem, filtros, limiares e parametros da placa do EMG.
//  Nenhuma logica mora aqui.
//  Placa alvo: ESP32 DevKit V1 (a segunda da protese)
// =====================================================================
#pragma once
#include <Arduino.h>

#include <limbia_emg.h>
#include <limbia_enlace.h>

#include "config_rede.h"

// ---------------------------------------------------------------------
//  Pinos - a regra do ADC1 vale aqui tambem
//
//  O Wi-Fi fica ligado o tempo todo nesta placa (ela e o ponto de acesso),
//  entao o ADC2 esta fora de questao. Os dois eletrodos vao em pinos de
//  ADC1 que sao so entrada.
// ---------------------------------------------------------------------
#define PIN_EMG_FLEXOR   34  // ADC1_CH6 - eletrodo 1, face da palma
#define PIN_EMG_EXTENSOR 35  // ADC1_CH7 - eletrodo 2, face do dorso
#define PIN_LED_PLACA    2   // pisca em ajuste, aceso em padrao
#define PIN_BOTAO        0   // BOOT: segurar 10 s volta a rede ao de fabrica

// ---------------------------------------------------------------------
//  O SENSOR
//
//  Nao ha sensor de EMG no estoque. A cadeia aceita os dois tipos de
//  modulo que existem no mercado:
//
//  EMG_ENTRADA_BRUTA 1 - sinal cru, oscilando no meio da escala: MyoWare
//    2.0 na saida RAW, BioAmp EXG Pill, placas com AD8232. O firmware
//    filtra tudo. E o recomendado: o filtro fica sob controle, e a
//    frequencia de amostragem de 1 kHz ve o sinal inteiro.
//  EMG_ENTRADA_BRUTA 0 - envelope pronto: MyoWare na saida ENV, "Muscle
//    Sensor V3". O modulo ja retificou; aqui so se suaviza.
//
//  ALIMENTAR O MODULO EM 3,3 V. Em 5 V a saida passa do teto do ADC e o
//  pino morre calado, lendo valor fixo - mesma licao do ACS712.
// ---------------------------------------------------------------------
#define EMG_ENTRADA_BRUTA 1

#define EMG_FS_HZ          1000   // amostragem por canal
#define EMG_PASSA_ALTAS_HZ 20.0f  // abaixo disso e artefato de movimento, nao musculo
#define EMG_NOTCH_HZ       60.0f  // rede eletrica brasileira
#define EMG_NOTCH_Q        8.0f   // largura de ~7,5 Hz: tira a rede, deixa o musculo
#define EMG_ENVELOPE_HZ    4.0f   // intencao de abrir/fechar nao muda mais rapido que isso

// Uma caracteristica (media do envelope) a cada janela. 50 ms = 20
// classificacoes por segundo: bem mais rapido do que a mao consegue
// executar, e lento o bastante para cada janela ter 50 amostras.
#define EMG_JANELA_MS 50

// Amostra a menos de 16 contagens de 0 ou de 4095 esta no trilho: o
// sinal real passou do que o ADC mede.
#define EMG_TRILHO_BAIXO 16
#define EMG_TRILHO_ALTO  4079

// ---------------------------------------------------------------------
//  QUALIDADE DO ELETRODO - a barra que vai de vermelho a verde
//
//  ATENCAO: estes numeros sao PONTO DE PARTIDA, nao medida. Vieram de
//  sinal sintetico (autoteste, secao [7]). Dependem do sensor, da pele e
//  de quem usa. Calibrar com o primeiro sensor e commitar com tipo calib.
// ---------------------------------------------------------------------
#define QUAL_DPRIME_OK    2.5f   // d' para "posicionado corretamente" (a barra enche aqui)
#define QUAL_DPRIME_PERDE 2.0f   // histerese: so perde o verde abaixo disso
#define QUAL_RAZAO_OK     3.0f   // musculo pelo menos 3x acima do repouso
#define QUAL_SATURACAO    0.02f  // 2% das amostras do gesto no trilho ja e sinal cortado
#define QUAL_REPOUSO_MAX  4.0f   // ln(1+env): repouso acima de ~54 contagens e mau contato

// Memoria da estatistica, em amostras efetivas (20 por segundo). ~3
// ciclos do protocolo: e o que faz a barra reagir quando o eletrodo muda
// de lugar, em vez de arrastar o lugar antigo para sempre.
#define QUAL_MEMORIA     120.0f
#define QUAL_PESO_MINIMO 60.0f  // amostras uteis de cada gesto antes de dar nota

// Calibracao geral: o pior acerto entre repouso, fechar e abrir, medido
// em amostras que o modelo ainda nao tinha visto.
#define QUAL_ACERTO_OK    0.90f
#define QUAL_ACERTO_PERDE 0.85f
#define PLACAR_MEMORIA    90.0f
#define PLACAR_PESO_MIN   40.0f

#define LDA_REGULARIZACAO 0.02f  // soma na diagonal da covariancia (unidades de log^2)
#define LDA_PESO_MINIMO   10.0f  // amostras por classe antes de o modelo existir

// ---------------------------------------------------------------------
//  O PROTOCOLO GUIADO - "feche", "relaxe", "abra", "relaxe"
// ---------------------------------------------------------------------
#define PROT_PREPARO_MS   4000  // repouso inicial, lendo a tela
#define PROT_CONTRACAO_MS 3000
#define PROT_REPOUSO_MS   2500
#define PROT_TRANSICAO_MS 700  // tempo de ler, reagir e o musculo chegar ao patamar

// ---------------------------------------------------------------------
//  O DECISOR - o que impede a mao de ser erratica
//
//  5 de 6 votos com probabilidade >= 0,80: a intencao precisa durar
//  ~250 ms para virar comando. Depois de uma troca, 800 ms sem outra.
// ---------------------------------------------------------------------
#define DEC_JANELA     6
#define DEC_VOTOS_MIN  5
#define DEC_PROB_MIN   0.80f
#define DEC_REFRATARIO 800

// Com a protese em uso, saturacao acima disso numa janela e eletrodo
// solto ou cabo arrancado: o sinal bate no trilho e parece contracao
// maxima. Nesse caso nao se decide nada - a mao fica como esta.
#define USO_SATURACAO_MAX 0.20f

// ---------------------------------------------------------------------
//  PERSISTENCIA
// ---------------------------------------------------------------------
#define NVS_EMG       "limbia_emg"
#define MODELO_SCHEMA 1

#define INTERVALO_SERIAL_EMG_MS 2000

#ifndef LIMBIA_VERSAO
#define LIMBIA_VERSAO "0.2.0-dev"
#endif

// ---------------------------------------------------------------------
//  Os parametros, montados nas structs que a biblioteca espera
// ---------------------------------------------------------------------
inline limbia::ParametrosCadeia parametrosCadeia() {
  limbia::ParametrosCadeia p;
  p.fsHz         = (float)EMG_FS_HZ;
  p.entradaBruta = EMG_ENTRADA_BRUTA != 0;
  p.passaAltasHz = EMG_PASSA_ALTAS_HZ;
  p.notchHz      = EMG_NOTCH_HZ;
  p.notchQ       = EMG_NOTCH_Q;
  p.envelopeHz   = EMG_ENVELOPE_HZ;
  return p;
}

inline limbia::LimiaresQualidade limiaresQualidade() {
  limbia::LimiaresQualidade l;
  l.dPrimeOk     = QUAL_DPRIME_OK;
  l.dPrimePerde  = QUAL_DPRIME_PERDE;
  l.razaoOk      = QUAL_RAZAO_OK;
  l.pesoMinimo   = QUAL_PESO_MINIMO;
  l.saturacaoMax = QUAL_SATURACAO;
  l.repousoMax   = QUAL_REPOUSO_MAX;
  return l;
}

inline limbia::TemposProtocolo temposProtocolo() {
  limbia::TemposProtocolo t;
  t.preparoMs   = PROT_PREPARO_MS;
  t.contracaoMs = PROT_CONTRACAO_MS;
  t.repousoMs   = PROT_REPOUSO_MS;
  t.transicaoMs = PROT_TRANSICAO_MS;
  return t;
}

inline limbia::ParametrosDecisor parametrosDecisor() {
  limbia::ParametrosDecisor p;
  p.janela       = DEC_JANELA;
  p.votosMin     = DEC_VOTOS_MIN;
  p.probMin      = DEC_PROB_MIN;
  p.refratarioMs = DEC_REFRATARIO;
  return p;
}

// ---------------------------------------------------------------------
//  ESTADO GLOBAL DA PLACA DO EMG
//  Fonte unica da verdade: serial, tela e enlace leem daqui.
// ---------------------------------------------------------------------
// Estado de algo que precisa da confirmacao da mao para valer: a troca
// de nome e senha da rede (as duas placas precisam aprender a senha nova
// JUNTAS, senao a mao fica fora da rede) e a gravacao das poses.
enum Espera : uint8_t {
  ESPERA_NADA = 0,
  ESPERA_PENDENTE,  // enviado, aguardando a mao confirmar
  ESPERA_OK,        // a mao confirmou
  ESPERA_FALHOU     // a mao nao confirmou a tempo, ou recusou; nada mudou
};

struct EstadoEmg {
  uint8_t modo;  // limbia::ModoProtese

  // sinal, janela a janela
  float envelope[limbia::N_CANAIS_EMG];
  float caracteristica[limbia::N_CANAIS_EMG];
  float saturacao[limbia::N_CANAIS_EMG];     // fracao, ultima janela
  float saturacaoCal[limbia::N_CANAIS_EMG];  // media durante o gesto de cada eletrodo
  uint32_t janelas;
  uint32_t janelasPerdidas;

  // calibracao
  bool calibrando;
  uint32_t inicioCalMs;
  limbia::PassoProtocolo passo;
  limbia::EstatClasse est[limbia::N_CLASSES_EMG];
  limbia::ModeloEmg candidato;  // em treino, nesta calibracao
  limbia::Placar placar;
  limbia::QualidadeEletrodo qual[limbia::N_CANAIS_EMG];
  float acertoGeral;  // pior acerto entre as tres classes, fora do treino
  uint8_t notaGeral;  // barra geral: 100 = verde
  bool geralOk;

  // o modelo em uso
  limbia::ModeloEmg modelo;
  bool modeloSalvo;

  // classificacao e decisao
  uint8_t classe;
  float prob[limbia::N_CLASSES_EMG];
  limbia::Decisor decisor;
  bool eletrodoSolto;

  // o que vai para a mao
  uint8_t acao;
  uint16_t seqAcao;

  // o que vem da mao
  bool maoLigada;
  uint32_t ultimaTelemetriaMs;
  limbia::PacoteTelemetria mao;

  // o que espera confirmacao da mao
  uint8_t trocaRede;     // Espera
  uint8_t gravaPoses;    // Espera
  uint32_t reiniciarEm;  // 0 = nada agendado
};

extern EstadoEmg E;

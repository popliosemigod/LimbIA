// =====================================================================
//  LimbIA - main_autoteste.cpp
//
//  Roda na placa SEM NADA LIGADO NELA. Exercita toda a logica de
//  limbia_mao contra casos sinteticos e imprime numeros.
//
//  Por que isto existe: a mao ainda nao esta montada. Sem ela, dizer que
//  o firmware "funciona" seria afirmacao sem lastro. O que da para
//  provar hoje e que a matematica de pulso fecha, que a tabela de gestos
//  bate com o caderno do INOVAWEEK e que o classificador de preensao
//  acerta a forma do objeto - e isso da para provar aqui, medido.
//
//  Rodar o teste na placa em que o codigo vai ser executado elimina o
//  erro classico de o algoritmo passar no notebook e errar no
//  microcontrolador, onde int tem outro tamanho e float e mais lento.
//
//  Da secao [6] em diante o assunto e o EMG: filtros, a nota de cada
//  eletrodo, o classificador e o decisor, exercitados contra um usuario
//  SINTETICO - nao ha eletrodo no estoque. O que se prova aqui e que a
//  logica separa os casos que o gerador define; se o gerador se parece
//  com um antebraco de verdade e pergunta para o primeiro ensaio.
//
//  Gravar:   pio run -e autoteste -t upload
//  Ver:      pio device monitor -e autoteste
// =====================================================================

#include <Arduino.h>

#include <limbia_emg.h>
#include <limbia_enlace.h>
#include <limbia_mao.h>
#include <math.h>
#include <string.h>

// So os parametros: o autoteste exercita os MESMOS limiares que o
// firmware do EMG usa. Testar valores diferentes dos que rodam na
// protese nao provaria nada sobre a protese.
#include "config_emg.h"

using namespace limbia;

// ---------------------------------------------------------------------
//  Placar
// ---------------------------------------------------------------------
static uint16_t passou = 0, falhou = 0;

static void checa(bool condicao, const char* nome) {
  if (condicao) {
    passou++;
  } else {
    falhou++;
    Serial.printf("  FALHOU: %s\n", nome);
  }
}

// ---------------------------------------------------------------------
//  Tamanho do banco de EMG
//
//  O ESP32-C3 nao tem unidade de ponto flutuante: cada multiplicacao da
//  cadeia de filtros vira rotina de biblioteca. O banco completo, que a
//  DevKit V1 roda em 44 s, passa de dez minutos la - e bancada parada
//  esperando teste e teste que ninguem roda.
//
//  No C3 o banco encolhe. As verificacoes sao AS MESMAS; o que muda e
//  quantos casos cada uma ve. O numero que interessa medir no C3 e o da
//  secao [11] - o custo por amostra, porque e o C3 que filtra o EMG a
//  1 kHz de verdade.
// ---------------------------------------------------------------------
#if CONFIG_IDF_TARGET_ESP32C3
#define AUTOTESTE_CICLOS 3
#define AUTOTESTE_GESTOS 12
#else
#define AUTOTESTE_CICLOS 6
#define AUTOTESTE_GESTOS 60
#endif

// Gerador deterministico: o mesmo banco em toda execucao, entao dois
// resultados diferentes significam mudanca no codigo, nao no sorteio.
static uint32_t semente = 20260909u;
static uint32_t aleatorio() {
  semente = semente * 1664525u + 1013904223u;
  return semente >> 8;
}
static int32_t entre(int32_t lo, int32_t hi) {
  if (hi <= lo) return lo;
  return lo + (int32_t)(aleatorio() % (uint32_t)(hi - lo + 1));
}

// ---------------------------------------------------------------------
//  1. Matematica de pulso
// ---------------------------------------------------------------------
static void testePulso() {
  Serial.println(F("\n[1] conversao posicao <-> pulso"));

  Calibracao normal = {1000, 2000};
  checa(pulsoDePerMil(normal, 0) == 1000, "normal: 0 -> 1000 us");
  checa(pulsoDePerMil(normal, 1000) == 2000, "normal: 1000 -> 2000 us");
  checa(pulsoDePerMil(normal, 500) == 1500, "normal: 500 -> 1500 us");

  // O caso que motiva guardar endpoints medidos em vez de min/max: o
  // polegar do LAD tem repouso MAIOR que trabalho.
  Calibracao invertida = {2300, 1026};
  checa(pulsoDePerMil(invertida, 0) == 2300, "invertida: 0 -> 2300 us");
  checa(pulsoDePerMil(invertida, 1000) == 1026, "invertida: 1000 -> 1026 us");
  const uint16_t meio = pulsoDePerMil(invertida, 500);
  checa(meio > 1600 && meio < 1700, "invertida: meio do curso entre os dois");

  // Ida e volta, nos dois sentidos.
  uint16_t piorErro = 0;
  for (uint16_t p = 0; p <= 1000; p += 25) {
    const uint16_t erroN =
        (uint16_t)abs((int)perMilDePulso(normal, pulsoDePerMil(normal, p)) - (int)p);
    const uint16_t erroI =
        (uint16_t)abs((int)perMilDePulso(invertida, pulsoDePerMil(invertida, p)) - (int)p);
    if (erroN > piorErro) piorErro = erroN;
    if (erroI > piorErro) piorErro = erroI;
  }
  Serial.printf("     pior erro de ida e volta: %u por mil\n", piorErro);
  checa(piorErro <= 3, "ida e volta preserva a posicao");

  // Nenhum pulso pode sair da faixa segura, nem com calibracao absurda.
  Calibracao absurda = {200, 3000};
  bool dentro        = true;
  for (uint16_t p = 0; p <= 1000; p += 50) {
    const uint16_t us = pulsoDePerMil(absurda, p);
    if (us < PULSO_MIN_US || us > PULSO_MAX_US) dentro = false;
  }
  checa(dentro, "pulso sempre dentro de 500..2500 us");

  checa(!calibracaoValida(Calibracao{1500, 1500}), "recusa curso zero");
  checa(!calibracaoValida(Calibracao{1500, 1580}), "recusa curso de 80 us");
  checa(calibracaoValida(Calibracao{2300, 1026}), "aceita curso invertido valido");
}

// ---------------------------------------------------------------------
//  2. Tabela de gestos
// ---------------------------------------------------------------------
static void testeGestos() {
  Serial.println(F("\n[2] tabela de gestos"));

  bool faixaOk = true;
  for (uint8_t g = 0; g < N_GESTOS; g++) {
    const Pose& p = poseDoGesto(g);
    for (uint8_t j = 0; j < N_JUNTAS; j++) {
      if (p.alvo[j] > 1000) faixaOk = false;
    }
  }
  checa(faixaOk, "todos os alvos em 0..1000");

  // O sinal da paz, conferido contra a tabela do caderno do INOVAWEEK:
  // MINDY e DONCARE tensionados 100%, FEIO e JULGADOR relaxados, DEDAO
  // tensionado, PULSO em 50%.
  const Pose& paz = poseDoGesto(G_PAZ);
  checa(paz.alvo[MINDY] == 1000, "paz: mindinho fechado");
  checa(paz.alvo[DONCARE] == 1000, "paz: anelar fechado");
  checa(paz.alvo[FEIO] == 0, "paz: medio estendido");
  checa(paz.alvo[JULGADOR] == 0, "paz: indicador estendido");
  checa(paz.alvo[DEDAO] == 1000, "paz: polegar fechado");
  checa(paz.alvo[PULSO] == 500, "paz: punho neutro");

  // A mao aberta nao pode ter nenhum dedo longo puxado.
  const Pose& abrir = poseDoGesto(G_ABRIR);
  bool tudoSolto    = true;
  for (uint8_t j = MINDY; j <= JULGADOR; j++) {
    if (abrir.alvo[j] != 0) tudoSolto = false;
  }
  checa(tudoSolto, "abrir: nenhum dedo longo tensionado");

  // O que separa fechar de fechar_v2 e so o polegar.
  const Pose& f1 = poseDoGesto(G_FECHAR);
  const Pose& f2 = poseDoGesto(G_FECHAR_V2);
  checa(f1.alvo[DEDAO_ABD] != f2.alvo[DEDAO_ABD], "fechar x fechar_v2 diferem na abducao");
}

// ---------------------------------------------------------------------
//  3. Classificacao de preensao
// ---------------------------------------------------------------------
static void montaAssinatura(AssinaturaPreensao* a, const uint16_t contatos[4], bool tocou,
                            uint16_t forca, uint8_t mascara) {
  for (uint8_t i = 0; i < N_JUNTAS; i++) {
    a->contatoEm[i] = 1000;
    a->forcaMa[i]   = 0;
    a->tocou[i]     = false;
  }
  for (uint8_t i = 0; i < 4; i++) {
    a->contatoEm[i] = contatos[i];
    a->tocou[i]     = tocou;
    a->forcaMa[i]   = tocou ? forca : 40;
  }
  a->comSensor = mascara;
}

static void geraCaso(uint8_t alvo, uint16_t contatos[4]) {
  switch (alvo) {
    case OBJ_CILINDRICO: {
      const int32_t centro = entre(420, 640);
      for (uint8_t i = 0; i < 4; i++) contatos[i] = (uint16_t)(centro + entre(-45, 45));
      break;
    }
    case OBJ_FINO: {
      const int32_t centro = entre(850, 950);
      for (uint8_t i = 0; i < 4; i++) contatos[i] = (uint16_t)(centro + entre(-40, 40));
      break;
    }
    case OBJ_PLANO: {
      // Superficie chata: cada dedo para numa profundidade diferente.
      const int32_t base  = entre(300, 380);
      const int32_t passo = entre(110, 150);
      for (uint8_t i = 0; i < 4; i++) contatos[i] = (uint16_t)(base + passo * i + entre(-20, 20));
      break;
    }
    case OBJ_GRANDE: {
      const int32_t centro = entre(110, 210);
      for (uint8_t i = 0; i < 4; i++) contatos[i] = (uint16_t)(centro + entre(-35, 35));
      break;
    }
    default:
      for (uint8_t i = 0; i < 4; i++) contatos[i] = 1000;
      break;
  }
}

static void testePreensao() {
  Serial.println(F("\n[3] classificacao de preensao"));

  const uint8_t classes[]    = {OBJ_CILINDRICO, OBJ_FINO, OBJ_PLANO, OBJ_GRANDE};
  const uint8_t N_POR_CLASSE = 60;
  const uint8_t mascaraCheia =
      LIMBIA_BIT(MINDY) | LIMBIA_BIT(DONCARE) | LIMBIA_BIT(FEIO) | LIMBIA_BIT(JULGADOR);

  uint16_t total = 0, acertos = 0;
  uint32_t somaConfianca = 0;

  Serial.println(F("     classe            n   acertos   confianca media"));
  for (uint8_t c = 0; c < 4; c++) {
    uint16_t acertoClasse = 0;
    uint32_t confClasse   = 0;
    for (uint8_t k = 0; k < N_POR_CLASSE; k++) {
      uint16_t contatos[4];
      geraCaso(classes[c], contatos);
      AssinaturaPreensao a;
      montaAssinatura(&a, contatos, true, (uint16_t)entre(450, 800), mascaraCheia);

      uint8_t conf         = 0;
      const uint8_t obtido = classificaPreensao(a, &conf);
      if (obtido == classes[c]) acertoClasse++;
      confClasse += conf;
      total++;
    }
    acertos += acertoClasse;
    somaConfianca += confClasse;
    Serial.printf("     %-16s %3u   %3u/%-3u   %u%%\n", nomeDoObjeto(classes[c]), N_POR_CLASSE,
                  acertoClasse, N_POR_CLASSE, (unsigned)(confClasse / N_POR_CLASSE));
  }

  const uint16_t pct = (uint16_t)((acertos * 100UL) / total);
  Serial.printf("     TOTAL: %u/%u = %u%%  |  confianca media %u%%\n", acertos, total, pct,
                (unsigned)(somaConfianca / total));
  checa(pct >= 90, "acerto do classificador >= 90%");

  // -------------------------------------------------------------------
  //  Casos de fronteira
  //
  //  Os 100% acima medem o gerador tanto quanto o classificador: as
  //  quatro classes nasceram bem separadas, entao acertar era o esperado.
  //  O numero que informa alguma coisa e este: assinaturas colocadas DE
  //  PROPOSITO em cima das fronteiras de decisao.
  //
  //  Nao se espera acerto alto aqui - espera-se que a CONFIANCA CAIA.
  //  Classificador que erra dizendo 90% e pior que classificador que
  //  erra dizendo 55%, porque o primeiro engana quem le.
  // -------------------------------------------------------------------
  {
    Serial.println(F("     --- casos de fronteira (ambiguos de proposito) ---"));
    uint16_t nFront = 0, acertoFront = 0;
    uint32_t confFront = 0;
    for (uint8_t k = 0; k < 40; k++) {
      uint16_t contatos[4];
      uint8_t esperado;
      // Fronteira fino/cilindrico (media ~800) ou grande/cilindrico (~250)
      if (k % 2 == 0) {
        const int32_t centro = entre(780, 820);
        for (uint8_t i = 0; i < 4; i++) contatos[i] = (uint16_t)(centro + entre(-25, 25));
        esperado = (centro >= 800) ? OBJ_FINO : OBJ_CILINDRICO;
      } else {
        const int32_t centro = entre(230, 270);
        for (uint8_t i = 0; i < 4; i++) contatos[i] = (uint16_t)(centro + entre(-25, 25));
        esperado = (centro <= 250) ? OBJ_GRANDE : OBJ_CILINDRICO;
      }
      AssinaturaPreensao a;
      montaAssinatura(&a, contatos, true, (uint16_t)entre(450, 800), mascaraCheia);
      uint8_t conf         = 0;
      const uint8_t obtido = classificaPreensao(a, &conf);
      if (obtido == esperado) acertoFront++;
      confFront += conf;
      nFront++;
    }
    const uint16_t pctFront       = (uint16_t)((acertoFront * 100UL) / nFront);
    const uint16_t confMediaFront = (uint16_t)(confFront / nFront);
    Serial.printf("     fronteira: %u/%u = %u%%  |  confianca media %u%%\n", acertoFront, nFront,
                  pctFront, confMediaFront);
    // O resultado que importa: a confianca precisa ser MENOR na
    // fronteira do que no meio das classes. Se nao for, o numero de
    // confianca nao esta medindo nada.
    checa(confMediaFront < (somaConfianca / total), "confianca cai nos casos de fronteira");
  }

  // Mao vazia com o polegar instrumentado: resposta confiante e correta.
  {
    uint16_t vazio[4] = {1000, 1000, 1000, 1000};
    AssinaturaPreensao a;
    montaAssinatura(&a, vazio, false, 0, mascaraCheia | LIMBIA_BIT(DEDAO));
    uint8_t conf = 0;
    checa(classificaPreensao(a, &conf) == OBJ_NENHUM, "mao vazia reconhecida");
    checa(conf >= 80, "mao vazia com polegar instrumentado: confianca alta");
  }

  // O MESMO caso, mas sem sensor no polegar - que e o hardware de hoje.
  // A resposta continua "nada na mao", porem com confianca menor: o
  // polegar pode estar prendendo algo fino contra a palma sem que
  // ninguem veja. Confianca alta aqui seria mentira.
  {
    uint16_t vazio[4] = {1000, 1000, 1000, 1000};
    AssinaturaPreensao a;
    montaAssinatura(&a, vazio, false, 0, mascaraCheia);
    uint8_t conf = 0;
    checa(classificaPreensao(a, &conf) == OBJ_NENHUM, "mao vazia sem sensor no polegar");
    checa(conf < 80, "sem sensor no polegar: confianca reduzida");
  }

  // Nenhum dedo longo instrumentado: nao ha assinatura para ler, e a
  // resposta honesta e "indefinido" com confianca zero.
  {
    uint16_t qualquer[4] = {500, 500, 500, 500};
    AssinaturaPreensao a;
    montaAssinatura(&a, qualquer, true, 600, 0);
    uint8_t conf = 0;
    checa(classificaPreensao(a, &conf) == OBJ_INDEFINIDO, "sem sensores: indefinido");
    checa(conf == 0, "sem sensores: confianca zero");
  }

  // Mao parcialmente instrumentada baixa a confianca proporcionalmente.
  {
    uint16_t cil[4] = {500, 520, 490, 510};
    AssinaturaPreensao aCheia, aMetade;
    uint8_t confCheia = 0, confMetade = 0;
    montaAssinatura(&aCheia, cil, true, 600, mascaraCheia);
    montaAssinatura(&aMetade, cil, true, 600, LIMBIA_BIT(FEIO) | LIMBIA_BIT(JULGADOR));
    classificaPreensao(aCheia, &confCheia);
    classificaPreensao(aMetade, &confMetade);
    Serial.printf("     confianca com 4 dedos: %u%%  |  com 2 dedos: %u%%\n", confCheia,
                  confMetade);
    checa(confMetade < confCheia, "menos sensores -> menos confianca");
  }
}

// ---------------------------------------------------------------------
//  4. Tendao frouxo
// ---------------------------------------------------------------------
static void testeFolga() {
  Serial.println(F("\n[4] diagnostico de tendao frouxo"));
  const uint16_t LIMIAR = 150;
  const uint8_t mascara =
      LIMBIA_BIT(MINDY) | LIMBIA_BIT(DONCARE) | LIMBIA_BIT(FEIO) | LIMBIA_BIT(JULGADOR);

  AssinaturaPreensao a;
  for (uint8_t i = 0; i < N_JUNTAS; i++) {
    a.contatoEm[i] = 600;
    a.forcaMa[i]   = 500;
    a.tocou[i]     = true;
  }
  a.comSensor = mascara;

  checa(juntasComFolga(a, LIMIAR) == 0, "mao saudavel: nenhuma folga");

  // MINDY percorreu o curso inteiro e a corrente nunca subiu: o servo
  // esta enrolando folga em vez de puxar dedo.
  a.contatoEm[MINDY] = 1000;
  a.forcaMa[MINDY]   = 60;
  a.tocou[MINDY]     = false;
  checa(tendaoFrouxo(a, MINDY, LIMIAR), "MINDY frouxo detectado");
  checa(juntasComFolga(a, LIMIAR) == 1, "conta uma folga");

  // Dedo que foi ate o fim mas com corrente alta nao esta frouxo - ele
  // simplesmente fechou no vazio, empurrando o proprio mecanismo.
  a.contatoEm[DONCARE] = 1000;
  a.forcaMa[DONCARE]   = 480;
  a.tocou[DONCARE]     = false;
  checa(!tendaoFrouxo(a, DONCARE, LIMIAR), "corrente alta no fim de curso nao e folga");

  // Junta sem sensor nunca e diagnosticada: nao da para distinguir
  // tendao frouxo de dedo que nao encontrou nada.
  a.contatoEm[DEDAO] = 1000;
  a.forcaMa[DEDAO]   = 0;
  a.tocou[DEDAO]     = false;
  checa(!tendaoFrouxo(a, DEDAO, LIMIAR), "junta sem sensor nao e diagnosticada");
}

// ---------------------------------------------------------------------
//  5. Tempo de execucao
// ---------------------------------------------------------------------
static void testeTempo() {
  Serial.println(F("\n[5] custo de execucao"));
  const uint8_t mascara =
      LIMBIA_BIT(MINDY) | LIMBIA_BIT(DONCARE) | LIMBIA_BIT(FEIO) | LIMBIA_BIT(JULGADOR);

  uint16_t contatos[4];
  geraCaso(OBJ_CILINDRICO, contatos);
  AssinaturaPreensao a;
  montaAssinatura(&a, contatos, true, 600, mascara);

  const uint32_t t0    = micros();
  const uint16_t N     = 2000;
  uint8_t conf         = 0;
  volatile uint8_t acc = 0;
  for (uint16_t i = 0; i < N; i++) acc = (uint8_t)(acc + classificaPreensao(a, &conf));
  const uint32_t dt = micros() - t0;
  Serial.printf("     classificacao: %.1f us por chamada\n", (float)dt / N);

  const uint32_t t1      = micros();
  Calibracao c           = {2300, 1026};
  volatile uint16_t soma = 0;
  for (uint16_t i = 0; i < N; i++) soma = (uint16_t)(soma + pulsoDePerMil(c, (uint16_t)(i % 1001)));
  const uint32_t dt1 = micros() - t1;
  Serial.printf("     conversao de pulso: %.2f us por chamada\n", (float)dt1 / N);

  // O tick de movimento roda a cada 20 ms e converte 7 juntas. Se a
  // conversao custasse mais que algumas dezenas de microssegundos, o
  // orcamento do loop apertaria.
  checa((float)dt1 / N < 20.0f, "conversao cabe folgada no tick de 20 ms");
}

// =====================================================================
//  EMG
// =====================================================================

// Gaussiana barata: soma de quatro uniformes (Irwin-Hall), escalada para
// desvio 1. Box-Muller pediria log, raiz e cosseno por amostra - no C3,
// que nao tem FPU, isso multiplicaria o tempo do teste.
static float gauss() {
  float s = 0;
  for (uint8_t i = 0; i < 4; i++) s += (float)(aleatorio() & 0xFFFF) / 65535.0f;
  return (s - 2.0f) * 1.7320508f;
}

// ---------------------------------------------------------------------
//  O usuario sintetico
//
//  ganho[canal][classe]: desvio do EMG, em contagens de ADC, que aquele
//  eletrodo ve quando o usuario faz aquela classe com forca total. E aqui
//  que "bem posicionado" e "em cima do tendao" viram numero.
//
//  O que torna o sinal parecido com um antebraco, e nao com um gerador:
//  - co-contracao: ao fechar a mao o extensor tambem acende um pouco;
//  - forca diferente a cada repeticao (60% a 100%);
//  - o musculo leva ~80 ms para chegar ao patamar;
//  - tonus de repouso que varia, ruido eletronico, rede de 60 Hz e a
//    quantizacao de 12 bits do ADC.
// ---------------------------------------------------------------------
struct UsuarioSintetico {
  float ganho[N_CANAIS_EMG][N_CLASSES_EMG];
  float ruido;  // desvio do ruido eletronico, contagens
  float rede;   // amplitude da interferencia de 60 Hz, contagens
  float tonus;  // atividade maxima de repouso, contagens
};

struct SimuladorEmg {
  UsuarioSintetico u;
  float ativacao[N_CLASSES_EMG];
  float forca;
  float tonusAgora;
  uint8_t classeAnterior;
  uint32_t n;
};

static void simInicia(SimuladorEmg* s, const UsuarioSintetico& u) {
  memset(s, 0, sizeof(*s));
  s->u              = u;
  s->forca          = 1.0f;
  s->classeAnterior = C_REPOUSO;
}

// Uma amostra de 1 kHz dos dois canais, ja quantizada como o ADC faria.
static void simAmostra(SimuladorEmg* s, uint8_t classe, uint16_t saida[N_CANAIS_EMG]) {
  if (classe != s->classeAnterior) {
    // Cada repeticao sai com forca propria, e o repouso com tonus proprio.
    s->forca          = 0.6f + 0.4f * (float)(aleatorio() % 1000) / 1000.0f;
    s->tonusAgora     = s->u.tonus * (float)(aleatorio() % 1000) / 1000.0f;
    s->classeAnterior = classe;
  }
  const float k = 1.0f / (0.080f * (float)EMG_FS_HZ);  // constante de tempo de 80 ms
  for (uint8_t c = 0; c < N_CLASSES_EMG; c++) {
    const float alvo = (c == classe && c != C_REPOUSO) ? s->forca : 0.0f;
    s->ativacao[c] += (alvo - s->ativacao[c]) * k;
  }
  const float t   = (float)s->n / (float)EMG_FS_HZ;
  const float hum = s->u.rede * sinf(2.0f * 3.14159265f * 60.0f * t);
  for (uint8_t ch = 0; ch < N_CANAIS_EMG; ch++) {
    float sigma = s->tonusAgora;
    for (uint8_t c = 1; c < N_CLASSES_EMG; c++) sigma += s->u.ganho[ch][c] * s->ativacao[c];
    float v = 2048.0f + hum + s->u.ruido * gauss() + sigma * gauss();
    if (v < 0) v = 0;
    if (v > 4095) v = 4095;
    saida[ch] = (uint16_t)v;
  }
  s->n++;
}

// A mesma janela que a tarefa de aquisicao monta no firmware: media do
// envelope e contagem de amostras no trilho.
struct JanelaSim {
  float caracteristica[N_CANAIS_EMG];
  float saturacao[N_CANAIS_EMG];
};

static void simJanela(SimuladorEmg* s, CadeiaEmg cadeia[N_CANAIS_EMG], uint8_t classe,
                      JanelaSim* j) {
  const uint16_t amostras    = (uint16_t)(EMG_FS_HZ * EMG_JANELA_MS / 1000);
  float soma[N_CANAIS_EMG]   = {0, 0};
  uint16_t sat[N_CANAIS_EMG] = {0, 0};
  for (uint16_t i = 0; i < amostras; i++) {
    uint16_t bruto[N_CANAIS_EMG];
    simAmostra(s, classe, bruto);
    for (uint8_t ch = 0; ch < N_CANAIS_EMG; ch++) {
      if (bruto[ch] <= EMG_TRILHO_BAIXO || bruto[ch] >= EMG_TRILHO_ALTO) sat[ch]++;
      soma[ch] += cadeiaProcessa(&cadeia[ch], (float)bruto[ch]);
    }
  }
  for (uint8_t ch = 0; ch < N_CANAIS_EMG; ch++) {
    j->caracteristica[ch] = caracteristicaEmg(soma[ch] / amostras);
    j->saturacao[ch]      = (float)sat[ch] / amostras;
  }
}

// ---------------------------------------------------------------------
//  Uma sessao de calibracao inteira, como a tela de ajuste conduz
//
//  O usuario sintetico segue a instrucao da tela com 300 ms de atraso de
//  reacao. Tudo o mais - rotulo, descarte da transicao, teste antes do
//  treino, nota, histerese - e o mesmo caminho do firmware.
// ---------------------------------------------------------------------
struct Calibracao_ {
  EstatClasse est[N_CLASSES_EMG];
  ModeloEmg modelo;
  Placar placar;
  QualidadeEletrodo qual[N_CANAIS_EMG];
  float saturacao[N_CANAIS_EMG];
  float acertoGeral;
  bool geralOk;
  uint16_t cicloPronto;  // primeiro ciclo em que tudo ficou verde (0 = nunca)
};

static void calZera(Calibracao_* c) {
  memset(c, 0, sizeof(*c));
}

static bool calPronta(const Calibracao_& c) {
  return c.qual[0].ok && c.qual[1].ok && c.geralOk;
}

// Roda `ciclos` ciclos do protocolo com o usuario `u`. `cal` pode vir de
// uma sessao anterior (para simular o eletrodo sendo reposicionado).
static void rodaCalibracao(Calibracao_* cal, const UsuarioSintetico& u, uint16_t ciclos,
                           uint32_t* tempoInicial) {
  const TemposProtocolo tp    = temposProtocolo();
  const LimiaresQualidade lim = limiaresQualidade();
  const ParametrosCadeia pc   = parametrosCadeia();
  const uint32_t duracaoTotalMs =
      tp.preparoMs + (uint32_t)ciclos * 2 * (tp.contracaoMs + tp.repousoMs);

  SimuladorEmg sim;
  simInicia(&sim, u);
  CadeiaEmg cadeia[N_CANAIS_EMG];
  for (uint8_t ch = 0; ch < N_CANAIS_EMG; ch++) cadeiaConfigura(&cadeia[ch], pc);

  const uint32_t t0 = tempoInicial ? *tempoInicial : 0;
  for (uint32_t t = 0; t < duracaoTotalMs; t += EMG_JANELA_MS) {
    const PassoProtocolo agora = passoDoProtocolo(t0 + t, tp);
    // O usuario reage a instrucao 300 ms depois de ela aparecer.
    const PassoProtocolo reacao = passoDoProtocolo(t0 + (t >= 300 ? t - 300 : 0), tp);

    JanelaSim j;
    simJanela(&sim, cadeia, reacao.rotulo, &j);
    if (!agora.rotulavel) continue;

    // Saturacao de cada eletrodo medida durante o gesto DELE: e ali que o
    // sinal e maior. Medida no tempo todo, o repouso a diluiria.
    if (agora.rotulo == C_FECHAR) cal->saturacao[0] += (j.saturacao[0] - cal->saturacao[0]) * 0.05f;
    if (agora.rotulo == C_ABRIR) cal->saturacao[1] += (j.saturacao[1] - cal->saturacao[1]) * 0.05f;

    if (cal->modelo.valido) {
      float p[N_CLASSES_EMG];
      placarInclui(&cal->placar, agora.rotulo, classificaEmg(cal->modelo, j.caracteristica, p),
                   PLACAR_MEMORIA);
    }
    estatInclui(&cal->est[agora.rotulo], j.caracteristica, QUAL_MEMORIA);
    treinaModelo(cal->est, LDA_PESO_MINIMO, LDA_REGULARIZACAO, &cal->modelo);

    avaliaEletrodo(cal->est, EMG_FLEXOR, C_FECHAR, C_ABRIR, cal->saturacao[0], lim, &cal->qual[0]);
    avaliaEletrodo(cal->est, EMG_EXTENSOR, C_ABRIR, C_FECHAR, cal->saturacao[1], lim,
                   &cal->qual[1]);
    cal->acertoGeral = placarPior(cal->placar, PLACAR_PESO_MIN);
    if (cal->geralOk) {
      cal->geralOk = cal->acertoGeral >= QUAL_ACERTO_PERDE;
    } else {
      cal->geralOk = cal->acertoGeral >= QUAL_ACERTO_OK;
    }
    if (calPronta(*cal) && cal->cicloPronto == 0) cal->cicloPronto = agora.ciclo;
  }
  if (tempoInicial) *tempoInicial = t0 + duracaoTotalMs;
}

static void imprimeCal(const char* nome, const Calibracao_& c) {
  Serial.printf("     %-22s", nome);
  for (uint8_t ch = 0; ch < N_CANAIS_EMG; ch++) {
    const QualidadeEletrodo& q = c.qual[ch];
    Serial.printf(" | %3u%% d'%5.1f %5.1fx sel %4.1fx %-15s", q.nota, q.dPrime, q.razao,
                  q.seletividade, textoDoDiagnostico(q.diagnostico));
  }
  Serial.printf(" | %3u%%%s\n", (unsigned)(c.acertoGeral * 100 + 0.5f),
                calPronta(c) ? "  PRONTA" : "");
}

// Os antebracos do teste. Ganhos em contagens de ADC (desvio do EMG).
static UsuarioSintetico usuarioBase() {
  UsuarioSintetico u;
  memset(&u, 0, sizeof(u));
  u.ruido = 5;
  u.rede  = 40;
  u.tonus = 6;
  return u;
}

static UsuarioSintetico usuarioBemPosicionado() {
  UsuarioSintetico u              = usuarioBase();
  u.ganho[EMG_FLEXOR][C_FECHAR]   = 220;
  u.ganho[EMG_FLEXOR][C_ABRIR]    = 45;  // co-contracao
  u.ganho[EMG_EXTENSOR][C_ABRIR]  = 200;
  u.ganho[EMG_EXTENSOR][C_FECHAR] = 40;
  return u;
}

// ---------------------------------------------------------------------
//  6. Filtros
// ---------------------------------------------------------------------
static void testeFiltros() {
  Serial.println(F("\n[6] filtros do EMG"));
  const float fs = (float)EMG_FS_HZ;

  Biquad pa, notch, pb;
  biquadPassaAltas(&pa, EMG_PASSA_ALTAS_HZ, fs);
  biquadNotch(&notch, EMG_NOTCH_HZ, EMG_NOTCH_Q, fs);
  biquadPassaBaixas(&pb, EMG_ENVELOPE_HZ, fs);

  const float paEm5   = biquadGanhoMedido(pa, 5, fs);
  const float paEm100 = biquadGanhoMedido(pa, 100, fs);
  const float nEm60   = biquadGanhoMedido(notch, 60, fs);
  const float nEm100  = biquadGanhoMedido(notch, 100, fs);
  const float pbEm60  = biquadGanhoMedido(pb, 60, fs);
  const float pbEm1   = biquadGanhoMedido(pb, 1, fs);

  Serial.printf("     passa-altas %.0f Hz: %.1f dB em 5 Hz, %.1f dB em 100 Hz\n",
                EMG_PASSA_ALTAS_HZ, 20 * log10f(paEm5), 20 * log10f(paEm100));
  Serial.printf("     notch %.0f Hz:       %.1f dB em 60 Hz, %.1f dB em 100 Hz\n", EMG_NOTCH_HZ,
                20 * log10f(nEm60 + 1e-6f), 20 * log10f(nEm100));
  Serial.printf("     envelope %.0f Hz:     %.1f dB em 60 Hz, %.1f dB em 1 Hz\n", EMG_ENVELOPE_HZ,
                20 * log10f(pbEm60), 20 * log10f(pbEm1));

  checa(paEm5 < 0.1f, "passa-altas corta artefato de 5 Hz (> 20 dB)");
  checa(paEm100 > 0.9f, "passa-altas deixa o musculo passar em 100 Hz");
  checa(nEm60 < 0.05f, "notch tira a rede de 60 Hz (> 26 dB)");
  checa(nEm100 > 0.9f, "notch nao mexe em 100 Hz");
  checa(pbEm60 < 0.01f, "envelope sem ondulacao de 60 Hz (> 40 dB)");

  // Sinal parado no meio da escala: sem o preaquecimento, a primeira
  // amostra seria um degrau de 2048 contagens e o envelope nasceria
  // mostrando uma contracao que nao existe.
  CadeiaEmg c;
  cadeiaConfigura(&c, parametrosCadeia());
  float maior = 0;
  for (uint16_t i = 0; i < 500; i++) {
    const float e = cadeiaProcessa(&c, 2048.0f);
    if (e > maior) maior = e;
  }
  Serial.printf("     envelope de sinal parado em 2048: pico de %.3f contagens\n", maior);
  checa(maior < 1.0f, "cadeia nasce em regime (sem transitorio de DC)");

  // Covariancia com memoria longa tem de bater com a conta direta.
  EstatClasse e;
  estatZera(&e);
  const float xs[5][2] = {{1, 2}, {2, 1}, {3, 5}, {4, 3}, {5, 4}};
  for (uint8_t i = 0; i < 5; i++) estatInclui(&e, xs[i], 1e9f);
  // media (3, 3); variancia populacional (2, 2); covariancia
  // [(-2)(-1) + (-1)(-2) + 0*2 + 1*0 + 2*1] / 5 = 1,2
  checa(fabsf(e.media[0] - 3) < 1e-4f && fabsf(e.media[1] - 3) < 1e-4f, "media recursiva");
  checa(fabsf(e.cov[0][0] - 2) < 1e-3f && fabsf(e.cov[1][1] - 2) < 1e-3f, "variancia recursiva");
  checa(fabsf(e.cov[0][1] - 1.2f) < 1e-3f, "covariancia recursiva");
}

// Pouco musculo sob os eletrodos, muita co-contracao, repouso inquieto.
static UsuarioSintetico usuarioDificil() {
  UsuarioSintetico u              = usuarioBase();
  u.tonus                         = 14;
  u.ruido                         = 8;
  u.ganho[EMG_FLEXOR][C_FECHAR]   = 70;
  u.ganho[EMG_FLEXOR][C_ABRIR]    = 38;
  u.ganho[EMG_EXTENSOR][C_ABRIR]  = 60;
  u.ganho[EMG_EXTENSOR][C_FECHAR] = 34;
  return u;
}

// ---------------------------------------------------------------------
//  7. Qualidade do eletrodo: a barra separa bem de mal posicionado?
// ---------------------------------------------------------------------
// Os dois eletrodos em cima de pouco musculo, vendo os dois gestos quase
// igual. E o caso em que a tela precisa dizer "ainda nao".
static UsuarioSintetico usuarioSemMusculo() {
  UsuarioSintetico u              = usuarioBase();
  u.tonus                         = 14;
  u.ruido                         = 8;
  u.ganho[EMG_FLEXOR][C_FECHAR]   = 26;
  u.ganho[EMG_FLEXOR][C_ABRIR]    = 22;
  u.ganho[EMG_EXTENSOR][C_ABRIR]  = 24;
  u.ganho[EMG_EXTENSOR][C_FECHAR] = 21;
  return u;
}

static Calibracao_ calibracaoBoa;      // reaproveitada pela secao [8]
static Calibracao_ calibracaoDificil;  // idem
static Calibracao_ calibracaoRuim;     // idem

static void testeQualidade() {
  Serial.println(F("\n[7] posicionamento dos eletrodos (usuario sintetico)"));
  Serial.println(F("     barra, d', razao contracao/repouso, seletividade, diagnostico"));
  Serial.println(
      F("     cenario                | eletrodo 1 (flexor)                            "
        "| eletrodo 2 (extensor)                          | acerto"));
  const uint16_t CICLOS = AUTOTESTE_CICLOS;

  // A - os dois no lugar certo
  calZera(&calibracaoBoa);
  rodaCalibracao(&calibracaoBoa, usuarioBemPosicionado(), CICLOS, nullptr);
  imprimeCal("bem posicionados", calibracaoBoa);
  checa(calibracaoBoa.qual[0].ok && calibracaoBoa.qual[1].ok, "A: os dois eletrodos ficam verdes");
  checa(calibracaoBoa.geralOk, "A: calibracao geral fica verde");
  Serial.printf("     -> pronta no ciclo %u de %u (~%lu s de calibracao)\n",
                calibracaoBoa.cicloPronto, CICLOS,
                (unsigned long)((PROT_PREPARO_MS + (uint32_t)calibracaoBoa.cicloPronto * 2 *
                                                       (PROT_CONTRACAO_MS + PROT_REPOUSO_MS)) /
                                1000));
  checa(calibracaoBoa.cicloPronto > 0 && calibracaoBoa.cicloPronto <= 4,
        "A: pronta em ate 4 ciclos");

  // B - eletrodo 2 em cima do tendao: o musculo mal aparece
  {
    UsuarioSintetico u              = usuarioBemPosicionado();
    u.ganho[EMG_EXTENSOR][C_ABRIR]  = 12;
    u.ganho[EMG_EXTENSOR][C_FECHAR] = 3;
    Calibracao_ c;
    calZera(&c);
    rodaCalibracao(&c, u, CICLOS, nullptr);
    imprimeCal("eletrodo 2 no tendao", c);
    checa(c.qual[0].ok, "B: eletrodo 1 continua verde");
    checa(!c.qual[1].ok && c.qual[1].diagnostico == D_SINAL_FRACO,
          "B: eletrodo 2 acusa sinal fraco");
    checa(!calPronta(c), "B: calibracao NAO libera o modo padrao");
  }

  // C - eletrodos trocados de lugar
  {
    UsuarioSintetico u = usuarioBemPosicionado();
    UsuarioSintetico t = u;
    for (uint8_t k = 0; k < N_CLASSES_EMG; k++) {
      t.ganho[EMG_FLEXOR][k]   = u.ganho[EMG_EXTENSOR][k];
      t.ganho[EMG_EXTENSOR][k] = u.ganho[EMG_FLEXOR][k];
    }
    Calibracao_ c;
    calZera(&c);
    rodaCalibracao(&c, t, CICLOS, nullptr);
    imprimeCal("eletrodos trocados", c);
    checa(c.qual[0].diagnostico == D_MUSCULO_TROCADO && c.qual[1].diagnostico == D_MUSCULO_TROCADO,
          "C: os dois acusam musculo trocado");
    checa(!calPronta(c), "C: calibracao NAO libera o modo padrao");
  }

  // D - ganho alto demais: o sinal bate no trilho do ADC
  {
    UsuarioSintetico u            = usuarioBemPosicionado();
    u.ganho[EMG_FLEXOR][C_FECHAR] = 2500;
    Calibracao_ c;
    calZera(&c);
    rodaCalibracao(&c, u, CICLOS, nullptr);
    imprimeCal("eletrodo 1 saturando", c);
    checa(c.qual[0].diagnostico == D_SATURADO, "D: eletrodo 1 acusa saturacao");
    checa(!calPronta(c), "D: calibracao NAO libera o modo padrao");
  }

  // E - mau contato: o eletrodo mal colado vira antena, e o ruido de
  //     fundo sobe em todas as frequencias, com o musculo parado ou nao.
  {
    UsuarioSintetico u = usuarioBemPosicionado();
    u.ruido            = 110;
    Calibracao_ c;
    calZera(&c);
    rodaCalibracao(&c, u, CICLOS, nullptr);
    imprimeCal("mau contato", c);
    checa(c.qual[0].diagnostico == D_RUIDO_REPOUSO || c.qual[1].diagnostico == D_RUIDO_REPOUSO,
          "E: acusa repouso ruidoso");
    checa(!calPronta(c), "E: calibracao NAO libera o modo padrao");
  }

  // F - o caso da tela: comeca mal posicionado, o usuario move o eletrodo
  //     e a barra tem de ir de vermelho a verde sem recomecar nada.
  {
    UsuarioSintetico mal              = usuarioBemPosicionado();
    mal.ganho[EMG_EXTENSOR][C_ABRIR]  = 12;
    mal.ganho[EMG_EXTENSOR][C_FECHAR] = 3;
    Calibracao_ c;
    calZera(&c);
    uint32_t t = 0;
    rodaCalibracao(&c, mal, 4, &t);
    const uint8_t notaAntes = c.qual[1].nota;
    const bool verdeAntes   = c.qual[1].ok;
    uint16_t ciclosAteVerde = 0;
    for (uint16_t k = 1; k <= 8 && !calPronta(c); k++) {
      rodaCalibracao(&c, usuarioBemPosicionado(), 1, &t);
      if (calPronta(c)) ciclosAteVerde = k;
    }
    Serial.printf("     reposicionado: eletrodo 2 de %u%% para %u%%, pronta %u ciclo(s) depois\n",
                  notaAntes, c.qual[1].nota, ciclosAteVerde);
    checa(!verdeAntes && notaAntes < 100, "F: antes de mover, a barra nao esta verde");
    checa(ciclosAteVerde > 0 && ciclosAteVerde <= 4,
          "F: depois de mover, fica verde em ate 4 ciclos sem recomecar");
  }

  // G - antebraco dificil: pouco musculo e muita co-contracao. Nao ha
  //     resposta "certa" para este cenario - ele existe para a secao [8]
  //     conferir a propriedade que importa: SE a tela ficou verde, a
  //     protese nao age errado.
  calZera(&calibracaoDificil);
  rodaCalibracao(&calibracaoDificil, usuarioDificil(), CICLOS, nullptr);
  imprimeCal("antebraco dificil", calibracaoDificil);

  // H - sem musculo util: os dois eletrodos mal enxergam os gestos e
  //     enxergam os dois quase igual. A tela TEM de segurar - e a secao
  //     [8] forca o uso deste modelo mesmo assim, para medir o que a
  //     trava evitou.
  calZera(&calibracaoRuim);
  rodaCalibracao(&calibracaoRuim, usuarioSemMusculo(), CICLOS, nullptr);
  imprimeCal("sem musculo util", calibracaoRuim);
  checa(!calPronta(calibracaoRuim), "H: calibracao NAO libera o modo padrao");
}

// ---------------------------------------------------------------------
//  8. Usando a protese: o classificador e o decisor contra intencoes
// ---------------------------------------------------------------------
struct ResultadoUso {
  uint16_t intencoes;  // gestos que deveriam mudar o comando
  uint16_t atendidas;  // ... e mudaram, para o lado certo
  uint16_t erradas;    // trocas para o lado errado
  uint16_t espurias;   // trocas durante repouso
  uint32_t somaLatencia;
  uint32_t maiorLatencia;
  uint32_t janelas, acertosJanela;
};

// Uma sessao de uso: sequencia aleatoria de repouso e gestos, com forca
// e duracao variando. `forcaMax` < 1 simula uma pessoa cansada.
static void rodaUso(const ModeloEmg& m, const UsuarioSintetico& u, uint16_t gestos, bool soRepouso,
                    ResultadoUso* r) {
  memset(r, 0, sizeof(*r));
  const ParametrosDecisor pd = parametrosDecisor();
  SimuladorEmg sim;
  simInicia(&sim, u);
  CadeiaEmg cadeia[N_CANAIS_EMG];
  for (uint8_t ch = 0; ch < N_CANAIS_EMG; ch++) cadeiaConfigura(&cadeia[ch], parametrosCadeia());
  Decisor d;
  decisorZera(&d, C_ABRIR);

  uint32_t t = 0;
  // Um segundo de repouso para os filtros e o decisor encherem.
  for (uint8_t i = 0; i < 20; i++) {
    JanelaSim j;
    simJanela(&sim, cadeia, C_REPOUSO, &j);
    float p[N_CLASSES_EMG];
    const uint8_t c = classificaEmg(m, j.caracteristica, p);
    decisorAtualiza(&d, c, p[c], t, pd);
    t += EMG_JANELA_MS;
  }
  const uint32_t trocasIniciais = d.trocas;

  for (uint16_t g = 0; g < gestos; g++) {
    // repouso entre gestos: 1,5 a 4 s
    const uint32_t durRep = soRepouso ? 60000 : (uint32_t)entre(1500, 4000);
    const uint8_t classe  = (g % 2 == 0) ? C_FECHAR : C_ABRIR;
    const uint32_t durGes = (uint32_t)entre(900, 2500);

    for (uint32_t k = 0; k < durRep; k += EMG_JANELA_MS) {
      JanelaSim j;
      simJanela(&sim, cadeia, C_REPOUSO, &j);
      float p[N_CLASSES_EMG];
      const uint8_t c = classificaEmg(m, j.caracteristica, p);
      if (k > 500) {
        r->janelas++;
        if (c == C_REPOUSO) r->acertosJanela++;
      }
      if (decisorAtualiza(&d, c, p[c], t, pd)) r->espurias++;
      t += EMG_JANELA_MS;
    }
    if (soRepouso) break;

    const bool deveMudar = (d.comando != classe);
    if (deveMudar) r->intencoes++;
    bool mudou = false;
    for (uint32_t k = 0; k < durGes; k += EMG_JANELA_MS) {
      JanelaSim j;
      simJanela(&sim, cadeia, classe, &j);
      float p[N_CLASSES_EMG];
      const uint8_t c = classificaEmg(m, j.caracteristica, p);
      if (k > 300) {
        r->janelas++;
        if (c == classe) r->acertosJanela++;
      }
      if (decisorAtualiza(&d, c, p[c], t, pd)) {
        if (d.comando == classe && !mudou && deveMudar) {
          r->atendidas++;
          r->somaLatencia += k + EMG_JANELA_MS;
          if (k + EMG_JANELA_MS > r->maiorLatencia) r->maiorLatencia = k + EMG_JANELA_MS;
          mudou = true;
        } else {
          r->erradas++;
        }
      }
      t += EMG_JANELA_MS;
    }
  }
  (void)trocasIniciais;
}

static void testeUso() {
  Serial.println(F("\n[8] usando a protese: classificador + decisor"));
  const ModeloEmg& m = calibracaoBoa.modelo;
  if (!m.valido) {
    checa(false, "modelo da secao [7] disponivel");
    return;
  }

  // O que o proprio modelo acha que vai acertar, pela distancia entre as
  // classes - para comparar com o que ele de fato acerta.
  float piorDelta = 1e9f;
  for (uint8_t a = 0; a < N_CLASSES_EMG; a++) {
    for (uint8_t b = a + 1; b < N_CLASSES_EMG; b++) {
      const float dd = distanciaEntreClasses(m, a, b);
      if (dd < piorDelta) piorDelta = dd;
    }
  }

  ResultadoUso r;
  rodaUso(m, usuarioBemPosicionado(), AUTOTESTE_GESTOS, false, &r);
  const float acertoJanela = r.janelas ? (float)r.acertosJanela / r.janelas : 0;
  Serial.printf("     %u gestos alternados, forca de 60%% a 100%%, 1 a 2,5 s cada\n",
                AUTOTESTE_GESTOS);
  Serial.printf("     por janela de 50 ms: acerto medido %.1f%% | previsto pelo modelo >= %.1f%%\n",
                acertoJanela * 100, acertoPrevisto(piorDelta) * 100);
  Serial.printf("     intencoes atendidas: %u/%u | trocas erradas: %u | espurias em repouso: %u\n",
                r.atendidas, r.intencoes, r.erradas, r.espurias);
  if (r.atendidas) {
    Serial.printf("     latencia do gesto ao comando: media %lu ms, pior %lu ms\n",
                  (unsigned long)(r.somaLatencia / r.atendidas), (unsigned long)r.maiorLatencia);
  }
  checa(r.atendidas * 100u >= r.intencoes * 95u, "atende >= 95% das intencoes");
  checa(r.erradas == 0, "nenhuma troca para o lado errado");
  checa(r.espurias == 0, "nenhuma troca durante o repouso");
  checa(r.atendidas && r.somaLatencia / r.atendidas <= 600, "latencia media <= 600 ms");

  // Um minuto inteiro de braco relaxado: a mao nao pode se mexer nenhuma vez.
  ResultadoUso rep;
  rodaUso(m, usuarioBemPosicionado(), 1, true, &rep);
  Serial.printf("     60 s de repouso: %u troca(s) de comando\n", rep.espurias);
  checa(rep.espurias == 0, "60 s de repouso sem nenhuma troca");

  // Pessoa cansada: os gestos com metade da forca de calibracao. O
  // decisor pode deixar de atender - mas nao pode atender ERRADO.
  UsuarioSintetico cansado = usuarioBemPosicionado();
  for (uint8_t ch = 0; ch < N_CANAIS_EMG; ch++) {
    for (uint8_t k = 0; k < N_CLASSES_EMG; k++) cansado.ganho[ch][k] *= 0.35f;
  }
  ResultadoUso rc;
  rodaUso(m, cansado, AUTOTESTE_GESTOS * 2 / 3, false, &rc);
  Serial.printf("     cansado (35%% da forca): atendidas %u/%u | erradas %u | espurias %u\n",
                rc.atendidas, rc.intencoes, rc.erradas, rc.espurias);
  checa(rc.erradas == 0 && rc.espurias == 0, "forca fraca: deixa de agir, mas nunca age errado");

  // O antebraco dificil. A propriedade verificada e condicional: se a
  // calibracao liberou o modo padrao, a protese nao pode agir errado. Se
  // nao liberou, a tela fez o trabalho dela e o teste passa sem usar.
  {
    const Calibracao_& cd = calibracaoDificil;
    if (calPronta(cd)) {
      ResultadoUso rd;
      rodaUso(cd.modelo, usuarioDificil(), AUTOTESTE_GESTOS, false, &rd);
      Serial.printf(
          "     antebraco dificil (calibracao liberou): atendidas %u/%u | erradas %u | "
          "espurias %u\n",
          rd.atendidas, rd.intencoes, rd.erradas, rd.espurias);
      checa(rd.erradas + rd.espurias <= 1, "liberado pela tela => no maximo 1 acao errada em 60");
    } else {
      Serial.println(F("     antebraco dificil: calibracao NAO liberou o modo padrao"));
      checa(true, "antebraco dificil: tela segurou");
    }
  }

  // O que a trava evitou: o modelo do cenario H, que a tela recusou,
  // usado a forca. Se ele funcionasse bem, a tela estaria segurando gente
  // que conseguiria usar a protese - e isso tambem e defeito.
  if (calibracaoRuim.modelo.valido) {
    ResultadoUso rr;
    rodaUso(calibracaoRuim.modelo, usuarioSemMusculo(), AUTOTESTE_GESTOS, false, &rr);
    Serial.printf(
        "     sem musculo util, USADO A FORCA: atendidas %u/%u | erradas %u | espurias %u "
        "| acerto por janela %.0f%%\n",
        rr.atendidas, rr.intencoes, rr.erradas, rr.espurias,
        rr.janelas ? 100.0f * rr.acertosJanela / rr.janelas : 0.0f);
    checa(rr.atendidas * 100u < rr.intencoes * 95u || rr.erradas + rr.espurias > 1,
          "o que a tela recusou teria funcionado mal");
  }

  // Probabilidade de cada classe tem de somar 1.
  float p[N_CLASSES_EMG];
  const float x[2] = {3.0f, 3.0f};
  classificaEmg(m, x, p);
  checa(fabsf(p[0] + p[1] + p[2] - 1.0f) < 1e-4f, "probabilidades somam 1");
}

// ---------------------------------------------------------------------
//  9. Decisor isolado: as tres travas
// ---------------------------------------------------------------------
static void testeDecisor() {
  Serial.println(F("\n[9] decisor"));
  const ParametrosDecisor pd = parametrosDecisor();
  Decisor d;
  decisorZera(&d, C_ABRIR);

  // Votos confiantes de FECHAR: troca so depois de encher a janela.
  uint8_t trocouNa = 0;
  for (uint8_t i = 1; i <= 10 && !trocouNa; i++) {
    if (decisorAtualiza(&d, C_FECHAR, 0.95f, i * 50u, pd)) trocouNa = i;
  }
  Serial.printf("     troca na %ua classificacao confiante (janela %u, minimo %u votos)\n",
                trocouNa, pd.janela, pd.votosMin);
  checa(trocouNa == pd.janela, "precisa da janela cheia para agir");

  // ABRIR logo em seguida: o refratario segura.
  bool trocou = false;
  uint32_t t  = trocouNa * 50u;
  for (uint8_t i = 1; i <= 40; i++) {
    t += 50;
    if (decisorAtualiza(&d, C_ABRIR, 0.95f, t, pd)) {
      trocou = true;
      break;
    }
  }
  checa(trocou && (t - trocouNa * 50u) >= pd.refratarioMs, "refratario segura a segunda troca");

  // Votos com probabilidade baixa nunca contam.
  Decisor d2;
  decisorZera(&d2, C_ABRIR);
  bool agiu = false;
  for (uint8_t i = 1; i <= 40; i++) {
    if (decisorAtualiza(&d2, C_FECHAR, pd.probMin - 0.05f, i * 50u, pd)) agiu = true;
  }
  checa(!agiu, "voto sem confianca nao move a mao");

  // Repouso nunca muda o comando.
  Decisor d3;
  decisorZera(&d3, C_FECHAR);
  agiu = false;
  for (uint8_t i = 1; i <= 40; i++) {
    if (decisorAtualiza(&d3, C_REPOUSO, 0.99f, i * 50u, pd)) agiu = true;
  }
  checa(!agiu && d3.comando == C_FECHAR, "repouso mantem a mao fechada");
}

// ---------------------------------------------------------------------
//  10. Enlace entre as placas
// ---------------------------------------------------------------------
static void testeEnlace() {
  Serial.println(F("\n[10] enlace entre as placas"));

  const uint8_t vetor[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  checa(crc16(vetor, sizeof(vetor)) == 0x29B1, "CRC-16/CCITT do vetor padrao = 0x29B1");

  PacoteComando cmd = {MODO_PADRAO, ACAO_FECHAR, 1234, 91};
  uint8_t buf[ENLACE_MAX_PACOTE];
  const uint16_t n = montaPacote(PKT_COMANDO, 7, &cmd, sizeof(cmd), buf, sizeof(buf));
  checa(n == sizeof(CabecalhoPacote) + sizeof(PacoteComando), "pacote de comando montado");

  CabecalhoPacote cab;
  const uint8_t* carga = nullptr;
  checa(abrePacote(buf, n, &cab, &carga), "pacote integro e aceito");
  PacoteComando lido;
  memcpy(&lido, carga, sizeof(lido));
  checa(cab.seq == 7 && lido.acao == ACAO_FECHAR && lido.seqAcao == 1234, "ida e volta preserva");

  // Um bit trocado em qualquer byte derruba o pacote.
  uint16_t aceitosCorrompidos = 0;
  for (uint16_t i = 0; i < n; i++) {
    for (uint8_t b = 0; b < 8; b++) {
      buf[i] ^= (uint8_t)(1u << b);
      if (abrePacote(buf, n, &cab, &carga)) aceitosCorrompidos++;
      buf[i] ^= (uint8_t)(1u << b);
    }
  }
  Serial.printf("     %u pacotes com 1 bit trocado: %u aceitos\n", n * 8u, aceitosCorrompidos);
  checa(aceitosCorrompidos == 0, "nenhum pacote corrompido aceito");

  checa(!abrePacote(buf, n - 1, &cab, &carga), "pacote truncado recusado");
  checa(montaPacote(PKT_COMANDO, 1, &cmd, sizeof(cmd) - 1, buf, sizeof(buf)) == 0,
        "carga de tamanho errado recusada");
  checa(montaPacote(99, 1, &cmd, sizeof(cmd), buf, sizeof(buf)) == 0, "tipo desconhecido recusado");

  checa(!senhaValida("1234567"), "senha de 7 caracteres recusada");
  checa(senhaValida("12345678"), "senha de 8 caracteres aceita");
  char longa[65];
  memset(longa, 'a', 64);
  longa[64] = 0;
  checa(!senhaValida(longa), "senha de 64 caracteres recusada");
  checa(!ssidValido(""), "nome vazio recusado");
  checa(ssidValido("LimbIA Modelo 2"), "nome com espaco no meio aceito");
  checa(!ssidValido(" LimbIA"), "nome com espaco na ponta recusado");
  checa(!ssidValido("mao\"x"), "nome com aspas recusado");
  const uint16_t poseRuim[N_JUNTAS] = {0, 0, 0, 0, 0, 0, 1001};
  checa(!poseValida(poseRuim), "pose fora de 0..1000 recusada");
}

// ---------------------------------------------------------------------
//  11. Custo do EMG
// ---------------------------------------------------------------------
static void testeCustoEmg() {
  Serial.println(F("\n[11] custo de execucao do EMG"));
  CadeiaEmg c;
  cadeiaConfigura(&c, parametrosCadeia());
  const uint16_t N   = 5000;
  volatile float acc = 0;
  const uint32_t t0  = micros();
  for (uint16_t i = 0; i < N; i++) acc = acc + cadeiaProcessa(&c, (float)(2048 + (i % 97) - 48));
  const float porAmostra = (float)(micros() - t0) / N;

  const ModeloEmg& m = calibracaoBoa.modelo;
  float p[N_CLASSES_EMG];
  const uint32_t t1 = micros();
  for (uint16_t i = 0; i < N; i++) {
    const float x[2] = {(float)(i % 7), (float)(i % 5)};
    acc              = acc + (float)classificaEmg(m, x, p);
  }
  const float porClassificacao = (float)(micros() - t1) / N;

  Serial.printf("     cadeia de filtros: %.2f us por amostra -> %.1f%% de um nucleo a 2 x 1 kHz\n",
                porAmostra, porAmostra * 2.0f * EMG_FS_HZ / 1e4f);
  Serial.printf("     classificacao LDA: %.2f us\n", porClassificacao);
  checa(porAmostra * 2.0f < 200.0f, "dois canais cabem folgados em 1 ms");
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println(F("====================================================="));
  Serial.println(F("  LimbIA - autoteste da biblioteca limbia_mao"));
  Serial.printf("  build %s %s\n", __DATE__, __TIME__);
  Serial.println(F("  nenhum hardware precisa estar ligado nesta placa"));
  Serial.println(F("====================================================="));

  const uint32_t t0 = millis();
  testePulso();
  testeGestos();
  testePreensao();
  testeFolga();
  testeTempo();
  testeFiltros();
  testeQualidade();
  testeUso();
  testeDecisor();
  testeEnlace();
  testeCustoEmg();
  const uint32_t dt = millis() - t0;

  Serial.println(F("\n====================================================="));
  Serial.printf("  %u verificacoes: %u passaram, %u falharam  (%lu ms)\n",
                (unsigned)(passou + falhou), passou, falhou, (unsigned long)dt);
  Serial.println(falhou == 0 ? F("  RESULTADO: OK") : F("  RESULTADO: FALHOU"));
  Serial.println(F("====================================================="));
}

void loop() {
  delay(1000);
}

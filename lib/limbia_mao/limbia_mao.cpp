// =====================================================================
//  limbia_mao - implementacao
// =====================================================================
#include "limbia_mao.h"

namespace limbia {

// ---------------------------------------------------------------------
//  Nomes
// ---------------------------------------------------------------------
static const char* const NOME_JUNTA[N_JUNTAS] = {"MINDY", "DONCARE",   "FEIO", "JULGADOR",
                                                 "DEDAO", "DEDAO_ABD", "PULSO"};

static const char* const ANATOMIA[N_JUNTAS] = {
    "mindinho", "anelar", "medio", "indicador", "polegar (flexao)", "polegar (abducao)", "punho"};

const char* nomeDaJunta(uint8_t junta) {
  return junta < N_JUNTAS ? NOME_JUNTA[junta] : "?";
}

const char* anatomiaDaJunta(uint8_t junta) {
  return junta < N_JUNTAS ? ANATOMIA[junta] : "?";
}

// ---------------------------------------------------------------------
//  Conversao posicao <-> pulso
// ---------------------------------------------------------------------
static uint16_t prende(int32_t v, int32_t lo, int32_t hi) {
  if (v < lo) return (uint16_t)lo;
  if (v > hi) return (uint16_t)hi;
  return (uint16_t)v;
}

uint16_t pulsoDePerMil(const Calibracao& c, uint16_t perMil) {
  if (perMil > 1000) perMil = 1000;
  const int32_t r = (int32_t)c.pulsoRepouso;
  const int32_t t = (int32_t)c.pulsoTrabalho;
  // (t - r) pode ser negativo: e o caso do servo montado espelhado, e o
  // calculo funciona igual nos dois sentidos justamente por isso.
  const int32_t us = r + ((t - r) * (int32_t)perMil) / 1000;
  return prende(us, PULSO_MIN_US, PULSO_MAX_US);
}

uint16_t perMilDePulso(const Calibracao& c, uint16_t us) {
  const int32_t r = (int32_t)c.pulsoRepouso;
  const int32_t t = (int32_t)c.pulsoTrabalho;
  if (r == t) return 0;  // calibracao degenerada; quem chama ja foi avisado
  const int32_t p = (((int32_t)us - r) * 1000) / (t - r);
  return prende(p, 0, 1000);
}

bool calibracaoValida(const Calibracao& c) {
  if (c.pulsoRepouso < PULSO_MIN_US || c.pulsoRepouso > PULSO_MAX_US) return false;
  if (c.pulsoTrabalho < PULSO_MIN_US || c.pulsoTrabalho > PULSO_MAX_US) return false;
  const int32_t curso     = (int32_t)c.pulsoTrabalho - (int32_t)c.pulsoRepouso;
  const int32_t abs_curso = curso < 0 ? -curso : curso;
  // Menos de 150 us de curso nao move dedo nenhum de forma util: ou a
  // calibracao ficou pela metade, ou os dois pontos foram medidos na
  // mesma posicao por engano.
  return abs_curso >= 150;
}

void calibracaoPadrao(Calibracao* d) {
  if (!d) return;

  // Dedos longos: a tabela do caderno do INOVAWEEK.
  //   1,0 ms = 0 grau (relaxado)   ...   2,0 ms = 180 graus (tensionado)
  for (uint8_t i = MINDY; i <= JULGADOR; i++) {
    d[i].pulsoRepouso  = 1000;
    d[i].pulsoTrabalho = 2000;
  }

  // Polegar: os numeros medidos no manual do LAD Robotic Hand V3.0.
  // Repare que os dois vem INVERTIDOS - repouso maior que trabalho.
  d[DEDAO].pulsoRepouso  = 2300;  // extensionF1: polegar estendido
  d[DEDAO].pulsoTrabalho = 1026;  // flexionF1_2: polegar flexionado

  d[DEDAO_ABD].pulsoRepouso  = 950;   // add_F1: junto a palma
  d[DEDAO_ABD].pulsoTrabalho = 1550;  // abd_F1: afastado da palma

  // Punho: 1,5 ms e o centro. Repouso e trabalho equidistantes do centro
  // para que a posicao 500 caia exatamente nele.
  d[PULSO].pulsoRepouso  = 1000;
  d[PULSO].pulsoTrabalho = 2000;
}

// ---------------------------------------------------------------------
//  Gestos
//
//  Ordem das colunas: MINDY, DONCARE, FEIO, JULGADOR, DEDAO, DEDAO_ABD, PULSO
//  0 = repouso/aberto, 1000 = trabalho/fechado, punho neutro em 500.
// ---------------------------------------------------------------------
static const Pose GESTOS[N_GESTOS] = {
    /* G_ABRIR      */ {{0, 0, 0, 0, 0, 300, 500}},
    /* G_FECHAR     */ {{1000, 1000, 1000, 1000, 1000, 0, 500}},
    /* G_FECHAR_V2  */ {{1000, 1000, 1000, 1000, 1000, 1000, 500}},
    /* G_PAZ        */ {{1000, 1000, 0, 0, 1000, 0, 500}},
    /* G_POSITIVO   */ {{1000, 1000, 1000, 1000, 0, 1000, 500}},
    /* G_APONTAR    */ {{1000, 1000, 1000, 0, 1000, 0, 500}},
    /* G_PINCA      */ {{0, 0, 0, 700, 700, 700, 500}},
    /* G_FLEX_DEDAO */ {{0, 0, 0, 0, 1000, 300, 500}},
    /* G_EXT_DEDAO  */ {{0, 0, 0, 0, 0, 300, 500}},
};

static const char* const NOME_GESTO[N_GESTOS] = {"abrir",
                                                 "fechar",
                                                 "fechar (polegar fora)",
                                                 "paz",
                                                 "positivo",
                                                 "apontar",
                                                 "pinca",
                                                 "flexionar polegar",
                                                 "estender polegar"};

const Pose& poseDoGesto(uint8_t gesto) {
  return GESTOS[gesto < N_GESTOS ? gesto : G_ABRIR];
}

const char* nomeDoGesto(uint8_t gesto) {
  return gesto < N_GESTOS ? NOME_GESTO[gesto] : "?";
}

// ---------------------------------------------------------------------
//  Preensao
// ---------------------------------------------------------------------
static const char* const NOME_OBJETO[N_OBJETOS] = {"nada na mao",       "objeto fino",
                                                   "objeto cilindrico", "objeto plano",
                                                   "objeto grande",     "indefinido"};

const char* nomeDoObjeto(uint8_t objeto) {
  return objeto < N_OBJETOS ? NOME_OBJETO[objeto] : "?";
}

uint8_t classificaPreensao(const AssinaturaPreensao& a, uint8_t* confianca) {
  if (confianca) *confianca = 0;

  // Só os quatro dedos longos formam a assinatura. O polegar se opoe a
  // eles e por isso mede outra coisa; o punho nao agarra nada.
  uint32_t soma  = 0;
  uint16_t menor = 1000, maior = 0;
  uint8_t tocaram = 0;
  uint8_t comVoto = 0;  // dedos longos que TEM sensor e portanto podem opinar

  for (uint8_t i = 0; i < N_DEDOS_LONGOS; i++) {
    if (!(a.comSensor & LIMBIA_BIT(i))) continue;  // sem sensor, sem voto
    comVoto++;
    if (!a.tocou[i]) continue;
    const uint16_t p = a.contatoEm[i] > 1000 ? 1000 : a.contatoEm[i];
    soma += p;
    if (p < menor) menor = p;
    if (p > maior) maior = p;
    tocaram++;
  }

  // Sem nenhum dedo longo instrumentado nao ha assinatura para ler. Dizer
  // "nada na mao" aqui seria inventar.
  if (comVoto == 0) {
    if (confianca) *confianca = 0;
    return OBJ_INDEFINIDO;
  }

  // Nenhum dedo longo instrumentado encontrou nada.
  if (tocaram == 0) {
    const bool dedaoInstrumentado = (a.comSensor & LIMBIA_BIT(DEDAO)) != 0;
    if (dedaoInstrumentado && a.tocou[DEDAO]) {
      // O polegar achou algo que os longos nao acharam: objeto preso
      // contra a palma. E leitura valida, mas incompleta.
      if (confianca) *confianca = 45;
      return OBJ_INDEFINIDO;
    }
    // Mao vazia. So da para afirmar isso com confianca alta se o polegar
    // tambem estiver instrumentado - senao ele pode estar segurando algo
    // fino sem que ninguem veja.
    if (confianca) *confianca = dedaoInstrumentado ? 90 : 60;
    return OBJ_NENHUM;
  }

  const uint16_t media        = (uint16_t)(soma / tocaram);
  const uint16_t espalhamento = (uint16_t)(maior - menor);

  // Espalhamento alto quer dizer que os dedos pararam em profundidades
  // muito diferentes - o que so acontece com superficie que nao e
  // redonda. E o caso do cartao e do celular.
  const bool disperso = espalhamento > 250;

  uint8_t objeto;
  if (media >= 800) {
    objeto = OBJ_FINO;  // fecharam quase tudo: caneta, chave, talher
  } else if (media <= 250) {
    objeto = OBJ_GRANDE;  // mal sairam do lugar
  } else if (disperso) {
    objeto = OBJ_PLANO;
  } else {
    objeto = OBJ_CILINDRICO;  // pararam juntos, no meio do curso
  }

  // Confianca: quantos dedos concordaram e quao longe a media ficou das
  // fronteiras. Assinatura em cima da fronteira devolve numero baixo, e
  // e assim que tem que ser.
  uint16_t c                   = 40 + (uint16_t)tocaram * 10;  // 50..80
  const uint16_t distFronteira = (media > 800   ? media - 800
                                  : media < 250 ? 250 - media
                                                : (media < 525 ? media - 250 : 800 - media));
  c += distFronteira > 200 ? 20 : (distFronteira / 10);
  if (objeto == OBJ_CILINDRICO && espalhamento < 80) c += 10;

  // Mao parcialmente instrumentada nao pode dar resposta de mao inteira:
  // com dois dos quatro dedos cegos, metade da forma do objeto e chute.
  c = (c * comVoto) / N_DEDOS_LONGOS;

  if (c > 100) c = 100;
  if (confianca) *confianca = (uint8_t)c;

  return objeto;
}

// ---------------------------------------------------------------------
//  Tendao frouxo
// ---------------------------------------------------------------------
bool tendaoFrouxo(const AssinaturaPreensao& a, uint8_t junta, uint16_t limiarMa) {
  if (junta >= N_JUNTAS) return false;
  // Sem sensor de corrente nao ha como distinguir tendao frouxo de dedo
  // que simplesmente nao encontrou nada. Nao diagnosticar e a resposta
  // correta - melhor calado que errado.
  if (!(a.comSensor & LIMBIA_BIT(junta))) return false;
  // A junta foi ate o fim do curso E a corrente nunca passou do limiar.
  // Servo girando sem carga = tendao com folga, ou tendao arrebentado.
  return (!a.tocou[junta]) && (a.contatoEm[junta] >= 1000) && (a.forcaMa[junta] < limiarMa);
}

uint8_t juntasComFolga(const AssinaturaPreensao& a, uint16_t limiarMa) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < N_JUNTAS; i++) {
    if (i == PULSO) continue;  // o punho nao tem tendao
    if (tendaoFrouxo(a, i, limiarMa)) n++;
  }
  return n;
}

}  // namespace limbia

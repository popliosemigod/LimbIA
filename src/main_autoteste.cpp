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
//  Nao ha compilador de host nesta bancada. Rodar o teste na placa em
//  que o codigo vai ser executado tambem elimina o erro classico de o
//  algoritmo passar no notebook e errar no microcontrolador, onde int
//  tem outro tamanho e float e mais lento.
//
//  Gravar:   pio run -e autoteste -t upload
//  Ver:      pio device monitor -e autoteste
// =====================================================================

#include <Arduino.h>

#include <limbia_mao.h>

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

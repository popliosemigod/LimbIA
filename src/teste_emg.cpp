// =====================================================================
//  LimbIA - teste_emg.cpp
//  Primeiro sinal de musculo: UM eletrodo no GPIO 4 do ESP32-C3.
//
//  Sem rede, sem tela, sem modelo. So a mesma cadeia de filtros da
//  protese (limbia_emg: passa-altas 20 Hz, notch 60 Hz, retificacao,
//  envelope 4 Hz) e uma barra no console. Serve para responder UMA
//  pergunta antes de qualquer calibracao: o eletrodo enxerga o musculo?
//
//  Ao ligar, mede 3 s de repouso. Depois mostra o envelope e quantas
//  vezes ele esta acima do repouso - a mesma razao que a tela de ajuste
//  exige que passe de 3x.
//
//  SEGURANCA: eletrodo na pele com a placa no USB so com o notebook FORA
//  DA TOMADA (na bateria). Nunca com PC de mesa.
//
//  Console:  r  medir o repouso de novo     v  modo Serial Plotter
//            e  alterna pino RAW / pino SIG (envelope) do MyoWare
//            d  captura 1 s cru (test/host/espectro_emg.py)
//            z  zera o pico                 ?  ajuda
//
//  Sensores aceitos:
//    MyoWare AT-04-001 - + no 3V3, - no GND, RAW no GPIO 4.
//    AD8232            - 3.3V (NUNCA 5 V: maximo absoluto 3,6 V), GND,
//                        OUTPUT no GPIO 4, LO+ no 5 e LO- no 6. Os dois
//                        LO sao deteccao de eletrodo solto em hardware.
// =====================================================================
#include <Arduino.h>

#include <limbia_emg.h>

#include "config_emg.h"

static const uint16_t AMOSTRAS_BLOCO  = 20;    // 20 ms por bloco = 50 Hz na saida
static const uint32_t REPOUSO_MS      = 3000;  // quanto tempo medir o repouso
static const uint8_t BLOCOS_POR_LINHA = 5;     // uma linha de texto a cada 100 ms
static const uint8_t LARGURA_BARRA    = 30;
static const float RAZAO_TOPO_BARRA   = 30.0f;  // a barra enche em 30x o repouso

struct Bloco {
  float envelope;   // media do envelope no bloco, em contagens
  uint16_t minimo;  // do sinal cru, para o pico-a-pico
  uint16_t maximo;
  uint16_t saturadas;
  uint16_t mv;    // nivel DC do pino, uma leitura calibrada por bloco
  uint8_t solto;  // bit 0: LO+ alto, bit 1: LO- alto (so AD8232)
};

// Eletrodo solto pelo proprio chip (AD8232, pinos LOD+ e LOD-). Com
// MyoWare os pinos ficam em nivel baixo e isto nunca acusa - por isso
// nao atrapalha os dois sensores.
// Desligavel pelo console: se os pinos LO estiverem mal ligados, ou o
// modulo em outro modo de leads-off, eles acusam solto para sempre e
// travam a medida. Ignorar e pior do que medir, mas melhor do que nao
// poder medir - por isso o aviso continua aparecendo na tela.
static bool usaLo = true;

static uint8_t leSolto() {
  if (!usaLo) return 0;
  uint8_t s = 0;
  if (digitalRead(PIN_LO_MAIS)) s |= 1;
  if (digitalRead(PIN_LO_MENOS)) s |= 2;
  return s;
}

static QueueHandle_t fila = nullptr;
static limbia::CadeiaEmg cadeia;
static volatile bool reconfigurar = false;
static bool entradaBruta          = EMG_ENTRADA_BRUTA != 0;
// Passa-altas da cadeia. 20 Hz e o da protese: abaixo disso e artefato de
// movimento. Mas o AD8232 corta acima de ~40 Hz, e entao 20 Hz deixa uma
// janela estreita demais - com ele vale medir tambem em 8 Hz, sabendo que
// parte do que entra e movimento, nao musculo.
static float passaAltasHz            = EMG_PASSA_ALTAS_HZ;
static volatile uint32_t descartados = 0;

// Captura crua: 1 s de amostras sem filtro, para ver o espectro no PC
// (test/host/espectro_emg.py). E o que separa musculo de rede eletrica.
static const uint16_t N_CAPTURA = 1000;
static uint16_t captura[N_CAPTURA];
static volatile bool capturando    = false;
static volatile bool capturaPronta = false;
static uint16_t nCaptura           = 0;

// ---------------------------------------------------------------------
//  Amostragem a 1 kHz numa tarefa - o mesmo esquema de emg_aquisicao.h,
//  para um canal. O loop pode atrasar imprimindo; a amostragem nao.
// ---------------------------------------------------------------------
static void tarefa(void*) {
  Bloco b;
  float soma  = 0;
  uint16_t n  = 0;
  b.minimo    = 4095;
  b.maximo    = 0;
  b.saturadas = 0;

  TickType_t ultimo = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(1000 / EMG_FS_HZ));

    if (reconfigurar) {
      limbia::ParametrosCadeia p = parametrosCadeia();
      p.entradaBruta             = entradaBruta;
      p.passaAltasHz             = passaAltasHz;
      limbia::cadeiaConfigura(&cadeia, p);
      reconfigurar = false;
    }

    const uint16_t bruto = analogRead(PIN_EMG_TESTE);
    if (capturando) {
      captura[nCaptura++] = bruto;
      if (nCaptura >= N_CAPTURA) {
        capturando    = false;
        capturaPronta = true;
      }
    }
    if (bruto < b.minimo) b.minimo = bruto;
    if (bruto > b.maximo) b.maximo = bruto;
    if (bruto <= EMG_TRILHO_BAIXO || bruto >= EMG_TRILHO_ALTO) b.saturadas++;
    soma += limbia::cadeiaProcessa(&cadeia, (float)bruto);

    if (++n >= AMOSTRAS_BLOCO) {
      b.envelope = soma / n;
      b.mv       = analogReadMilliVolts(PIN_EMG_TESTE);
      b.solto    = leSolto();
      if (xQueueSend(fila, &b, 0) != pdTRUE) descartados++;
      soma        = 0;
      n           = 0;
      b.minimo    = 4095;
      b.maximo    = 0;
      b.saturadas = 0;
    }
  }
}

// ---------------------------------------------------------------------
//  Estado do console
// ---------------------------------------------------------------------
static bool plotter              = false;
static bool medindoRepouso       = true;
static uint32_t inicioRepouso    = 0;
static uint32_t ultimoAviso      = 0;
static uint32_t ultimoAvisoSolto = 0;
static float somaRepouso         = 0;
static uint32_t nRepouso         = 0;
static float repouso             = 0;
static float pico                = 0;
// Saturacao desde o ultimo resumo: com o sinal batendo no trilho, o
// envelope mede o recorte, nao o musculo, e a razao nao vale nada.
static uint32_t satResumo      = 0;
static uint32_t amostrasResumo = 0;

// acumulado da linha de texto
static float lEnv   = 0;
static uint16_t lPP = 0, lSat = 0, lMv = 0;
static uint8_t lSolto = 0;
static uint8_t lN     = 0;

static void ajuda() {
  Serial.println();
  Serial.println(F("============ LimbIA - teste de EMG no GPIO 4 ============"));
  Serial.println(F("  r  medir o repouso de novo   z  zera o pico"));
  Serial.println(F("  v  alterna texto / Serial Plotter"));
  Serial.println(F("  d  captura 1 s cru (test/host/espectro_emg.py)"));
  Serial.printf("  f  passa-altas 20 Hz / 8 Hz (agora: %.0f Hz)\n", passaAltasHz);
  Serial.printf("  l  usar / ignorar os pinos LO (agora: %s)\n", usaLo ? "em uso" : "ignorados");
  Serial.println(F("  e  alterna pino RAW / pino SIG (envelope) do MyoWare"));
  Serial.printf("  entrada: %s\n", entradaBruta ? "RAW do MyoWare" : "SIG do MyoWare (envelope)");
  Serial.println(F("  MyoWare: + no 3V3, - no GND, RAW no GPIO 4, chave em ON."));
  Serial.println(F("  AD8232: 3.3V (NUNCA 5 V), GND, OUTPUT no GPIO 4,"));
  Serial.println(F("          LO+ no GPIO 5 e LO- no GPIO 6 (eletrodo solto)."));
  Serial.println(F("  Notebook na BATERIA com o eletrodo na pele."));
  Serial.println(F("========================================================="));
}

static void comecaRepouso() {
  medindoRepouso = true;
  inicioRepouso  = millis();
  ultimoAviso    = 0;
  somaRepouso    = 0;
  nRepouso       = 0;
  pico           = 0;
  Serial.println(F("\n>>> RELAXE o braco por 3 segundos: medindo o repouso..."));
}

static void console() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'r':
      case 'R': comecaRepouso(); break;
      case 'z':
      case 'Z':
        pico = 0;
        Serial.println(F("  pico zerado"));
        break;
      case 'v':
      case 'V': plotter = !plotter; break;
      case 'e':
      case 'E':
        entradaBruta = !entradaBruta;
        reconfigurar = true;
        Serial.printf("  entrada: %s\n", entradaBruta ? "CRUA" : "ENVELOPE");
        comecaRepouso();
        break;
      case 'l':
      case 'L':
        usaLo = !usaLo;
        Serial.printf("  pinos LO (eletrodo solto): %s\n",
                      usaLo ? "em uso" : "IGNORADOS - a medida pode ser ruido");
        comecaRepouso();
        break;
      case 'f':
      case 'F':
        passaAltasHz = passaAltasHz > 10.0f ? 8.0f : EMG_PASSA_ALTAS_HZ;
        reconfigurar = true;
        Serial.printf("  passa-altas: %.0f Hz%s\n", passaAltasHz,
                      passaAltasHz < 10.0f ? " (entra movimento junto)" : "");
        comecaRepouso();
        break;
      case 'd':
      case 'D':
        if (!capturando && !capturaPronta) {
          nCaptura   = 0;
          capturando = true;
        }
        break;
      case '?': ajuda(); break;
      default: break;
    }
  }
}

// A barra e logaritmica: 1x = vazia, RAZAO_TOPO_BARRA = cheia. Musculo
// vai de 3x a 30x do repouso, e numa escala linear tudo abaixo de 10x
// pareceria parado.
static void imprimeLinha(float env, uint16_t pp, uint16_t mv, float satPct, uint8_t solto) {
  // Sinal no trilho: o envelope mede o recorte. Nunca chamar isso de musculo.
  const bool saturou = satPct > QUAL_SATURACAO * 100.0f;
  const float razao  = repouso > 0.5f ? env / repouso : 0;
  int cheio          = 0;
  if (razao > 1.0f) cheio = (int)(log(razao) / log(RAZAO_TOPO_BARRA) * LARGURA_BARRA + 0.5f);
  if (cheio > LARGURA_BARRA) cheio = LARGURA_BARRA;
  const int marca = (int)(log(QUAL_RAZAO_OK) / log(RAZAO_TOPO_BARRA) * LARGURA_BARRA + 0.5f);

  char barra[LARGURA_BARRA + 1];
  for (int i = 0; i < LARGURA_BARRA; i++) barra[i] = i < cheio ? '#' : (i == marca ? ':' : ' ');
  barra[LARGURA_BARRA] = 0;

  Serial.printf("env %5.0f  %5.1fx [%s] %s pp %4u  dc %4u mV", env, razao, barra,
                razao >= QUAL_RAZAO_OK && !saturou ? "MUSCULO" : "       ", pp, mv);
  if (saturou) Serial.printf("  SATURANDO %.0f%%", satPct);
  if (solto) {
    Serial.printf("  ELETRODO SOLTO (%s%s)", (solto & 1) ? "LO+ " : "", (solto & 2) ? "LO-" : "");
  }
  if (mv < 300 || mv > 2400) Serial.print(F("  DC fora do meio"));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  // O C3 com USB nativo perde o que imprime antes de o PC abrir a porta.
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_EMG_TESTE, ADC_11db);
  // Pull-down: sensor sem esses pinos (MyoWare) le "colado" e nao alarma.
  pinMode(PIN_LO_MAIS, INPUT_PULLDOWN);
  pinMode(PIN_LO_MENOS, INPUT_PULLDOWN);

  limbia::ParametrosCadeia p = parametrosCadeia();
  p.entradaBruta             = entradaBruta;
  p.passaAltasHz             = passaAltasHz;
  limbia::cadeiaConfigura(&cadeia, p);

  fila = xQueueCreate(16, sizeof(Bloco));
  xTaskCreatePinnedToCore(tarefa, "emg", 4096, nullptr, 5, nullptr, portNUM_PROCESSORS > 1 ? 1 : 0);

  ajuda();
  comecaRepouso();
}

void loop() {
  console();

  if (capturaPronta) {
    Serial.printf("CAPTURA_INICIO fs=%u n=%u\n", EMG_FS_HZ, N_CAPTURA);
    for (uint16_t i = 0; i < N_CAPTURA; i++) {
      Serial.print(captura[i]);
      Serial.print((i % 20 == 19) ? '\n' : ',');
    }
    Serial.println(F("CAPTURA_FIM"));
    capturaPronta = false;
  }

  Bloco b;
  while (xQueueReceive(fila, &b, 0) == pdTRUE) {
    if (medindoRepouso) {
      // Eletrodo solto durante o repouso invalida a medida inteira: em vez
      // de gravar um repouso que e ruido, espera o contato voltar.
      if (b.solto) {
        if (millis() - ultimoAvisoSolto > 2000) {
          ultimoAvisoSolto = millis();
          Serial.printf("    ELETRODO SOLTO (%s%s): cole firme, a medida recomeca sozinha\n",
                        (b.solto & 1) ? "LO+ " : "", (b.solto & 2) ? "LO-" : "");
        }
        // Mostrar a leitura mesmo sem contato: e olhando a linha que se ve
        // o contato voltar enquanto se ajusta o eletrodo. O que nao se faz
        // e gravar esse trecho como repouso.
        if (!plotter) {
          const uint16_t ppSolto = b.maximo - b.minimo;
          lEnv += b.envelope;
          if (ppSolto > lPP) lPP = ppSolto;
          lSat += b.saturadas;
          lMv = b.mv;
          lSolto |= b.solto;
          if (++lN >= BLOCOS_POR_LINHA) {
            imprimeLinha(lEnv / lN, lPP, lMv, 100.0f * lSat / (lN * AMOSTRAS_BLOCO), lSolto);
            lEnv   = 0;
            lPP    = 0;
            lSat   = 0;
            lN     = 0;
            lSolto = 0;
          }
        }
        inicioRepouso = millis();
        somaRepouso   = 0;
        nRepouso      = 0;
        ultimoAviso   = 0;
        continue;
      }
      // O primeiro meio segundo e o filtro assentando: fica de fora.
      const uint32_t decorrido = millis() - inicioRepouso;
      if (decorrido > 500) {
        somaRepouso += b.envelope;
        nRepouso++;
      }
      if (decorrido / 1000 != ultimoAviso && decorrido < REPOUSO_MS && !plotter) {
        ultimoAviso = decorrido / 1000;
        Serial.printf("    %lu...\n", (unsigned long)(3 - ultimoAviso));
      }
      if (decorrido >= REPOUSO_MS) {
        repouso        = nRepouso ? somaRepouso / nRepouso : 0;
        medindoRepouso = false;
        Serial.printf(">>> repouso = %.1f contagens (dc %u mV). Agora FECHE a mao com forca.\n",
                      repouso, b.mv);
        if (repouso < 0.5f) {
          Serial.println(F("    repouso quase zero: pino sem sinal? Confira fio e alimentacao."));
        }
      }
      continue;
    }

    if (b.envelope > pico) pico = b.envelope;
    satResumo += b.saturadas;
    amostrasResumo += AMOSTRAS_BLOCO;

    if (plotter) {
      Serial.printf("envelope:%.1f,repouso:%.1f,limiar3x:%.1f\n", b.envelope, repouso,
                    repouso * QUAL_RAZAO_OK);
      continue;
    }

    lEnv += b.envelope;
    const uint16_t pp = b.maximo - b.minimo;
    if (pp > lPP) lPP = pp;
    lSat += b.saturadas;
    lMv = b.mv;
    lSolto |= b.solto;
    if (++lN >= BLOCOS_POR_LINHA) {
      imprimeLinha(lEnv / lN, lPP, lMv, 100.0f * lSat / (lN * AMOSTRAS_BLOCO), lSolto);
      lEnv   = 0;
      lPP    = 0;
      lSat   = 0;
      lN     = 0;
      lSolto = 0;
    }
  }

  // Resumo a cada 5 s: o numero que vai para o diario.
  static uint32_t ultimoResumo = 0;
  if (!plotter && !medindoRepouso && millis() - ultimoResumo >= 5000) {
    ultimoResumo       = millis();
    const float satPct = amostrasResumo ? 100.0f * satResumo / amostrasResumo : 0.0f;
    Serial.printf("--- repouso %.1f | pico %.1f | pico/repouso %.1fx | blocos perdidos %lu",
                  repouso, pico, repouso > 0.5f ? pico / repouso : 0.0f,
                  (unsigned long)descartados);
    if (satPct > QUAL_SATURACAO * 100.0f) {
      Serial.printf(
          "\n    RAZAO INVALIDA: %.1f%% das amostras no trilho. Refaca o contato e mande r.",
          satPct);
    }
    Serial.println();
    satResumo      = 0;
    amostrasResumo = 0;
  }
}

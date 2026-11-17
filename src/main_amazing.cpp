// =====================================================================
//  LimbIA - main_amazing.cpp
//  Mao Amazing Hand (Pollen Robotics) com 8 MG90S num PCA9685, comandada
//  por um MyoWare, numa ESP32-C3 SuperMini.
//
//  O que faz
//  ---------
//  1. Liga com o PCA9685 nas 16 saidas em "full off": os servos ficam sem
//     pulso, soltos, e nao se movem. O MUSCULO fica TRAVADO.
//  2. Descobre se o MyoWare esta na saida RAW ou SIG/ENV pelo nivel medio.
//  3. Espera a CALIBRACAO (botao na pagina ou `cal`). Ela e adaptativa:
//     pede contracoes ate ter certeza, e prova o limiar antes de aceitar
//     (limbia_sinal.h). So depois de aprovada o MUSCULO comanda a mao.
//     O controle MANUAL (pagina e console) vale sempre, mas devagar.
//  4. Liberou: AUTO-HOME (mao aberta: todos os servos no centro).
//  5. Liberada, COMANDO ALTERNADO: contraiu uma vez, fecha; contraiu de
//     novo, abre. Pela pagina: abrir, fechar, auto-home e cada um dos 8
//     servos individualmente.
//  6. Rede Wi-Fi ABERTA "LimbIA-mao", sem internet. Conectou, a pagina
//     abre sozinha (portal cativo) em http://192.168.4.1.
//  7. Serial Plotter na USB: 50 linhas por segundo.
//
//  Cada dedo sao DOIS servos em sentidos opostos (limbia_paralelo.h). Um
//  servo controlado sozinho anda em "curso": 0 = casa (mao aberta) ate o
//  limite de flexao (90 no maximo), sempre para o lado que fecha o dedo.
//
//  Quem faz o que
//  --------------
//  Tarefa do EMG (FreeRTOS, 1 kHz cravado por vTaskDelayUntil): le o ADC,
//  filtra, decide, roda a calibracao e e dona da TRAVA. O loop so pede
//  coisas por uma fila de comandos e le o que ela publica. Loop: servos
//  (I2C), Wi-Fi, console e Serial Plotter.
//
//  Modo seguro: no boot nenhum servo se move; o musculo so comanda depois
//  da calibracao; PCA9685 ausente para tudo; entrada RAW saturada congela
//  o que o musculo pediu.
//
//  Console serial (115200, fim de linha LF):
//    ?                 ajuda e estado
//    cal / cancela     calibracao (siga as instrucoes)
//    abre / fecha      mao inteira, devagar (sempre)
//    home              auto-home = mao aberta, devagar (sempre)
//    servo <i> <c>     servo i (0..7) no curso c (0..limite), devagar (sempre)
//    lim <g>           limite de flexao, 0..90
//    centro <d> <a> <b>   centro dos dois servos do dedo d (graus abs.)
//    inv <d>           inverte o sentido de flexao do dedo d
//    dedos <mask>      quais dedos se movem, ex: dedos 1 (so indicador)
//    liga <l> <d>      ajuste fino do limiar depois de calibrado
//    raw / env / auto  tipo de saida do MyoWare
//    i2c               diagnostico do barramento e nova tentativa no PCA
//    t                 liga/desliga as linhas do Serial Plotter
// =====================================================================
#include <Adafruit_PWMServoDriver.h>
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <limbia_emg.h>
#include <limbia_paralelo.h>
#include <limbia_sinal.h>

#include "amazing_html.h"
#include "config_amazing.h"

using namespace limbia;

#define N_SERVOS (2 * AMZ_N_DEDOS)

// =====================================================================
//  Estado compartilhado entre a tarefa do EMG e o loop
// =====================================================================

// Um ponto do grafico a cada AMZ_DECIMACAO amostras.
enum : uint8_t {
  PT_CONTRACAO = 1,
  PT_FECHADA   = 2,
  PT_LIBERADA  = 4
};
struct Ponto {
  uint16_t adc;
  uint16_t env10;  // envelope x 10
  uint8_t flags;   // PT_*
};
static Ponto historico[AMZ_HISTORICO];
static volatile uint32_t nPontos = 0;  // total ja escrito (o indice e nPontos % N)

enum TipoCmd : uint8_t {
  CMD_CALIBRA,
  CMD_CANCELA,
  CMD_LIMIAR,
  CMD_ABRE,
  CMD_FECHA,
  CMD_ENTRADA
};
struct Comando {
  TipoCmd tipo;
  float a, b;
};
static QueueHandle_t filaCmd;

enum TipoEvento : uint8_t {
  EV_ENTRADA,
  EV_FECHOU,
  EV_ABRIU,
  EV_REJEITOU,
  EV_CAL_ETAPA,
  EV_CAL_FIM,
  EV_LIMIAR,
  EV_SATURADO
};
struct Evento {
  TipoEvento tipo;
  uint32_t ms;
  float v1, v2;
};
static QueueHandle_t filaEv;

// Publicado pela tarefa, lido pelo loop. Leitura "rasgada" no meio de uma
// escrita so afeta o que aparece na tela por 100 ms - nunca a decisao,
// que mora inteira dentro da tarefa.
struct Publico {
  bool detectando, raw, saturado, fechada;
  bool liberada;   // a TRAVA: so vira true com calibracao aprovada
  bool contracao;  // contracao reconhecida agora (a "lampada" da pagina)
  float liga, desliga, teto, envelope;
  uint8_t calEtapa, calCiclo, calProva, calBoas, calAval;
  uint32_t calRestante;
  const char* calMotivo;
  bool temRes;
  ResultadoCal res;
  uint32_t espigoes, rejeitadas, contracoes;
};
static Publico PUB;

static const TemposCal TEMPOS_CAL = {AMZ_CAL_PREPARO_MS,  AMZ_CAL_REPOUSO_MS,   AMZ_CAL_CONTRAIA_MS,
                                     AMZ_CAL_RELAXE_MS,   AMZ_CAL_DESCARTE_MS,  AMZ_CAL_BOAS,
                                     AMZ_CAL_CICLOS_MAX,  AMZ_CAL_PROVA,        AMZ_CAL_PROVAS_MAX,
                                     AMZ_CAL_FRACAO_LIGA, AMZ_CAL_FRACAO_SOLTA, AMZ_CAL_SEPARACAO,
                                     AMZ_CAL_RAZAO,       AMZ_CAL_CONSISTENCIA, AMZ_CAL_TETO_FATOR,
                                     AMZ_ATIVA_MS,        AMZ_SOLTA_MS};

// =====================================================================
//  Tarefa do EMG
// =====================================================================
static void emite(TipoEvento t, uint32_t ms, float v1 = 0, float v2 = 0) {
  Evento e = {t, ms, v1, v2};
  xQueueSend(filaEv, &e, 0);  // fila cheia: perde o aviso, nunca a amostra
}

static void tarefaEmg(void*) {
  ParametrosFiltro pf;
  pf.fsHz         = AMZ_EMG_FS_HZ;
  pf.entradaBruta = false;
  pf.hampelK      = AMZ_HAMPEL_K;
  pf.hampelPiso   = AMZ_HAMPEL_PISO;
  pf.passaAltasHz = AMZ_EMG_PASSA_ALTAS_HZ;
  pf.redeHz       = AMZ_EMG_REDE_HZ;
  pf.notchQ       = AMZ_EMG_NOTCH_Q;
  pf.envelopeHz   = AMZ_EMG_ENVELOPE_HZ;
  pf.rapidoHz     = AMZ_EMG_RAPIDO_HZ;
  FiltroEmg filtro;
  filtroConfigura(&filtro, pf);

  // Sem limiar ate a calibracao: nada e reconhecido como contracao.
  ParametrosAlterna pa;
  pa.ativ.liga    = 0;
  pa.ativ.desliga = 0;
  pa.ativ.ligaMs  = AMZ_ATIVA_MS;
  pa.ativ.soltaMs = AMZ_SOLTA_MS;
  pa.refratarioMs = AMZ_REFRATARIO_MS;
  pa.tetoArtefato = 0;
  DetectorAlterna det;
  alternaZera(&det, false);

  CalibracaoGuiada cal;
  memset(&cal, 0, sizeof(cal));
  cal.etapa  = CAL_PARADA;
  cal.motivo = "";

  bool liberada     = false;  // A TRAVA
  bool lampada      = false;
  bool detectando   = true;
  int8_t forca      = -1;  // -1 auto, 0 SIG/ENV, 1 RAW
  double somaDet    = 0;
  uint32_t nDet     = 0;
  uint32_t detDesde = millis();
  uint16_t satN = 0, satTotal = 0;
  uint32_t satDesde = millis();
  bool saturado     = false;
  uint8_t dec       = 0;
  float env         = 0;

  TickType_t prox = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&prox, 1);  // 1 ms: tick do FreeRTOS e 1 kHz no core
    const uint32_t agora = millis();

    // --- pedidos do loop
    Comando c;
    while (xQueueReceive(filaCmd, &c, 0) == pdTRUE) {
      switch (c.tipo) {
        case CMD_CALIBRA:
          // Recalibrar TRAVA de novo: o limiar antigo deixou de valer.
          liberada = false;
          lampada  = false;
          calInicia(&cal, agora);
          PUB.temRes = false;
          emite(EV_CAL_ETAPA, agora);
          break;
        case CMD_CANCELA:
          if (calEmAndamento(cal)) {
            calCancela(&cal);
            emite(EV_CAL_FIM, agora);
          }
          break;
        case CMD_LIMIAR:
          if (liberada) {
            pa.ativ.liga    = c.a;
            pa.ativ.desliga = c.b;
            emite(EV_LIMIAR, agora, pa.ativ.liga, pa.ativ.desliga);
          }
          break;
        case CMD_ABRE:
        case CMD_FECHA:
          det.fechada       = c.tipo == CMD_FECHA;
          det.jaTrocou      = true;  // refratario: nao desfaz no mesmo instante
          det.ultimaTrocaMs = agora;
          break;
        case CMD_ENTRADA:
          forca      = (int8_t)c.a;
          detectando = true;
          somaDet    = 0;
          nDet       = 0;
          detDesde   = agora;
          liberada   = false;  // limiar de outra escala: calibrar de novo
          break;
      }
    }

    const uint16_t x = (uint16_t)analogRead(AMZ_PINO_EMG);

    // --- saturacao: so conta na RAW (na SIG, trilho alto e contracao forte)
    satTotal++;
    if (x >= AMZ_TRILHO_ALTO || x <= AMZ_TRILHO_BAIXO) satN++;
    if (agora - satDesde >= AMZ_JANELA_SATURACAO_MS) {
      const bool s = pf.entradaBruta && !detectando && (float)satN / satTotal > AMZ_SATURACAO_MAX;
      if (s && !saturado) emite(EV_SATURADO, agora);
      saturado = s;
      satN = satTotal = 0;
      satDesde        = agora;
    }

    // --- deteccao do tipo de saida
    if (detectando) {
      somaDet += x;
      nDet++;
      if (agora - detDesde >= AMZ_EMG_DETECCAO_MS) {
        const float media = (float)(somaDet / nDet);
        pf.entradaBruta   = forca >= 0 ? forca == 1 : media > AMZ_EMG_LIMIAR_RAW_CONTAGENS;
        filtroConfigura(&filtro, pf);
        // Regime pela MEDIA dos 300 ms, nunca por uma amostra: um espigao
        // no instante do boot fecharia a mao sozinha (autoteste 13/11).
        filtroPreaquece(&filtro, media, pf);
        detectando = false;
        emite(EV_ENTRADA, agora, media, forca);
      }
    } else {
      env = filtroPassa(&filtro, (float)x, pf);

      if (calEmAndamento(cal)) {
        // Durante o exercicio a mao NAO alterna (e esta travada).
        if (calAtualiza(&cal, env, filtro.rapido, agora, TEMPOS_CAL)) {
          if (cal.etapa == CAL_OK) {
            pa.ativ.liga    = cal.r.liga;
            pa.ativ.desliga = cal.r.desliga;
            pa.tetoArtefato = cal.r.teto;
            // Liberou: a mao parte ABERTA (o loop faz o auto-home). A
            // ultima contracao da prova nao vale como comando.
            alternaZera(&det, false);
            det.jaTrocou      = true;
            det.ultimaTrocaMs = agora;
            liberada          = true;
          }
          if (cal.etapa == CAL_OK || cal.etapa == CAL_FALHOU) {
            PUB.res    = cal.r;
            PUB.temRes = true;
            emite(EV_CAL_FIM, agora);
          } else {
            emite(EV_CAL_ETAPA, agora);
          }
        }
        // Lampada na prova: o detector da propria calibracao.
        lampada =
            (cal.etapa == CAL_PROVA_CONTRAIA || cal.etapa == CAL_PROVA_RELAXE) && cal.det.a.ativo;
      } else if (liberada && !saturado) {
        const EventoAlterna ev = alternaAtualiza(&det, env, filtro.rapido, agora, pa);
        if (ev == ALT_FECHOU || ev == ALT_ABRIU) {
          PUB.contracoes++;
          lampada = true;
          emite(ev == ALT_FECHOU ? EV_FECHOU : EV_ABRIU, agora, env);
        }
        if (ev == ALT_REJEITOU) {
          // Contracao de verdade no refratario ainda acende a lampada:
          // e musculo, so nao troca o estado. Curta ou pancada, nao.
          if (det.motivo == REJ_REFRATARIO) lampada = true;
          emite(EV_REJEITOU, agora, det.motivo, det.picoCandidato);
        }
        if (!det.a.ativo) lampada = false;
      } else {
        lampada = false;
      }
    }

    // --- publica
    PUB.detectando  = detectando;
    PUB.raw         = pf.entradaBruta;
    PUB.saturado    = saturado;
    PUB.fechada     = det.fechada;
    PUB.liberada    = liberada;
    PUB.contracao   = lampada;
    PUB.liga        = calEmAndamento(cal) ? cal.pDet.ativ.liga : pa.ativ.liga;
    PUB.desliga     = calEmAndamento(cal) ? cal.pDet.ativ.desliga : pa.ativ.desliga;
    PUB.teto        = pa.tetoArtefato;
    PUB.envelope    = env;
    PUB.calEtapa    = cal.etapa;
    PUB.calCiclo    = cal.ciclo;
    PUB.calProva    = cal.provaCiclo;
    PUB.calBoas     = cal.nBoas;
    PUB.calAval     = cal.avaliacao;
    PUB.calRestante = calRestanteMs(cal, agora, TEMPOS_CAL);
    PUB.calMotivo   = cal.motivo;
    PUB.espigoes    = filtro.hampel.trocadas;
    PUB.rejeitadas  = det.rejeitadas;

    if (++dec >= AMZ_DECIMACAO) {
      dec      = 0;
      Ponto& p = historico[nPontos % AMZ_HISTORICO];
      p.adc    = x;
      p.env10  = (uint16_t)limita(env * 10.0f, 0, 65535);
      p.flags  = (lampada ? PT_CONTRACAO : 0) | (det.fechada ? PT_FECHADA : 0) |
                (liberada ? PT_LIBERADA : 0);
      nPontos = nPontos + 1;
    }
  }
}

// =====================================================================
//  Mao: os 8 servos no PCA9685
//
//  Cada servo anda em CURSO: 0 = casa (mao aberta, servo no centro) ate o
//  limite de flexao, sempre para o lado que fecha o dedo. Servo A de cada
//  dedo soma, servo B subtrai (com `sentido` invertendo os dois) - a regra
//  do Amazing Hand (limbia_paralelo.h). Assim nenhum comando, nem o de um
//  servo sozinho, estende o dedo alem da casa.
//
//  Duas origens de movimento (decisao de 17/11, revista no mesmo dia):
//   - MUSCULO: so com a calibracao aprovada (a trava). Velocidade normal.
//   - MANUAL (pagina e console, o desenvolvedor): SEMPRE, travada ou nao,
//     mas DEVAGAR (AMZ_VEL_MANUAL_GRAUS_S). E um processo controlado: quem
//     move esta olhando a mao.
//  No boot ninguem se move: os servos ficam sem pulso ate alguem pedir.
// =====================================================================
static Adafruit_PWMServoDriver pca(AMZ_PCA_ENDERECO);
static bool pcaOk       = false;
static uint8_t comPulso = 0;  // bit s = servo s recebendo pulso; 0 = solto
static DedoParalelo dedos[AMZ_N_DEDOS];
static uint8_t mascaraDedos = (1u << AMZ_N_DEDOS) - 1;
static LimitesParalelo limites;
static float cursoAtual[N_SERVOS];
static float cursoAlvo[N_SERVOS];
static bool alvoManual[N_SERVOS];  // o alvo atual veio do desenvolvedor: anda devagar

static const char* const NOME_SERVO[N_SERVOS] = {"indicador A", "indicador B", "medio A",
                                                 "medio B",     "anelar A",    "anelar B",
                                                 "polegar A",   "polegar B"};

static uint8_t canalServo(uint8_t s) {
  return (s & 1) ? dedos[s / 2].canalB : dedos[s / 2].canalA;
}

// Curso -> graus absolutos do servo.
static float grausServo(uint8_t s, float curso) {
  const DedoParalelo& d = dedos[s / 2];
  const float sen       = d.sentido < 0 ? -1.0f : 1.0f;
  const float g         = (s & 1) ? d.centroB - sen * curso : d.centroA + sen * curso;
  return limita(g, AMZ_SERVO_MIN, AMZ_SERVO_MAX);
}

static void escreveServo(uint8_t s) {
  const float us = servoGrausParaUs(grausServo(s, cursoAtual[s]), AMZ_US_EM_0, AMZ_US_EM_180);
  pca.setPWM(canalServo(s), 0, usParaTicksPca(us, AMZ_PWM_HZ));
}

static bool servoAtivo(uint8_t s) {
  return mascaraDedos & (1u << (s / 2));
}

static bool temPulso(uint8_t s) {
  return comPulso & (1u << s);
}

static bool iniciaPca() {
  Wire.beginTransmission(AMZ_PCA_ENDERECO);
  if (Wire.endTransmission() != 0) return false;
  pca.begin();
  pca.setOscillatorFrequency(AMZ_PCA_OSCILADOR_HZ);
  pca.setPWMFreq(AMZ_PWM_HZ);
  delay(10);  // so no boot ou no `i2c`: o oscilador do PCA9685 precisa de 500 us
  // As 16 saidas em FULL OFF: servo sem pulso fica solto e nao se move.
  // O PCA9685 mantem os registradores quando so o ESP reinicia - sem isto
  // um reset no meio do uso deixaria os servos segurando a ultima pose.
  for (uint8_t ch = 0; ch < 16; ch++) pca.setPWM(ch, 0, 4096);
  comPulso = 0;
  return true;
}

// Servo solto nao tem posicao conhecida. Ao acordar, recebe a CASA direto
// (vai ate ela na velocidade dele - nao ha rampa possivel de onde nao se
// sabe) e, dali, anda em rampa. So o servo pedido acorda.
static void acorda(uint8_t s) {
  if (temPulso(s) || !pcaOk) return;
  cursoAtual[s] = 0;
  escreveServo(s);
  comPulso |= (uint8_t)(1u << s);
}

static void alvo(uint8_t s, float curso, bool manual) {
  if (!servoAtivo(s)) return;
  acorda(s);
  cursoAlvo[s]  = limita(curso, 0, limites.flexaoMax);
  alvoManual[s] = manual;
}

// Mao inteira: todos os servos ativos no mesmo curso.
static void mao(float curso, bool manual) {
  for (uint8_t s = 0; s < N_SERVOS; s++) alvo(s, curso, manual);
}

// Auto-home = mao aberta.
static void autoHome(bool manual) {
  mao(0, manual);
}

static void tickServos() {
  static uint32_t ultimoMs = 0;
  const uint32_t agora     = millis();
  if (agora - ultimoMs < AMZ_TICK_SERVO_MS) return;
  const float dt = (agora - ultimoMs) / 1000.0f;
  ultimoMs       = agora;

  // PCA ausente nao tenta de novo sozinho: medido em 12/11, cada tentativa
  // com o barramento ruim trava o loop por 1 s. Use `i2c`.
  if (!pcaOk) return;

  for (uint8_t s = 0; s < N_SERVOS; s++) {
    if (!servoAtivo(s) || !temPulso(s)) continue;
    // Entrada saturada congela o que o MUSCULO pediu; o manual segue.
    if (!alvoManual[s] && PUB.saturado) continue;
    const float a    = limita(cursoAlvo[s], 0, limites.flexaoMax);
    const float vel  = alvoManual[s]       ? AMZ_VEL_MANUAL_GRAUS_S
                       : a > cursoAtual[s] ? AMZ_VEL_FECHA_GRAUS_S
                                           : AMZ_VEL_ABRE_GRAUS_S;
    const float novo = rampa(cursoAtual[s], a, vel * dt);
    if (novo != cursoAtual[s]) {
      cursoAtual[s] = novo;
      escreveServo(s);
    }
  }
}

// O loop acompanha a tarefa, e SO com a mao liberada: liberou ->
// auto-home; o EMG trocou o estado da mao -> abre ou fecha. Travada, o
// musculo nao move nada.
static void tickMao() {
  static bool liberadaAntes = false;
  static bool fechadaAntes  = false;
  const bool liberada       = PUB.liberada;
  const bool fechada        = PUB.fechada;
  if (liberada && !liberadaAntes) {
    autoHome(false);
    Serial.println(F(">>> MUSCULO LIBERADO: auto-home (mao aberta)"));
  }
  if (liberada && liberadaAntes && fechada != fechadaAntes)
    mao(fechada ? limites.flexaoMax : 0, false);
  liberadaAntes = liberada;
  fechadaAntes  = fechada;
}

// --- diagnostico do barramento I2C ----------------------------------
//
// Endereco ausente num barramento saudavel devolve NACK (codigo 2) em
// ~0,1 ms. Erro de barramento ou timeout (4, 5) quer dizer linha sem
// pull-up ou presa em 0 - nao e endereco. Tres seguidos e a varredura
// desiste em vez de travar a placa por segundos.
static uint8_t varreBarramento(int sda, int scl) {
  Wire.end();
  Wire.begin(sda, scl, 100000);
  Wire.setTimeOut(3);
  uint8_t achados = 0, errosSeguidos = 0;
  uint16_t cont[6] = {0, 0, 0, 0, 0, 0};
  for (uint8_t ad = 1; ad < 127; ad++) {
    Wire.beginTransmission(ad);
    const uint32_t t0 = micros();
    uint8_t r         = Wire.endTransmission();
    const uint32_t dt = micros() - t0;
    if (r > 5) r = 4;
    cont[r]++;
    if (r == 0) {
      Serial.printf("    responde 0x%02X%s\n", ad, ad == AMZ_PCA_ENDERECO ? " <- PCA9685" : "");
      achados++;
    }
    errosSeguidos = (r >= 4) ? errosSeguidos + 1 : 0;
    if (errosSeguidos >= 3) {
      Serial.printf("    erro de barramento %u (%lu us por tentativa): desisti em 0x%02X\n", r,
                    (unsigned long)dt, ad);
      break;
    }
  }
  Serial.printf("    ok %u  nack %u  erro %u  timeout %u\n", cont[0], cont[2] + cont[3], cont[4],
                cont[5]);
  return achados;
}

// Escravo que ficou no meio de um byte segura SDA em 0 e trava o
// barramento. Nove pulsos de clock com SDA solta o liberam.
static void recuperaBarramento(int sda, int scl) {
  Wire.end();
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT_OPEN_DRAIN);
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(scl, LOW);
    delayMicroseconds(10);
    digitalWrite(scl, HIGH);
    delayMicroseconds(10);
  }
  pinMode(scl, INPUT_PULLUP);
}

static void diagnosticoI2C() {
  Serial.printf("Sonda 0x%02X (SDA %d SCL %d, 100 kHz), 4 tentativas:", AMZ_PCA_ENDERECO,
                AMZ_PINO_SDA, AMZ_PINO_SCL);
  for (uint8_t i = 0; i < 4; i++) {
    recuperaBarramento(AMZ_PINO_SDA, AMZ_PINO_SCL);
    Wire.begin(AMZ_PINO_SDA, AMZ_PINO_SCL, 100000);
    Wire.beginTransmission(AMZ_PCA_ENDERECO);
    Serial.printf(" %u", Wire.endTransmission());
  }
  Serial.println(F("   (0 ok, 2 nack, 5 timeout)"));

  // Nivel das linhas paradas COM pull-down interno (~45 k): so um pull-up
  // de verdade (10 k no VCC do PCA9685) puxa para 1.
  Wire.end();
  pinMode(AMZ_PINO_SDA, INPUT_PULLDOWN);
  pinMode(AMZ_PINO_SCL, INPUT_PULLDOWN);
  delay(2);
  const int nSda = digitalRead(AMZ_PINO_SDA), nScl = digitalRead(AMZ_PINO_SCL);
  pinMode(AMZ_PINO_SDA, INPUT);
  pinMode(AMZ_PINO_SCL, INPUT);
  Serial.printf("Pull-up externo: SDA(GPIO%d)=%d SCL(GPIO%d)=%d  (esperado 1 e 1)\n", AMZ_PINO_SDA,
                nSda, AMZ_PINO_SCL, nScl);
  if (!nSda || !nScl)
    Serial.println(F(">>> sem pull-up: VCC do PCA9685 no 3V3, GND comum, fio no pino certo?"));

  Serial.printf("I2C, SDA %d SCL %d:\n", AMZ_PINO_SDA, AMZ_PINO_SCL);
  const uint8_t normal = varreBarramento(AMZ_PINO_SDA, AMZ_PINO_SCL);
  if (!normal) Serial.println(F("    ninguem respondeu"));
  Serial.printf("I2C trocado, SDA %d SCL %d:\n", AMZ_PINO_SCL, AMZ_PINO_SDA);
  const uint8_t trocado = varreBarramento(AMZ_PINO_SCL, AMZ_PINO_SDA);
  if (!trocado) Serial.println(F("    ninguem respondeu"));
  if (!normal && trocado) Serial.println(F(">>> SDA e SCL estao invertidos na fiacao"));
  if (!normal && !trocado)
    Serial.println(F(">>> nada no barramento: conferir GND comum, VCC do PCA9685 e os fios"));
  Wire.end();
  Wire.begin(AMZ_PINO_SDA, AMZ_PINO_SCL, AMZ_I2C_HZ);
  Wire.setTimeOut(20);
}

// =====================================================================
//  Avisos (eventos da tarefa) -> serial e pagina
// =====================================================================
static char ultimoAviso[160] = "";

static const char* textoEtapa(uint8_t e) {
  switch (e) {
    case CAL_PREPARO: return "PREPARE-SE: braco solto e parado";
    case CAL_REPOUSO: return "RELAXE o braco (medindo o repouso)";
    case CAL_CONTRAIA: return "CONTRAIA forte!";
    case CAL_RELAXE: return "RELAXE";
    case CAL_PROVA_CONTRAIA: return "PROVA: CONTRAIA!";
    case CAL_PROVA_RELAXE: return "PROVA: relaxe";
    default: return "";
  }
}

static void tickAvisos() {
  Evento e;
  while (xQueueReceive(filaEv, &e, 0) == pdTRUE) {
    char* s        = ultimoAviso;
    const size_t n = sizeof(ultimoAviso);
    switch (e.tipo) {
      case EV_ENTRADA:
        snprintf(s, n, "MyoWare: nivel medio %.0f contagens (%.0f mV) -> saida %s%s", e.v1,
                 e.v1 * 3300.0f / 4095.0f, PUB.raw ? "RAW" : "SIG/ENV",
                 e.v2 >= 0 ? " (forcada)" : " (detectada)");
        break;
      case EV_FECHOU:
        snprintf(s, n, "[%lu ms] CONTRACAO -> FECHA (envelope %.0f)", (unsigned long)e.ms, e.v1);
        break;
      case EV_ABRIU:
        snprintf(s, n, "[%lu ms] CONTRACAO -> ABRE (envelope %.0f)", (unsigned long)e.ms, e.v1);
        break;
      case EV_REJEITOU:
        snprintf(s, n, "[%lu ms] contracao IGNORADA (%s, pico %.0f)", (unsigned long)e.ms,
                 e.v1 == REJ_PANCADA ? "alta demais: pancada no eletrodo?"
                 : e.v1 == REJ_CURTA ? "curta demais: pancada ou tremor"
                                     : "cedo demais depois da ultima troca",
                 e.v2);
        break;
      case EV_CAL_ETAPA: {
        const uint8_t et = PUB.calEtapa;
        const char* av   = calNomeAvaliacao(PUB.calAval);
        if (et == CAL_CONTRAIA || et == CAL_RELAXE)
          snprintf(s, n, ">>> CALIBRACAO (tentativa %u, boas %u/%u%s%s): %s", PUB.calCiclo + 1,
                   PUB.calBoas, AMZ_CAL_BOAS, av[0] ? ", ultima: " : "", av, textoEtapa(et));
        else if (et == CAL_PROVA_CONTRAIA || et == CAL_PROVA_RELAXE)
          snprintf(s, n, ">>> CALIBRACAO prova %u/%u%s%s: %s", PUB.calProva + 1, AMZ_CAL_PROVA,
                   av[0] ? ", ultima: " : "", av, textoEtapa(et));
        else
          snprintf(s, n, ">>> CALIBRACAO: %s", textoEtapa(et));
        break;
      }
      case EV_CAL_FIM: {
        const ResultadoCal& r = PUB.res;
        if (PUB.calEtapa == CAL_OK)
          snprintf(s, n,
                   ">>> CALIBRADO e PROVADO (%u/%u): repouso %.1f+-%.1f contracao %.1f -> liga "
                   "%.1f desliga %.1f",
                   r.acertos, AMZ_CAL_PROVA, r.repouso, r.desvio, r.contracao, r.liga, r.desliga);
        else if (PUB.temRes && PUB.calMotivo && strcmp(PUB.calMotivo, "cancelada"))
          snprintf(s, n, ">>> CALIBRACAO RECUSADA: %s. Mao continua TRAVADA.", PUB.calMotivo);
        else
          snprintf(s, n, ">>> calibracao cancelada. Mao continua TRAVADA.");
        break;
      }
      case EV_LIMIAR: snprintf(s, n, "limiar ajustado: liga %.1f desliga %.1f", e.v1, e.v2); break;
      case EV_SATURADO:
        snprintf(s, n, "[%lu ms] ENTRADA SATURADA: eletrodo solto? Mao congelada.",
                 (unsigned long)e.ms);
        break;
    }
    Serial.println(s);
  }
}

// =====================================================================
//  Wi-Fi aberto, portal cativo e pagina
// =====================================================================
static WebServer srv(80);
static DNSServer dns;
static const IPAddress IP_MAO(192, 168, 4, 1);
static bool wifiOk = false;

static void pede(TipoCmd t, float a = 0, float b = 0) {
  Comando c = {t, a, b};
  xQueueSend(filaCmd, &c, 0);
}

// Movimento MANUAL (desenvolvedor) vale sempre, travada ou nao; so precisa
// do PCA9685. Devolve a mensagem de recusa, ou nullptr.
static const char* podeMover() {
  // PCA ausente: tenta de novo na hora do pedido manual (o PCA pode ter sido
  // ligado ou consertado depois do boot). No maximo uma vez a cada 3 s:
  // com o barramento ruim cada tentativa trava o loop por ~1 s (12/11).
  static uint32_t ultimaTentativaMs = 0;
  if (!pcaOk && millis() - ultimaTentativaMs >= 3000) {
    ultimaTentativaMs = millis();
    pcaOk             = iniciaPca();
    if (pcaOk) Serial.println(F("PCA9685 encontrado: controle manual ativo"));
  }
  if (!pcaOk) return "PCA9685 ausente: confira GND, VCC, SDA (GPIO6) e SCL (GPIO7)";
  return nullptr;
}

static void webDados() {
  static char buf[300 * 18 + 2000];
  const uint32_t total = nPontos;
  const long s         = srv.hasArg("s") ? srv.arg("s").toInt() : -1;
  // No maximo 300 pontos (3 s) por resposta, e nunca algo que a tarefa
  // ja pode estar sobrescrevendo.
  uint32_t desde = (s < 0 || (uint32_t)s > total) ? 0 : (uint32_t)s;
  if (total - desde > 300) desde = total - 300;
  if (total > AMZ_HISTORICO - 64 && desde < total - (AMZ_HISTORICO - 64))
    desde = total - (AMZ_HISTORICO - 64);

  size_t k = 0;
#define ADD(...) k += snprintf(buf + k, sizeof(buf) - k, __VA_ARGS__)
  ADD("{\"s\":%lu,\"p\":[", (unsigned long)total);
  for (uint32_t i = desde; i < total; i++) {
    const Ponto p = historico[i % AMZ_HISTORICO];
    ADD("%s[%u,%u,%u]", i == desde ? "" : ",", p.adc, p.env10, p.flags);
  }
  const Publico& u = PUB;
  ADD("],\"lib\":%d,\"con\":%d,\"mao\":%d,\"liga\":%.1f,\"desl\":%.1f,\"sat\":%d,\"raw\":%d,"
      "\"det\":%d,\"pca\":%d,\"pul\":%d,\"ncon\":%lu,\"rej\":%lu,\"esp\":%lu,\"lim\":%.0f,",
      u.liberada, u.contracao, u.fechada, u.liga, u.desliga, u.saturado, u.raw, u.detectando, pcaOk,
      comPulso, (unsigned long)u.contracoes, (unsigned long)u.rejeitadas, (unsigned long)u.espigoes,
      limites.flexaoMax);
  ADD("\"sv\":[");
  for (uint8_t i = 0; i < N_SERVOS; i++) ADD("%s%.0f", i ? "," : "", cursoAtual[i]);
  ADD("],\"svl\":[");
  for (uint8_t i = 0; i < N_SERVOS; i++) ADD("%s%d", i ? "," : "", servoAtivo(i) ? 1 : 0);
  ADD("],\"cal\":{\"e\":\"%s\",\"r\":%lu,\"t\":%u,\"tm\":%u,\"b\":%u,\"bm\":%u,\"pv\":%u,\"pm\":%u,"
      "\"av\":\"%s\",\"m\":\"%s\"},",
      calNomeEtapa((EtapaCal)u.calEtapa), (unsigned long)u.calRestante,
      u.calCiclo + ((u.calEtapa == CAL_CONTRAIA || u.calEtapa == CAL_RELAXE) ? 1 : 0),
      AMZ_CAL_CICLOS_MAX, u.calBoas, AMZ_CAL_BOAS, u.calProva, AMZ_CAL_PROVA,
      calNomeAvaliacao(u.calAval), u.calMotivo ? u.calMotivo : "");
  if (u.temRes) {
    const ResultadoCal& r = u.res;
    ADD("\"res\":{\"rep\":%.1f,\"dsv\":%.1f,\"con\":%.1f,\"sep\":%.0f,\"liga\":%.1f,\"desl\":%.1f,"
        "\"ac\":%u,\"fa\":%u,\"pr\":%u},",
        r.repouso, r.desvio, r.contracao, r.separacao, r.liga, r.desliga, r.acertos, r.falsos,
        r.provas);
  } else {
    ADD("\"res\":null,");
  }
  ADD("\"ev\":\"%s\"}", ultimoAviso);
#undef ADD
  srv.sendHeader("Cache-Control", "no-store");
  srv.send(200, "application/json", buf);
}

static void webCmd() {
  const String c  = srv.arg("c");
  const char* msg = "ok";
  if (c == "calibrar") {
    pede(CMD_CALIBRA);
    msg = "calibracao iniciada: siga as instrucoes na tela";
  } else if (c == "cancelar") {
    pede(CMD_CANCELA);
  } else if (c == "abrir" || c == "fechar" || c == "home" || c == "servo") {
    msg = podeMover();
    if (!msg) {
      if (c == "abrir" || c == "home") {
        pede(CMD_ABRE);  // o EMG continua do estado certo
        autoHome(true);
        msg = c == "home" ? "auto-home (devagar): mao aberta" : "abrindo devagar";
      } else if (c == "fechar") {
        pede(CMD_FECHA);
        mao(limites.flexaoMax, true);
        msg = "fechando devagar";
      } else {
        const int i = srv.arg("i").toInt();
        if (i >= 0 && i < N_SERVOS) {
          alvo(i, srv.arg("v").toFloat(), true);
          msg = "servo ok (devagar)";
        } else {
          msg = "servo invalido";
        }
      }
    }
  } else {
    srv.send(400, "text/plain", "comando desconhecido");
    return;
  }
  srv.send(200, "text/plain", msg);
}

// Portal cativo: qualquer endereco que o celular tente (generate_204,
// hotspot-detect.html, connecttest.txt...) volta para a pagina. E isso
// que faz o celular abrir a tela sozinho ao conectar.
static void webQualquer() {
  srv.sendHeader("Location", "http://192.168.4.1/", true);
  srv.send(302, "text/plain", "");
}

static void iniciaWifi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IP_MAO, IP_MAO, IPAddress(255, 255, 255, 0));
  wifiOk = WiFi.softAP(AMZ_WIFI_SSID, nullptr, AMZ_WIFI_CANAL, 0, 4);  // nullptr = rede aberta
  WiFi.setTxPower((wifi_power_t)AMZ_WIFI_POT_DBM_X4);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", IP_MAO);

  srv.on("/", []() {
    srv.sendHeader("Cache-Control", "no-store");
    srv.send_P(200, "text/html", AMZ_PAGINA);
  });
  srv.on("/dados", webDados);
  srv.on("/cmd", webCmd);
  srv.onNotFound(webQualquer);
  srv.begin();
}

// =====================================================================
//  Serial: Plotter e console
// =====================================================================
static bool plotter = true;

static void tickPlotter() {
  static uint32_t ultimoMs = 0;
  const uint32_t agora     = millis();
  if (!plotter || agora - ultimoMs < AMZ_PLOTTER_MS) return;
  ultimoMs         = agora;
  const uint32_t n = nPontos;
  if (!n || PUB.detectando) return;
  const Ponto p = historico[(n - 1) % AMZ_HISTORICO];
  // Formato chave:valor - Serial Plotter da IDE do Arduino e do
  // PlatformIO desenham direto. `contracao` salta ate o limiar "liga"
  // enquanto uma contracao esta reconhecida, para cair na mesma escala.
  Serial.printf("envelope:%.1f liga:%.1f desliga:%.1f contracao:%.0f\n", p.env10 / 10.0f, PUB.liga,
                PUB.desliga, (p.flags & PT_CONTRACAO) ? PUB.liga : 0.0f);
}

static void imprimeEstado() {
  Serial.println(F("\n--- LimbIA / Amazing Hand (C3 + PCA9685 + 8 MG90S + MyoWare) ---"));
  Serial.printf("MUSCULO %s   manual: sempre, devagar (%.0f graus/s)   PCA9685: %s\n",
                PUB.liberada ? "LIBERADO" : "TRAVADO (calibre: cal)", AMZ_VEL_MANUAL_GRAUS_S,
                pcaOk ? "ok" : "AUSENTE");
  Serial.printf("Wi-Fi aberto \"%s\" canal %d: %s, IP %s, %u aparelho(s) conectado(s)\n",
                AMZ_WIFI_SSID, AMZ_WIFI_CANAL, wifiOk ? "NO AR" : "FALHOU",
                WiFi.softAPIP().toString().c_str(), (unsigned)WiFi.softAPgetStationNum());
  Serial.printf("MyoWare: %s   liga %.1f desliga %.1f teto %.1f   mao %s\n",
                PUB.detectando ? "detectando" : (PUB.raw ? "RAW" : "SIG/ENV"), PUB.liga,
                PUB.desliga, PUB.teto, PUB.fechada ? "FECHADA" : "aberta");
  Serial.printf("limite de flexao %.0f (teto %.0f)\n", limites.flexaoMax, AMZ_FLEXAO_TETO);
  for (uint8_t s = 0; s < N_SERVOS; s++) {
    if (temPulso(s))
      Serial.printf("  servo %u %-11s canal %2u  %s  curso %3.0f -> %3.0f graus abs\n", s,
                    NOME_SERVO[s], canalServo(s), servoAtivo(s) ? "ON " : "off", cursoAtual[s],
                    grausServo(s, cursoAtual[s]));
    else
      Serial.printf("  servo %u %-11s canal %2u  %s  sem pulso (solto)\n", s, NOME_SERVO[s],
                    canalServo(s), servoAtivo(s) ? "ON " : "off");
  }
  Serial.println(
      F("comandos: ? cal cancela abre fecha home servo lim centro inv dedos liga raw env "
        "auto i2c t"));
}

static void executa(char* linha) {
  char* cmd = strtok(linha, " \t");
  if (!cmd) return;
  char* a1 = strtok(nullptr, " \t");
  char* a2 = strtok(nullptr, " \t");
  char* a3 = strtok(nullptr, " \t");

  const bool movimento = !strcmp(cmd, "abre") || !strcmp(cmd, "fecha") || !strcmp(cmd, "home") ||
                         !strcmp(cmd, "servo");
  if (movimento) {
    const char* nao = podeMover();
    if (nao) {
      Serial.println(nao);
      return;
    }
  }

  if (!strcmp(cmd, "?")) {
    imprimeEstado();
  } else if (!strcmp(cmd, "cal")) {
    pede(CMD_CALIBRA);
  } else if (!strcmp(cmd, "cancela")) {
    pede(CMD_CANCELA);
  } else if (!strcmp(cmd, "abre") || !strcmp(cmd, "home")) {
    pede(CMD_ABRE);
    autoHome(true);
    Serial.println(F("abrindo devagar (auto-home)"));
  } else if (!strcmp(cmd, "fecha")) {
    pede(CMD_FECHA);
    mao(limites.flexaoMax, true);
    Serial.println(F("fechando devagar"));
  } else if (!strcmp(cmd, "servo") && a1 && a2) {
    const int i = atoi(a1);
    if (i >= 0 && i < N_SERVOS) {
      alvo(i, atof(a2), true);
      Serial.printf("servo %d (%s) -> curso %.0f, devagar\n", i, NOME_SERVO[i], cursoAlvo[i]);
    }
  } else if (!strcmp(cmd, "lim") && a1) {
    limites.flexaoMax = limita(atof(a1), AMZ_FLEXAO_MIN, AMZ_FLEXAO_TETO);
    Serial.printf("limite de flexao: %.0f\n", limites.flexaoMax);
  } else if (!strcmp(cmd, "centro") && a1 && a2 && a3) {
    const int d = atoi(a1);
    if (d >= 0 && d < AMZ_N_DEDOS) {
      dedos[d].centroA = limita(atof(a2), AMZ_SERVO_MIN, AMZ_SERVO_MAX);
      dedos[d].centroB = limita(atof(a3), AMZ_SERVO_MIN, AMZ_SERVO_MAX);
      for (uint8_t s = 2 * d; s <= 2 * d + 1; s++)
        if (pcaOk && temPulso(s)) escreveServo(s);
      Serial.printf("dedo %d centro A %.0f B %.0f\n", d, dedos[d].centroA, dedos[d].centroB);
    }
  } else if (!strcmp(cmd, "inv") && a1) {
    const int d = atoi(a1);
    if (d >= 0 && d < AMZ_N_DEDOS) {
      dedos[d].sentido = -dedos[d].sentido;
      for (uint8_t s = 2 * d; s <= 2 * d + 1; s++)
        if (pcaOk && temPulso(s)) escreveServo(s);
      Serial.printf("dedo %d sentido %+d\n", d, dedos[d].sentido);
    }
  } else if (!strcmp(cmd, "dedos") && a1) {
    // Dedo que sai da mascara para de receber passo, mas fica com o ultimo
    // pulso: o PCA9685 segura a posicao.
    mascaraDedos = (uint8_t)(atoi(a1) & ((1u << AMZ_N_DEDOS) - 1));
    Serial.printf("dedos ativos: mascara %u\n", mascaraDedos);
  } else if (!strcmp(cmd, "liga") && a1 && a2) {
    const float l = atof(a1), d = atof(a2);
    if (!PUB.liberada)
      Serial.println(F("calibre primeiro: o ajuste fino parte do calibrado"));
    else if (!(l > 0) || !(d > 0) || d >= l)
      Serial.println(F("precisa 0 < desliga < liga"));
    else
      pede(CMD_LIMIAR, l, d);
  } else if (!strcmp(cmd, "raw") || !strcmp(cmd, "env") || !strcmp(cmd, "auto")) {
    pede(CMD_ENTRADA, !strcmp(cmd, "raw") ? 1 : (!strcmp(cmd, "env") ? 0 : -1));
    Serial.println(F("entrada trocada: a mao TRAVA ate calibrar de novo"));
  } else if (!strcmp(cmd, "i2c")) {
    diagnosticoI2C();
    pcaOk = iniciaPca();
    Serial.println(pcaOk ? F("PCA9685 ok (servos sem pulso ate alguem pedir movimento)")
                         : F("PCA9685 continua ausente"));
    if (pcaOk && PUB.liberada) autoHome(false);
  } else if (!strcmp(cmd, "t")) {
    plotter = !plotter;
  } else {
    Serial.printf("comando desconhecido: %s (? para ajuda)\n", cmd);
  }
}

static void tickConsole() {
  static char buf[64];
  static uint8_t n = 0;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[n] = 0;
      n      = 0;
      executa(buf);
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = c;
    }
  }
}

// =====================================================================
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // Sem computador na USB (mao na bateria), escrever na serial nao pode
  // travar o loop esperando alguem ler.
  Serial.setTxTimeoutMs(0);
#endif
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) {
  }

  for (uint8_t i = 0; i < AMZ_N_DEDOS; i++) {
    dedos[i].canalA  = AMZ_DEDOS[i].canalA;
    dedos[i].canalB  = AMZ_DEDOS[i].canalB;
    dedos[i].centroA = AMZ_DEDOS[i].centroA;
    dedos[i].centroB = AMZ_DEDOS[i].centroB;
    dedos[i].sentido = AMZ_DEDOS[i].sentido;
  }
  for (uint8_t s = 0; s < N_SERVOS; s++) cursoAtual[s] = cursoAlvo[s] = 0;
  limites.flexaoMin  = AMZ_FLEXAO_MIN;
  limites.flexaoMax  = limita(AMZ_FLEXAO_INICIAL, AMZ_FLEXAO_MIN, AMZ_FLEXAO_TETO);
  limites.abducaoMax = AMZ_ABDUCAO_MAX;
  limites.servoMin   = AMZ_SERVO_MIN;
  limites.servoMax   = AMZ_SERVO_MAX;

  PUB.detectando = true;
  PUB.liberada   = false;

  analogReadResolution(12);
  analogSetPinAttenuation(AMZ_PINO_EMG, ADC_11db);

  Wire.begin(AMZ_PINO_SDA, AMZ_PINO_SCL, AMZ_I2C_HZ);
  Wire.setTimeOut(20);
  pcaOk = iniciaPca();  // nasce com os servos SEM pulso

  filaCmd = xQueueCreate(8, sizeof(Comando));
  filaEv  = xQueueCreate(16, sizeof(Evento));
  // Prioridade 5: acima do loop (1), abaixo do Wi-Fi e do lwIP.
  xTaskCreate(tarefaEmg, "emg", 4096, nullptr, 5, nullptr);

  iniciaWifi();
  imprimeEstado();
  if (!pcaOk)
    Serial.println(
        F("ERRO: PCA9685 nao responde em 0x40 (SDA 6, SCL 7). "
          "Depois de conferir a fiacao: i2c"));
  Serial.println(F(">>> MAO TRAVADA: abra http://192.168.4.1 (rede LimbIA-mao) e calibre"));
}

void loop() {
  dns.processNextRequest();
  srv.handleClient();
  tickAvisos();
  tickMao();
  tickServos();
  tickConsole();
  tickPlotter();
}

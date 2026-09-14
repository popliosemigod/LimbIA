// =====================================================================
//  LimbIA - motores.h
//  A mao do LAD Robotic Hand V3.0: quatro dedos longos com motores DC em
//  dois L293D, e o polegar com dois servos.
//
//  Mesmo namespace `Dedos` do backend de servos (include/dedos.h): quem
//  usa - preensao, enlace, console - nao sabe qual dos dois esta embaixo.
//
//  O PROBLEMA QUE ESTE ARQUIVO RESOLVE
//  -----------------------------------
//  Motor DC nao tem posicao. O codigo original do LAD liga o motor e
//  espera a corrente subir; ele sabe que o dedo travou, nunca ONDE. Mas a
//  assinatura do objeto do LimbIA e exatamente "em que posicao cada dedo
//  encostou" - sem posicao, a mao volta a so saber que agarrou algo.
//
//  A saida aqui e POSICAO ESTIMADA POR TEMPO: mede-se uma vez quanto tempo
//  o dedo leva de ponta a ponta (comando 'm' do console, gravado na
//  flash), e a posicao passa a ser a integral do tempo de acionamento.
//
//  Estimativa por tempo escorrega - tensao da bateria cai, atrito muda,
//  tendao estica. Por isso ela se corrige sozinha: TODA abertura completa
//  termina com o dedo batendo no fim de curso, e ali a posicao volta a ser
//  zero por medida, nao por conta. Quem usa a protese abre a mao o tempo
//  todo; o erro nunca acumula por muitos gestos.
//
//  Isso NAO e encoder, e o diario tem de dizer isso com essas palavras.
//
//  POR QUE PWM NAS ENTRADAS DO L293D
//  ---------------------------------
//  Motor DC ligado direto fecha o dedo rapido demais para a corrente ser
//  lida no meio do caminho - e ler no meio do caminho e o que faz o dedo
//  parar quando encosta, em vez de depois de ter empurrado. O EN dos
//  modulos vem amarrado em nivel alto, entao a velocidade e controlada
//  pelas ENTRADAS: PWM numa, zero na outra.
//
//  ARRANQUE CEGO
//  -------------
//  Motor DC parado puxa varias vezes a corrente de regime no instante em
//  que parte. Sem ignorar esse pico, todo dedo "encosta em alguma coisa"
//  no primeiro instante de movimento. Os primeiros ARRANQUE_CEGO_MS nao
//  contam como contato.
// =====================================================================
#pragma once
#include <Arduino.h>

#include "config.h"
#include "corrente.h"

namespace Dedos {

// ---------------------------------------------------------------------
//  Tabelas de hardware
// ---------------------------------------------------------------------
struct LigacaoDedo {
  uint8_t pinoA;  // nivel alto flexiona (fecha)
  uint8_t pinoB;  // nivel alto estende (abre)
};

inline const LigacaoDedo& ligacao(uint8_t junta) {
  static const LigacaoDedo L[limbia::N_DEDOS_LONGOS] = {
      {PIN_F_MINDY_A, PIN_F_MINDY_B},
      {PIN_F_DONCARE_A, PIN_F_DONCARE_B},
      {PIN_F_FEIO_A, PIN_F_FEIO_B},
      {PIN_F_JULGADOR_A, PIN_F_JULGADOR_B},
  };
  return L[junta < limbia::N_DEDOS_LONGOS ? junta : 0];
}

inline bool dedoDc(uint8_t junta) {
  return junta < limbia::N_DEDOS_LONGOS;
}

inline bool dedoServo(uint8_t junta) {
  return junta == limbia::DEDAO || junta == limbia::DEDAO_ABD;
}

inline uint8_t canalMotor(uint8_t junta, bool a) {
  return (uint8_t)(LEDC_CANAL_MOTOR_0 + junta * 2 + (a ? 0 : 1));
}

inline uint8_t canalServo(uint8_t junta) {
  return junta == limbia::DEDAO ? LEDC_CANAL_SERVO_0 : LEDC_CANAL_SERVO_1;
}

// ---------------------------------------------------------------------
//  Estado que so existe nesta variante
// ---------------------------------------------------------------------
enum Sentido : uint8_t {
  PARADA = 0,
  FLEXIONA,  // fecha: posicao sobe
  ESTENDE    // abre: posicao desce
};

struct EstadoMotor {
  uint8_t sentido;
  uint32_t desdeMs;      // quando este acionamento comecou (arranque cego)
  uint32_t ultimoMs;     // ultima integracao de posicao
  uint8_t confirmacoes;  // leituras seguidas acima do limiar de contato
  bool ateOFim;          // ignora a estimativa e vai ate travar (referencia)
};

inline EstadoMotor* motores() {
  static EstadoMotor m[limbia::N_DEDOS_LONGOS] = {};
  return m;
}

inline bool& pararNoContato() {
  static bool v = true;
  return v;
}

inline bool& saidasHabilitadas() {
  static bool v = false;
  return v;
}

// ---------------------------------------------------------------------
//  Saida de baixo nivel
// ---------------------------------------------------------------------
inline void acionaDc(uint8_t junta, uint8_t sentido, uint8_t velocidade) {
  if (!dedoDc(junta)) return;
  const uint8_t duty = saidasHabilitadas() ? velocidade : 0;
  // Duas entradas em zero = motor solto. E este o estado de repouso e o
  // modo de falha seguro desta mao.
  ledcWrite(canalMotor(junta, true), sentido == FLEXIONA ? duty : 0);
  ledcWrite(canalMotor(junta, false), sentido == ESTENDE ? duty : 0);
  motores()[junta].sentido = sentido;
}

inline void escrevePulso(uint8_t junta, uint16_t us) {
  if (!dedoServo(junta)) return;  // dedo de motor DC nao tem pulso
  if (us < limbia::PULSO_MIN_US) us = limbia::PULSO_MIN_US;
  if (us > limbia::PULSO_MAX_US) us = limbia::PULSO_MAX_US;
  M.junta[junta].pulsoUs = us;
  // 50 Hz em 16 bits: 20000 us = 65536 contagens.
  const uint32_t duty = saidasHabilitadas() ? ((uint32_t)us * 65536u) / 20000u : 0;
  ledcWrite(canalServo(junta), duty);
}

// ---------------------------------------------------------------------
//  Saidas
// ---------------------------------------------------------------------
inline void para();

inline void ligaSaidas(bool ligar) {
  saidasHabilitadas() = ligar;
  M.saidasLigadas     = ligar;
  if (!ligar) {
    for (uint8_t i = 0; i < limbia::N_DEDOS_LONGOS; i++) acionaDc(i, PARADA, 0);
    // Servo sem pulso fica solto - e o equivalente ao OE alto da outra mao.
    ledcWrite(canalServo(limbia::DEDAO), 0);
    ledcWrite(canalServo(limbia::DEDAO_ABD), 0);
  } else {
    escrevePulso(limbia::DEDAO, M.junta[limbia::DEDAO].pulsoUs);
    escrevePulso(limbia::DEDAO_ABD, M.junta[limbia::DEDAO_ABD].pulsoUs);
  }
}

// Posiciona sem rampa. No dedo de motor DC nao ha "posicionar": o que se
// pode fazer e ANOTAR a posicao estimada (usado pela referencia).
inline void escreve(uint8_t junta, uint16_t perMil) {
  if (junta >= limbia::N_JUNTAS) return;
  if (perMil > 1000) perMil = 1000;
  M.junta[junta].posicao     = perMil;
  M.junta[junta].destino     = perMil;
  M.junta[junta].emMovimento = false;
  if (dedoServo(junta)) escrevePulso(junta, limbia::pulsoDePerMil(M.calib[junta], perMil));
  if (dedoDc(junta)) acionaDc(junta, PARADA, 0);
}

// ---------------------------------------------------------------------
//  Movimento
// ---------------------------------------------------------------------
inline void vaiPara(uint8_t junta, uint16_t perMil) {
  if (junta >= limbia::N_JUNTAS || !JUNTA_TEM_ATUADOR(junta)) return;
  if (perMil > 1000) perMil = 1000;
  EstadoJunta& j = M.junta[junta];
  j.destino      = perMil;
  j.contato      = false;
  j.picoMa       = 0;
  j.emMovimento  = (j.posicao != perMil) || (dedoDc(junta) && (perMil == 0 || perMil == 1000));

  if (dedoDc(junta)) {
    EstadoMotor& m = motores()[junta];
    m.confirmacoes = 0;
    m.desdeMs      = 0;
    m.ultimoMs     = 0;
    // Ir ao extremo nao usa a estimativa: vai ate travar. E assim que o
    // fim de curso volta a ser medido e a estimativa se corrige.
    m.ateOFim = (perMil == 0 || perMil == 1000);
  }
}

inline void vaiParaPose(const limbia::Pose& p) {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) vaiPara(i, p.alvo[i]);
}

inline void para() {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    M.junta[i].destino     = M.junta[i].posicao;
    M.junta[i].emMovimento = false;
    if (dedoDc(i)) acionaDc(i, PARADA, 0);
  }
}

inline bool emMovimento() {
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    if (M.junta[i].emMovimento) return true;
  }
  return false;
}

inline void relaxa() {
  vaiParaPose(limbia::poseDoGesto(limbia::G_ABRIR));
}

// ---------------------------------------------------------------------
//  begin
//
//  A ordem importa, pelo mesmo motivo da outra mao:
//    1. entradas do L293D em zero, em hardware, antes de qualquer coisa
//    2. mede o zero dos ACS712 (so faz sentido com tudo parado)
//    3. so entao habilita as saidas
//
//  Enquanto o firmware nao sobe, os GPIO ficam em alta impedancia e as
//  entradas do L293D flutuam. RESISTOR DE PULL-DOWN DE 10k EM CADA UMA
//  DAS OITO ENTRADAS - e o equivalente ao pull-up do OE na mao de servos,
//  e resolve em hardware o que o firmware nao alcanca durante o boot.
// ---------------------------------------------------------------------
inline bool begin() {
  saidasHabilitadas() = false;

  for (uint8_t i = 0; i < limbia::N_DEDOS_LONGOS; i++) {
    const LigacaoDedo& l = ligacao(i);
    pinMode(l.pinoA, OUTPUT);
    pinMode(l.pinoB, OUTPUT);
    digitalWrite(l.pinoA, LOW);
    digitalWrite(l.pinoB, LOW);
    ledcSetup(canalMotor(i, true), LEDC_MOTOR_HZ, LEDC_MOTOR_BITS);
    ledcSetup(canalMotor(i, false), LEDC_MOTOR_HZ, LEDC_MOTOR_BITS);
    ledcAttachPin(l.pinoA, canalMotor(i, true));
    ledcAttachPin(l.pinoB, canalMotor(i, false));
    ledcWrite(canalMotor(i, true), 0);
    ledcWrite(canalMotor(i, false), 0);
  }

  ledcSetup(LEDC_CANAL_SERVO_0, LEDC_SERVO_HZ, LEDC_SERVO_BITS);
  ledcSetup(LEDC_CANAL_SERVO_1, LEDC_SERVO_HZ, LEDC_SERVO_BITS);
  ledcAttachPin(PIN_SERVO_DEDAO, LEDC_CANAL_SERVO_0);
  ledcAttachPin(PIN_SERVO_DEDAO_ABD, LEDC_CANAL_SERVO_1);
  ledcWrite(LEDC_CANAL_SERVO_0, 0);
  ledcWrite(LEDC_CANAL_SERVO_1, 0);

  Corr::medeZero();  // com tudo desenergizado, o que se le e o zero

  const limbia::Pose& repouso = limbia::poseDoGesto(limbia::G_ABRIR);
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    M.junta[i].posicao = repouso.alvo[i];
    M.junta[i].destino = repouso.alvo[i];
    if (dedoServo(i)) M.junta[i].pulsoUs = limbia::pulsoDePerMil(M.calib[i], repouso.alvo[i]);
  }

  ligaSaidas(true);
  return true;
}

// ---------------------------------------------------------------------
//  tick - chamar todo loop
// ---------------------------------------------------------------------
inline void tickDedoDc(uint8_t i, uint32_t agora) {
  EstadoJunta& j = M.junta[i];
  EstadoMotor& m = motores()[i];
  if (!j.emMovimento) {
    if (m.sentido != PARADA) acionaDc(i, PARADA, 0);
    return;
  }

  const int32_t delta = (int32_t)j.destino - (int32_t)j.posicao;
  const uint8_t sentidoAlvo =
      m.ateOFim ? (j.destino == 0 ? ESTENDE : FLEXIONA) : (delta > 0 ? FLEXIONA : ESTENDE);

  if (m.sentido != sentidoAlvo) {
    m.desdeMs      = agora;
    m.ultimoMs     = agora;
    m.confirmacoes = 0;
    acionaDc(i, sentidoAlvo, VELOCIDADE_DEDO);
    return;  // o proximo tick ja integra tempo de verdade
  }

  // ---- contato ----
  // O pico de partida nao conta: motor DC parado puxa varias vezes a
  // corrente de regime nos primeiros instantes.
  const bool depoisDoArranque = (agora - m.desdeMs) > ARRANQUE_CEGO_MS;
  if (depoisDoArranque && Corr::temSensor(i)) {
    if (j.correnteMa >= CORRENTE_CONTATO_MA) {
      if (m.confirmacoes < 255) m.confirmacoes++;
    } else if (m.confirmacoes > 0) {
      m.confirmacoes--;  // ruido isolado nao acumula
    }
  }
  const bool travou = m.confirmacoes >= CONTATO_CONFIRMACOES;

  // ---- posicao estimada ----
  const uint32_t dt    = agora - m.ultimoMs;
  m.ultimoMs           = agora;
  const uint16_t curso = M.tempoCursoMs[i] ? M.tempoCursoMs[i] : TEMPO_CURSO_PADRAO_MS;
  const int32_t passo  = (int32_t)((dt * 1000UL) / curso);
  int32_t nova         = (int32_t)j.posicao + (sentidoAlvo == FLEXIONA ? passo : -passo);
  if (nova < 0) nova = 0;
  if (nova > 1000) nova = 1000;
  j.posicao = (uint16_t)nova;

  if (travou && (pararNoContato() || m.ateOFim)) {
    acionaDc(i, PARADA, 0);
    j.emMovimento = false;

    if (sentidoAlvo == ESTENDE) {
      // Travou abrindo. Perto do zero estimado, e o fim de curso: a
      // referencia volta a ser medida e o erro acumulado some. Longe do
      // zero, e mecanismo preso - nao se corrige nada e o console avisa.
      if (j.posicao < 350) {
        j.posicao = 0;
        j.destino = 0;
        j.contato = false;
      } else {
        j.contato = true;  // travou onde nao devia
      }
    } else {
      // Travou fechando: encostou em alguma coisa. Onde encostou e a
      // assinatura do objeto.
      j.contato = true;
      j.destino = j.posicao;
    }
    m.ateOFim = false;
    return;
  }

  // ---- chegou ----
  const bool chegou = m.ateOFim ? false
                                : ((sentidoAlvo == FLEXIONA && j.posicao >= j.destino) ||
                                   (sentidoAlvo == ESTENDE && j.posicao <= j.destino));
  if (chegou) {
    acionaDc(i, PARADA, 0);
    j.posicao     = j.destino;
    j.emMovimento = false;
  }
}

inline void tickDedoServo(uint8_t i) {
  static uint8_t confirmacoes[limbia::N_JUNTAS] = {0};
  EstadoJunta& j                                = M.junta[i];
  if (!j.emMovimento) {
    confirmacoes[i] = 0;
    return;
  }

  if (pararNoContato() && Corr::temSensor(i)) {
    if (j.correnteMa >= CORRENTE_CONTATO_MA) {
      if (confirmacoes[i] < 255) confirmacoes[i]++;
      if (confirmacoes[i] >= CONTATO_CONFIRMACOES) {
        confirmacoes[i] = 0;
        j.contato       = true;
        j.destino       = j.posicao;
        j.emMovimento   = false;
        return;
      }
    } else if (confirmacoes[i] > 0) {
      confirmacoes[i]--;  // ruido isolado nao acumula
    }
  }

  const int32_t delta = (int32_t)j.destino - (int32_t)j.posicao;
  if (delta == 0) {
    j.emMovimento = false;
    return;
  }
  const int32_t passo = delta > 0 ? PASSO_PERMIL : -PASSO_PERMIL;
  int32_t nova        = (int32_t)j.posicao + passo;
  if ((passo > 0 && nova > (int32_t)j.destino) || (passo < 0 && nova < (int32_t)j.destino)) {
    nova = (int32_t)j.destino;
  }
  j.posicao = (uint16_t)nova;
  escrevePulso(i, limbia::pulsoDePerMil(M.calib[i], j.posicao));
  if (j.posicao == j.destino) j.emMovimento = false;
}

inline void tick() {
  static uint32_t proximo         = 0;
  static uint32_t inicioMovimento = 0;

  const uint32_t agora = millis();
  if ((int32_t)(agora - proximo) < 0) return;
  proximo = agora + INTERVALO_MOVIMENTO_MS;

  if (!emMovimento()) {
    inicioMovimento = 0;
    return;
  }
  if (inicioMovimento == 0) inicioMovimento = agora;

  // Guarda de tempo: movimento que nao termina e mecanismo travado - ou,
  // num motor DC, tendao arrebentado girando no vazio. Abortar deixando
  // tudo parado e mais seguro que insistir.
  if (agora - inicioMovimento > MOVIMENTO_LIMITE_MS) {
    para();
    return;
  }

  for (uint8_t i = 0; i < limbia::N_DEDOS_LONGOS; i++) tickDedoDc(i, agora);
  tickDedoServo(limbia::DEDAO);
  tickDedoServo(limbia::DEDAO_ABD);
}

// ---------------------------------------------------------------------
//  Medicao do tempo de curso
//
//  E a calibracao desta mao, equivalente aos dois pulsos medidos com o
//  servo tester na mao de servos: abre o dedo ate travar, fecha contando o
//  tempo ate travar de novo, e grava. Sem isto, a posicao estimada usa o
//  TEMPO_CURSO_PADRAO_MS - um chute que vale so para o dedo se mover.
//
//  Roda pelo tick, sem bloquear: o console dispara e acompanha.
// ---------------------------------------------------------------------
namespace Curso {

enum Fase : uint8_t {
  OCIOSO = 0,
  ABRINDO,
  FECHANDO,
  FEITO,
  FALHOU
};

struct Estado {
  uint8_t fase;
  uint8_t junta;
  uint32_t inicioMs;
  uint16_t medidoMs;
};

inline Estado& S() {
  static Estado e = Estado();
  return e;
}

inline bool inicia(uint8_t junta) {
  if (!dedoDc(junta) || S().fase == ABRINDO || S().fase == FECHANDO) return false;
  S().fase         = ABRINDO;
  S().junta        = junta;
  S().inicioMs     = millis();
  S().medidoMs     = 0;
  pararNoContato() = true;
  vaiPara(junta, 0);  // ate o fim de curso aberto
  return true;
}

inline void tick() {
  Estado& s = S();
  if (s.fase != ABRINDO && s.fase != FECHANDO) return;
  if (M.junta[s.junta].emMovimento) return;

  if (s.fase == ABRINDO) {
    s.fase     = FECHANDO;
    s.inicioMs = millis();
    vaiPara(s.junta, 1000);
    return;
  }

  // Fechou ate travar: o tempo gasto e o curso do dedo.
  const uint32_t dt = millis() - s.inicioMs;
  if (dt < TEMPO_CURSO_MIN_MS || dt > TEMPO_CURSO_MAX_MS) {
    s.fase = FALHOU;
    return;
  }
  s.medidoMs               = (uint16_t)dt;
  M.tempoCursoMs[s.junta]  = (uint16_t)dt;
  M.junta[s.junta].posicao = 1000;
  s.fase                   = FEITO;
}

}  // namespace Curso

}  // namespace Dedos

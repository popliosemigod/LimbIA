// =====================================================================
//  _      _           _     ___    _
//  | |    (_)_ __ ___ | |__ |_ _|  / \
//  | |    | | '_ ` _ \| '_ \ | |  / _ \
//  | |___ | | | | | | | |_) || | / ___ \
//  |_____||_|_| |_| |_|_.__/|___/_/   \_\
//
//  Mao robotica e ortese. Sete juntas, quatro sensores de corrente,
//  um console de calibracao que grava na flash.
//
//  Fusao de dois projetos anteriores:
//    INOVAWEEK  - a mao impressa, os nomes das juntas, as tabelas de
//                 pulso e a ideia de deduzir o objeto pela posicao
//    LAD V3.0   - a calibracao por endpoints medidos, o polegar com dois
//                 graus de liberdade e a corrente em serie com cada motor
//
//  A regra que vale para o arquivo inteiro: NADA BLOQUEIA. Sem delay()
//  em regime, sem for() varrendo angulo. E isso que permite o dedo parar
//  quando encosta, em vez de descobrir o obstaculo depois de empurra-lo.
//
//  Compilar: pio run          Gravar: pio run -t upload
// =====================================================================

#include <Arduino.h>

#include "config.h"
#include "corrente.h"
#include "dedos.h"
#include "memoria.h"
#include "preensao.h"

// ---- definicao do global declarado em config.h -----------------------
EstadoMao M;

// ---------------------------------------------------------------------
//  Sequencias: gestos que sao um caminho, nao uma pose
//
//  A oposicao do polegar (comando '7' do manual do LAD) percorre a
//  amplitude toda; contar de um a cinco e uma sequencia de poses. Um
//  passo so avanca quando o anterior termina de se mover - sem delay().
// ---------------------------------------------------------------------
struct Passo {
  uint8_t junta;
  uint16_t alvo;
};

static const Passo SEQ_OPOSICAO[] = {
    {limbia::DEDAO_ABD, 1000}, {limbia::DEDAO, 1000},    {limbia::DEDAO_ABD, 0},
    {limbia::DEDAO, 0},        {limbia::DEDAO_ABD, 300},
};
static const uint8_t SEQ_OPOSICAO_N = sizeof(SEQ_OPOSICAO) / sizeof(SEQ_OPOSICAO[0]);

static const Passo* seqAtual = nullptr;
static uint8_t seqTamanho    = 0;
static uint8_t seqPasso      = 0;

static void iniciaSequencia(const Passo* s, uint8_t n) {
  seqAtual   = s;
  seqTamanho = n;
  seqPasso   = 0;
}

static void tickSequencia() {
  if (!seqAtual) return;
  if (Dedos::emMovimento()) return;
  if (seqPasso >= seqTamanho) {
    seqAtual = nullptr;
    return;
  }
  Dedos::vaiPara(seqAtual[seqPasso].junta, seqAtual[seqPasso].alvo);
  seqPasso++;
}

// ---------------------------------------------------------------------
//  Console
// ---------------------------------------------------------------------
static void ajuda() {
  Serial.println();
  Serial.println(F("===================== LimbIA ====================="));
  Serial.println(F("  GESTOS"));
  Serial.println(F("   1  fechar a mao            2  abrir a mao"));
  Serial.println(F("   3  fechar (polegar fora)   4  sinal da paz"));
  Serial.println(F("   5  flexionar polegar       6  estender polegar"));
  Serial.println(F("   7  oposicao do polegar     8  positivo"));
  Serial.println(F("   9  apontar                 0  pinca"));
  Serial.println(F("  PREENSAO"));
  Serial.println(F("   p  fechar ate encostar e classificar o objeto"));
  Serial.println(F("   l  soltar"));
  Serial.println(F("  CALIBRACAO"));
  Serial.println(F("   c            lista a calibracao de todas as juntas"));
  Serial.println(F("   c <j> r <us> define o pulso de REPOUSO da junta j"));
  Serial.println(F("   c <j> t <us> define o pulso de TRABALHO da junta j"));
  Serial.println(F("   c <j> +<n>   move a junta j em +n us (achar o fim de curso)"));
  Serial.println(F("   c <j> -<n>   move a junta j em -n us"));
  Serial.println(F("   w  gravar na flash          f  voltar ao padrao de fabrica"));
  Serial.println(F("  DIVERSOS"));
  Serial.println(F("   s  status      x  desligar as saidas (emergencia)"));
  Serial.println(F("   e  ligar as saidas          ?  esta ajuda"));
  Serial.println(F("=================================================="));
}

static void status() {
  Serial.println();
  Serial.printf("LimbIA %s | %s | saidas %s | calibracao %s\n", LIMBIA_VERSAO, LIMBIA_NOME,
                M.saidasLigadas ? "LIGADAS" : "desligadas",
                M.calibrada ? "da flash" : "PADRAO DE FABRICA");
  Serial.println(F("  junta       pos   pulso   corrente  pico   estado"));
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    const EstadoJunta& j = M.junta[i];
    Serial.printf("  %-10s %4u  %4u us", limbia::nomeDaJunta(i), j.posicao, j.pulsoUs);
    if (Corr::temSensor(i)) {
      Serial.printf("  %5u mA %5u", j.correnteMa, j.picoMa);
    } else {
      Serial.print(F("        --    -- "));
    }
    Serial.printf("   %s%s\n", j.emMovimento ? "movendo" : "parada", j.contato ? " (contato)" : "");
  }
  Serial.printf("  gestos executados: %lu | juntas com folga: %u\n", (unsigned long)M.movimentos,
                M.folgas);
}

static void listaCalibracao() {
  Serial.println();
  Serial.println(F("  #  junta       anatomia            repouso  trabalho  curso"));
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    const limbia::Calibracao& c = M.calib[i];
    const int32_t curso         = (int32_t)c.pulsoTrabalho - (int32_t)c.pulsoRepouso;
    Serial.printf("  %u  %-10s  %-18s  %4u us  %4u us  %+5ld us%s\n", i, limbia::nomeDaJunta(i),
                  limbia::anatomiaDaJunta(i), c.pulsoRepouso, c.pulsoTrabalho, (long)curso,
                  limbia::calibracaoValida(c) ? "" : "   << INVALIDA");
  }
  Serial.println(F("  curso negativo e normal: servo montado espelhado."));
}

// c <j> r <us> | c <j> t <us> | c <j> +<n> | c <j> -<n>
static void comandoCalibracao(const char* linha) {
  int junta  = -1;
  char op    = 0;
  long valor = 0;

  if (sscanf(linha, "c %d %c %ld", &junta, &op, &valor) < 2) {
    // pode ser a forma "c 3 +25"
    if (sscanf(linha, "c %d %ld", &junta, &valor) == 2) {
      op = valor >= 0 ? '+' : '-';
      if (valor < 0) valor = -valor;
    } else {
      listaCalibracao();
      return;
    }
  }

  if (junta < 0 || junta >= (int)limbia::N_JUNTAS) {
    Serial.println(F("  junta invalida (0..6). Use 'c' para listar."));
    return;
  }

  limbia::Calibracao& c = M.calib[junta];

  if (op == 'r' || op == 'R') {
    c.pulsoRepouso = (uint16_t)constrain(valor, limbia::PULSO_MIN_US, limbia::PULSO_MAX_US);
    Serial.printf("  %s: repouso = %u us\n", limbia::nomeDaJunta(junta), c.pulsoRepouso);
    Dedos::escreve((uint8_t)junta, 0);
  } else if (op == 't' || op == 'T') {
    c.pulsoTrabalho = (uint16_t)constrain(valor, limbia::PULSO_MIN_US, limbia::PULSO_MAX_US);
    Serial.printf("  %s: trabalho = %u us\n", limbia::nomeDaJunta(junta), c.pulsoTrabalho);
    Dedos::escreve((uint8_t)junta, 1000);
  } else if (op == '+' || op == '-') {
    // Empurrao fino: move o servo em microssegundos, sem mexer na
    // calibracao. E assim que se acha o fim de curso com a mao montada,
    // um passo de cada vez, olhando o dedo.
    const int32_t atual     = M.junta[junta].pulsoUs;
    const int32_t novo      = atual + (op == '+' ? valor : -valor);
    Dedos::pararNoContato() = false;  // aqui o objetivo e varrer o curso
    Dedos::escrevePulso((uint8_t)junta,
                        (uint16_t)constrain(novo, limbia::PULSO_MIN_US, limbia::PULSO_MAX_US));
    Serial.printf("  %s: pulso = %u us  (r para marcar repouso, t para trabalho)\n",
                  limbia::nomeDaJunta(junta), M.junta[junta].pulsoUs);
  } else {
    listaCalibracao();
    return;
  }

  if (!limbia::calibracaoValida(c)) {
    Serial.println(F("  aviso: curso menor que 150 us - a junta nao tera movimento util."));
  }
}

static void executaGesto(uint8_t gesto) {
  Dedos::pararNoContato() = false;  // gesto vai ate a pose, nao para no caminho
  Dedos::vaiParaPose(limbia::poseDoGesto(gesto));
  M.gestoAtual = gesto;
  M.movimentos++;
  Serial.printf("  gesto: %s\n", limbia::nomeDoGesto(gesto));
}

static void processaLinha(char* linha) {
  // Remove espacos do inicio
  while (*linha == ' ') linha++;
  if (*linha == 0) return;

  switch (linha[0]) {
    case '1': executaGesto(limbia::G_FECHAR); break;
    case '2': executaGesto(limbia::G_ABRIR); break;
    case '3': executaGesto(limbia::G_FECHAR_V2); break;
    case '4': executaGesto(limbia::G_PAZ); break;
    case '5': executaGesto(limbia::G_FLEX_DEDAO); break;
    case '6': executaGesto(limbia::G_EXT_DEDAO); break;
    case '8': executaGesto(limbia::G_POSITIVO); break;
    case '9': executaGesto(limbia::G_APONTAR); break;
    case '0': executaGesto(limbia::G_PINCA); break;

    case '7':
      Dedos::pararNoContato() = false;
      iniciaSequencia(SEQ_OPOSICAO, SEQ_OPOSICAO_N);
      M.movimentos++;
      Serial.println(F("  sequencia: oposicao do polegar"));
      break;

    case 'p':
    case 'P':
      Preensao::inicia();
      Serial.println(F("  fechando ate encostar..."));
      break;

    case 'l':
    case 'L':
      Preensao::solta();
      Serial.println(F("  soltando"));
      break;

    case 'c':
    case 'C': comandoCalibracao(linha); break;

    case 'w':
    case 'W':
      Serial.println(Memoria::salva() ? F("  calibracao gravada na flash")
                                      : F("  FALHA ao gravar"));
      break;

    case 'f':
    case 'F':
      Memoria::apaga();
      Serial.println(F("  calibracao de volta ao padrao de fabrica"));
      listaCalibracao();
      break;

    case 's':
    case 'S': status(); break;

    case 'x':
    case 'X':
      Dedos::para();
      Dedos::ligaSaidas(false);
      Serial.println(F("  SAIDAS DESLIGADAS - os servos estao livres"));
      break;

    case 'e':
    case 'E':
      Dedos::ligaSaidas(true);
      Serial.println(F("  saidas ligadas"));
      break;

    default: ajuda(); break;
  }
}

static void console() {
  static char buf[64];
  static uint8_t n = 0;

  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[n] = 0;
      if (n) processaLinha(buf);
      n = 0;
      continue;
    }
    if (n < sizeof(buf) - 1) buf[n++] = c;
  }
}

// ---------------------------------------------------------------------
static void telemetria() {
  static uint32_t proximo = 0;
  const uint32_t agora    = millis();
  if ((int32_t)(agora - proximo) < 0) return;
  proximo = agora + INTERVALO_SERIAL_MS;

  // So fala quando ha o que dizer: mao parada nao precisa encher o log.
  if (!Dedos::emMovimento() && !Preensao::ocupado()) return;

  Serial.print(F("  ["));
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    Serial.printf("%u%s", M.junta[i].posicao, i + 1 < limbia::N_JUNTAS ? " " : "");
  }
  Serial.println(F("]"));
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);  // unica espera do firmware: janela para o monitor engatar

  memset(&M, 0, sizeof(M));

  Serial.println();
  Serial.println(F("====================================================="));
  Serial.printf("  LimbIA %s  |  no: %s\n", LIMBIA_VERSAO, LIMBIA_NOME);
  Serial.printf("  build %s %s\n", __DATE__, __TIME__);
  Serial.println(F("====================================================="));

  Memoria::begin();
  const bool daFlash = Memoria::carrega();
  Serial.printf("[calib] %s\n",
                daFlash ? "carregada da flash" : "PADRAO DE FABRICA - calibre antes de montar");

  Corr::begin();

  if (Dedos::begin()) {
    Serial.println(F("[pca9685] respondeu no I2C"));
  } else {
    Serial.println(F("[pca9685] NAO respondeu - conferir SDA/SCL e alimentacao"));
  }

  Serial.print(F("[corrente] sensores em:"));
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    if (Corr::temSensor(i)) Serial.printf(" %s", limbia::nomeDaJunta(i));
  }
  Serial.println();

  ajuda();
}

// ---------------------------------------------------------------------
void loop() {
  Corr::tick();
  Dedos::tick();
  tickSequencia();
  Preensao::tick();
  console();
  telemetria();

  // Protecao: corrente acima do limite corta o movimento na hora.
  uint8_t culpada = 0;
  if (Corr::sobrecarga(&culpada)) {
    Dedos::para();
    Serial.printf("  SOBRECARGA em %s (%u mA) - movimento abortado\n", limbia::nomeDaJunta(culpada),
                  M.junta[culpada].correnteMa);
  }

  if (Preensao::pronta()) {
    Preensao::imprime(Serial);
    Preensao::fase() = Preensao::PARADO;
  }

  digitalWrite(PIN_LED_PLACA, Dedos::emMovimento() ? HIGH : LOW);
}

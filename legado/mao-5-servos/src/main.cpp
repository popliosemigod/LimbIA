// =====================================================================
//  LimbIA - mao de 5 servos, gestos por botao
//  Placa: Arduino Mega 2560
//
//  Cada dedo tem um servo que puxa o tendao. Um botao percorre uma lista
//  de gestos (numeros 1 a 5, joinha, rock...). A mesma mao obedece a
//  serial, no formato que o cvzone manda - e isso que deixa a visao
//  computacional plugar depois sem mudar este codigo.
//
//  Ao ligar, a mao nasce FECHADA: no nosso mecanismo o dedo sai do
//  fechado para o aberto com mais facilidade do que o contrario.
//
//  Botao:   toque curto = proximo gesto
//           segurar 1 s = mao fechada (volta ao comeco da lista)
//
//  Serial (9600):
//    $PIMAM  cinco digitos 0/1, na ordem POLEGAR INDICADOR MEDIO ANELAR
//            MINIMO, 1 = dedo levantado. E exatamente o que manda
//            mySerial.sendData(detector.fingersUp()) no Python.
//    0..9    escolhe o gesto da lista pelo numero
//    n       proximo gesto      ?  lista os gestos
//
//  Os dedos andam aos poucos (VELOCIDADE_GRAUS por passo), sem delay():
//  o loop continua lendo botao e serial enquanto a mao se mexe, e trocar
//  de gesto no meio do caminho apenas muda o destino.
//
//  ALIMENTACAO: cinco servos NAO podem sair do 5V do Mega - o pico de
//  partida derruba a placa e ela reinicia. Fonte externa de 5 a 6 V para
//  os servos, com o GND da fonte ligado ao GND do Mega.
// =====================================================================
#include <Arduino.h>
#include <Servo.h>

// ---------------------------------------------------------------------
//  Pinagem - polegar, indicador, medio, anelar, minimo
//  No Mega, a biblioteca Servo usa o Timer5 e desliga o PWM dos pinos
//  44, 45 e 46; os pinos 2 a 6 nao sao afetados.
// ---------------------------------------------------------------------
static const uint8_t N_DEDOS              = 5;
static const uint8_t PINO_SERVO[N_DEDOS]  = {2, 3, 4, 5, 6};
static const uint8_t PINO_BOTAO           = 7;  // botao entre o pino e o GND
static const char* const NOME_DEDO[N_DEDOS] = {"polegar", "indicador", "medio", "anelar",
                                               "minimo"};

// ---------------------------------------------------------------------
//  Calibracao de cada dedo, em graus
//  Servo montado ao contrario? Troque os dois valores daquele dedo.
//  Dedo que nao abre ou nao fecha inteiro? Ajuste ate encostar SEM forcar
//  - servo parado contra o batente esquenta e queima.
// ---------------------------------------------------------------------
static const uint8_t ABERTO[N_DEDOS]  = {0, 0, 0, 0, 0};
static const uint8_t FECHADO[N_DEDOS] = {180, 180, 180, 180, 180};

static const uint8_t VELOCIDADE_GRAUS = 3;   // graus por passo
static const uint16_t PASSO_MS        = 15;  // ~180 graus em 0,9 s
static const uint16_t DEBOUNCE_MS     = 30;
static const uint16_t SEGURAR_MS      = 1000;

// ---------------------------------------------------------------------
//  Gestos: um bit por dedo, 1 = levantado. Mesma ordem e mesmo sentido
//  do fingersUp() do cvzone, para a tabela e a camera falarem igual.
// ---------------------------------------------------------------------
struct Gesto {
  const char* nome;
  uint8_t dedos[N_DEDOS];  // polegar, indicador, medio, anelar, minimo
};

// O numero do gesto e o numero de dedos: 0 = fechada ... 5 = aberta.
static const Gesto GESTOS[] = {
    {"mao fechada", {0, 0, 0, 0, 0}},
    {"numero 1", {0, 1, 0, 0, 0}},
    {"numero 2 (paz)", {0, 1, 1, 0, 0}},
    {"numero 3", {0, 1, 1, 1, 0}},
    {"numero 4", {0, 1, 1, 1, 1}},
    {"mao aberta (5)", {1, 1, 1, 1, 1}},
    {"joinha", {1, 0, 0, 0, 0}},
    {"rock", {0, 1, 0, 0, 1}},
    {"hang loose", {1, 0, 0, 0, 1}},
    {"arminha", {1, 1, 0, 0, 0}},
};
static const uint8_t N_GESTOS = sizeof(GESTOS) / sizeof(GESTOS[0]);

// ---------------------------------------------------------------------
//  Estado
// ---------------------------------------------------------------------
static Servo servo[N_DEDOS];
static uint8_t angulo[N_DEDOS];  // onde o dedo esta agora
static uint8_t alvo[N_DEDOS];    // para onde ele vai
static uint8_t gestoAtual = 0;

static uint8_t anguloDe(uint8_t dedo, bool levantado) {
  return levantado ? ABERTO[dedo] : FECHADO[dedo];
}

static void aplicaDedos(const uint8_t* dedos) {
  for (uint8_t i = 0; i < N_DEDOS; i++) alvo[i] = anguloDe(i, dedos[i] != 0);
}

static void aplicaGesto(uint8_t g) {
  gestoAtual = g % N_GESTOS;
  aplicaDedos(GESTOS[gestoAtual].dedos);
  Serial.print(F("gesto "));
  Serial.print(gestoAtual);
  Serial.print(F(": "));
  Serial.println(GESTOS[gestoAtual].nome);
}

static void listaGestos() {
  Serial.println(F("--- gestos (digite o numero) ---"));
  for (uint8_t g = 0; g < N_GESTOS; g++) {
    Serial.print(F("  "));
    Serial.print(g);
    Serial.print(F("  "));
    Serial.println(GESTOS[g].nome);
  }
  Serial.println(F("  n  proximo   $PIMAM  dedos direto (cvzone)"));
}

// ---------------------------------------------------------------------
//  Movimento - um passo de cada dedo em direcao ao alvo, sem bloquear
// ---------------------------------------------------------------------
static void moveDedos() {
  static uint32_t ultimo = 0;
  if (millis() - ultimo < PASSO_MS) return;
  ultimo = millis();

  for (uint8_t i = 0; i < N_DEDOS; i++) {
    if (angulo[i] == alvo[i]) continue;
    if (angulo[i] < alvo[i]) {
      angulo[i] = (alvo[i] - angulo[i] > VELOCIDADE_GRAUS) ? angulo[i] + VELOCIDADE_GRAUS : alvo[i];
    } else {
      angulo[i] = (angulo[i] - alvo[i] > VELOCIDADE_GRAUS) ? angulo[i] - VELOCIDADE_GRAUS : alvo[i];
    }
    servo[i].write(angulo[i]);
  }
}

// ---------------------------------------------------------------------
//  Botao - debounce por tempo; toque curto e toque longo
// ---------------------------------------------------------------------
static void leBotao() {
  static bool estavel          = HIGH;  // INPUT_PULLUP: solto = HIGH
  static bool ultimaLeitura    = HIGH;
  static uint32_t mudouEm      = 0;
  static uint32_t apertouEm    = 0;
  static bool longoJaDisparado = false;

  const bool leitura = digitalRead(PINO_BOTAO);
  if (leitura != ultimaLeitura) {
    ultimaLeitura = leitura;
    mudouEm       = millis();
  }
  if (millis() - mudouEm < DEBOUNCE_MS) return;

  if (leitura != estavel) {
    estavel = leitura;
    if (estavel == LOW) {  // apertou
      apertouEm        = millis();
      longoJaDisparado = false;
    } else if (!longoJaDisparado) {  // soltou antes de 1 s
      aplicaGesto(gestoAtual + 1);
    }
  }

  // Segurando: dispara o toque longo sem esperar soltar.
  if (estavel == LOW && !longoJaDisparado && millis() - apertouEm >= SEGURAR_MS) {
    longoJaDisparado = true;
    aplicaGesto(0);
  }
}

// ---------------------------------------------------------------------
//  Serial - quadro do cvzone ou comando de uma letra
// ---------------------------------------------------------------------
static void leSerial() {
  static uint8_t quadro[N_DEDOS];
  static int8_t posQuadro = -1;  // -1 = fora de um quadro "$..."

  while (Serial.available()) {
    const char c = Serial.read();

    if (c == '$') {
      posQuadro = 0;
      continue;
    }
    if (posQuadro >= 0) {
      if (c != '0' && c != '1') {  // quadro corrompido: descarta
        posQuadro = -1;
        continue;
      }
      quadro[posQuadro++] = c - '0';
      if (posQuadro == N_DEDOS) {
        aplicaDedos(quadro);
        posQuadro = -1;
      }
      continue;
    }

    if (c >= '0' && c <= '9') {
      aplicaGesto(c - '0');
    } else if (c == 'n' || c == 'N') {
      aplicaGesto(gestoAtual + 1);
    } else if (c == '?') {
      listaGestos();
    }
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(PINO_BOTAO, INPUT_PULLUP);

  // Nasce FECHADA. write() antes de attach(): o primeiro pulso ja sai na
  // posicao fechada, em vez de o servo saltar para o meio (90 graus).
  for (uint8_t i = 0; i < N_DEDOS; i++) {
    angulo[i] = FECHADO[i];
    alvo[i]   = FECHADO[i];
    servo[i].write(angulo[i]);
    servo[i].attach(PINO_SERVO[i]);
  }

  Serial.println(F("\nLimbIA - mao de 5 servos"));
  for (uint8_t i = 0; i < N_DEDOS; i++) {
    Serial.print(F("  "));
    Serial.print(NOME_DEDO[i]);
    Serial.print(F(" no pino "));
    Serial.println(PINO_SERVO[i]);
  }
  Serial.print(F("  botao no pino "));
  Serial.println(PINO_BOTAO);
  listaGestos();
  aplicaGesto(0);
}

void loop() {
  leBotao();
  leSerial();
  moveDedos();
}

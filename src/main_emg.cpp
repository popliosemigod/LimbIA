// =====================================================================
//  LimbIA - main_emg.cpp
//
//  A PLACA DO EMG. Le dois eletrodos do antebraco, aprende os gestos de
//  quem vai usar a protese, decide "abrir" ou "fechar" e manda para a
//  placa da mao. E ela tambem quem levanta a rede da protese e serve a
//  tela de ajuste.
//
//  Placa: ESP32-C3 SuperMini (nao aciona nada, entao dois canais de ADC1
//  bastam). A mao fica na DevKit V1, que precisa dos seis.
//
//  O caminho do sinal, do musculo ao dedo:
//
//    eletrodo -> ADC 1 kHz -> passa-altas 20 Hz -> notch 60 Hz ->
//    retificacao -> envelope 4 Hz -> janela de 50 ms -> ln(1+x) ->
//    LDA (repouso/fechar/abrir) -> decisor (3 travas) -> pacote -> mao
//
//  Nada aqui bloqueia: a amostragem mora numa tarefa propria, a tela e
//  servida entre uma janela e outra, e o comando sai 20 vezes por segundo.
//
//  Gravar por USB:  pio run -e emg -t upload
//  Gravar por OTA:  pio run -e emg_ota -t upload   (em modo ajuste)
// =====================================================================

#include <Arduino.h>

#include "config_emg.h"
#include "emg_aquisicao.h"
#include "emg_controle.h"
#include "emg_ponte.h"
#include "painel.h"
#include "rede.h"

// ---- definicao do global declarado em config_emg.h -------------------
EstadoEmg E;

// Impressao continua do envelope, para o Serial Plotter da bancada. E o
// jeito de ver o eletrodo respondendo antes de existir tela ou modelo.
static bool plotter = false;

// ---------------------------------------------------------------------
//  Console
// ---------------------------------------------------------------------
static void ajuda() {
  Serial.println();
  Serial.println(F("================= LimbIA - placa do EMG ================="));
  Serial.println(F("   s  status                 ?  esta ajuda"));
  Serial.println(F("   c  iniciar calibracao     k  cancelar calibracao"));
  Serial.println(F("   p  modo PADRAO (usar)     a  modo AJUSTE"));
  Serial.println(F("   v  liga/desliga o envelope no Serial Plotter"));
  Serial.println(F("   z  apagar o modelo gravado"));
  Serial.println(F("   n  rede e enlace com a mao"));
  Serial.println(F("   1  mandar FECHAR    2  mandar ABRIR    0  mandar PARAR"));
  Serial.println(F("      (so em modo ajuste: em uso, quem manda e o musculo)"));
  Serial.println(F("  A tela de ajuste abre sozinha ao entrar na rede da protese."));
  Serial.println(F("========================================================="));
}

static void status() {
  Serial.println();
  Serial.printf("LimbIA %s | placa do EMG | modo %s\n", LIMBIA_VERSAO, limbia::nomeDoModo(E.modo));
  Serial.printf("  janelas: %lu (perdidas %lu) | modelo: %s\n", (unsigned long)E.janelas,
                (unsigned long)Aquisicao::descartadas(),
                E.modelo.valido ? (E.modeloSalvo ? "gravado na flash" : "em memoria") : "nenhum");

  for (uint8_t ch = 0; ch < limbia::N_CANAIS_EMG; ch++) {
    const limbia::QualidadeEletrodo& q = E.qual[ch];
    Serial.printf(
        "  eletrodo %u (%s): envelope %6.1f | nota %3u%% %s | d' %.1f | razao %.1fx | "
        "seletividade %.1fx | %s\n",
        ch + 1, ch == limbia::EMG_FLEXOR ? "fecha" : "abre", E.envelope[ch], q.nota,
        q.ok ? "OK" : "--", q.dPrime, q.razao, q.seletividade,
        limbia::textoDoDiagnostico(q.diagnostico));
  }
  if (E.calibrando) {
    Serial.printf("  calibrando: %s (ciclo %u) | acerto fora do treino %u%%\n",
                  limbia::nomeDaInstrucao(E.passo.instrucao), E.passo.ciclo,
                  (unsigned)(E.acertoGeral * 100));
  }
  Serial.printf("  entendendo: %s (%.0f%%) | comando atual: %s\n", limbia::nomeDaClasse(E.classe),
                100.0f * E.prob[E.classe], limbia::nomeDaAcao(E.acao));
  Serial.printf("  mao: %s", E.maoLigada ? "conectada" : "SEM SINAL");
  if (E.maoLigada) {
    Serial.print(F(" | posicoes ["));
    for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
      Serial.printf("%u%s", E.mao.posicao[i], i + 1 < limbia::N_JUNTAS ? " " : "");
    }
    Serial.printf("] | contato 0x%02X", E.mao.contato);
  }
  Serial.println();
}

static void statusRede() {
  const Rede::Credenciais& c = Rede::cred();
  Serial.println();
  Serial.printf("  rede \"%s\" %s | %u cliente(s) | tela em http://%s/\n", c.ssid,
                c.deFabrica ? "(DE FABRICA)" : "(escolhida pelo cliente)",
                WiFi.softAPgetStationNum(), WiFi.softAPIP().toString().c_str());
  Serial.printf("  mao: %s | OTA %s\n", E.maoLigada ? "no enlace" : "sem sinal",
                E.modo == limbia::MODO_AJUSTE ? "liberado" : "BLOQUEADO (protese em uso)");
}

static void processa(char c) {
  switch (c) {
    case 's':
    case 'S': status(); break;

    case 'c':
    case 'C':
      if (E.modo != limbia::MODO_AJUSTE) {
        Serial.println(F("  entre em modo ajuste primeiro (comando 'a')"));
        break;
      }
      Controle::iniciaCalibracao();
      Serial.println(F("  calibracao iniciada - siga as instrucoes da tela"));
      break;

    case 'k':
    case 'K':
      Controle::cancelaCalibracao();
      Serial.println(F("  calibracao cancelada"));
      break;

    case 'p':
    case 'P': {
      const char* erro = Controle::entraPadrao();
      if (erro) Serial.printf("  nao deu: %s\n", erro);
      break;
    }

    case 'a':
    case 'A': Controle::entraAjuste(); break;

    case 'v':
    case 'V':
      plotter = !plotter;
      Serial.println(plotter ? F("  envelope no plotter: ligado") : F("  envelope: desligado"));
      break;

    case 'z':
    case 'Z':
      Controle::apagaModelo();
      Controle::entraAjuste();
      Serial.println(F("  modelo apagado - e preciso calibrar de novo"));
      break;

    case 'n':
    case 'N': statusRede(); break;

    // Teste de bancada do caminho inteiro, sem precisar da tela nem de
    // eletrodo: o comando sai daqui igualzinho ao que o decisor manda.
    case '1':
    case '2':
    case '0': {
      if (E.modo != limbia::MODO_AJUSTE) {
        Serial.println(F("  em uso, quem manda na mao e o musculo (comando 'a' para ajustar)"));
        break;
      }
      if (!E.maoLigada) {
        Serial.println(F("  a mao nao esta no enlace"));
        break;
      }
      const uint8_t acao = c == '1'   ? limbia::ACAO_FECHAR
                           : c == '2' ? limbia::ACAO_ABRIR
                                      : limbia::ACAO_PARAR;
      Controle::novaAcao(acao);
      Serial.printf("  mandei %s para a mao\n", limbia::nomeDaAcao(acao));
      break;
    }

    default: ajuda(); break;
  }
}

static void console() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ') continue;
    processa(c);
  }
}

// ---------------------------------------------------------------------
//  LED: piscando em ajuste, aceso em uso, apagado sem modelo.
// ---------------------------------------------------------------------
static void led() {
  bool aceso;
  if (E.modo == limbia::MODO_PADRAO) {
    aceso = true;
  } else if (E.calibrando) {
    aceso = (millis() / 150) % 2;  // piscando rapido: calibrando
  } else {
    aceso = (millis() / 700) % 2;  // piscando devagar: ajuste
  }
#if LED_ATIVO_BAIXO
  digitalWrite(PIN_LED_PLACA, aceso ? LOW : HIGH);
#else
  digitalWrite(PIN_LED_PLACA, aceso ? HIGH : LOW);
#endif
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);  // unica espera do firmware: janela para o monitor engatar

  memset(&E, 0, sizeof(E));
  E.acao = limbia::ACAO_NENHUMA;

  pinMode(PIN_LED_PLACA, OUTPUT);

  Serial.println();
  Serial.println(F("====================================================="));
  Serial.printf("  LimbIA %s  |  placa do EMG  |  no: %s\n", LIMBIA_VERSAO, LIMBIA_NOME);
  Serial.printf("  build %s %s\n", __DATE__, __TIME__);
  Serial.println(F("====================================================="));

  Rede::iniciaBotao(PIN_BOTAO);
  Rede::avisaSegredos();
  Rede::carrega();
  if (Rede::iniciaPontoDeAcesso()) {
    Serial.printf("[rede] \"%s\"%s no ar em %s\n", Rede::cred().ssid,
                  Rede::cred().deFabrica ? " (senha de fabrica)" : "",
                  WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println(F("[rede] FALHA ao levantar o ponto de acesso"));
  }
  Rede::iniciaOta(REDE_HOST_EMG, nullptr);
  Ponte::begin();
  Painel::begin();

  // A protese liga pronta para uso se ja houver calibracao gravada. Quem
  // ja calibrou nao precisa calibrar de novo a cada manha.
  if (Controle::carregaModelo()) {
    E.modo = limbia::MODO_PADRAO;
    limbia::decisorZera(&E.decisor, limbia::C_ABRIR);
    Serial.println(F("[emg] modelo da flash - entrando em MODO PADRAO"));
  } else {
    E.modo = limbia::MODO_AJUSTE;
    Serial.println(F("[emg] sem modelo gravado - MODO AJUSTE, calibre os eletrodos"));
  }

  Aquisicao::begin();
  Serial.printf("[emg] amostrando %d Hz, janela de %d ms, entrada %s\n", EMG_FS_HZ, EMG_JANELA_MS,
                EMG_ENTRADA_BRUTA ? "crua (o firmware filtra)" : "envelope do modulo");

  ajuda();
}

// ---------------------------------------------------------------------
void loop() {
  // As janelas prontas primeiro: e delas que sai a decisao.
  Aquisicao::Janela j;
  while (Aquisicao::proxima(&j)) {
    Controle::processaJanela(j);
    if (plotter) {
      Serial.printf("flexor:%.0f,extensor:%.0f\n", E.envelope[limbia::EMG_FLEXOR],
                    E.envelope[limbia::EMG_EXTENSOR]);
    }
  }

  Ponte::tick();
  Painel::tick();
  console();
  led();

  // OTA so em ajuste: gravar firmware com a protese em uso pararia a mao
  // no meio de um gesto.
  Rede::tickOta(E.modo == limbia::MODO_AJUSTE);

  if (Rede::botaoSegurado(PIN_BOTAO)) {
    Serial.println(F("[rede] BOOT segurado 10 s - rede de volta ao de fabrica, reiniciando"));
    Rede::apaga();
    Serial.flush();
    ESP.restart();
  }

  if (E.reiniciarEm && (int32_t)(millis() - E.reiniciarEm) >= 0) {
    Serial.flush();
    ESP.restart();
  }
}

// =====================================================================
//  LimbIA - rede.h
//  Nome e senha da rede, gravados na flash; ponto de acesso (placa do
//  EMG) ou cliente (placa da mao); OTA; e o botao que volta ao de
//  fabrica. Comum as duas placas.
//
//  "Escolher uma vez e ficar gravado para sempre"
//  -----------------------------------------------
//  O nome e a senha vao para a NVS, num namespace proprio. Sobrevivem a
//  queda de energia, a regravacao do firmware por USB ou por OTA, e ao
//  comando 'f' da mao, que apaga a calibracao mas nao isto. So saem de la
//  por dois caminhos: o cliente troca na tela de ajuste, ou alguem segura
//  o BOOT por dez segundos - que e a saida para "esqueci a senha".
// =====================================================================
#pragma once
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFi.h>

#include <limbia_enlace.h>

#include "config_rede.h"
#include "segredos.h"

namespace Rede {

struct Credenciais {
  char ssid[33];
  char senha[64];
  bool deFabrica;  // ainda com o nome e a senha que vieram gravados
};

inline Credenciais& cred() {
  static Credenciais c;
  return c;
}

inline void padraoDeFabrica(Credenciais* c) {
  strncpy(c->ssid, REDE_SSID_FABRICA, sizeof(c->ssid) - 1);
  c->ssid[sizeof(c->ssid) - 1] = 0;
  strncpy(c->senha, LIMBIA_AP_PASS, sizeof(c->senha) - 1);
  c->senha[sizeof(c->senha) - 1] = 0;
  c->deFabrica                   = true;
}

// Devolve true se o nome e a senha vieram da flash.
inline bool carrega() {
  Credenciais& c = cred();
  padraoDeFabrica(&c);

  Preferences p;
  if (!p.begin(NVS_REDE, true)) return false;  // namespace ainda nao existe
  char ssid[33]  = {0};
  char senha[64] = {0};
  p.getString("ssid", ssid, sizeof(ssid));
  p.getString("senha", senha, sizeof(senha));
  p.end();

  // Confere antes de usar: credencial que o WPA2 recusaria faria a placa
  // subir sem rede nenhuma - e sem rede ninguem alcanca a tela para
  // consertar.
  if (!limbia::ssidValido(ssid) || !limbia::senhaValida(senha)) return false;
  strcpy(c.ssid, ssid);
  strcpy(c.senha, senha);
  c.deFabrica = false;
  return true;
}

inline bool salva(const char* ssid, const char* senha) {
  if (!limbia::ssidValido(ssid) || !limbia::senhaValida(senha)) return false;
  Preferences p;
  if (!p.begin(NVS_REDE, false)) return false;
  const bool ok = p.putString("ssid", ssid) > 0 && p.putString("senha", senha) > 0;
  p.end();
  if (!ok) return false;
  Credenciais& c = cred();
  strcpy(c.ssid, ssid);
  strcpy(c.senha, senha);
  c.deFabrica = false;
  return true;
}

inline void apaga() {
  Preferences p;
  if (p.begin(NVS_REDE, false)) {
    p.clear();
    p.end();
  }
  padraoDeFabrica(&cred());
}

// ---------------------------------------------------------------------
//  Placa do EMG: ponto de acesso
// ---------------------------------------------------------------------
inline bool iniciaPontoDeAcesso() {
  const Credenciais& c = cred();
  WiFi.persistent(false);  // a NVS do Wi-Fi nao e a nossa fonte da verdade
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(REDE_IP_EMG), IPAddress(REDE_IP_EMG), IPAddress(REDE_MASCARA));
  return WiFi.softAP(c.ssid, c.senha, REDE_CANAL, 0, REDE_MAX_CLIENT);
}

// ---------------------------------------------------------------------
//  Placa da mao: cliente, com IP fixo
//
//  Modem sleep DESLIGADO. Com ele ligado, o radio cochila entre os
//  beacons e o pacote de comando pode esperar mais de 100 ms para ser
//  entregue - latencia que o decisor ja gastou com proposito.
// ---------------------------------------------------------------------
inline uint32_t& ultimaTentativa() {
  static uint32_t t = 0;
  return t;
}

inline void conectaCliente() {
  const Credenciais& c = cred();
  WiFi.config(IPAddress(REDE_IP_MAO), IPAddress(REDE_IP_EMG), IPAddress(REDE_MASCARA));
  WiFi.begin(c.ssid, c.senha, REDE_CANAL);
  ultimaTentativa() = millis();
}

inline void iniciaCliente() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);  // a reconexao e feita no tick, com ritmo proprio
  conectaCliente();
}

// Chamar todo loop na placa da mao. Nao bloqueia.
inline void tickCliente() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - ultimaTentativa() < REDE_RECONEXAO_MS) return;
  WiFi.disconnect(false, false);
  conectaCliente();
}

// ---------------------------------------------------------------------
//  OTA
//
//  So escuta quando `permitido` - quem decide e cada placa: a mao nao
//  aceita firmware novo enquanto esta sendo comandada pelo EMG, porque
//  gravar desliga os servos e ela soltaria o que estiver segurando.
//  Fora da permissao, `ArduinoOTA.handle()` nao e chamado e a tentativa
//  de gravacao expira do lado do PC com "No response from device".
// ---------------------------------------------------------------------
typedef void (*AoComecarOta)();

inline void iniciaOta(const char* host, AoComecarOta aoComecar) {
  ArduinoOTA.setHostname(host);
  ArduinoOTA.setPassword(LIMBIA_OTA_PASS);
  ArduinoOTA.setRebootOnSuccess(true);
  ArduinoOTA.onStart([aoComecar]() {
    if (aoComecar) aoComecar();
    Serial.println(F("[ota] recebendo firmware..."));
  });
  ArduinoOTA.onEnd([]() { Serial.println(F("[ota] gravado - reiniciando")); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] ERRO %u\n", (unsigned)e); });
  ArduinoOTA.begin();
}

inline void tickOta(bool permitido) {
  if (permitido) ArduinoOTA.handle();
}

// ---------------------------------------------------------------------
//  Botao de volta ao de fabrica (BOOT segurado por 10 s)
// ---------------------------------------------------------------------
inline void iniciaBotao(uint8_t pino) {
  pinMode(pino, INPUT_PULLUP);
}

// Devolve true no instante em que o tempo e atingido.
inline bool botaoSegurado(uint8_t pino) {
  static uint32_t desde = 0;
  static bool disparou  = false;
  if (digitalRead(pino) == LOW) {
    if (desde == 0) desde = millis() | 1;
    if (!disparou && millis() - desde >= REDE_RESET_BOTAO_MS) {
      disparou = true;
      return true;
    }
  } else {
    desde    = 0;
    disparou = false;
  }
  return false;
}

inline void avisaSegredos() {
  if (LIMBIA_SEGREDOS_DE_EXEMPLO) {
    Serial.println(F("[rede] AVISO: compilado SEM include/secrets.h - senha de OTA publica."));
    Serial.println(F("[rede]        Serve para bancada. Nao entregue protese assim."));
  }
}

}  // namespace Rede

// =====================================================================
//  LimbIA - painel.h
//  O servidor da tela de ajuste, na placa do EMG.
//
//  A tela abre sozinha: portal cativo
//  ----------------------------------
//  Quando o PC ou o celular entra numa rede Wi-Fi, o sistema faz uma
//  pergunta de teste a um endereco conhecido (o Windows pede
//  msftconnecttest.com, o Android pede generate_204, o iPhone pede
//  captive.apple.com). Aqui, o DNS da placa responde QUALQUER nome com o
//  proprio IP, e o servidor redireciona qualquer caminho desconhecido
//  para a tela. O sistema entende "esta rede tem uma pagina de entrada"
//  e abre a tela sozinho - e isso que faz "conectei na protese e a tela
//  de ajuste apareceu".
//
//  O servidor e o sincrono do proprio core (WebServer). A amostragem do
//  EMG nao depende dele: roda numa tarefa propria, e uma pagina demorando
//  para sair nao abre buraco no sinal.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <limbia_enlace.h>

#include "config_emg.h"
#include "emg_controle.h"
#include "emg_ponte.h"
#include "painel_html.h"
#include "rede.h"

namespace Painel {

inline WebServer& servidor() {
  static WebServer s(80);
  return s;
}

inline DNSServer& dns() {
  static DNSServer d;
  return d;
}

// ---------------------------------------------------------------------
//  JSON do estado, montado a mao num buffer fixo: sem alocacao, sem
//  biblioteca. Os nomes sao curtos de proposito - vai 4 vezes por
//  segundo. O significado de cada campo esta no JavaScript da pagina.
// ---------------------------------------------------------------------
struct Json {
  char buf[2600];
  size_t n;
  void zera() {
    n      = 0;
    buf[0] = 0;
  }
  void add(const char* fmt, ...) {
    if (n >= sizeof(buf) - 1) return;
    va_list a;
    va_start(a, fmt);
    const int w = vsnprintf(buf + n, sizeof(buf) - n, fmt, a);
    va_end(a);
    if (w > 0) n += (size_t)w < sizeof(buf) - n ? (size_t)w : sizeof(buf) - n - 1;
  }
  // Array de uint16 lidos um a um: o PacoteTelemetria e empacotado, e
  // passar ponteiro para membro dele seria leitura desalinhada.
  template <typename F>
  void lista(uint8_t quantos, F valor) {
    add("[");
    for (uint8_t i = 0; i < quantos; i++) add(i ? ",%u" : "%u", (unsigned)valor(i));
    add("]");
  }
};

inline Json& json() {
  static Json j;
  return j;
}

inline void enviaJson(int codigo, const char* corpo) {
  servidor().sendHeader("Cache-Control", "no-store");
  servidor().send(codigo, "application/json", corpo);
}

// Resposta de acao: {"ok":1} ou {"ok":0,"msg":"..."}. As mensagens sao
// constantes do firmware, sem aspas dentro.
inline void responde(const char* erro) {
  char b[260];
  if (erro) {
    snprintf(b, sizeof(b), "{\"ok\":0,\"msg\":\"%s\"}", erro);
  } else {
    snprintf(b, sizeof(b), "{\"ok\":1}");
  }
  enviaJson(200, b);
}

inline void rotaEstado() {
  Json& j = json();
  j.zera();
  const Rede::Credenciais& c        = Rede::cred();
  const limbia::PacoteTelemetria& t = E.mao;

  // O nome da rede so tem caracteres imprimiveis e nunca aspas nem barra
  // invertida (limbia::ssidValido) - vai direto para o JSON.
  j.add("{\"m\":%u,\"v\":\"%s\",\"r\":{\"s\":\"%s\",\"f\":%u,\"t\":%u},", E.modo, LIMBIA_VERSAO,
        c.ssid, c.deFabrica ? 1 : 0, E.trocaRede);

  j.add("\"c\":{\"on\":%u,\"i\":%u,\"rs\":%u,\"d\":%u,\"ci\":%u,\"e\":[", E.calibrando ? 1 : 0,
        E.passo.instrucao, E.passo.restanteMs, E.passo.duracaoMs ? E.passo.duracaoMs : 1,
        E.passo.ciclo);
  for (uint8_t ch = 0; ch < limbia::N_CANAIS_EMG; ch++) {
    const limbia::QualidadeEletrodo& q = E.qual[ch];
    j.add("%s[%u,%u,%u,%.1f,%.1f,%.1f]", ch ? "," : "", q.nota, q.ok, q.diagnostico, q.dPrime,
          q.razao, q.seletividade);
  }
  j.add("],\"g\":[%u,%u,%u]},", E.notaGeral, E.geralOk ? 1 : 0,
        (unsigned)(E.acertoGeral * 100.0f + 0.5f));

  j.add("\"pu\":%u,\"ms\":%u,", Controle::podeUsar() ? 1 : 0, E.modeloSalvo ? 1 : 0);
  j.add("\"s\":{\"n\":[%.2f,%.2f],\"sat\":[%.3f,%.3f],\"solto\":%u},", Controle::nivel(0),
        Controle::nivel(1), E.saturacao[0], E.saturacao[1], E.eletrodoSolto ? 1 : 0);
  j.add("\"k\":{\"v\":%u,\"c\":%u,\"p\":[%.3f,%.3f,%.3f]},", Controle::modeloAtivo() ? 1 : 0,
        E.classe, E.prob[0], E.prob[1], E.prob[2]);
  j.add("\"a\":{\"a\":%u,\"q\":%u},\"gp\":%u,", E.acao, E.seqAcao, E.gravaPoses);

  j.add("\"h\":{\"on\":%u,\"pos\":", E.maoLigada ? 1 : 0);
  j.lista(limbia::N_JUNTAS, [&](uint8_t i) { return t.posicao[i]; });
  j.add(",\"a\":");
  j.lista(limbia::N_JUNTAS, [&](uint8_t i) { return t.aberta[i]; });
  j.add(",\"f\":");
  j.lista(limbia::N_JUNTAS, [&](uint8_t i) { return t.fechada[i]; });
  j.add(",\"ctt\":%u,\"mov\":%u,\"fl\":%u,\"o\":%u,\"oc\":%u,\"fo\":%u}}", t.contato, t.emMovimento,
        t.flags, t.objeto, t.confianca, t.folgas);

  enviaJson(200, j.buf);
}

inline bool emAjuste() {
  return E.modo == limbia::MODO_AJUSTE;
}

inline void rotaModo() {
  const String para = servidor().arg("para");
  if (para == "padrao") {
    responde(Controle::entraPadrao());
  } else if (para == "ajuste") {
    Controle::entraAjuste();
    responde(nullptr);
  } else {
    responde("Modo desconhecido.");
  }
}

inline void rotaCalibracao() {
  if (!emAjuste()) return responde("Entre no modo ajuste primeiro.");
  const String acao = servidor().arg("acao");
  if (acao == "iniciar") {
    Controle::iniciaCalibracao();
    Serial.println(F("[emg] calibracao iniciada"));
  } else if (acao == "cancelar") {
    Controle::cancelaCalibracao();
    Serial.println(F("[emg] calibracao cancelada - o modelo anterior continua"));
  }
  responde(nullptr);
}

inline void rotaRede() {
  if (!emAjuste()) return responde("Entre no modo ajuste primeiro.");
  const String ssid  = servidor().arg("ssid");
  const String senha = servidor().arg("senha");
  responde(Ponte::trocaRede(ssid.c_str(), senha.c_str()));
}

// "0,0,0,0,0,300,500" -> sete valores. Recusa qualquer coisa fora disso.
inline bool lePose(const String& csv, uint16_t destino[limbia::N_JUNTAS]) {
  const char* p = csv.c_str();
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    char* fim    = nullptr;
    const long v = strtol(p, &fim, 10);
    if (fim == p || v < 0 || v > 1000) return false;
    destino[i] = (uint16_t)v;
    p          = fim;
    if (i + 1 < limbia::N_JUNTAS) {
      if (*p != ',') return false;
      p++;
    }
  }
  return *p == 0;
}

inline void rotaPoses() {
  if (!emAjuste()) return responde("Entre no modo ajuste primeiro.");
  uint16_t a[limbia::N_JUNTAS], f[limbia::N_JUNTAS];
  if (!lePose(servidor().arg("a"), a) || !lePose(servidor().arg("f"), f)) {
    return responde("Pose inválida.");
  }
  responde(Ponte::enviaPoses(a, f, servidor().arg("g") == "1"));
}

// Testar um movimento so e permitido em ajuste: em uso, quem manda na mao
// e o musculo, e um botao na tela competindo com ele seria perigoso.
inline void rotaTestar() {
  if (!emAjuste()) return responde("Entre no modo ajuste primeiro.");
  if (!E.maoLigada) return responde("A mão não está conectada.");
  const String pose = servidor().arg("pose");
  if (pose == "aberta") {
    Controle::novaAcao(limbia::ACAO_ABRIR);
  } else if (pose == "fechada") {
    Controle::novaAcao(limbia::ACAO_FECHAR);
  } else {
    Controle::novaAcao(limbia::ACAO_PARAR);
  }
  responde(nullptr);
}

// Qualquer outro endereco - inclusive as perguntas de teste dos sistemas
// operacionais - vai para a tela. E o portal cativo.
inline void rotaQualquer() {
  servidor().sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  servidor().send(302, "text/plain", "");
}

inline void begin() {
  dns().setErrorReplyCode(DNSReplyCode::NoError);
  dns().start(53, "*", WiFi.softAPIP());

  WebServer& s = servidor();
  s.on("/", HTTP_GET, []() {
    servidor().sendHeader("Cache-Control", "no-store");
    servidor().send_P(200, "text/html; charset=utf-8", PAINEL_HTML);
  });
  s.on("/estado", HTTP_GET, rotaEstado);
  s.on("/modo", HTTP_POST, rotaModo);
  s.on("/calibracao", HTTP_POST, rotaCalibracao);
  s.on("/rede", HTTP_POST, rotaRede);
  s.on("/poses", HTTP_POST, rotaPoses);
  s.on("/testar", HTTP_POST, rotaTestar);
  s.onNotFound(rotaQualquer);
  s.begin();
}

inline void tick() {
  dns().processNextRequest();
  servidor().handleClient();
}

}  // namespace Painel

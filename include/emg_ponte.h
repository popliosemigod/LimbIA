// =====================================================================
//  LimbIA - emg_ponte.h
//  A placa do EMG falando com a mao: comando a 20 Hz, telemetria de
//  volta, e o envio que espera confirmacao.
//
//  A troca de senha e o caso que justifica a confirmacao
//  -----------------------------------------------------
//  O nome e a senha da rede sao das DUAS placas. Se so a placa do EMG
//  trocasse, a mao reiniciaria procurando a rede velha e nunca mais
//  voltaria - protese com a mao muda, e so um cabo USB para consertar.
//  Por isso a ordem e:
//    1. manda a senha nova para a mao, repetindo ate ela confirmar;
//    2. a mao grava, confirma e reinicia;
//    3. SO ENTAO esta placa grava e reinicia.
//  Se a mao nao confirmar em 3 s, nada muda em nenhuma das duas.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <stddef.h>

#include <limbia_enlace.h>

#include "config_emg.h"
#include "enlace.h"
#include "rede.h"

namespace Ponte {

struct Pendente {
  bool ativo;
  uint8_t tipo;
  uint8_t carga[sizeof(limbia::PacoteRede)];  // o maior dos confiaveis
  uint16_t n;
  uint32_t inicioMs;
  uint32_t ultimoEnvioMs;
};

static_assert(sizeof(limbia::PacoteRede) >= sizeof(limbia::PacotePoses),
              "o buffer do envio confiavel precisa caber as poses");

inline Pendente& P() {
  static Pendente p = Pendente();
  return p;
}

inline void begin() {
  Enlace::begin(ENLACE_PORTA_EMG);
}

// Um envio confiavel de cada vez. Devolve false se ja houver um.
inline bool enviaConfiavel(uint8_t tipo, const void* carga, uint16_t n) {
  Pendente& p = P();
  if (p.ativo || n > sizeof(p.carga)) return false;
  p.tipo = tipo;
  memcpy(p.carga, carga, n);
  p.n             = n;
  p.inicioMs      = millis();
  p.ultimoEnvioMs = 0;
  p.ativo         = true;
  return true;
}

inline uint8_t& esperaDoTipo(uint8_t tipo) {
  static uint8_t nada = ESPERA_NADA;
  if (tipo == limbia::PKT_REDE) return E.trocaRede;
  if (tipo == limbia::PKT_POSES) return E.gravaPoses;
  return nada;
}

inline void conclui(bool ok) {
  Pendente& p = P();
  p.ativo     = false;
  if (p.tipo == limbia::PKT_REDE && ok) {
    limbia::PacoteRede r;
    memcpy(&r, p.carga, sizeof(r));
    // Passo 3: a mao ja gravou. Agora esta placa.
    ok = Rede::salva(r.ssid, r.senha);
    if (ok) {
      // Tempo para a tela buscar o estado uma ultima vez e mostrar ao
      // cliente o nome da rede nova antes de a conexao cair.
      E.reiniciarEm = (millis() + 2500) | 1;
      Serial.printf("[rede] mao confirmou; gravado. Reiniciando na rede \"%s\"\n", r.ssid);
    }
  }
  esperaDoTipo(p.tipo) = ok ? ESPERA_OK : ESPERA_FALHOU;
  if (!ok) Serial.printf("[enlace] a mao NAO confirmou (%u) - nada mudou\n", p.tipo);
}

inline void tick() {
  const uint32_t agora = millis();

  // ---- recebe ----
  uint8_t buf[limbia::ENLACE_MAX_PACOTE];
  limbia::CabecalhoPacote cab;
  const uint8_t* carga = nullptr;
  IPAddress origem;
  for (uint8_t n = 0; n < 8 && Enlace::recebe(buf, &cab, &carga, &origem); n++) {
    if (origem != IPAddress(REDE_IP_MAO)) continue;
    if (cab.tipo == limbia::PKT_TELEMETRIA) {
      memcpy(&E.mao, carga, sizeof(E.mao));
      if (!E.maoLigada) Serial.println(F("[enlace] mao conectada"));
      E.maoLigada          = true;
      E.ultimaTelemetriaMs = agora;
    } else if (cab.tipo == limbia::PKT_CONFIRMA) {
      limbia::PacoteConfirma c;
      memcpy(&c, carga, sizeof(c));
      // Um envio confiavel por vez: basta casar o tipo. Casar o seq faria
      // a confirmacao de uma repeticao anterior ser jogada fora.
      if (P().ativo && c.tipo == P().tipo) conclui(c.ok != 0);
    }
  }

  if (E.maoLigada && agora - E.ultimaTelemetriaMs > ENLACE_TIMEOUT_MS) {
    E.maoLigada = false;
    Serial.println(F("[enlace] mao sem sinal"));
  }

  // ---- comando, 20 Hz, sempre - e o que a mao usa como batimento ----
  static uint32_t proximo = 0;
  if ((int32_t)(agora - proximo) >= 0) {
    proximo = agora + ENLACE_COMANDO_MS;
    limbia::PacoteComando c;
    c.modo      = E.modo;
    c.acao      = E.acao;
    c.seqAcao   = E.seqAcao;
    c.confianca = (uint8_t)(100.0f * E.prob[E.classe] + 0.5f);
    Enlace::envia(IPAddress(REDE_IP_MAO), ENLACE_PORTA_MAO, limbia::PKT_COMANDO, &c, sizeof(c));
  }

  // ---- envio confiavel: repete ate confirmar ou desistir ----
  Pendente& p = P();
  if (p.ativo) {
    if (agora - p.inicioMs > ENLACE_TROCA_REDE_MS) {
      conclui(false);
    } else if (p.ultimoEnvioMs == 0 || agora - p.ultimoEnvioMs >= ENLACE_TROCA_REDE_REPETIR) {
      Enlace::envia(IPAddress(REDE_IP_MAO), ENLACE_PORTA_MAO, p.tipo, p.carga, p.n);
      p.ultimoEnvioMs = agora;
    }
  }
}

// ---------------------------------------------------------------------
//  O que a tela pede
// ---------------------------------------------------------------------

// Devolve nullptr se comecou; senao o motivo.
inline const char* trocaRede(const char* ssid, const char* senha) {
  if (!limbia::ssidValido(ssid)) return "Nome inválido: de 1 a 32 caracteres, sem aspas.";
  if (!limbia::senhaValida(senha)) return "Senha inválida: de 8 a 63 caracteres.";
  if (!E.maoLigada) {
    return "A mão não está conectada. Ligue a mão: as duas placas precisam aprender a senha "
           "nova juntas.";
  }
  limbia::PacoteRede r;
  memset(&r, 0, sizeof(r));
  strncpy(r.ssid, ssid, sizeof(r.ssid) - 1);
  strncpy(r.senha, senha, sizeof(r.senha) - 1);
  if (!enviaConfiavel(limbia::PKT_REDE, &r, sizeof(r))) return "Aguarde a operação anterior.";
  E.trocaRede = ESPERA_PENDENTE;
  return nullptr;
}

inline const char* enviaPoses(const uint16_t aberta[limbia::N_JUNTAS],
                              const uint16_t fechada[limbia::N_JUNTAS], bool gravar) {
  if (!limbia::poseValida(aberta) || !limbia::poseValida(fechada)) return "Pose fora de 0..1000.";
  if (!E.maoLigada) return "A mão não está conectada.";
  // Escrita por deslocamento, sem ponteiro para membro de struct
  // empacotada: no Xtensa, acesso de 16 bits desalinhado e excecao.
  limbia::PacotePoses p;
  uint8_t* base = (uint8_t*)&p;
  memcpy(base + offsetof(limbia::PacotePoses, aberta), aberta, sizeof(p.aberta));
  memcpy(base + offsetof(limbia::PacotePoses, fechada), fechada, sizeof(p.fechada));
  p.gravar = gravar ? 1 : 0;
  if (!enviaConfiavel(limbia::PKT_POSES, &p, sizeof(p))) return "Aguarde a operação anterior.";
  E.gravaPoses = ESPERA_PENDENTE;
  return nullptr;
}

}  // namespace Ponte

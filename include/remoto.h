// =====================================================================
//  LimbIA - remoto.h
//  A mao ouvindo a placa do EMG.
//
//  Tres regras, e cada uma e uma decisao de seguranca:
//
//  1. Acao so e executada quando o numero dela MUDA. O comando chega 20
//     vezes por segundo repetindo o mesmo estado; a mao nao recomeca o
//     movimento a cada repeticao.
//
//  2. O primeiro pacote depois de um silencio so SINCRONIZA - nao move.
//     A mao que acabou de ligar, ou que acabou de recuperar o enlace, nao
//     sabe ha quanto tempo aquela acao foi decidida. Nada acontece sem
//     uma decisao nova, tomada com a mao ja ouvindo.
//
//  3. Enlace perdido com a protese em uso = cada junta para onde esta.
//     Servo parado e o modo de falha seguro. A mao NAO abre: abrir
//     derrubaria o que ela estiver segurando.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <stddef.h>

#include <limbia_enlace.h>

#include "atuador.h"
#include "config.h"
#include "enlace.h"
#include "memoria.h"
#include "preensao.h"
#include "rede.h"

namespace Remoto {

struct Estado {
  bool ativo;         // recebendo pacotes dentro do timeout
  bool sincronizado;  // ja conhece o numero da acao atual
  uint8_t modo;
  uint16_t seqAcao;
  uint8_t ultimaAcao;
  uint32_t ultimoMs;
  uint32_t reiniciarEm;  // 0 = nada agendado
  uint32_t perdidos;     // quantas vezes o enlace caiu
};

inline Estado& S() {
  static Estado e = Estado();
  return e;
}

inline void begin() {
  Enlace::begin(ENLACE_PORTA_MAO);
}

// Sendo comandada pelo EMG agora? E o que bloqueia o OTA.
inline bool emUsoPeloEmg() {
  return S().ativo && S().modo == limbia::MODO_PADRAO;
}

inline void executa(uint8_t acao) {
  M.sobrecarga = false;
  switch (acao) {
    case limbia::ACAO_ABRIR:
      Preensao::fase()        = Preensao::PARADO;
      Preensao::temLeitura()  = false;
      Dedos::pararNoContato() = false;  // abrir vai ate a pose, nada a encontrar no caminho
      Dedos::vaiParaPose(M.poseAberta);
      break;
    case limbia::ACAO_FECHAR:
      Preensao::inicia(&M.poseFechada);  // fecha ate a pose ou ate encostar
      break;
    case limbia::ACAO_PARAR:
      Dedos::para();
      Preensao::fase() = Preensao::PARADO;
      break;
    default: return;
  }
  M.movimentos++;
  Serial.printf("  [emg] %s  (modo %s)\n", limbia::nomeDaAcao(acao), limbia::nomeDoModo(S().modo));
}

inline void confirma(uint8_t tipo, uint16_t seq, bool ok) {
  limbia::PacoteConfirma c;
  c.tipo = tipo;
  c.seq  = seq;
  c.ok   = ok ? 1 : 0;
  Enlace::envia(IPAddress(REDE_IP_EMG), ENLACE_PORTA_EMG, limbia::PKT_CONFIRMA, &c, sizeof(c));
}

inline void processa(const limbia::CabecalhoPacote& cab, const uint8_t* carga) {
  Estado& s  = S();
  s.ultimoMs = millis();
  if (!s.ativo) {
    s.ativo        = true;
    s.sincronizado = false;
    Serial.println(F("[enlace] placa do EMG conectada"));
  }

  switch (cab.tipo) {
    case limbia::PKT_COMANDO: {
      limbia::PacoteComando c;
      memcpy(&c, carga, sizeof(c));
      s.modo = c.modo;
      if (!s.sincronizado) {
        s.seqAcao      = c.seqAcao;  // regra 2: so sincroniza
        s.sincronizado = true;
        break;
      }
      if (c.seqAcao != s.seqAcao) {
        s.seqAcao    = c.seqAcao;
        s.ultimaAcao = c.acao;
        executa(c.acao);
      }
      break;
    }

    case limbia::PKT_POSES: {
      // Copia para arrays alinhados ANTES de ler: a struct e empacotada, e
      // no Xtensa uma leitura de 16 bits por ponteiro desalinhado e
      // excecao (LoadStoreAlignment), nao lentidao.
      limbia::Pose aberta, fechada;
      memcpy(aberta.alvo, carga + offsetof(limbia::PacotePoses, aberta), sizeof(aberta.alvo));
      memcpy(fechada.alvo, carga + offsetof(limbia::PacotePoses, fechada), sizeof(fechada.alvo));
      const bool gravar = carga[offsetof(limbia::PacotePoses, gravar)] != 0;

      bool ok = limbia::poseValida(aberta.alvo) && limbia::poseValida(fechada.alvo);
      if (ok) {
        M.poseAberta   = aberta;
        M.poseFechada  = fechada;
        M.posesDaFlash = false;  // em RAM, diferente da flash, ate gravar
        if (gravar) ok = Memoria::salvaPoses();
        Serial.printf("[enlace] poses %s\n",
                      gravar ? (ok ? "gravadas" : "FALHA ao gravar") : "atualizadas (sem gravar)");
      }
      confirma(cab.tipo, cab.seq, ok);
      break;
    }

    case limbia::PKT_REDE: {
      limbia::PacoteRede r;
      memcpy(&r, carga, sizeof(r));
      r.ssid[sizeof(r.ssid) - 1]   = 0;
      r.senha[sizeof(r.senha) - 1] = 0;
      const bool ok                = Rede::salva(r.ssid, r.senha);
      confirma(cab.tipo, cab.seq, ok);
      if (ok && s.reiniciarEm == 0) {
        // Reinicia um instante depois, para a confirmacao sair antes.
        s.reiniciarEm = (millis() + 800) | 1;
        Serial.printf("[rede] nome e senha novos gravados (rede \"%s\") - reiniciando\n", r.ssid);
      }
      break;
    }

    default: break;
  }
}

inline void enviaTelemetria() {
  static uint32_t proximo = 0;
  const uint32_t agora    = millis();
  if ((int32_t)(agora - proximo) < 0) return;
  // Sem enlace ativo nao ha quem escute: manda de dois em dois segundos,
  // so para se anunciar quando a placa do EMG voltar.
  proximo = agora + (S().ativo ? ENLACE_TELEMETRIA_MS : 2000);
  if (WiFi.status() != WL_CONNECTED) return;

  limbia::PacoteTelemetria t;
  memset(&t, 0, sizeof(t));
  for (uint8_t i = 0; i < limbia::N_JUNTAS; i++) {
    t.posicao[i]    = M.junta[i].posicao;
    t.correnteMa[i] = M.junta[i].correnteMa;
    if (M.junta[i].contato) t.contato |= LIMBIA_BIT(i);
    if (M.junta[i].emMovimento) t.emMovimento |= LIMBIA_BIT(i);
    t.aberta[i]  = M.poseAberta.alvo[i];
    t.fechada[i] = M.poseFechada.alvo[i];
  }
  if (M.saidasLigadas) t.flags |= limbia::TEL_SAIDAS;
  if (M.calibrada) t.flags |= limbia::TEL_CALIBRADA;
  if (M.posesDaFlash) t.flags |= limbia::TEL_POSES;
  if (M.sobrecarga) t.flags |= limbia::TEL_SOBRECARGA;
  if (Preensao::temLeitura()) t.flags |= limbia::TEL_LEITURA;
  t.objeto          = Preensao::objeto();
  t.confianca       = Preensao::confianca();
  t.folgas          = M.folgas;
  t.seqAcaoAtendida = S().seqAcao;
  strncpy(t.versao, LIMBIA_VERSAO, sizeof(t.versao) - 1);

  // Enquanto a placa do EMG estiver fora do ar, o lwIP nao tem rota e cada
  // tentativa vira erro no log. Espaca em vez de insistir 10 vezes por
  // segundo - a mao continua funcionando pelo console, sozinha.
  if (Enlace::envia(IPAddress(REDE_IP_EMG), ENLACE_PORTA_EMG, limbia::PKT_TELEMETRIA, &t,
                    sizeof(t)) == 0) {
    proximo = agora + 500;
  }
}

// Chamar todo loop.
inline void tick() {
  Estado& s = S();

  uint8_t buf[limbia::ENLACE_MAX_PACOTE];
  limbia::CabecalhoPacote cab;
  const uint8_t* carga = nullptr;
  IPAddress origem;
  // No maximo alguns pacotes por loop: uma rajada nao pode prender o
  // loop longe do tick de movimento.
  for (uint8_t n = 0; n < 8 && Enlace::recebe(buf, &cab, &carga, &origem); n++) {
    // So a placa do EMG comanda. Um PC na rede da protese nao move a mao.
    if (origem != IPAddress(REDE_IP_EMG)) continue;
    processa(cab, carga);
  }

  if (s.ativo && millis() - s.ultimoMs > ENLACE_TIMEOUT_MS) {
    s.ativo        = false;
    s.sincronizado = false;
    s.perdidos++;
    if (s.modo == limbia::MODO_PADRAO) {
      Dedos::para();  // regra 3
      Preensao::fase() = Preensao::PARADO;
      Serial.println(F("[enlace] PERDIDO com a protese em uso - juntas paradas onde estao"));
    } else {
      Serial.println(F("[enlace] placa do EMG desconectada"));
    }
  }

  enviaTelemetria();

  if (s.reiniciarEm && (int32_t)(millis() - s.reiniciarEm) >= 0) {
    Serial.flush();
    ESP.restart();
  }
}

}  // namespace Remoto

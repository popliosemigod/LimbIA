// =====================================================================
//  LimbIA - enlace.h
//  UDP entre as duas placas, dentro da rede da protese. Comum as duas.
//
//  Por que UDP e nao TCP: o comando se repete 20 vezes por segundo e cada
//  pacote carrega o ESTADO inteiro (modo, acao, numero da acao), nao um
//  evento. Pacote perdido e substituido pelo proximo 50 ms depois. TCP
//  retransmitiria o velho, atrasando o novo - o oposto do que se quer.
//
//  Validacao, CRC e formato moram em lib/limbia_mao/limbia_enlace.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <limbia_enlace.h>

namespace Enlace {

inline WiFiUDP& udp() {
  static WiFiUDP u;
  return u;
}

inline uint16_t& seq() {
  static uint16_t s = 0;
  return s;
}

inline bool& aberto() {
  static bool a = false;
  return a;
}

inline void begin(uint16_t porta) {
  aberto() = udp().begin(porta) != 0;
}

// Devolve o seq usado, para quem precisa esperar confirmacao.
inline uint16_t envia(const IPAddress& ip, uint16_t porta, uint8_t tipo, const void* carga,
                      uint16_t n) {
  if (!aberto()) return 0;
  uint8_t buf[limbia::ENLACE_MAX_PACOTE];
  const uint16_t s     = ++seq();
  const uint16_t total = limbia::montaPacote(tipo, s, carga, n, buf, sizeof(buf));
  if (total == 0) return 0;
  if (!udp().beginPacket(ip, porta)) return 0;
  udp().write(buf, total);
  udp().endPacket();
  return s;
}

// Um pacote valido, se houver. Nao bloqueia. `buf` precisa ter
// ENLACE_MAX_PACOTE bytes; `carga` aponta para dentro dele.
inline bool recebe(uint8_t* buf, limbia::CabecalhoPacote* cab, const uint8_t** carga,
                   IPAddress* origem) {
  if (!aberto()) return false;
  const int n = udp().parsePacket();
  if (n <= 0) return false;
  if (n > (int)limbia::ENLACE_MAX_PACOTE) {
    udp().flush();  // grande demais: nao e nosso
    return false;
  }
  const int lidos = udp().read(buf, n);
  if (origem) *origem = udp().remoteIP();
  return lidos == n && limbia::abrePacote(buf, (uint16_t)n, cab, carga);
}

}  // namespace Enlace

// =====================================================================
//  limbia_enlace - implementacao
// =====================================================================
#include "limbia_enlace.h"

#include <string.h>

namespace limbia {

static const char* const NOME_MODO[] = {"ajuste", "padrao"};
static const char* const NOME_ACAO[] = {"nenhuma", "abrir", "fechar", "parar"};

const char* nomeDoModo(uint8_t modo) {
  return modo <= MODO_PADRAO ? NOME_MODO[modo] : "?";
}

const char* nomeDaAcao(uint8_t acao) {
  return acao <= ACAO_PARAR ? NOME_ACAO[acao] : "?";
}

uint16_t crc16(const uint8_t* dados, uint16_t n, uint16_t crc) {
  for (uint16_t i = 0; i < n; i++) {
    crc ^= (uint16_t)dados[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

uint16_t tamanhoDaCarga(uint8_t tipo) {
  switch (tipo) {
    case PKT_COMANDO: return sizeof(PacoteComando);
    case PKT_TELEMETRIA: return sizeof(PacoteTelemetria);
    case PKT_POSES: return sizeof(PacotePoses);
    case PKT_REDE: return sizeof(PacoteRede);
    case PKT_CONFIRMA: return sizeof(PacoteConfirma);
    default: return 0;
  }
}

static uint16_t crcDoPacote(const CabecalhoPacote& cab, const uint8_t* carga) {
  CabecalhoPacote semCrc = cab;
  semCrc.crc             = 0;
  const uint16_t c       = crc16((const uint8_t*)&semCrc, sizeof(semCrc));
  return crc16(carga, cab.tamanho, c);
}

uint16_t montaPacote(uint8_t tipo, uint16_t seq, const void* carga, uint16_t n, uint8_t* saida,
                     uint16_t capacidade) {
  const uint16_t esperado = tamanhoDaCarga(tipo);
  if (esperado == 0 || n != esperado || !carga || !saida) return 0;
  const uint16_t total = (uint16_t)(sizeof(CabecalhoPacote) + n);
  if (total > capacidade) return 0;

  CabecalhoPacote cab;
  cab.magico  = ENLACE_MAGICO;
  cab.versao  = ENLACE_VERSAO;
  cab.tipo    = tipo;
  cab.seq     = seq;
  cab.tamanho = n;
  cab.crc     = crcDoPacote(cab, (const uint8_t*)carga);

  memcpy(saida, &cab, sizeof(cab));
  memcpy(saida + sizeof(cab), carga, n);
  return total;
}

bool abrePacote(const uint8_t* buf, uint16_t n, CabecalhoPacote* cab, const uint8_t** carga) {
  if (!buf || n < sizeof(CabecalhoPacote)) return false;
  CabecalhoPacote c;
  memcpy(&c, buf, sizeof(c));
  if (c.magico != ENLACE_MAGICO || c.versao != ENLACE_VERSAO) return false;
  const uint16_t esperado = tamanhoDaCarga(c.tipo);
  if (esperado == 0 || c.tamanho != esperado) return false;
  if (n != sizeof(CabecalhoPacote) + c.tamanho) return false;

  const uint8_t* dados = buf + sizeof(CabecalhoPacote);
  if (crcDoPacote(c, dados) != c.crc) return false;

  if (cab) *cab = c;
  if (carga) *carga = dados;
  return true;
}

static bool imprimivel(char c) {
  return c >= 0x20 && c <= 0x7E;
}

bool ssidValido(const char* s) {
  if (!s) return false;
  const size_t n = strlen(s);
  if (n < 1 || n > 32) return false;
  for (size_t i = 0; i < n; i++) {
    if (!imprimivel(s[i]) || s[i] == '"' || s[i] == '\\') return false;
  }
  // Espaco nas pontas some em muita tela de celular e vira "rede que nao
  // conecta" sem explicacao.
  if (s[0] == ' ' || s[n - 1] == ' ') return false;
  return true;
}

bool senhaValida(const char* s) {
  if (!s) return false;
  const size_t n = strlen(s);
  if (n < 8 || n > 63) return false;
  for (size_t i = 0; i < n; i++) {
    if (!imprimivel(s[i])) return false;
  }
  return true;
}

bool poseValida(const uint16_t alvo[N_JUNTAS]) {
  if (!alvo) return false;
  for (uint8_t i = 0; i < N_JUNTAS; i++) {
    if (alvo[i] > 1000) return false;
  }
  return true;
}

}  // namespace limbia

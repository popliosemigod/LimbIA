// =====================================================================
//  limbia_enlace - o que a placa do EMG e a placa da mao dizem uma a outra
//
//  Duas placas, uma protese
//  ------------------------
//  A placa do EMG fica no antebraco, perto dos eletrodos: le os dois
//  musculos, decide, serve a tela de ajuste e levanta a rede da protese.
//  A placa da mao fica com os servos: executa, sente corrente e responde.
//
//  O fio entre elas e o Wi-Fi da propria protese, e isso e escolha, nao
//  conveniencia: sem fio entre as placas, o terra ruidoso dos servos -
//  que puxam amperes em pico - nao tem caminho para chegar ao terra do
//  amplificador de EMG, que mede microvolts. Ver docs/05-emg-rede-e-ota.md.
//
//  O formato
//  ---------
//  Cabecalho fixo + carga de tamanho fixo por tipo + CRC-16. Pacote com
//  magico, versao, tipo ou tamanho errado, ou CRC que nao fecha, e
//  descartado em silencio. Os comandos se repetem 20 vezes por segundo,
//  entao um pacote perdido nao faz falta - e um pacote corrompido que
//  fosse aceito poderia fechar a mao sozinho.
//
//  C++11 puro. As structs sao empacotadas e de largura fixa: as duas
//  placas compilam com o mesmo toolchain, e os static_assert garantem que
//  ninguem mude o tamanho de uma sem perceber.
// =====================================================================
#ifndef LIMBIA_ENLACE_H
#define LIMBIA_ENLACE_H

#include <stdint.h>

#include "limbia_mao.h"

namespace limbia {

static const uint16_t ENLACE_MAGICO = 0x4C49;  // "LI"
static const uint8_t ENLACE_VERSAO  = 1;

enum TipoPacote : uint8_t {
  PKT_COMANDO = 1,  // EMG -> mao, 20 Hz: modo e acao
  PKT_TELEMETRIA,   // mao -> EMG, 10 Hz: onde cada junta esta
  PKT_POSES,        // EMG -> mao: as duas poses gravadas
  PKT_REDE,         // EMG -> mao: nome e senha novos da rede
  PKT_CONFIRMA,     // mao -> EMG: recebi e apliquei
  N_TIPOS_PACOTE
};

enum ModoProtese : uint8_t {
  MODO_AJUSTE = 0,  // calibracao, poses, rede, OTA
  MODO_PADRAO = 1   // a protese em uso: o EMG comanda a mao
};

enum AcaoMao : uint8_t {
  ACAO_NENHUMA = 0,
  ACAO_ABRIR,   // vai para a pose "mao aberta"
  ACAO_FECHAR,  // vai para a pose "mao fechada", parando no contato
  ACAO_PARAR    // cada junta para onde esta
};

const char* nomeDoModo(uint8_t modo);
const char* nomeDaAcao(uint8_t acao);

#pragma pack(push, 1)

struct CabecalhoPacote {
  uint16_t magico;
  uint8_t versao;
  uint8_t tipo;
  uint16_t seq;
  uint16_t tamanho;  // bytes de carga
  uint16_t crc;      // CRC-16/CCITT do cabecalho (com crc = 0) e da carga
};

// A acao so e executada quando seqAcao MUDA. O pacote se repete 20 vezes
// por segundo com a mesma acao, e a mao nao pode recomecar o movimento a
// cada repeticao.
struct PacoteComando {
  uint8_t modo;
  uint8_t acao;
  uint16_t seqAcao;
  uint8_t confianca;  // 0..100, so para registro
};

struct PacoteTelemetria {
  uint16_t posicao[N_JUNTAS];
  uint16_t correnteMa[N_JUNTAS];
  uint8_t contato;      // bit por junta
  uint8_t emMovimento;  // bit por junta
  uint8_t flags;        // TEL_*
  uint8_t objeto;       // Objeto, da ultima preensao
  uint8_t confianca;    // da classificacao do objeto
  uint8_t folgas;       // juntas com tendao frouxo
  uint16_t seqAcaoAtendida;
  uint16_t aberta[N_JUNTAS];
  uint16_t fechada[N_JUNTAS];
  char versao[12];
};

struct PacotePoses {
  uint16_t aberta[N_JUNTAS];
  uint16_t fechada[N_JUNTAS];
  uint8_t gravar;  // 1 = persistir na flash da mao; 0 = so experimentar
};

// Maximos do WPA2: SSID de 32 bytes, senha de 8 a 63 caracteres.
struct PacoteRede {
  char ssid[33];
  char senha[64];
};

struct PacoteConfirma {
  uint8_t tipo;  // tipo do pacote confirmado
  uint16_t seq;  // seq do pacote confirmado
  uint8_t ok;
};

#pragma pack(pop)

static_assert(sizeof(CabecalhoPacote) == 10, "cabecalho do enlace mudou de tamanho");
static_assert(sizeof(PacoteComando) == 5, "PacoteComando mudou de tamanho");
static_assert(sizeof(PacoteTelemetria) == 2 * N_JUNTAS * 4 + 8 + 12,
              "PacoteTelemetria mudou de tamanho");
static_assert(sizeof(PacotePoses) == 4 * N_JUNTAS + 1, "PacotePoses mudou de tamanho");
static_assert(sizeof(PacoteRede) == 97, "PacoteRede mudou de tamanho");
static_assert(sizeof(PacoteConfirma) == 4, "PacoteConfirma mudou de tamanho");

// Bits de PacoteTelemetria::flags
static const uint8_t TEL_SAIDAS     = 1 << 0;  // OE baixo: servos energizados
static const uint8_t TEL_CALIBRADA  = 1 << 1;  // calibracao de pulso veio da flash
static const uint8_t TEL_POSES      = 1 << 2;  // poses vieram da flash
static const uint8_t TEL_SOBRECARGA = 1 << 3;  // ultima parada foi por corrente
static const uint8_t TEL_LEITURA    = 1 << 4;  // objeto/confianca valem (houve preensao)

static const uint16_t ENLACE_MAX_PACOTE = sizeof(CabecalhoPacote) + sizeof(PacoteTelemetria);

// CRC-16/CCITT-FALSE: polinomio 0x1021, inicio 0xFFFF. "123456789" da 0x29B1.
uint16_t crc16(const uint8_t* dados, uint16_t n, uint16_t crc = 0xFFFF);

// Tamanho da carga de cada tipo; 0 para tipo desconhecido.
uint16_t tamanhoDaCarga(uint8_t tipo);

// Monta cabecalho + carga em `saida`. Devolve o tamanho total, ou 0 se
// o tipo for desconhecido, a carga tiver o tamanho errado ou nao couber.
uint16_t montaPacote(uint8_t tipo, uint16_t seq, const void* carga, uint16_t n, uint8_t* saida,
                     uint16_t capacidade);

// Valida e abre. Em caso de sucesso preenche `cab` e aponta `carga` para
// dentro de `buf`.
bool abrePacote(const uint8_t* buf, uint16_t n, CabecalhoPacote* cab, const uint8_t** carga);

// ---------------------------------------------------------------------
//  Validacao do que o cliente digita na tela
// ---------------------------------------------------------------------

// 1 a 32 caracteres imprimiveis, sem aspas nem barra invertida - que
// quebrariam o JSON da tela e o texto da serial.
bool ssidValido(const char* ssid);

// 8 a 63 caracteres ASCII imprimiveis: o que o WPA2 aceita como frase.
bool senhaValida(const char* senha);

bool poseValida(const uint16_t alvo[N_JUNTAS]);

}  // namespace limbia

#endif  // LIMBIA_ENLACE_H

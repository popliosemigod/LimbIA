// =====================================================================
//  limbia_sinal - filtro de EMG a prova de ruido, comando alternado
//  (contrai uma vez fecha, contrai de novo abre) e calibracao guiada por
//  exercicio. Sem Arduino dentro: o autoteste mede tudo isto na placa.
//
//  O filtro, estagio por estagio
//  -----------------------------
//  Cada estagio mata um tipo de ruido que ja apareceu nesta bancada (ver
//  diario 15-16/09 e 12/11). Nada aqui pede mudanca de hardware.
//
//   1. Hampel de 5 amostras - mata ESPIGAO: mau contato, faisca do motor,
//      erro de leitura do ADC. Troca a amostra pela mediana das 5 ultimas
//      so quando ela foge mais que k * MAD (desvio absoluto mediano). Um
//      sinal de verdade (musculo leva dezenas de ms para subir) nunca e
//      tocado; um pico de 1 ou 2 amostras some inteiro, em vez de virar
//      degrau no envelope como faria num filtro linear.
//   2. Passa-altas 20 Hz (so na entrada RAW) - tira DC e o artefato de
//      movimento do cabo, que mora abaixo de 20 Hz.
//   3. Notch 60 Hz e notch 120 Hz - a rede eletrica e a primeira
//      harmonica. Chegou a 93-99% da potencia nos ensaios de setembro.
//   4. Retificacao (so RAW) e passa-baixas Butterworth - o envelope.
//   5. Segundo passa-baixas em cascata - 4a ordem no total: 80 dB por
//      decada acima do corte, o que sobra de ondulacao vira fracao de
//      contagem.
//
//  Na saida SIG/ENV do MyoWare o modulo ja retificou; entram 1, 3, 4, 5.
//
//  A decisao, a prova de tremedeira
//  --------------------------------
//  Dois limiares (histerese), tempo minimo acima para valer, tempo
//  minimo abaixo para soltar, periodo refratario depois de cada troca e
//  rejeicao de artefato: um pico muito acima de qualquer contracao
//  calibrada (pancada no eletrodo) nao conta.
// =====================================================================
#pragma once
#include <stdint.h>

#include "limbia_emg.h"
#include "limbia_paralelo.h"

namespace limbia {

// ---------------------------------------------------------------------
//  1. Hampel de 5 amostras
// ---------------------------------------------------------------------
struct Hampel5 {
  float b[5];
  uint8_t i;
  uint8_t n;
  uint32_t trocadas;  // quantas amostras ja foram consideradas espigao
};

void hampelZera(Hampel5* h);
// k: quantos MAD contam como espigao. piso: desvio minimo, em contagens,
// para nunca mexer em sinal quase parado (MAD ~ 0).
float hampelPassa(Hampel5* h, float x, float k, float piso);

// ---------------------------------------------------------------------
//  O filtro inteiro
// ---------------------------------------------------------------------
struct ParametrosFiltro {
  float fsHz;
  bool entradaBruta;  // true: MyoWare RAW; false: SIG/ENV
  float hampelK;
  float hampelPiso;
  float passaAltasHz;  // so RAW
  float redeHz;        // 60 no Brasil; 0 desliga os dois notch
  float notchQ;
  float envelopeHz;
  float rapidoHz;  // envelope rapido, so para medir DURACAO (ver abaixo)
};

// Dois envelopes saem do mesmo sinal limpo:
//  - o LENTO (4a ordem, envelopeHz) diz o NIVEL: e liso, nao treme no
//    limiar;
//  - o RAPIDO (2a ordem, rapidoHz) diz a DURACAO. O lento espalha no
//    tempo: uma pancada de 40 ms vira envelope acima do limiar por mais
//    de 80 ms, e passaria por contracao. Medido no autoteste de 13/11.
struct FiltroEmg {
  Hampel5 hampel;
  Biquad pa, n1, n2, pb1, pb2, rap;
  bool bruto;
  bool comRede;
  bool iniciado;
  float ultimoLimpo;  // saida do Hampel, para o grafico
  float rapido;       // envelope rapido da ultima amostra
};

void filtroConfigura(FiltroEmg* f, const ParametrosFiltro& p);
// Poe a cadeia em regime para um nivel conhecido - a MEDIA de um trecho,
// nunca uma amostra so. Pre-aquecer com a primeira amostra fazia um
// espigao no instante do boot virar envelope de 2500 e fechar a mao
// sozinha (autoteste de 13/11). Sem chamada explicita, filtroPassa
// pre-aquece com a primeira amostra.
void filtroPreaquece(FiltroEmg* f, float nivel, const ParametrosFiltro& p);
// Devolve o envelope lento (>= 0), em contagens de ADC; o rapido fica em
// f->rapido.
float filtroPassa(FiltroEmg* f, float amostra, const ParametrosFiltro& p);

// ---------------------------------------------------------------------
//  Comando alternado: cada contracao troca o estado da mao
// ---------------------------------------------------------------------
struct ParametrosAlterna {
  ParametrosAtivacao ativ;
  uint32_t refratarioMs;  // depois de trocar, ignora novas contracoes
  float tetoArtefato;     // envelope acima disso = pancada, nao conta (0 desliga)
};

struct DetectorAlterna {
  Ativacao a;
  bool fechada;
  uint32_t ultimaTrocaMs;
  bool jaTrocou;           // houve alguma troca (o refratario so vale depois)
  float picoCandidato;     // maior envelope desde que o sinal passou do limiar
  uint32_t rejeitadas;     // contracoes ignoradas (refratario ou artefato)
  bool rapidoAcima;        // envelope rapido acima de `liga` agora
  uint32_t rapidoDesdeMs;  // desde quando
  uint8_t motivo;          // MotivoRejeicao da ultima rejeitada
};

enum EventoAlterna : uint8_t {
  ALT_NADA,
  ALT_FECHOU,
  ALT_ABRIU,
  ALT_REJEITOU
};
enum MotivoRejeicao : uint8_t {
  REJ_REFRATARIO,
  REJ_PANCADA,
  REJ_CURTA
};

void alternaZera(DetectorAlterna* d, bool fechada);
// `rapido` e o envelope rapido (FiltroEmg::rapido): a contracao so vale
// se ele tambem ficou acima de `liga` por ativ.ligaMs seguidos.
EventoAlterna alternaAtualiza(DetectorAlterna* d, float envelope, float rapido, uint32_t agoraMs,
                              const ParametrosAlterna& p);

// ---------------------------------------------------------------------
//  Calibracao guiada, ADAPTATIVA e com PROVA
//
//  E ela que libera a protese para se mover (decisao de 17/11): sem
//  calibracao aprovada, nenhum servo recebe pulso. Por isso ela nao
//  aceita "mais ou menos" - insiste ate ter certeza, e prova o que
//  aprendeu antes de liberar.
//
//  1. PREPARO e REPOUSO: braco solto. Mede media e desvio do repouso.
//  2. APRENDIZADO: pede "contraia" / "relaxe" quantas vezes precisar, ate
//     ter `boasMin` contracoes BOAS seguidas e CONSISTENTES:
//       - boa: pico acima de repouso + separacaoMin desvios e acima de
//         razaoMin x repouso;
//       - e o braco voltou ao repouso no fim do relaxe (senao a proxima
//         medida nao vale);
//       - consistentes: maior / menor pico das ultimas `boasMin` <=
//         consistenciaMax.
//     Desiste depois de `ciclosMax` tentativas.
//  3. Limiares saem da mediana das contracoes boas:
//       liga    = repouso + fracaoLiga    * (contracao - repouso)
//       desliga = repouso + fracaoDesliga * (contracao - repouso)
//     nunca a menos de 4 / 2 desvios do repouso.
//  4. PROVA: `provaCiclos` contracoes pedidas, julgadas pelo MESMO
//     detector que vai comandar a mao, com os limiares novos. Passa so se
//     detectou TODAS as contracoes pedidas e NENHUMA durante os relaxes.
//     Errou: volta ao aprendizado. `provasMax` provas erradas: falha.
//
//  Resposta confiante e errada e o pior tipo - aqui ela custaria a mao
//  fechando sozinha.
// ---------------------------------------------------------------------
#define CAL_MAX_BOAS 5

enum EtapaCal : uint8_t {
  CAL_PARADA,
  CAL_PREPARO,
  CAL_REPOUSO,
  CAL_CONTRAIA,
  CAL_RELAXE,
  CAL_PROVA_CONTRAIA,
  CAL_PROVA_RELAXE,
  CAL_OK,
  CAL_FALHOU
};

struct TemposCal {
  uint32_t preparoMs;
  uint32_t repousoMs;
  uint32_t contraiaMs;
  uint32_t relaxeMs;
  uint32_t descarteMs;  // inicio de cada janela, enquanto o musculo reage
  uint8_t boasMin;      // contracoes boas e consistentes para aprender
  uint8_t ciclosMax;    // tentativas de aprendizado antes de desistir
  uint8_t provaCiclos;  // contracoes na prova
  uint8_t provasMax;    // provas erradas antes de desistir
  float fracaoLiga;
  float fracaoDesliga;
  float separacaoMin;     // (contracao - repouso) / desvio do repouso
  float razaoMin;         // contracao / repouso
  float consistenciaMax;  // maior / menor pico entre as boas
  float tetoFator;        // teto de artefato = repouso + tetoFator * (contracao - repouso)
  uint32_t ativaMs;       // o detector da prova usa os mesmos tempos da mao
  uint32_t soltaMs;
};

struct ResultadoCal {
  float repouso;
  float desvio;
  float contracao;  // mediana das contracoes boas
  float separacao;
  float liga;
  float desliga;
  float teto;
  uint8_t boas;     // boas e consistentes no fim do aprendizado
  uint8_t acertos;  // contracoes detectadas na ultima prova
  uint8_t falsos;   // deteccoes durante relaxe na ultima prova
  uint8_t provas;   // provas feitas
};

// O que aconteceu com a ultima contracao pedida (para a tela).
enum AvaliacaoCal : uint8_t {
  AVAL_NADA,
  AVAL_BOA,
  AVAL_FRACA,        // nao se separou do repouso
  AVAL_NAO_RELAXOU,  // nao voltou ao repouso no relaxe
  AVAL_INCONSISTENTE,
  AVAL_DETECTOU,  // prova: detectou a contracao pedida
  AVAL_PERDEU,    // prova: nao detectou
  AVAL_FALSO      // prova: detectou sem contracao pedida
};

struct CalibracaoGuiada {
  EtapaCal etapa;
  uint8_t ciclo;       // tentativas de aprendizado ja feitas
  uint8_t provaCiclo;  // contracao da prova em andamento (0..provaCiclos-1)
  uint32_t etapaDesdeMs;
  EstatRepouso rep;
  float boas[CAL_MAX_BOAS];  // ultimas contracoes boas (mais antiga primeiro)
  uint8_t nBoas;
  float picoAtual;
  uint8_t avaliacao;  // AvaliacaoCal da ultima janela
  bool detectouNaJanela;
  DetectorAlterna det;  // o detector da prova
  ParametrosAlterna pDet;
  ResultadoCal r;
  const char* motivo;  // por que falhou (texto curto, sem acento)
};

void calInicia(CalibracaoGuiada* c, uint32_t agoraMs);
void calCancela(CalibracaoGuiada* c);
// `rapido` e o envelope rapido, que o detector da prova precisa.
// Devolve true quando a etapa muda (para avisar quem esta fazendo).
bool calAtualiza(CalibracaoGuiada* c, float envelope, float rapido, uint32_t agoraMs,
                 const TemposCal& t);
uint32_t calRestanteMs(const CalibracaoGuiada& c, uint32_t agoraMs, const TemposCal& t);
const char* calNomeEtapa(EtapaCal e);
const char* calNomeAvaliacao(uint8_t a);
bool calEmAndamento(const CalibracaoGuiada& c);

}  // namespace limbia

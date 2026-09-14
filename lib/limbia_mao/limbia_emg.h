// =====================================================================
//  limbia_emg - do sinal muscular ao comando da mao, sem Arduino dentro
//
//  O que mora aqui
//  ---------------
//  1. A cadeia de filtros: passa-altas, notch de 60 Hz, retificacao e
//     envelope. E o que transforma o sinal cru do eletrodo - que e ruido
//     de rede, artefato de movimento e musculo, tudo misturado - numa
//     grandeza que sobe quando o musculo contrai e so entao.
//  2. A calibracao por usuario: estatistica de cada gesto com
//     esquecimento, nota de cada eletrodo, e o placar que diz se a
//     protese esta entendendo quem a usa.
//  3. A "IA": um classificador LDA (analise discriminante linear) com
//     tres classes - repouso, fechar, abrir - treinado com o antebraco
//     de quem vai usar a protese.
//  4. O decisor: o que impede o sinal ruidoso de virar mao errática.
//
//  Por que LDA, e nao uma rede neural
//  ----------------------------------
//  Dois canais, tres classes e um minuto de calibracao. Com tao pouco
//  dado, uma rede neural decora o minuto de calibracao e erra no minuto
//  seguinte. O LDA tem seis medias e uma covariancia 2x2 para aprender -
//  cabe no que um minuto de dado consegue sustentar - e e o classificador
//  de referencia em controle mioeletrico justamente por isso. Ele tambem
//  devolve probabilidade, e probabilidade e o que o decisor precisa para
//  saber quando NAO agir.
//
//  Por que separado do firmware
//  ----------------------------
//  Pelo mesmo motivo de limbia_mao: nao ha eletrodo no estoque. O unico
//  jeito honesto de dizer que a calibracao funciona antes de o sensor
//  existir e rodar esta logica contra sinal sintetico e medir - e o que
//  o ambiente `autoteste` faz.
//
//  C++11 puro, sem alocacao dinamica, sem excecao. Float simples: o
//  ESP32 tem FPU de precisao simples.
// =====================================================================
#ifndef LIMBIA_EMG_H
#define LIMBIA_EMG_H

#include <stdint.h>

namespace limbia {

// ---------------------------------------------------------------------
//  Os dois eletrodos
//
//  Esquema classico de controle mioeletrico de dois sitios: um eletrodo
//  sobre os flexores do antebraco (face da palma), que contraem para
//  FECHAR a mao; outro sobre os extensores (face do dorso), que contraem
//  para ABRIR. Cada eletrodo tem um gesto que e dele.
// ---------------------------------------------------------------------
static const uint8_t N_CANAIS_EMG = 2;

enum CanalEmg : uint8_t {
  EMG_FLEXOR   = 0,  // eletrodo 1 - fecha a mao
  EMG_EXTENSOR = 1   // eletrodo 2 - abre a mao
};

enum ClasseEmg : uint8_t {
  C_REPOUSO = 0,
  C_FECHAR,
  C_ABRIR,
  N_CLASSES_EMG
};

// Voto que nao conta: o classificador nao teve certeza suficiente.
static const uint8_t C_INCERTO = 0xFF;

const char* nomeDaClasse(uint8_t classe);

// ---------------------------------------------------------------------
//  Filtros biquad (segunda ordem, forma direta II transposta)
//
//  Coeficientes pelo metodo classico de Robert Bristow-Johnson. Segunda
//  ordem basta: o que o sinal de EMG pede e tirar DC, tirar a rede e
//  suavizar - nada disso precisa de corte ingreme.
// ---------------------------------------------------------------------
struct Biquad {
  float b0, b1, b2, a1, a2;
  float z1, z2;
};

void biquadPassaAltas(Biquad* f, float fcHz, float fsHz);   // Butterworth
void biquadPassaBaixas(Biquad* f, float fcHz, float fsHz);  // Butterworth
void biquadNotch(Biquad* f, float f0Hz, float q, float fsHz);
void biquadZera(Biquad* f);
float biquadPassa(Biquad* f, float x);

// Poe o filtro em regime para uma entrada constante x. Sem isso, a
// primeira amostra de um sinal centrado em 2048 contagens vira um degrau
// de 2048 na entrada do passa-altas - e o transitorio parece, para o
// resto da cadeia, uma contracao violenta no primeiro segundo de vida.
void biquadPreaquece(Biquad* f, float x);

// Ganho (0..1+) do filtro numa frequencia, medido pela resposta em
// regime a uma senoide. Usado pelo autoteste para conferir o projeto.
float biquadGanhoMedido(const Biquad& modelo, float freqHz, float fsHz);

// ---------------------------------------------------------------------
//  A cadeia de um canal
//
//  Entrada BRUTA (MyoWare 2.0 na saida RAW, BioAmp EXG Pill, modulos com
//  AD8232): o sinal oscila em torno do meio da escala.
//      passa-altas 20 Hz -> notch 60 Hz -> |x| -> passa-baixas 4 Hz
//  - passa-altas: tira o DC e o artefato de movimento do cabo, que mora
//    abaixo de 20 Hz e nao tem nada de musculo;
//  - notch: a rede eletrica no Brasil e 60 Hz, e o corpo e uma antena;
//  - retificacao + passa-baixas: o envelope, que e a "forca" da
//    contracao. 4 Hz porque a intencao de abrir ou fechar a mao nao muda
//    mais rapido que isso - o que muda mais rapido e ruido.
//
//  Entrada de ENVELOPE (MyoWare na saida ENV, "Muscle Sensor V3"): o
//  modulo ja retificou e suavizou em hardware. Aqui so entra o
//  passa-baixas, para tirar o que sobrou de ondulacao.
// ---------------------------------------------------------------------
struct ParametrosCadeia {
  float fsHz;
  bool entradaBruta;
  float passaAltasHz;
  float notchHz;  // 0 desliga
  float notchQ;
  float envelopeHz;
};

struct CadeiaEmg {
  Biquad pa, notch, pb;
  bool bruto;
  bool comNotch;
  bool iniciada;
};

void cadeiaConfigura(CadeiaEmg* c, const ParametrosCadeia& p);

// Devolve o envelope (>= 0), em contagens de ADC.
float cadeiaProcessa(CadeiaEmg* c, float amostra);

// A caracteristica que o classificador enxerga: ln(1 + envelope).
//
// Log porque a amplitude do EMG e aproximadamente log-normal: em escala
// linear, o repouso vira um pontinho colado no zero e a contracao um
// borrao enorme, e nenhuma fronteira reta separa bem os dois. Em log, as
// classes ficam com espalhamento parecido - que e exatamente a hipotese
// do LDA.
float caracteristicaEmg(float envelope);

// ---------------------------------------------------------------------
//  Estatistica de uma classe, com esquecimento
//
//  Media e covariancia que acompanham o sinal: amostra velha perde peso
//  exponencialmente. E o que faz a barra da tela reagir quando o usuario
//  MOVE o eletrodo - a estatistica do lugar antigo sai de cena em alguns
//  ciclos, em vez de ficar puxando a media para sempre.
//
//  `memoria` e o numero efetivo de amostras lembradas. peso tende a ele.
// ---------------------------------------------------------------------
struct EstatClasse {
  float peso;
  float media[N_CANAIS_EMG];
  float cov[N_CANAIS_EMG][N_CANAIS_EMG];
};

void estatZera(EstatClasse* e);
void estatInclui(EstatClasse* e, const float x[N_CANAIS_EMG], float memoria);

// ---------------------------------------------------------------------
//  O classificador (LDA, tres classes, dois canais)
// ---------------------------------------------------------------------
struct ModeloEmg {
  float media[N_CLASSES_EMG][N_CANAIS_EMG];
  float covInv[N_CANAIS_EMG][N_CANAIS_EMG];  // inversa da covariancia agrupada
  float w[N_CLASSES_EMG][N_CANAIS_EMG];
  float w0[N_CLASSES_EMG];
  uint8_t valido;
};

// Treina a partir das estatisticas. Recusa (valido = 0) se alguma classe
// tiver menos de `pesoMinimo` amostras efetivas. `regularizacao` soma na
// diagonal da covariancia: sem ela, um repouso muito quieto deixa a
// matriz quase singular e o modelo fica confiante demais.
bool treinaModelo(const EstatClasse est[N_CLASSES_EMG], float pesoMinimo, float regularizacao,
                  ModeloEmg* m);

// Classe mais provavel e a probabilidade de cada uma (somam 1).
uint8_t classificaEmg(const ModeloEmg& m, const float x[N_CANAIS_EMG], float prob[N_CLASSES_EMG]);

// Distancia de Mahalanobis entre duas classes do modelo.
float distanciaEntreClasses(const ModeloEmg& m, uint8_t a, uint8_t b);

// Acerto que o proprio modelo preve para separar duas classes a essa
// distancia, se as duas fossem gaussianas: Phi(delta / 2). E a opiniao
// do modelo sobre si mesmo - o autoteste compara com o acerto medido.
float acertoPrevisto(float delta);

// ---------------------------------------------------------------------
//  Qualidade de cada eletrodo
//
//  A pergunta que a barra de cada eletrodo responde: "este eletrodo esta
//  em cima do musculo que ele deveria ouvir?". A medida e o d' (d-linha)
//  entre o gesto do eletrodo e o repouso:
//
//      d' = (media_gesto - media_repouso) / desvio_agrupado
//
//  d' de 3 quer dizer que as duas distribuicoes estao a tres desvios uma
//  da outra - praticamente nao se tocam. d' de 1 e um eletrodo em cima
//  de tendao ou de osso: o musculo ate aparece, mas afogado no ruido.
//
//  So o d' nao basta. Ele mede a separacao com a variabilidade DE AGORA,
//  e um repouso muito quieto faz qualquer contracao minuscula parecer
//  otima. A segunda grandeza e a RAZAO entre o envelope da contracao e o
//  do repouso: e ela que sobrevive ao suor, a pele que muda de impedancia
//  e ao eletrodo que escorrega um pouco ao longo do dia. Musculo que
//  aparece so 1,5 vez acima do repouso hoje some amanha.
//
//  E a terceira e a SELETIVIDADE: o eletrodo do flexor tem de responder
//  mais a "fechar" do que a "abrir". Parece redundante, e nao e: a
//  co-contracao sozinha ja poe o musculo do outro gesto varias vezes
//  acima de um repouso quieto. Sem esta verificacao, dois eletrodos
//  trocados de lugar passam como "posicionados corretamente" - medido no
//  autoteste, na primeira versao.
//
//  A barra enche exatamente ate o limiar: barra cheia = verde =
//  "posicionado corretamente". Nao ha "verde pela metade".
// ---------------------------------------------------------------------
struct LimiaresQualidade {
  float dPrimeOk;      // d' para "posicionado corretamente"
  float dPrimePerde;   // abaixo disso perde o ok (histerese)
  float razaoOk;       // contracao / repouso minima
  float pesoMinimo;    // amostras efetivas por classe antes de opinar
  float saturacaoMax;  // fracao de amostras no trilho, durante o gesto
  float repousoMax;    // caracteristica de repouso acima disso = mau contato
};

enum DiagnosticoEletrodo : uint8_t {
  D_AGUARDANDO = 0,   // ainda sem amostras suficientes
  D_OK,               // posicionado corretamente
  D_SINAL_FRACO,      // pouco musculo: mover o eletrodo
  D_SATURADO,         // sinal batendo no limite do ADC
  D_RUIDO_REPOUSO,    // repouso barulhento: contato ruim ou referencia solta
  D_MUSCULO_TROCADO,  // responde ao gesto do OUTRO eletrodo
  N_DIAGNOSTICOS
};

const char* textoDoDiagnostico(uint8_t diag);

struct QualidadeEletrodo {
  float dPrime;
  float razao;         // envelope do proprio gesto / envelope do repouso
  float seletividade;  // envelope do proprio gesto / envelope do gesto do outro
  uint8_t nota;        // 0..100, a barra da tela; 100 = verde
  uint8_t ok;
  uint8_t diagnostico;
};

// `q` guarda o estado anterior: o ok tem histerese, para a tela nao
// piscar entre verde e vermelho quando o sinal esta perto do limiar.
// `saturacao` e a fracao de amostras no trilho DURANTE o gesto deste
// eletrodo - medida no tempo todo, o repouso a diluiria.
void avaliaEletrodo(const EstatClasse est[N_CLASSES_EMG], uint8_t canal, uint8_t classeAtiva,
                    uint8_t classeOposta, float saturacao, const LimiaresQualidade& l,
                    QualidadeEletrodo* q);

// ---------------------------------------------------------------------
//  Placar: o acerto medido fora do treino
//
//  Cada amostra rotulada da calibracao e classificada ANTES de entrar no
//  treino. O acerto dessas previsoes e medido em dado que o modelo ainda
//  nao viu - o mesmo principio de separar treino e teste, feito amostra
//  a amostra. E esse numero que decide se a calibracao terminou, e nao a
//  opiniao do modelo sobre si mesmo.
// ---------------------------------------------------------------------
struct Placar {
  float acerto[N_CLASSES_EMG];  // fracao de acerto por classe, com esquecimento
  float peso[N_CLASSES_EMG];
};

void placarZera(Placar* p);
void placarInclui(Placar* p, uint8_t verdade, uint8_t previsto, float memoria);

// O pior acerto entre as tres classes, ponderado por quanto dado cada uma
// ja tem. A calibracao so esta pronta quando a protese entende as TRES -
// entender "fechar" muito bem e nunca entender "abrir" nao serve.
float placarPior(const Placar& p, float pesoMinimo);

// ---------------------------------------------------------------------
//  O protocolo guiado da tela de ajuste
//
//  "Feche a mao", "relaxe", "abra a mao", "relaxe" - e o relogio que
//  diz ao firmware qual gesto o usuario esta fazendo em cada instante.
//  O comeco de cada fase e descartado (`transicaoMs`): e o tempo de a
//  pessoa ler, reagir e o musculo chegar ao patamar.
// ---------------------------------------------------------------------
struct TemposProtocolo {
  uint16_t preparoMs;
  uint16_t contracaoMs;
  uint16_t repousoMs;
  uint16_t transicaoMs;
};

enum Instrucao : uint8_t {
  I_PREPARE = 0,  // mao relaxada, antes do primeiro ciclo
  I_FECHE,
  I_RELAXE,
  I_ABRA,
  N_INSTRUCOES
};

struct PassoProtocolo {
  uint8_t instrucao;
  uint8_t rotulo;  // classe que o usuario deveria estar fazendo
  bool rotulavel;  // fora da transicao: a amostra entra no treino
  uint16_t restanteMs;
  uint16_t duracaoMs;
  uint16_t ciclo;  // 0 no preparo, 1.. depois
};

PassoProtocolo passoDoProtocolo(uint32_t decorridoMs, const TemposProtocolo& t);
const char* nomeDaInstrucao(uint8_t instrucao);

// ---------------------------------------------------------------------
//  O decisor: o que separa sinal ruidoso de mao errática
//
//  O pedido do projeto e explicito: nada erratico, nada rapido demais,
//  nada responsivo ao extremo. Tres travas, em serie:
//
//  1. Probabilidade minima - voto do classificador abaixo de `probMin`
//     vira "incerto" e nao conta para nada.
//  2. Maioria numa janela - so troca o comando quando `votosMin` das
//     ultimas `janela` classificacoes concordam. Com 20 classificacoes
//     por segundo e 5 de 6, uma intencao precisa durar ~250 ms.
//  3. Periodo refratario - depois de uma troca, nenhuma outra por
//     `refratarioMs`. A mao termina o que comecou.
//
//  E REPOUSO nunca muda nada: relaxar o braco mantem a mao como esta.
//  E isso que deixa a pessoa segurar um copo sem ter de manter o musculo
//  contraido o tempo todo.
// ---------------------------------------------------------------------
struct ParametrosDecisor {
  uint8_t janela;  // no maximo 16
  uint8_t votosMin;
  float probMin;
  uint16_t refratarioMs;
};

struct Decisor {
  uint8_t hist[16];
  uint8_t n;
  uint8_t idx;
  uint8_t comando;  // C_ABRIR ou C_FECHAR
  bool jaTrocou;
  uint32_t ultimaTrocaMs;
  uint32_t trocas;
};

void decisorZera(Decisor* d, uint8_t comandoInicial);

// Esquece os votos, mantendo o comando e o refratario. Usado quando o
// sinal deixou de ser confiavel (eletrodo solto): os votos de antes da
// falha nao podem decidir nada depois que ele volta.
void decisorEsquece(Decisor* d);

// Devolve true quando o comando mudou nesta chamada.
bool decisorAtualiza(Decisor* d, uint8_t classe, float prob, uint32_t agoraMs,
                     const ParametrosDecisor& p);

}  // namespace limbia

#endif  // LIMBIA_EMG_H

// =====================================================================
//  limbia_mao - a logica da mao, sem Arduino dentro
//
//  Por que separado do firmware
//  ----------------------------
//  Tudo aqui e matematica e tabela: converter posicao em pulso, guardar
//  os gestos, decidir que objeto a mao esta segurando. Nada disso precisa
//  de placa, e justamente por isso pode ser exercitado sem uma - o
//  ambiente `autoteste` roda esta biblioteca contra centenas de casos
//  sinteticos e mede o acerto. Numa mao que ainda nao existe montada,
//  esse e o unico jeito honesto de dizer que a logica funciona.
//
//  C++11 puro, sem alocacao dinamica, sem excecao.
// =====================================================================
#ifndef LIMBIA_MAO_H
#define LIMBIA_MAO_H

#include <stdint.h>

namespace limbia {

// ---------------------------------------------------------------------
//  As juntas
//
//  Os nomes sao os do caderno de pesquisa do INOVAWEEK, mantidos de
//  proposito: e assim que a bancada chama cada dedo, e trocar por
//  "servo_2" so criaria a necessidade de traduzir de cabeca toda vez.
//  A anatomia formal vem junto em anatomiaDaJunta().
//
//  DEDAO_ABD e a setima junta, herdada do LAD Robotic Hand V3.0: o
//  polegar de verdade tem dois graus de liberdade, e sem o segundo nao
//  existe oposicao - so um dedo que dobra ao lado da palma.
// ---------------------------------------------------------------------
enum Junta : uint8_t {
  MINDY = 0,  // mindinho
  DONCARE,    // anelar
  FEIO,       // medio
  JULGADOR,   // indicador
  DEDAO,      // polegar - flexao
  DEDAO_ABD,  // polegar - abducao (do LAD)
  PULSO,      // punho
  N_JUNTAS
};

// Dedos que agarram: os quatro longos. O polegar entra na conta com
// peso proprio e o pulso nao entra nenhuma.
static const uint8_t N_DEDOS_LONGOS = 4;

const char* nomeDaJunta(uint8_t junta);
const char* anatomiaDaJunta(uint8_t junta);

// ---------------------------------------------------------------------
//  Calibracao: dois pulsos medidos, por junta
//
//  O metodo e o do manual do LAD: com um servo tester, achar o pulso que
//  leva a junta ao repouso e o que a leva ao fim de curso, e anotar os
//  dois. Nao se supoe qual e maior - no LAD, o polegar tem repouso em
//  2300 us e trabalho em 1026 us, invertido. Servo montado espelhado e
//  regra, nao excecao, e a conversao abaixo aceita as duas ordens.
//
//  Posicao circula pelo firmware em POR MIL (0..1000), nunca em graus:
//    0    = repouso   (dedo aberto / polegar junto a palma)
//    1000 = trabalho  (dedo fechado / polegar afastado)
//
//  Por mil em vez de grau porque grau e uma ficcao aqui: o que o servo
//  entrega e largura de pulso, e o angulo real do dedo depende da polia,
//  do tendao e de quanto ele ja esticou.
// ---------------------------------------------------------------------
struct Calibracao {
  uint16_t pulsoRepouso;   // us na posicao 0
  uint16_t pulsoTrabalho;  // us na posicao 1000
};

// Limites absolutos de seguranca. Servo de hobby costuma aceitar de 500
// a 2500 us; fora disso ele bate no batente interno e trava puxando
// corrente ate queimar. Nenhum pulso sai daqui fora dessa faixa.
static const uint16_t PULSO_MIN_US = 500;
static const uint16_t PULSO_MAX_US = 2500;

uint16_t pulsoDePerMil(const Calibracao& c, uint16_t perMil);
uint16_t perMilDePulso(const Calibracao& c, uint16_t us);

// Calibracao coerente? Endpoints distantes o bastante para o dedo ter
// curso util, e ambos dentro da faixa segura.
bool calibracaoValida(const Calibracao& c);

// Padrao de fabrica: os numeros do caderno do INOVAWEEK para os dedos
// (1000 us = 0 grau, 2000 us = 180 graus, pulso neutro em 1500) e os do
// manual do LAD para as duas juntas do polegar.
void calibracaoPadrao(Calibracao* destino);

// ---------------------------------------------------------------------
//  Gestos
// ---------------------------------------------------------------------
struct Pose {
  uint16_t alvo[N_JUNTAS];  // por mil, por junta
};

enum Gesto : uint8_t {
  G_ABRIR = 0,   // mao relaxada, todos os dedos estendidos
  G_FECHAR,      // punho, polegar cruzando a palma
  G_FECHAR_V2,   // punho com o polegar afastado da palma  (LAD '3')
  G_PAZ,         // indicador e medio estendidos           (INOVAWEEK)
  G_POSITIVO,    // joinha
  G_APONTAR,     // so o indicador estendido
  G_PINCA,       // polegar contra indicador
  G_FLEX_DEDAO,  // so o polegar flexiona                  (LAD '5')
  G_EXT_DEDAO,   // so o polegar estende                   (LAD '6')
  N_GESTOS
};

const Pose& poseDoGesto(uint8_t gesto);
const char* nomeDoGesto(uint8_t gesto);

// ---------------------------------------------------------------------
//  Preensao: que objeto a mao esta segurando
//
//  A ideia e do caderno do INOVAWEEK - deduzir o objeto lendo so posicao
//  e pulso. O que esta biblioteca acrescenta e a CORRENTE, e a diferenca
//  nao e detalhe:
//
//  Posicao sozinha responde "onde o servo esta", nao "onde o dedo esta".
//  Quando o tendao estica com o uso - e o proprio caderno registra que
//  estica - os dois deixam de ser a mesma coisa, e a assinatura de um
//  objeto medida hoje nao vale amanha. A corrente e medida do outro lado
//  do problema: ela sobe quando o dedo encontra resistencia, esteja o
//  tendao como estiver.
//
//  Entao o dedo nao para num angulo combinado: para quando ENCOSTA. E o
//  que sobra da leitura - em que posicao cada dedo encostou - e a forma
//  do objeto.
// ---------------------------------------------------------------------
struct AssinaturaPreensao {
  uint16_t contatoEm[N_JUNTAS];  // por mil onde a junta parou
  uint16_t forcaMa[N_JUNTAS];    // corrente no instante do contato, em mA
  bool tocou[N_JUNTAS];          // false = fechou o curso inteiro sem achar nada

  // Bit i ligado = a junta i tem sensor de corrente instalado.
  //
  // Este campo existe porque a bancada tem quatro ACS712 e sete juntas, e
  // sem ele o classificador leria "junta sem sensor" como "junta que nao
  // encostou em nada" - que e uma resposta CONFIANTE e ERRADA, o pior
  // tipo. Junta sem sensor nao vota; ela reduz a confianca do resultado.
  uint8_t comSensor;
};

// Ajuda a montar a mascara: DEDO_BIT(FEIO) | DEDO_BIT(JULGADOR) | ...
#define LIMBIA_BIT(junta) ((uint8_t)(1u << (junta)))

enum Objeto : uint8_t {
  OBJ_NENHUM = 0,  // a mao fechou no vazio
  OBJ_FINO,        // caneta, chave, talher: dedos quase fecharam
  OBJ_CILINDRICO,  // copo, macaneta: dedos pararam juntos, no meio
  OBJ_PLANO,       // cartao, celular: dedos pararam em posicoes diferentes
  OBJ_GRANDE,      // dedos mal sairam do lugar
  OBJ_INDEFINIDO,  // leitura inconsistente
  N_OBJETOS
};

const char* nomeDoObjeto(uint8_t objeto);

// Classifica e devolve confianca em 0..100. Confianca baixa nao e erro:
// e a resposta honesta quando a assinatura fica em cima da fronteira.
uint8_t classificaPreensao(const AssinaturaPreensao& a, uint8_t* confianca);

// ---------------------------------------------------------------------
//  Diagnostico de tendao frouxo
//
//  O caderno do INOVAWEEK ja tinha diagnosticado o problema em palavras:
//  "as linhas se deformam a toda hora" e "nao tem como confiar 100% num
//  sistema que precisa ser vigiado constantemente".
//
//  Com corrente, o firmware para de depender de alguem vigiando. Tendao
//  frouxo tem assinatura inconfundivel: a junta percorre o curso inteiro
//  e a corrente nunca sobe, porque o servo esta enrolando folga em vez
//  de puxar dedo. Isso e detectavel, e o que e detectavel vira aviso.
// ---------------------------------------------------------------------
bool tendaoFrouxo(const AssinaturaPreensao& a, uint8_t junta, uint16_t limiarMa);

// Quantas juntas estao com folga. Zero e o estado saudavel.
uint8_t juntasComFolga(const AssinaturaPreensao& a, uint16_t limiarMa);

}  // namespace limbia

#endif  // LIMBIA_MAO_H

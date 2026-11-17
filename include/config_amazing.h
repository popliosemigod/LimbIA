// =====================================================================
//  LimbIA - config_amazing.h
//  Pinagem e parametros da mao Amazing Hand: ESP32-C3 SuperMini + PCA9685
//  + MG90S + MyoWare. So numeros - a logica mora em limbia_paralelo e em
//  src/main_amazing.cpp.
//
//  Ligacao (12/11/2026)
//  --------------------
//    C3 SuperMini   PCA9685    MyoWare
//    GPIO6  ------- SDA
//    GPIO7  ------- SCL
//    GPIO3  -------------------- SIG (ou RAW)   ADC1_CH3
//    3V3    ------- VCC  ------- +Vs
//    GND    ------- GND  ------- GND
//
//  V+ do PCA9685 (borne dos servos) vem de fonte EXTERNA de 5 V, com GND
//  comum ao C3. Oito MG90S travados puxam mais de 5 A; o regulador de
//  3V3 do C3 nao alimenta nem um.
// =====================================================================
#pragma once
#include <stdint.h>

// --- I2C / PCA9685 ---------------------------------------------------
#define AMZ_PINO_SDA         6
#define AMZ_PINO_SCL         7
#define AMZ_PCA_ENDERECO     0x40
#define AMZ_I2C_HZ           400000
// O cristal interno do PCA9685 nao e 25 MHz exato; 27 MHz e o valor que a
// Adafruit recomenda e que deixa o periodo perto de 20 ms. Chute ate
// medir com osciloscopio.
#define AMZ_PCA_OSCILADOR_HZ 27000000UL
#define AMZ_PWM_HZ           50.0f

// --- MG90S -----------------------------------------------------------
// Pulso nos extremos do curso de 180 graus. O datasheet da Tower Pro so
// garante 1,0-2,0 ms; 500-2400 us e o curso tipico dos MG90S de mercado.
// Chute: medir com o servo tester e entrar com commit `calib`.
#define AMZ_US_EM_0   500.0f
#define AMZ_US_EM_180 2400.0f

// --- Dedos -----------------------------------------------------------
// Mesma ordem e mesmo pareamento do Amazing Hand: IDs 1/2 indicador,
// 3/4 medio, 5/6 anelar, 7/8 polegar. Aqui, canais 0/1, 2/3, 4/5, 6/7.
#define AMZ_N_DEDOS 4

struct DedoConfig {
  const char* nome;
  uint8_t canalA;
  uint8_t canalB;
  float centroA;  // graus absolutos; 90 = meio do curso (MiddlePos)
  float centroB;
  int8_t sentido;
};

// Centro 90 em todos ate a calibracao: e a posicao em que o Amazing Hand
// manda encaixar o horn. Ajustavel pelo console (`centro`) sem regravar.
static const DedoConfig AMZ_DEDOS[AMZ_N_DEDOS] = {
    {"indicador", 0, 1, 90.0f, 90.0f, +1},
    {"medio", 2, 3, 90.0f, 90.0f, +1},
    {"anelar", 4, 5, 90.0f, 90.0f, +1},
    {"polegar", 6, 7, 90.0f, 90.0f, +1},
};

// --- Limites de movimento (graus do Amazing Hand, relativos ao centro)
// Fase 1 dos ensaios: so flexao, de 0 a 90 no maximo. A extensao do
// Amazing Hand (OpenHand = -35) e a abducao ficam travadas em zero ate o
// curso ser conhecido. Para afrouxar: console `lim <graus>` (nunca passa
// de AMZ_FLEXAO_TETO).
#define AMZ_FLEXAO_MIN     0.0f
#define AMZ_FLEXAO_INICIAL 90.0f  // limite ao ligar; `lim` encurta
#define AMZ_FLEXAO_TETO    90.0f
#define AMZ_ABDUCAO_MAX    0.0f
#define AMZ_SERVO_MIN      0.0f
#define AMZ_SERVO_MAX      180.0f

// Velocidade de cada servo, em graus de curso por segundo (vale para a
// mao inteira e para o servo comandado sozinho). O demo
// oficial fecha mais devagar do que abre (CloseSpeed < MaxSpeed).
#define AMZ_VEL_FECHA_GRAUS_S  90.0f
#define AMZ_VEL_ABRE_GRAUS_S   180.0f
// Movimento MANUAL (pagina e console): vale sempre, travado ou nao, e por
// isso devagar - quem move esta olhando a mao (decisao de 17/11).
#define AMZ_VEL_MANUAL_GRAUS_S 30.0f

// AUTO-HOME = MAO ABERTA (decidido em 17/11): flexao 0, os dois servos de
// cada dedo no centro. E a pose de montagem do Amazing Hand e a segura:
// nao belisca, nao aperta objeto, e cada servo fica com curso para os dois
// lados. Roda sozinho quando a calibracao libera a mao, e pelo botao.
#define AMZ_TICK_SERVO_MS 20  // um passo por periodo de PWM

// --- EMG (MyoWare) ---------------------------------------------------
#define AMZ_PINO_EMG                 3  // ADC1_CH3 no C3
#define AMZ_EMG_FS_HZ                1000
// A saida e detectada no inicio: RAW oscila em torno do meio da escala,
// SIG/ENV fica perto de zero em repouso. Acima deste nivel medio, RAW.
#define AMZ_EMG_LIMIAR_RAW_CONTAGENS 1000.0f
#define AMZ_EMG_DETECCAO_MS          300

// Filtro (limbia_sinal.h explica cada estagio).
#define AMZ_HAMPEL_K           4.0f   // desvios (MAD) para contar como espigao
#define AMZ_HAMPEL_PISO        40.0f  // contagens; abaixo disso nunca mexe
#define AMZ_EMG_PASSA_ALTAS_HZ 20.0f  // so RAW
#define AMZ_EMG_REDE_HZ        60.0f  // notch em 60 e 120 Hz
#define AMZ_EMG_NOTCH_Q        8.0f
#define AMZ_EMG_ENVELOPE_HZ    5.0f   // dois estagios: 4a ordem (nivel)
#define AMZ_EMG_RAPIDO_HZ      25.0f  // envelope rapido: so mede duracao

// Comando alternado: contrai uma vez fecha, contrai de novo abre.
#define AMZ_ATIVA_MS      80   // tempo continuo acima do limiar para valer
#define AMZ_SOLTA_MS      200  // tempo continuo abaixo para soltar
#define AMZ_REFRATARIO_MS 800  // depois de trocar, ignora contracoes

// Calibracao OBRIGATORIA a cada vez que liga (decisao de 17/11): sem ela
// nenhum servo recebe pulso. Adaptativa: pede contracoes ate ter 3 boas
// e consistentes SEGUIDAS (no maximo 10 tentativas) e depois PROVA o
// limiar com mais 3 - todas detectadas, nenhuma deteccao no relaxe.
// Errou a prova: aprende de novo (no maximo 3 provas). Caminho mais
// curto: 3 + 4 + 3 x 5 + 3 x 5 = ~37 s.
#define AMZ_CAL_PREPARO_MS   3000
#define AMZ_CAL_REPOUSO_MS   4000
#define AMZ_CAL_CONTRAIA_MS  2000
#define AMZ_CAL_RELAXE_MS    3000
#define AMZ_CAL_DESCARTE_MS  500
#define AMZ_CAL_BOAS         3   // seguidas e consistentes
#define AMZ_CAL_CICLOS_MAX   10  // tentativas de aprendizado
#define AMZ_CAL_PROVA        3   // contracoes na prova
#define AMZ_CAL_PROVAS_MAX   3
#define AMZ_CAL_FRACAO_LIGA  0.35f
#define AMZ_CAL_FRACAO_SOLTA 0.15f
#define AMZ_CAL_SEPARACAO    6.0f  // (contracao - repouso) / desvio, minimo
#define AMZ_CAL_RAZAO        1.5f  // contracao / repouso, minimo
#define AMZ_CAL_CONSISTENCIA 2.5f  // maior / menor pico entre as boas
#define AMZ_CAL_TETO_FATOR   4.0f  // pancada = 4x a faixa da contracao

// Entrada RAW saturada (no trilho) nao e musculo: e eletrodo solto ou
// contato ruim. Acima desta fracao de amostras no trilho, a mao congela.
#define AMZ_TRILHO_ALTO         4090
#define AMZ_TRILHO_BAIXO        5
#define AMZ_SATURACAO_MAX       0.05f
#define AMZ_JANELA_SATURACAO_MS 100

// --- Registro para o grafico ------------------------------------------
// O EMG roda a 1 kHz; grafico e serial recebem 1 ponto a cada 10
// amostras (100 Hz). 1024 pontos = ~10 s de historico na pagina.
#define AMZ_DECIMACAO  10
#define AMZ_HISTORICO  1024
#define AMZ_PLOTTER_MS 20  // Serial Plotter: 50 linhas por segundo

// --- Wi-Fi da propria mao ----------------------------------------------
// Rede ABERTA, sem senha e sem internet, de proposito (pedido de
// 12/11): qualquer celular conecta e a pagina abre sozinha (portal
// cativo). O preco: qualquer um no alcance tambem pode abrir e fechar a
// mao pela pagina.
#define AMZ_WIFI_SSID       "LimbIA-mao"
#define AMZ_WIFI_CANAL      6
// O C3 SuperMini tem antena mal casada: na potencia maxima o radio se
// perde. 8,5 dBm e o valor que a comunidade usa para estabilizar.
#define AMZ_WIFI_POT_DBM_X4 34  // em quartos de dBm (34 = 8,5 dBm)

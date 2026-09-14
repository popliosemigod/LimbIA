// =====================================================================
//  LimbIA - config_rede.h
//  A rede da protese, comum as duas placas. So parametro; logica nenhuma.
//
//  Quem e quem
//  -----------
//  A placa do EMG levanta a rede (ponto de acesso) e serve a tela de
//  ajuste. A placa da mao entra nela como cliente, com IP fixo. O PC ou o
//  celular do cliente entra na mesma rede, e a tela abre sozinha (portal
//  cativo). Uma rede, uma senha, e tudo o que trafega fica dentro do WPA2.
//
//      192.168.4.1    placa do EMG   (ponto de acesso, tela, OTA)
//      192.168.4.200  placa da mao   (cliente, OTA)
//      192.168.4.2..  PC e celular   (DHCP da placa do EMG)
//
//  A mao fica em .200, e nao em .2, porque o DHCP do ESP32 distribui a
//  partir de .2 e iria entregar o mesmo endereco ao primeiro PC que
//  entrasse na rede.
// =====================================================================
#pragma once

#define REDE_IP_EMG     192, 168, 4, 1
#define REDE_IP_MAO     192, 168, 4, 200
#define REDE_MASCARA    255, 255, 255, 0
#define REDE_CANAL      6  // fixo: a mao nao precisa varrer canais para achar a rede
#define REDE_MAX_CLIENT 4  // mao + PC + celular + folga

// Nome de fabrica da rede: o nome do no, que vem do platformio.ini. Dois
// kits na mesma sala precisam ser gravados com LIMBIA_NOME diferentes -
// senao a mao de um pode entrar na rede do outro antes de o cliente
// escolher o nome definitivo.
#ifndef LIMBIA_NOME
#define LIMBIA_NOME "limbia-01"
#endif
#define REDE_SSID_FABRICA LIMBIA_NOME

// Nomes para OTA e mDNS
#define REDE_HOST_EMG "limbia-emg"
#define REDE_HOST_MAO "limbia-mao"

// ---------------------------------------------------------------------
//  Enlace entre as placas (UDP dentro da rede da protese)
// ---------------------------------------------------------------------
#define ENLACE_PORTA_MAO 4210  // a mao escuta aqui
#define ENLACE_PORTA_EMG 4211  // a placa do EMG escuta aqui

#define ENLACE_COMANDO_MS    50   // EMG -> mao, 20 Hz: o comando se repete
#define ENLACE_TELEMETRIA_MS 100  // mao -> EMG, 10 Hz

// Silencio maior que isso e enlace perdido. Com a protese em uso, a mao
// PARA cada junta onde esta - servo parado e o modo de falha seguro. Nao
// abre: abrir derrubaria o que ela estiver segurando.
#define ENLACE_TIMEOUT_MS 1000

// Troca de senha: quanto tempo a placa do EMG insiste ate a mao confirmar.
#define ENLACE_TROCA_REDE_MS      3000
#define ENLACE_TROCA_REDE_REPETIR 250

// Cliente que perdeu a rede tenta de novo a cada tanto.
#define REDE_RECONEXAO_MS 8000

// ---------------------------------------------------------------------
//  Persistencia
//
//  Namespace proprio, separado da calibracao: o comando 'f' da mao
//  (voltar ao padrao de fabrica) apaga pulso e poses, e NAO pode levar a
//  senha da rede junto - senao a mao sai da rede e ninguem mais a acha.
// ---------------------------------------------------------------------
#define NVS_REDE "limbia_rede"

// Segurar o BOOT por este tempo apaga nome e senha da rede e volta ao de
// fabrica. E a saida para "o cliente esqueceu a senha". Longo de
// proposito: ninguem segura um botao por dez segundos sem querer.
#define REDE_RESET_BOTAO_MS 10000

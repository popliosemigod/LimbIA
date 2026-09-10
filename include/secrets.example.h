// =====================================================================
//  secrets.example.h - MODELO. Copie para secrets.h e preencha.
//
//      Copy-Item include\secrets.example.h include\secrets.h
//
//  include/secrets.h esta no .gitignore e NUNCA vai para o repositorio.
//
//  O firmware da v0.1 nao usa rede: o console e a serial, e a mao nao
//  depende de Wi-Fi para funcionar. Este arquivo ja existe porque o
//  painel web esta previsto - e porque a regra do laboratorio e que
//  credencial nasce fora do git, nao que ela seja movida para fora
//  depois que alguem percebe.
// =====================================================================
#pragma once

// Lembrete de hardware: o radio do ESP32 classico e 2,4 GHz APENAS.
// Apontar para um SSID de 5 GHz devolve "rede nao encontrada", nao erro
// de senha - e o diagnostico se perde procurando no lugar errado.
#define LIMBIA_WIFI_SSID "REDE_2G_AQUI"
#define LIMBIA_WIFI_PASS "SENHA_AQUI"

// Senha do AP que a mao levanta quando nao ha credencial.
#define LIMBIA_AP_PASS "limbia123"

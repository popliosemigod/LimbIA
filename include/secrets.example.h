// =====================================================================
//  secrets.example.h - MODELO. Copie para secrets.h e preencha.
//
//      Copy-Item include\secrets.example.h include\secrets.h
//
//  include/secrets.h esta no .gitignore e NUNCA vai para o repositorio.
//
//  Sem secrets.h o firmware compila mesmo assim, com ESTES valores - e
//  avisa no boot. E o que deixa o CI compilar sem credencial nenhuma. Mas
//  gravar numa protese de verdade um firmware com a senha de OTA publica
//  deste arquivo e deixar qualquer um na rede dela regravar a mao.
// =====================================================================
#pragma once

// Senha de FABRICA da rede da protese. O cliente troca na tela de ajuste
// na primeira vez; esta so vale ate la, e de novo depois de segurar o
// BOOT por 10 s. O WPA2 exige de 8 a 63 caracteres.
#define LIMBIA_AP_PASS "limbia123"

// Senha do OTA das duas placas. E do laboratorio, nao do cliente: e ela
// que impede alguem na rede da protese de gravar firmware na mao. O
// `pio run -e mao_ota -t upload` le a mesma constante deste arquivo (ou
// do secrets.h), entao ha uma fonte so.
#define LIMBIA_OTA_PASS "SENHA_OTA_AQUI"

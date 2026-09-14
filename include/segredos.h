// =====================================================================
//  LimbIA - segredos.h
//  Escolhe de onde vem a credencial: secrets.h (local, fora do git) ou,
//  na falta dele, o modelo secrets.example.h - com aviso no boot.
// =====================================================================
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#define LIMBIA_SEGREDOS_DE_EXEMPLO 0
#else
#include "secrets.example.h"
#define LIMBIA_SEGREDOS_DE_EXEMPLO 1
#endif

// =====================================================================
//  LimbIA - atuador.h
//  Escolhe qual mao esta sendo compilada. Quem inclui este arquivo ganha
//  o namespace `Dedos` e nao precisa saber o que ha embaixo dele.
//
//  LIMBIA_MAO_LAD = 1  -> motores.h  (motores DC em L293D + servos no
//                         polegar; a mao que esta na bancada)
//  LIMBIA_MAO_LAD = 0  -> dedos.h    (sete servos num PCA9685)
//
//  As duas expoem a mesma interface: begin, tick, vaiPara, vaiParaPose,
//  para, emMovimento, escreve, escrevePulso, ligaSaidas, pararNoContato e
//  relaxa. O que muda de verdade entre elas e o que o hardware sabe
//  responder - e isso o firmware ja trata pela mascara SENSORES_INSTALADOS
//  e pela posicao, que numa e medida e na outra e estimada.
// =====================================================================
#pragma once
#include "config.h"

#if LIMBIA_MAO_LAD
#include "motores.h"
#else
#include "dedos.h"
#endif

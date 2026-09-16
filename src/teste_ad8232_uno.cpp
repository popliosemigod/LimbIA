// =====================================================================
//  LimbIA - teste_ad8232_uno.cpp
//  Teste BASE do AD8232 num Arduino, fora da cadeia do LimbIA.
//
//  E o sketch de referencia do tutorial da how2electronics / SparkFun,
//  mantido COMO ESTA de proposito: sem filtro, sem envelope, sem tarefa
//  de 1 kHz. A pergunta aqui e a mais simples possivel - o modulo entrega
//  sinal que varia quando o musculo trabalha?
//
//  Por isso ele viola a regra 1 do projeto (nada bloqueia o loop): o
//  delay(1) e do sketch original. Quando o teste responder, a logica
//  volta para o firmware do ESP32, que ja tem a cadeia certa.
//
//  LIGACAO (Arduino UNO)
//    3.3V   -> +  do modulo   ** NUNCA 5 V: maximo absoluto e 3,6 V **
//    GND    -> -
//    A0     -> OUTPUT
//    D11    -> LO+
//    D10    -> LO-
//
//  Eletrodos: vermelho e amarelo sobre o ventre dos flexores, alinhados
//  com as fibras; verde no osso do cotovelo.
//
//  Saida: 9600 baud. Um "!" por amostra significa eletrodo solto; numero
//  e a leitura crua do ADC de 10 bits (0 a 1023, centro perto de 512).
// =====================================================================
#include <Arduino.h>

void setup() {
  // initialize the serial communication:
  Serial.begin(9600);
  pinMode(10, INPUT);  // Setup for leads off detection LO +
  pinMode(11, INPUT);  // Setup for leads off detection LO -
}

void loop() {
  if ((digitalRead(10) == 1) || (digitalRead(11) == 1)) {
    Serial.println('!');
  } else {
    // send the value of analog input 0:
    Serial.println(analogRead(A0));
  }
  // Wait for a bit to keep serial data from saturating
  delay(1);
}

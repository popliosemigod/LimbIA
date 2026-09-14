# test/

Testes unity (`pio test`). Ainda vazio: quem exercita a logica hoje e o
ambiente `autoteste`, que roda na placa e mede acerto contra casos sinteticos.
Ver `docs/04-propriocepcao.md`.

`host/` compila o MESMO autoteste para o PC, com o zig (clang empacotado no
pip), para iterar rapido na logica sem gravar placa. Instrucoes em
`host/roda.sh`. Numero medido no PC nao vai para o diario como medida de
placa: o que vale e o autoteste rodando no ESP32.

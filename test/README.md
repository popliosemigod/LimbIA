# test/

Testes unity (`pio test`). Ainda vazio: quem exercita a logica hoje e o
ambiente `autoteste`, que roda na placa e mede acerto contra casos sinteticos.
Ver `docs/04-propriocepcao.md`.

`host/` compila o MESMO autoteste para o PC, com o zig (clang empacotado no
pip), para iterar rapido na logica sem gravar placa. Instrucoes em
`host/roda.sh`. Numero medido no PC nao vai para o diario como medida de
placa: o que vale e o autoteste rodando no ESP32.

`host/testa_tela.py` roda o JavaScript da tela de ajuste contra um DOM falso e
contra o JSON que `include/painel.h` monta - com os nomes de campo extraidos do
proprio firmware, para um JSON escrito a mao nao esconder diferenca. Pega erro
de sintaxe, campo que o firmware nao manda e id que o HTML nao tem. Nao
substitui abrir a pagina num navegador: nao desenha nada e nao testa toque.

```powershell
pip install quickjs
python test/host/testa_tela.py
```

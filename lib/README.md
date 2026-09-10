# lib/

Bibliotecas privadas deste firmware.

`limbia_mao/` guarda a logica que nao depende de placa: matematica de pulso,
tabela de gestos e classificacao de preensao. E ela que o ambiente `autoteste`
exercita sem nenhum hardware ligado.

O que servir para mais de um projeto do laboratorio nao mora aqui: mora em
`jaspa-core`, no repositorio Jaspy.

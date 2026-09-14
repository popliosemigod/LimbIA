"""Roda o JavaScript da tela de ajuste contra um DOM falso.

Nao substitui abrir a pagina num navegador: nao desenha nada, nao testa
CSS nem toque. O que ele pega e o erro que o navegador esconderia - campo
do JSON que o firmware nao manda, id de elemento que nao existe no HTML,
indice de array fora da tabela.

Precisa do interpretador QuickJS:  pip install quickjs
Rodar da raiz do repositorio:      python test/host/testa_tela.py
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

import quickjs

RAIZ = Path(__file__).resolve().parents[2]

html = (RAIZ / "include/painel_html.h").read_text(encoding="utf-8")
corpo = html[html.index('R"HTML(') + 7 : html.rindex(')HTML"')]
js = corpo[corpo.index("<script>") + 8 : corpo.rindex("</script>")]

# Todos os id= do HTML: o DOM falso so conhece estes, como o navegador.
ids = set(re.findall(r'id="([^"]+)"', corpo))
# Criados por montaJuntas() com innerHTML, nao estao no HTML estatico.
ids |= {"v%d" % i for i in range(7)}

# Os nomes de campo que include/painel.h realmente emite. Se o firmware
# renomear um campo, o JSON de teste abaixo deixa de bater e o teste
# acusa - sem isto, um JSON escrito a mao esconderia a diferenca.
painel = (RAIZ / "include/painel.h").read_text(encoding="utf-8")
campos_firmware = set(re.findall(r'\\"([a-z]{1,8})\\":', painel))
# "ok" e "msg" sao das respostas de acao (responde()), nao do /estado.
campos_firmware -= {"ok", "msg"}

# O JSON que include/painel.h monta, com os mesmos nomes de campo. Se o
# firmware mudar um nome e o JS nao acompanhar, e aqui que quebra.
ESTADO = """{
 "m":0,"v":"0.2.0","r":{"s":"limbia-01","f":1,"t":0},
 "c":{"on":1,"i":1,"rs":1200,"d":3000,"ci":2,
      "e":[[100,1,1,3.4,5.2,4.1],[58,0,2,1.9,1.7,1.2]],"g":[72,0,65]},
 "pu":0,"ms":0,
 "s":{"n":[0.42,0.08],"sat":[0.000,0.000],"solto":0},
 "k":{"v":1,"c":1,"p":[0.10,0.85,0.05]},
 "a":{"a":1,"q":7},"gp":0,
 "h":{"on":1,"pos":[0,0,0,0,0,300,500],"a":[0,0,0,0,0,300,500],
      "f":[1000,1000,1000,1000,1000,0,500],
      "ctt":3,"mov":0,"fl":5,"o":2,"oc":86,"fo":1}}"""

DOM = """
var ERROS = [];
var IDS = %s;
function Elem(nome){
  this.nome=nome; this.style={}; this.dataset={}; this.textContent="";
  this.innerHTML=""; this.hidden=false; this.className=""; this.value="0";
  this.open=false; this.onclick=null; this.oninput=null; this.children=[];
}
Elem.prototype.querySelector=function(sel){ return new Elem(this.nome+" "+sel); };
Elem.prototype.querySelectorAll=function(sel){ return []; };
var document = {
  querySelector: function(sel){
    if (sel[0] === "#" && IDS.indexOf(sel.substring(1)) < 0) {
      ERROS.push("elemento inexistente no HTML: " + sel);
    }
    return new Elem(sel);
  },
  querySelectorAll: function(sel){
    var n = [];
    for (var i=0;i<7;i++){ var e = new Elem(sel); e.dataset={j:String(i)}; n.push(e); }
    return n;
  }
};
function fetch(){ return { then: function(){ return this; } }; }
function setTimeout(){ }
""" % (list(ids),)

# O script termina chamando atualiza(), que faz fetch de verdade; aqui o
# estado e injetado a mao para desenha() rodar de forma sincrona.
js_sem_arranque = js.replace("montaJuntas();atualiza();", "montaJuntas();")

teste = """
E = %s;
var erroDesenha = null;
try { desenha(); } catch (e) { erroDesenha = "" + e + " | " + (e.stack || ""); }
// Modo padrao exercita o outro caminho da tela.
E.m = 1;
try { desenha(); } catch (e) { erroDesenha = (erroDesenha||"") + " [padrao] " + e; }
// Sem calibracao em curso, e sem mao no enlace.
E.m = 0; E.c.on = 0; E.h.on = 0;
try { desenha(); } catch (e) { erroDesenha = (erroDesenha||"") + " [sem mao] " + e; }
JSON.stringify({erro: erroDesenha, dom: ERROS});
""" % (ESTADO,)

ctx = quickjs.Context()
try:
    ctx.eval(DOM)
    ctx.eval(js_sem_arranque)
except Exception as e:  # noqa: BLE001
    print("ERRO DE SINTAXE no JavaScript da tela:")
    print(e)
    sys.exit(1)

import json as _json
campos_teste = set()
def _colhe(o):
    if isinstance(o, dict):
        for k, v in o.items():
            campos_teste.add(k)
            _colhe(v)
so_firmware = campos_firmware - campos_teste
so_teste = campos_teste - campos_firmware
_colhe(_json.loads(ESTADO))
so_firmware = campos_firmware - campos_teste
so_teste = campos_teste - campos_firmware
if so_firmware or so_teste:
    print("CAMPOS FORA DE SINCRONIA entre painel.h e o JSON deste teste:")
    if so_firmware:
        print("  so no firmware:", sorted(so_firmware))
    if so_teste:
        print("  so no teste:   ", sorted(so_teste))
else:
    print("campos: os %d nomes do JSON do firmware batem com os do teste" % len(campos_firmware))

print("sintaxe: ok (%d caracteres de JS, %d ids no HTML)" % (len(js), len(ids)))

import json

r = json.loads(ctx.eval(teste))
if r["erro"]:
    print("ERRO ao desenhar:", r["erro"])
if r["dom"]:
    print("IDS QUE O JS PROCURA E O HTML NAO TEM:")
    for e in dict.fromkeys(r["dom"]):
        print("  -", e)
if not r["erro"] and not r["dom"]:
    print("desenho: ok nos tres estados (ajuste, padrao, sem mao)")

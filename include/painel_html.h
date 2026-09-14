// =====================================================================
//  LimbIA - painel_html.h
//  A tela de ajuste e uso, servida pela placa do EMG.
//
//  Uma pagina so, sem nada externo: a rede da protese nao tem internet,
//  entao fonte, icone e biblioteca de fora simplesmente nao carregariam.
//  O JavaScript busca /estado quatro vezes por segundo e redesenha; os
//  formularios (nome e senha da rede) nunca sao tocados pela atualizacao,
//  para o que o cliente esta digitando nao sumir no meio.
//
//  O texto da tela tem acento: e o navegador que decide a codificacao,
//  pelo charset=utf-8 do cabecalho. A regra "sem acento" do repositorio e
//  para comentario de codigo embarcado e para a serial.
// =====================================================================
#pragma once

static const char PAINEL_HTML[] = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LimbIA</title>
<style>
:root{--fundo:#f4f5f7;--cartao:#fff;--texto:#16181d;--suave:#5b6270;--linha:#dde0e6;
--verde:#1f9d55;--ambar:#c77700;--vermelho:#d23f3f;--azul:#2563eb;--roxo:#7c3aed;--trilho:#e7e9ee}
@media (prefers-color-scheme:dark){:root{--fundo:#101216;--cartao:#1a1d23;--texto:#eceef2;
--suave:#9aa1ad;--linha:#2b3039;--trilho:#2a2e36}}
*{box-sizing:border-box}body{margin:0;background:var(--fundo);color:var(--texto);
font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;padding:0 16px 120px}
header{display:flex;align-items:center;gap:10px;flex-wrap:wrap;max-width:760px;margin:0 auto;padding:16px 0}
.marca{font-weight:800;font-size:22px;letter-spacing:-.5px}.marca b{color:var(--azul)}
.rede{color:var(--suave);font-size:14px;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.selo{font-size:13px;font-weight:700;padding:4px 10px;border-radius:99px;color:#fff;background:var(--ambar)}
.selo.padrao{background:var(--verde)}
.ponto{width:10px;height:10px;border-radius:50%;background:var(--vermelho);display:inline-block;margin-right:6px}
.ponto.on{background:var(--verde)}
main{max-width:760px;margin:0 auto}
.cartao{background:var(--cartao);border:1px solid var(--linha);border-radius:14px;padding:18px;margin-bottom:14px}
h2{font-size:17px;margin:0 0 10px}h2 small{color:var(--suave);font-weight:500}
.dica{color:var(--suave);font-size:14px;margin:4px 0 10px}
.aviso{border-left:4px solid var(--ambar);padding:8px 12px;background:color-mix(in srgb,var(--ambar) 10%,transparent);
border-radius:6px;font-size:14px;margin:8px 0}
.aviso.ruim{border-color:var(--vermelho);background:color-mix(in srgb,var(--vermelho) 10%,transparent)}
.aviso.bom{border-color:var(--verde);background:color-mix(in srgb,var(--verde) 10%,transparent)}
label{display:block;font-size:14px;color:var(--suave);margin:10px 0 4px}
input[type=text],input[type=password]{width:100%;font:inherit;padding:10px 12px;border:1px solid var(--linha);
border-radius:10px;background:var(--fundo);color:var(--texto)}
button{font:inherit;font-weight:650;border:0;border-radius:10px;padding:11px 16px;cursor:pointer;
background:var(--azul);color:#fff}
button.sec{background:var(--trilho);color:var(--texto)}button:disabled{opacity:.45;cursor:not-allowed}
.botoes{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
.instr{border-radius:12px;padding:18px;text-align:center;background:var(--trilho);margin-bottom:14px}
.instr .t{font-size:30px;font-weight:800;letter-spacing:-.5px}.instr .s{color:var(--suave);font-size:14px}
.instr.feche{background:color-mix(in srgb,var(--roxo) 18%,var(--cartao))}
.instr.abra{background:color-mix(in srgb,var(--azul) 18%,var(--cartao))}
.relogio{height:6px;border-radius:3px;background:color-mix(in srgb,var(--texto) 12%,transparent);margin-top:12px;overflow:hidden}
.relogio i{display:block;height:100%;width:0;background:var(--texto);opacity:.55}
.el{margin:14px 0}.el .rot{display:flex;justify-content:space-between;gap:8px;font-size:15px;font-weight:600}
.el .est{font-weight:600;font-size:14px;color:var(--suave);text-align:right}.el .est.ok{color:var(--verde)}
.barra{height:16px;border-radius:8px;background:var(--trilho);overflow:hidden;margin-top:6px}
.barra i{display:block;height:100%;width:0;border-radius:8px;transition:width .3s,background .3s}
.nivel{height:5px;border-radius:3px;background:var(--trilho);margin-top:5px;overflow:hidden}
.nivel i{display:block;height:100%;width:0;background:var(--suave);transition:width .1s}
.diag{font-size:13px;color:var(--suave);margin:4px 0 0}
details{font-size:14px;margin:6px 0 12px}summary{cursor:pointer;color:var(--azul);font-weight:600}
details li{margin:4px 0}
.abas{display:flex;gap:6px;margin-bottom:10px}.abas button{flex:1}
.junta{display:grid;grid-template-columns:130px 1fr 44px;align-items:center;gap:8px;font-size:14px;margin:6px 0}
.junta span:last-child{text-align:right;color:var(--suave)}input[type=range]{width:100%}
.gesto{font-size:40px;font-weight:850;text-align:center;letter-spacing:-1px;margin:6px 0}
.centro{text-align:center;color:var(--suave)}
.dedos{display:flex;gap:6px;justify-content:center;flex-wrap:wrap;margin-top:10px}
.dedos span{font-size:12px;padding:3px 8px;border-radius:99px;background:var(--trilho)}
.dedos span.ctt{background:var(--verde);color:#fff}
footer{position:fixed;left:0;right:0;bottom:0;background:var(--cartao);border-top:1px solid var(--linha);
padding:12px 16px calc(12px + env(safe-area-inset-bottom))}
footer div{max-width:760px;margin:0 auto}
#btnModo{width:100%;font-size:18px;padding:15px;background:var(--verde)}
#btnModo.ajuste{background:var(--ambar)}
#modoDica{font-size:13px;color:var(--suave);text-align:center;margin:6px 0 0;min-height:1em}
#off{position:fixed;inset:0;background:rgba(0,0,0,.6);color:#fff;display:flex;align-items:center;
justify-content:center;text-align:center;padding:24px;font-size:18px;z-index:9}
</style></head><body>
<header><div class="marca">Limb<b>IA</b></div>
<div class="rede"><span class="ponto" id="pMao"></span><span id="txtRede">…</span></div>
<div class="selo" id="selo">…</div></header>
<main>
<div id="ajuste">

<section class="cartao" id="cRede">
<h2>1. Rede da prótese</h2>
<div id="redeAviso"></div>
<details id="redeForm"><summary id="redeResumo">Alterar nome e senha</summary>
<p class="dica">Geralmente o nome é o modelo da prótese. A senha é sua: guarde-a. As duas placas da
prótese aprendem juntas e a guardam para sempre.</p>
<label for="fSsid">Nome da rede</label><input type="text" id="fSsid" maxlength="32" autocomplete="off">
<label for="fSenha">Senha (8 a 63 caracteres)</label><input type="password" id="fSenha" maxlength="63">
<label for="fSenha2">Repita a senha</label><input type="password" id="fSenha2" maxlength="63">
<div class="botoes"><button id="btnRede">Salvar nome e senha</button></div>
</details>
<p class="dica" id="redeMsg"></p>
</section>

<section class="cartao">
<h2>2. Posicione os eletrodos</h2>
<details><summary>Onde colocar cada eletrodo</summary><ul>
<li><b>Eletrodo 1 (fecha a mão):</b> na parte de dentro do antebraço, do lado da palma, a um terço do
caminho entre o cotovelo e o punho. Feche a mão com força: é o músculo que endurece ali.</li>
<li><b>Eletrodo 2 (abre a mão):</b> na parte de fora do antebraço, do lado das costas da mão, na mesma
altura. Estique os dedos: é o músculo que endurece ali.</li>
<li><b>Referência:</b> sobre osso, longe dos dois — o cotovelo é o lugar clássico.</li>
<li>Os eletrodos de cada sensor alinhados no sentido do músculo, do cotovelo para o punho. Pele
limpa e seca.</li>
<li><b>Segurança:</b> com os eletrodos na pele, a placa do EMG fica na bateria — nunca ligada ao PC
por cabo USB.</li></ul></details>
<div class="instr" id="instr"><div class="t" id="instrT">Pronto para começar</div>
<div class="s" id="instrS">Toque em Começar e siga as instruções. A barra de cada eletrodo vai de
vermelho a verde; se não subir, mova o eletrodo um pouco e continue.</div>
<div class="relogio"><i id="relogio"></i></div></div>
<div class="el" id="e0"><div class="rot"><span>Eletrodo 1 · fecha a mão</span><span class="est"></span></div>
<div class="barra"><i></i></div><div class="nivel"><i></i></div><p class="diag"></p></div>
<div class="el" id="e1"><div class="rot"><span>Eletrodo 2 · abre a mão</span><span class="est"></span></div>
<div class="barra"><i></i></div><div class="nivel"><i></i></div><p class="diag"></p></div>
<div class="el" id="eG"><div class="rot"><span>Calibração · a prótese entende você</span><span class="est"></span></div>
<div class="barra"><i></i></div><p class="diag"></p></div>
<div class="botoes"><button id="btnCal">Começar</button><button class="sec" id="btnCancela" hidden>Cancelar</button></div>
<p class="dica" id="entende"></p>
</section>

<section class="cartao" id="cPoses">
<h2>3. Movimentos da mão <small>(opcional)</small></h2>
<p class="dica" id="posesDica">A prótese faz dois movimentos: mão aberta e mão fechada. Ajuste o quanto
cada dedo vai, teste e grave. Ao fechar, a mão também para sozinha quando encosta num objeto.</p>
<div id="posesCorpo">
<div class="abas"><button id="abaA">Mão aberta</button><button class="sec" id="abaF">Mão fechada</button></div>
<div id="juntas"></div>
<div class="botoes"><button class="sec" id="btnTestar">Testar este movimento</button>
<button id="btnGravar">Gravar os dois movimentos</button></div>
<p class="dica" id="posesMsg"></p></div>
</section>
</div>

<div id="padrao" hidden>
<section class="cartao"><div class="centro">A prótese está obedecendo aos seus músculos</div>
<div class="gesto" id="gesto">—</div><div class="centro" id="segura"></div>
<div class="dedos" id="dedos"></div><div id="avisosUso"></div></section>
<section class="cartao"><h2>Sinal dos músculos</h2>
<div class="el" id="u0"><div class="rot"><span>Eletrodo 1 · fecha</span><span class="est"></span></div><div class="barra"><i></i></div></div>
<div class="el" id="u1"><div class="rot"><span>Eletrodo 2 · abre</span><span class="est"></span></div><div class="barra"><i></i></div></div>
<p class="dica" id="entendeUso"></p></section>
</div>
</main>
<footer><div><button id="btnModo">…</button><p id="modoDica"></p></div></footer>
<div id="off" hidden>Sem conexão com a prótese.<br>Confira se este aparelho está na rede dela.</div>
<script>
const $=s=>document.querySelector(s);
const INSTR=[['Relaxe a mão','Braço apoiado, mão solta. Estou medindo o seu repouso.',''],
['FECHE A MÃO','Com força, como se apertasse um objeto.','feche'],
['Relaxe','Solte a mão.',''],
['ABRA A MÃO','Estique bem os dedos, abrindo a mão.','abra']];
const DIAG=['medindo…','posicionado corretamente',
'sinal fraco: mova o eletrodo para o centro do músculo',
'sinal forte demais (saturando): confira o ganho do sensor e a alimentação em 3,3 V',
'repouso com ruído: confira se o eletrodo está bem colado e a referência no lugar',
'responde mais ao outro gesto: troque os eletrodos 1 e 2 de lugar'];
const CLASSE=['repouso','fechar','abrir'];
const OBJ=['nada na mão','objeto fino','objeto cilíndrico','objeto plano','objeto grande','indefinido'];
const JUNTAS=['Mindinho','Anelar','Médio','Indicador','Polegar','Polegar (afastar)','Punho'];
let E=null,falhas=0,aba='a',rasc=null,sujo=false;

async function post(u,d){try{const r=await fetch(u,{method:'POST',body:new URLSearchParams(d)});
return await r.json()}catch(e){return{ok:0,msg:'Sem conexão com a prótese.'}}}
function cor(n,ok){if(ok)return'var(--verde)';return'hsl('+Math.round(Math.min(n,99)*.5)+' 78% 50%)'}
function barra(el,n,ok){const i=el.querySelector('.barra i');i.style.width=Math.max(n,3)+'%';i.style.background=cor(n,ok)}
function nivel(el,v){el.querySelector('.nivel i').style.width=Math.min(100,v*80)+'%'}
function txt(s,t){const e=$(s);if(e.textContent!==t)e.textContent=t}

function montaJuntas(){let h='';JUNTAS.forEach((n,i)=>{h+='<div class="junta"><span>'+n+'</span>'+
'<input type="range" min="0" max="1000" step="10" data-j="'+i+'"><span id="v'+i+'"></span></div>'});
$('#juntas').innerHTML=h;document.querySelectorAll('#juntas input').forEach(r=>r.oninput=()=>{
rasc[aba][+r.dataset.j]=+r.value;sujo=true;mostraJuntas()})}
function mostraJuntas(){if(!rasc)return;document.querySelectorAll('#juntas input').forEach(r=>{
const j=+r.dataset.j;r.value=rasc[aba][j];$('#v'+j).textContent=Math.round(rasc[aba][j]/10)+'%'});
$('#abaA').className=aba=='a'?'':'sec';$('#abaF').className=aba=='f'?'':'sec'}

function desenha(){
const h=E.h,c=E.c;
$('#pMao').className='ponto'+(h.on?' on':'');
txt('#txtRede',E.r.s+(h.on?' · mão conectada':' · mão sem sinal'));
const pad=E.m==1;$('#selo').textContent=pad?'MODO PADRÃO':'MODO AJUSTE';$('#selo').className='selo'+(pad?' padrao':'');
$('#ajuste').hidden=pad;$('#padrao').hidden=!pad;
const bm=$('#btnModo');
if(pad){bm.textContent='Voltar ao modo ajuste';bm.className='ajuste';bm.disabled=false;txt('#modoDica','')}
else{bm.textContent='Usar a prótese';bm.className='';bm.disabled=!E.pu;
txt('#modoDica',E.pu?(c.on?'Calibração pronta: toque para gravar e usar.':'Usa a calibração gravada.'):
(c.on?'Continue até as três barras ficarem verdes.':'Calibre os eletrodos primeiro.'))}

// rede
const ra=$('#redeAviso');
ra.innerHTML=E.r.f?'<div class="aviso ruim">A prótese ainda está com o nome e a senha de fábrica. Escolha os seus.</div>':'';
if(E.r.f&&!$('#redeForm').dataset.aberto){$('#redeForm').open=true;$('#redeForm').dataset.aberto=1}
txt('#redeResumo',E.r.f?'Escolher nome e senha':'Rede: '+E.r.s+' — alterar');
const tm=['',"Enviando para a mão…",'Pronto! A prótese vai reiniciar. Conecte-se de novo à rede com o nome e a senha novos.',
'A mão não confirmou. Nada mudou: ligue a mão e tente de novo.'][E.r.t]||'';if(tm)txt('#redeMsg',tm);

// calibracao
const ins=$('#instr');
if(c.on){const k=INSTR[c.i]||INSTR[0];txt('#instrT',k[0]);txt('#instrS',k[1]+(c.ci?'  ·  ciclo '+c.ci:''));
ins.className='instr '+k[2];$('#relogio').style.width=(100*(c.d-c.rs)/c.d)+'%'}
else{ins.className='instr';txt('#instrT',E.ms?'Calibração gravada':'Pronto para começar');
txt('#instrS',E.ms?'Toque em Começar para calibrar de novo (a atual só é trocada quando a nova ficar verde).':
'Toque em Começar e siga as instruções. Se a barra não subir, mova o eletrodo um pouco e continue.');
$('#relogio').style.width='0'}
$('#btnCal').textContent=c.on?'Recomeçar':'Começar';$('#btnCancela').hidden=!c.on;
[0,1].forEach(i=>{const el=$('#e'+i),q=c.e[i];barra(el,c.on?q[0]:0,c.on&&q[1]);nivel(el,E.s.n[i]);
const est=el.querySelector('.est');est.textContent=c.on?(q[1]?'✓ posicionado corretamente':q[0]+'%'):'';
est.className='est'+(c.on&&q[1]?' ok':'');
el.querySelector('.diag').textContent=c.on&&!q[1]&&q[2]>1?DIAG[q[2]]:(c.on&&q[1]?'músculo '+q[4]+'× acima do repouso':'')});
const g=$('#eG');barra(g,c.on?c.g[0]:0,c.on&&c.g[1]);
g.querySelector('.est').textContent=c.on?(c.g[1]?'✓ pronta':''):'';g.querySelector('.est').className='est'+(c.on&&c.g[1]?' ok':'');
g.querySelector('.diag').textContent=c.on?'acertou '+c.g[2]+'% dos seus gestos recentes (precisa de 90%)':'';
txt('#entende',E.k.v?'Agora a prótese entende: '+CLASSE[E.k.c]+' ('+Math.round(E.k.p[E.k.c]*100)+'%)':'');

// poses
$('#posesCorpo').hidden=!h.on;
if(!h.on)txt('#posesDica','Ligue a mão para ajustar os movimentos.');
else txt('#posesDica','A prótese faz dois movimentos: mão aberta e mão fechada. Ajuste o quanto cada dedo vai, teste e grave. Ao fechar, a mão também para sozinha quando encosta num objeto.');
if(h.on&&!sujo){rasc={a:h.a.slice(),f:h.f.slice()};mostraJuntas()}
const pm=['','Enviando…','Gravado na mão.','A mão não confirmou.'][E.gp]||'';
txt('#posesMsg',pm||(h.on&&!(h.fl&4)?'Movimentos atuais ainda não gravados.':''));

// uso
if(pad){txt('#gesto',E.a.a==2?'MÃO FECHADA':'MÃO ABERTA');
txt('#segura',(h.fl&16)?'Segurando: '+OBJ[h.o]+' ('+h.oc+'% de certeza)':'');
let d='';for(let i=0;i<4;i++)d+='<span class="'+((h.ctt>>i)&1?'ctt':'')+'">'+JUNTAS[i]+'</span>';$('#dedos').innerHTML=h.on?d:'';
let av='';if(!h.on)av+='<div class="aviso ruim">A mão está sem sinal. Ela fica parada onde está até voltar.</div>';
if(E.s.solto)av+='<div class="aviso ruim">Um eletrodo parece solto. Nenhuma decisão até o sinal voltar.</div>';
if(h.fl&8)av+='<div class="aviso">A mão parou por esforço alto num dedo.</div>';
if(h.fo)av+='<div class="aviso">'+h.fo+' dedo(s) com o tendão frouxo: peça o ajuste da prótese.</div>';
$('#avisosUso').innerHTML=av;
[0,1].forEach(i=>{const el=$('#u'+i);const v=Math.min(100,E.s.n[i]*80);el.querySelector('.barra i').style.width=v+'%';
el.querySelector('.barra i').style.background='var(--azul)'});
txt('#entendeUso','Entendendo: '+CLASSE[E.k.c]+' ('+Math.round(E.k.p[E.k.c]*100)+'%)')}
}

async function atualiza(){try{const r=await fetch('/estado',{cache:'no-store'});E=await r.json();falhas=0;
$('#off').hidden=true;desenha()}catch(e){if(++falhas>4)$('#off').hidden=false}finally{setTimeout(atualiza,250)}}

$('#btnModo').onclick=async()=>{const r=await post('/modo',{para:E.m==1?'ajuste':'padrao'});
if(!r.ok)txt('#modoDica',r.msg)};
$('#btnCal').onclick=()=>post('/calibracao',{acao:'iniciar'});
$('#btnCancela').onclick=()=>post('/calibracao',{acao:'cancelar'});
$('#btnRede').onclick=async()=>{const s=$('#fSsid').value.trim(),p=$('#fSenha').value,p2=$('#fSenha2').value;
if(!s)return txt('#redeMsg','Escolha um nome para a rede.');
if(p.length<8)return txt('#redeMsg','A senha precisa ter pelo menos 8 caracteres.');
if(p!==p2)return txt('#redeMsg','As duas senhas não são iguais.');
const r=await post('/rede',{ssid:s,senha:p});txt('#redeMsg',r.ok?'Enviando para a mão…':r.msg)};
$('#abaA').onclick=()=>{aba='a';mostraJuntas()};$('#abaF').onclick=()=>{aba='f';mostraJuntas()};
async function mandaPoses(g){const r=await post('/poses',{a:rasc.a.join(','),f:rasc.f.join(','),g:g?1:0});
if(!r.ok){txt('#posesMsg',r.msg);return false}
E.gp=1;// ate o proximo /estado, o valor local e o da operacao anterior
for(let i=0;i<20;i++){await new Promise(z=>setTimeout(z,200));if(E.gp==2){if(g)sujo=false;return true}if(E.gp==3)return false}return false}
$('#btnTestar').onclick=async()=>{if(await mandaPoses(false))post('/testar',{pose:aba=='a'?'aberta':'fechada'})};
$('#btnGravar').onclick=()=>mandaPoses(true);
montaJuntas();atualiza();
</script></body></html>)HTML";

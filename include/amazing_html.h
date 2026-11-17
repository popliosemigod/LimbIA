// =====================================================================
//  LimbIA - amazing_html.h
//  A pagina servida pela propria mao (http://192.168.4.1). Autocontida:
//  nenhuma biblioteca externa, porque o celular conectado na rede da mao
//  nao tem internet. Grafico em <canvas> desenhado a mao. Texto com
//  acento so como entidade HTML: o arquivo fica ASCII puro.
// =====================================================================
#pragma once
#include <Arduino.h>

static const char AMZ_PAGINA[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LimbIA - m&atilde;o</title>
<style>
:root{--fundo:#f4f5f7;--cartao:#fff;--texto:#1b1f24;--fraco:#667085;--linha:#d9dde3;
--env:#1f6feb;--liga:#d92d20;--solta:#f79009;--verde:#12b76a;--trava:#b42318;--travafundo:#fef3f2}
@media (prefers-color-scheme:dark){:root{--fundo:#0f1217;--cartao:#171b22;--texto:#e6e8eb;
--fraco:#98a2b3;--linha:#2a313c;--env:#58a6ff;--liga:#f97066;--solta:#fdb022;--verde:#32d583;
--trava:#f97066;--travafundo:#2a1514}}
*{box-sizing:border-box}body{margin:0;background:var(--fundo);color:var(--texto);
font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:860px;margin:0 auto;padding:10px 16px 40px}
header{display:flex;align-items:center;justify-content:space-between;margin:4px 0 8px}
h1{font-size:19px;margin:0}h2{font-size:15px;margin:0 0 8px;color:var(--fraco);font-weight:600}
.cartao{background:var(--cartao);border:1px solid var(--linha);border-radius:12px;padding:14px;margin:10px 0}
.chip{border:1px solid var(--linha);border-radius:99px;padding:2px 10px;font-size:13px;color:var(--fraco)}
.chip.ruim{color:var(--trava);border-color:var(--trava)}
#trava{display:flex;align-items:center;gap:12px;flex-wrap:wrap;font-weight:700;font-size:20px}
#trava.travada{background:var(--travafundo);border-color:var(--trava);color:var(--trava)}
#trava.liberada{border-color:var(--verde);color:var(--verde)}
.sinal{display:flex;gap:16px;align-items:center;flex-wrap:wrap}
#lamp{width:120px;height:120px;border-radius:50%;display:flex;align-items:center;justify-content:center;
text-align:center;font-weight:800;font-size:15px;background:var(--linha);color:var(--fraco);
transition:background .08s;flex:none}
#lamp.on{background:var(--verde);color:#fff;box-shadow:0 0 0 8px rgba(18,183,106,.25)}
#nc{font-size:44px;font-weight:800;line-height:1}
canvas{width:100%;height:220px;display:block;margin-top:10px}
.leg{font-size:13px;color:var(--fraco)}.leg i{display:inline-block;width:16px;height:3px;vertical-align:middle;margin:0 4px 0 10px}
button{font:inherit;font-weight:600;padding:10px 16px;border-radius:10px;border:1px solid var(--linha);
background:var(--cartao);color:var(--texto);cursor:pointer}
button.prim{background:var(--env);border-color:var(--env);color:#fff}
button:disabled{opacity:.45;cursor:not-allowed}
.botoes{display:flex;gap:8px;flex-wrap:wrap}
#instr{font-size:28px;font-weight:800;text-align:center;margin:6px 0 2px;min-height:38px}
#instr.contraia{color:var(--verde)}#instr.relaxe{color:var(--env)}
#aval{text-align:center;min-height:24px;font-weight:600}
.prog{display:flex;gap:18px;justify-content:center;flex-wrap:wrap;margin:6px 0 10px;color:var(--fraco)}
.prog b{color:var(--texto);letter-spacing:3px}
.fraco{color:var(--fraco);font-size:13px}.ruim{color:var(--trava)}
.servos{display:grid;grid-template-columns:1fr;gap:10px}
@media (min-width:640px){.servos{grid-template-columns:1fr 1fr}}
.sv label{display:flex;justify-content:space-between;font-size:14px}
.sv input{width:100%}
</style></head><body><main>
<header><h1>LimbIA &middot; m&atilde;o</h1><span id="con" class="chip">conectando...</span></header>

<div id="trava" class="cartao travada"><span id="travatxt">...</span>
  <button class="prim" id="btrava" onclick="cmd('calibrar')">Calibrar agora</button></div>

<div class="cartao">
  <h2>Sinal do m&uacute;sculo</h2>
  <div class="sinal">
    <div id="lamp">repouso</div>
    <div><div id="nc">0</div><div class="fraco">contra&ccedil;&otilde;es reconhecidas</div>
      <div class="fraco" id="ent" style="margin-top:6px"></div></div>
  </div>
  <canvas id="g"></canvas>
  <div class="leg"><i style="background:var(--env)"></i>envelope<i style="background:var(--liga)"></i>disparo
    <i style="background:var(--solta)"></i>solta<i style="background:var(--verde)"></i>contra&ccedil;&atilde;o reconhecida</div>
  <div class="fraco" id="ultimo" style="margin-top:6px">&nbsp;</div>
</div>

<div class="cartao">
  <h2>Calibra&ccedil;&atilde;o</h2>
  <div id="instr">&nbsp;</div><div id="aval">&nbsp;</div>
  <div class="prog" id="prog"><span>boas seguidas <b id="pb"></b></span><span>prova <b id="pp"></b></span>
    <span>tentativa <b id="pt"></b></span></div>
  <div class="botoes" style="justify-content:center">
    <button class="prim" id="bcal" onclick="calibrar()">Iniciar calibra&ccedil;&atilde;o</button>
    <button id="bcanc" onclick="cmd('cancelar')">Cancelar</button></div>
  <div class="fraco" style="margin-top:10px">A m&atilde;o s&oacute; se move depois de calibrar. Siga a tela:
    relaxe, depois contraia e relaxe quantas vezes ela pedir, at&eacute; 3 contra&ccedil;&otilde;es boas seguidas.
    Em seguida vem a <b>prova</b>: mais 3, que ela precisa reconhecer todas, sem nenhum disparo com o bra&ccedil;o relaxado.</div>
  <div id="res" class="fraco" style="margin-top:8px"></div>
</div>

<div class="cartao">
  <h2>M&atilde;o</h2>
  <div class="botoes">
    <button class="mov" onclick="cmd('abrir')">Abrir m&atilde;o</button>
    <button class="mov" onclick="cmd('fechar')">Fechar m&atilde;o</button>
    <button class="mov prim" onclick="cmd('home')">Auto-home</button></div>
  <div class="fraco" style="margin-top:8px">Auto-home = m&atilde;o aberta, todos os servos no centro.
    Pelo m&uacute;sculo: contraiu uma vez fecha, contraiu de novo abre.</div>
</div>

<div class="cartao">
  <h2>Servos, um a um</h2>
  <div class="fraco" style="margin-bottom:8px">0 = casa (m&atilde;o aberta). Subir fecha o dedo.
    Cada dedo tem dois servos (A e B) que giram em sentidos opostos.</div>
  <div class="servos" id="servos"></div>
</div>

<div class="cartao">
  <h2>Capturar leitura</h2>
  <div class="botoes"><button id="bgrava" onclick="grava()">Gravar</button>
    <button id="bbaixa" onclick="baixa()" disabled>Baixar CSV</button>
    <span class="fraco" id="ngrav" style="align-self:center"></span></div>
</div>
<div class="fraco" style="text-align:center">rede LimbIA-mao &middot; http://192.168.4.1 &middot; sem internet</div>
</main>
<script>
const N=500,DT=10,NOMES=['Indicador A','Indicador B','M&eacute;dio A','M&eacute;dio B','Anelar A','Anelar B','Polegar A','Polegar B'];
let seq=-1,pts=[],st={},gravando=false,grav=[],falhas=0,mexeu=[0,0,0,0,0,0,0,0],envio={};
const $=id=>document.getElementById(id),cv=$('g'),cx=cv.getContext('2d');
function cor(n){return getComputedStyle(document.documentElement).getPropertyValue(n).trim()}
function calibrar(){if(!st.lib||confirm('Recalibrar trava a mao ate a nova calibracao ser aprovada. Continuar?'))cmd('calibrar')}
function cmd(c,extra){fetch('/cmd?c='+c+(extra||'')).then(r=>r.text()).then(t=>{$('ultimo').textContent=t})}
let h='';for(let i=0;i<8;i++)h+='<div class="sv"><label><span>'+NOMES[i]+' &middot; canal '+i+'</span><b id="v'+i+'">0&deg;</b></label>'+
 '<input type="range" id="s'+i+'" min="0" max="90" step="1" value="0"></div>';
$('servos').innerHTML=h;
for(let i=0;i<8;i++){const s=$('s'+i);s.addEventListener('input',()=>{mexeu[i]=Date.now();$('v'+i).innerHTML=s.value+'&deg;';
 clearTimeout(envio[i]);envio[i]=setTimeout(()=>cmd('servo','&i='+i+'&v='+s.value),60)})}
function grava(){gravando=!gravando;if(gravando)grav=[];$('bgrava').textContent=gravando?'Parar':'Gravar';
 $('bbaixa').disabled=gravando||!grav.length}
function baixa(){let s='t_ms,adc,envelope,disparo,solta,contracao,mao_fechada,liberada\n';
 for(const p of grav)s+=p.join(',')+'\n';const a=document.createElement('a');
 a.href=URL.createObjectURL(new Blob([s],{type:'text/csv'}));a.download='emg_'+Date.now()+'.csv';a.click()}
const INSTR={preparo:'Prepare-se: bra&ccedil;o solto',repouso:'RELAXE o bra&ccedil;o',contraia:'CONTRAIA forte!',
 relaxe:'RELAXE',prova_contraia:'PROVA: CONTRAIA!',prova_relaxe:'PROVA: relaxe',ok:'Calibrada e provada',falhou:'Calibra&ccedil;&atilde;o recusada',parada:''};
const AVAL={boa:'&#10003; boa!',fraca:'fraca: contraia mais forte',nao_relaxou:'n&atilde;o relaxou: solte o bra&ccedil;o todo',
 inconsistente:'diferente das anteriores: tente igual',detectou:'&#10003; reconhecida!',perdeu:'&#10007; n&atilde;o reconhecida',
 falso:'&#10007; disparou com o bra&ccedil;o relaxado'};
function bolas(n,m){let s='';for(let i=0;i<m;i++)s+=i<n?'&#9679;':'&#9675;';return s}
function atualiza(j){
 st=j;const k=j.cal||{},em=['preparo','repouso','contraia','relaxe','prova_contraia','prova_relaxe'].includes(k.e);
 $('trava').className='cartao '+(j.lib?'liberada':'travada');
 $('travatxt').innerHTML=j.lib?'&#10003; M&Atilde;O LIBERADA':(em?'&#128274; M&Atilde;O TRAVADA &mdash; calibrando...':'&#128274; M&Atilde;O TRAVADA &mdash; calibre para liberar');
 $('btrava').style.display=(j.lib||em)?'none':'';
 $('lamp').className=j.con?'on':'';$('lamp').innerHTML=j.con?'CONTRA&Ccedil;&Atilde;O':(j.lib||em?'repouso':'calibre<br>primeiro');
 $('nc').textContent=j.ncon;
 $('ent').innerHTML='MyoWare '+(j.det?'detectando...':(j.raw?'RAW':'SIG/ENV'))+(j.sat?' <span class="ruim">SATURADO</span>':'')+
  ' &middot; servos '+(j.pca?(j.pul?'com pulso':'soltos (sem pulso)'):'<span class="ruim">sem PCA9685</span>');
 if(j.ev)$('ultimo').textContent=j.ev;
 let t=INSTR[k.e]||'';if(em&&k.r)t+=' &middot; '+Math.ceil(k.r/1000)+' s';
 $('instr').innerHTML=t||'&nbsp;';$('instr').className=/contraia/.test(k.e)?'contraia':(/relaxe|repouso/.test(k.e)?'relaxe':'');
 $('aval').innerHTML=em&&AVAL[k.av]?AVAL[k.av]:'&nbsp;';
 $('pb').innerHTML=bolas(k.b,k.bm);$('pp').innerHTML=/prova/.test(k.e)?bolas(k.pv,k.pm):bolas(k.e=='ok'?k.pm:0,k.pm);
 $('pt').textContent=k.t+'/'+k.tm;
 $('bcal').disabled=em;$('bcanc').disabled=!em;$('prog').style.visibility=k.e=='parada'?'hidden':'';
 $('bcal').innerHTML=j.lib?'Recalibrar':'Iniciar calibra&ccedil;&atilde;o';
 if(j.res){const r=j.res;$('res').innerHTML='repouso '+r.rep.toFixed(1)+' &plusmn; '+r.dsv.toFixed(1)+
  ' &middot; contra&ccedil;&atilde;o '+r.con.toFixed(1)+' &middot; disparo '+r.liga.toFixed(1)+' &middot; solta '+r.desl.toFixed(1)+
  ' &middot; prova '+r.ac+' reconhecidas, '+r.fa+' falsas'+(k.e=='falhou'?'<br><span class="ruim">Recusada: '+k.m+'. A m&atilde;o continua travada.</span>':'')}
 document.querySelectorAll('.mov').forEach(b=>b.disabled=!j.lib||!j.pca);
 for(let i=0;i<8;i++){const s=$('s'+i);s.max=j.lim;s.disabled=!j.lib||!j.pca||!j.svl[i];
  if(Date.now()-mexeu[i]>1500){s.value=j.sv[i];$('v'+i).innerHTML=j.sv[i]+'&deg;'}}
 const p0=j.s-j.p.length;
 j.p.forEach((p,i)=>{pts.push({e:p[1]/10,c:p[2]&1});
  if(gravando)grav.push([(p0+i)*DT,p[0],(p[1]/10).toFixed(1),j.liga.toFixed(1),j.desl.toFixed(1),p[2]&1,(p[2]>>1)&1,(p[2]>>2)&1])});
 if(pts.length>N)pts=pts.slice(pts.length-N);seq=j.s;
 $('ngrav').textContent=grav.length?(grav.length/100).toFixed(1)+' s '+(gravando?'gravando':'prontos'):'';
 if(!gravando)$('bbaixa').disabled=!grav.length;
}
function desenha(){
 const W=cv.clientWidth,H=cv.clientHeight,r=window.devicePixelRatio||1;
 if(cv.width!=W*r||cv.height!=H*r){cv.width=W*r;cv.height=H*r}
 cx.setTransform(r,0,0,r,0,0);cx.clearRect(0,0,W,H);if(!pts.length)return;
 let mx=Math.max(st.liga||0,10)*1.3;for(const p of pts)if(p.e>mx)mx=p.e;mx*=1.05;
 const X=i=>i*W/(N-1),Y=v=>H-6-v/mx*(H-12);
 cx.fillStyle=cor('--verde');cx.globalAlpha=.18;pts.forEach((p,i)=>{if(p.c)cx.fillRect(X(i),0,W/N+1,H)});cx.globalAlpha=1;
 cx.strokeStyle=cor('--linha');cx.fillStyle=cor('--fraco');cx.font='11px system-ui';cx.lineWidth=1;
 for(let k=0;k<=3;k++){const v=mx*k/3,y=Y(v);cx.beginPath();cx.moveTo(0,y);cx.lineTo(W,y);cx.stroke();cx.fillText(v.toFixed(0),3,y-2)}
 if(st.liga>0){cx.setLineDash([7,5]);[[st.liga,'--liga'],[st.desl,'--solta']].forEach(([v,c])=>{
  cx.strokeStyle=cor(c);cx.lineWidth=2;cx.beginPath();cx.moveTo(0,Y(v));cx.lineTo(W,Y(v));cx.stroke()});cx.setLineDash([])}
 cx.strokeStyle=cor('--env');cx.lineWidth=3;cx.beginPath();pts.forEach((p,i)=>{const y=Y(p.e);i?cx.lineTo(X(i),y):cx.moveTo(X(i),y)});cx.stroke();
}
async function laco(){
 try{const r=await fetch('/dados?s='+seq);const j=await r.json();falhas=0;
  $('con').textContent='ao vivo';$('con').className='chip';atualiza(j);desenha()}
 catch(e){falhas++;$('con').textContent='sem conex\u00e3o';$('con').className='chip ruim'}
 setTimeout(laco,falhas?500:100)}
laco();
</script></body></html>)HTML";

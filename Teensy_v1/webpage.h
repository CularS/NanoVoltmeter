#pragma once

// Embedded dashboard. Three tabs mirroring the TFT UI. Talks to
// GET /api/status (1 Hz poll) and POST /api/cmd (same CLI as
// serial/telnet — the console at the bottom is a full CLI).

static const char WEBPAGE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Nanovoltmeter</title>
<style>
:root{--bg:#070b11;--panel:#0e141e;--panel2:#121a26;--rule:#1b2533;
--ink:#e8eef6;--dim:#8696aa;--acc:#2dd4cf;--warn:#fbbf24;--err:#ff5a6a;--ok:#34e0a1}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--ink);font:14px/1.45 system-ui,sans-serif;padding:14px;max-width:860px;margin:auto}
h1{font-size:17px;margin-bottom:10px}h1 small{color:var(--dim);font-weight:400;margin-left:8px}
.tabs{display:flex;gap:6px;margin-bottom:12px}
.tabs button{flex:1;background:var(--panel);border:1px solid var(--rule);color:var(--dim);
padding:9px;border-radius:7px;cursor:pointer;font-size:14px}
.tabs button.on{background:var(--acc);color:#042120;font-weight:600;border-color:var(--acc)}
.card{background:var(--panel);border:1px solid var(--rule);border-radius:9px;padding:14px;margin-bottom:12px}
.read{font-size:44px;font-weight:600;font-variant-numeric:tabular-nums;letter-spacing:.5px}
.read.ovl{color:var(--err)}
.badges{margin-top:6px;color:var(--dim);font-size:13px}.badges b{color:var(--ink)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;margin-top:12px}
.cell{background:var(--panel2);border:1px solid var(--rule);border-radius:7px;padding:8px}
.cell .l{color:var(--dim);font-size:11px;text-transform:uppercase;letter-spacing:.06em}
.cell .v{font-size:17px;margin-top:2px;font-variant-numeric:tabular-nums}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px;align-items:center}
button.b{background:var(--panel2);border:1px solid var(--rule);color:var(--ink);
padding:8px 14px;border-radius:7px;cursor:pointer}
button.b.acc{background:var(--acc);color:#042120;border-color:var(--acc);font-weight:600}
button.b.red{background:var(--err);color:#210407;border-color:var(--err);font-weight:600}
label{color:var(--dim);font-size:13px}
input,select{background:var(--panel2);border:1px solid var(--rule);color:var(--ink);
padding:7px;border-radius:6px;font-size:14px;width:110px}
table{width:100%;border-collapse:collapse;font-size:13px}
td{padding:5px 8px;border-bottom:1px solid var(--rule)}td:first-child{color:var(--dim)}
#log{background:#04070c;border:1px solid var(--rule);border-radius:7px;height:150px;
overflow-y:auto;padding:8px;font:12px/1.5 ui-monospace,monospace;white-space:pre-wrap}
#cmd{width:100%;margin-top:6px;font-family:ui-monospace,monospace}
.st{font-size:12px;color:var(--dim);margin-top:6px}
.hide{display:none}
</style></head><body>
<h1>Nanovoltmeter <small id="hd">connecting…</small></h1>
<div class="tabs">
<button id="tb0" class="on" onclick="tab(0)">Operation</button>
<button id="tb1" onclick="tab(1)">Settings</button>
<button id="tb2" onclick="tab(2)">Calibration</button>
</div>

<div id="tab0">
<div class="card">
<div class="read" id="rd">---.--- &micro;V</div>
<div class="badges" id="bd"></div>
<div class="grid">
<div class="cell"><div class="l">mean</div><div class="v" id="mn">-</div></div>
<div class="cell"><div class="l">sigma</div><div class="v" id="sg">-</div></div>
<div class="cell"><div class="l">N</div><div class="v" id="nn">-</div></div>
<div class="cell"><div class="l">phase</div><div class="v" id="ph">-</div></div>
</div>
<div class="row">
<button class="b acc" id="runbtn" onclick="cmd(J.running?'stop':'run')">RUN</button>
<button class="b" onclick="cmd('stats reset')">Reset stats</button>
<button class="b" onclick="cmd('az')">Auto-zero now</button>
<label>Range</label>
<select id="rgsel" onchange="cmd('range '+this.value)">
<option value="20mv">20 mV</option><option value="200mv">200 mV</option>
<option value="auto">auto</option></select>
</div>
</div>
</div>

<div id="tab1" class="hide">
<div class="card">
<table>
<tr><td>Mode</td><td><select id="s_mode" onchange="cmd('mode '+this.value)">
<option value="chop">low-Z chop</option><option value="dc">high-Z auto-zero</option></select></td></tr>
<tr><td>Chop half-period [ms]</td><td><input id="s_chop" onchange="cmd('chop '+this.value)"></td></tr>
<tr><td>Settle after relay [ms]</td><td><input id="s_settle" onchange="cmd('settle '+this.value)"></td></tr>
<tr><td>Aperture, high-Z [ms]</td><td><input id="s_aper" onchange="cmd('aper '+this.value)"></td></tr>
<tr><td>Auto-zero interval [s]</td><td><input id="s_azint" onchange="cmd('azint '+this.value)"></td></tr>
<tr><td>Backlight</td><td><select id="s_bl" onchange="cmd('bl '+this.value)">
<option value="1">1 (dim)</option><option value="2">2</option><option value="3">3</option>
<option value="4">4</option><option value="5">5 (full)</option></select></td></tr>
</table>
<div class="st">Changes apply immediately and persist in EEPROM.</div>
</div>
<div class="card">
<b style="font-size:13px">Network</b>
<table style="margin-top:6px">
<tr><td>DHCP</td><td><select id="n_dhcp" onchange="cmd('dhcp '+this.value)">
<option value="on">on</option><option value="off">off</option></select></td></tr>
<tr><td>Static IP</td><td><input id="n_ip" onchange="cmd('ip '+this.value)"></td></tr>
<tr><td>Netmask</td><td><input id="n_mask" onchange="cmd('mask '+this.value)"></td></tr>
<tr><td>Gateway</td><td><input id="n_gw" onchange="cmd('gw '+this.value)"></td></tr>
<tr><td>Hostname</td><td><input id="n_host" onchange="cmd('hostname '+this.value)"></td></tr>
</table>
<div class="row"><button class="b" onclick="cmd('netapply')">Apply network settings</button>
<button class="b" onclick="cmd('reboot')">Reboot</button></div>
<div class="st">Network fields are saved on change; Apply restarts the interface (page may need the new address).</div>
</div>
</div>

<div id="tab2" class="hide">
<div class="card">
<div class="row">
<button class="b acc" onclick="cmd('zero')">Zero cal (short input)</button>
<button class="b acc" onclick="cmd('calbias')">Bias cal (100k std)</button>
<button class="b red" onclick="cmd('calabort')">Abort</button>
</div>
<div class="st" id="calst">cal: idle</div>
<div class="row">
<label>Gain cal — apply reference, wait for stable reading, enter its true value:</label>
<input id="gv" placeholder="8.2mV">
<button class="b" onclick="cmd('calgain '+document.getElementById('gv').value)">Set gain</button>
</div>
<table style="margin-top:10px">
<tr><td>gain corr 20 mV</td><td id="g0">-</td></tr>
<tr><td>gain corr 200 mV</td><td id="g1">-</td></tr>
<tr><td>zero in use</td><td id="z0">-</td></tr>
<tr><td>bias DAC / I<sub>b</sub> comp</td><td id="bi">-</td></tr>
</table>
</div>
</div>

<div class="card">
<div id="log"></div>
<input id="cmd" placeholder="CLI command — 'help' for list" onkeydown="if(event.key==='Enter'){cmd(this.value);this.value=''}">
</div>

<script>
let J={},curtab=0;
function tab(i){curtab=i;for(let k=0;k<3;k++){
document.getElementById('tb'+k).classList.toggle('on',k===i);
document.getElementById('tab'+k).classList.toggle('hide',k!==i);}}
function logln(s){const l=document.getElementById('log');
l.textContent+=s+"\n";l.scrollTop=l.scrollHeight;}
function cmd(c){if(!c)return;logln('> '+c);
fetch('/api/cmd',{method:'POST',body:c}).then(r=>r.text())
.then(t=>{if(t.trim())logln(t.trim())}).catch(e=>logln('ERR '+e));}
function fv(v){if(v==null)return '-';
const a=Math.abs(v);
if(a<1e-3)return (v*1e6).toFixed(3)+' µV';
return (v*1e3).toFixed(4)+' mV';}
function fnv(v){return v==null?'-':(v*1e9).toFixed(1)+' nV';}
const seldirty={};
document.querySelectorAll('input,select').forEach(e=>{
e.addEventListener('focus',()=>seldirty[e.id]=1);
e.addEventListener('blur',()=>setTimeout(()=>delete seldirty[e.id],300));});
function put(id,v){const e=document.getElementById(id);
if(!seldirty[id]&&document.activeElement!==e)e.value=v;}
function poll(){fetch('/api/status').then(r=>r.json()).then(j=>{
J=j;
document.getElementById('hd').textContent=j.ip+' — up '+j.uptime_s+' s'+(j.demo?'  [DEMO]':'');
const rd=document.getElementById('rd');
rd.innerHTML=fv(j.reading_v)+(j.overload?' OVLD':'');
rd.classList.toggle('ovl',j.overload);
document.getElementById('bd').innerHTML=
(j.running?'<b>RUN</b>':'STOP')+' &middot; '+j.range+(j.autorange?' (auto)':'')+
' &middot; '+j.mode+(j.mode==='highZ-az'?' &middot; AZ age '+j.az_age_s+'s':'')+
' &middot; ADC '+j.adc_v.toFixed(4)+' V';
document.getElementById('mn').textContent=fv(j.mean_v);
document.getElementById('sg').textContent=fnv(j.sigma_v);
document.getElementById('nn').textContent=j.n;
document.getElementById('ph').textContent=j.phase;
document.getElementById('runbtn').textContent=j.running?'STOP':'RUN';
document.getElementById('runbtn').className='b '+(j.running?'red':'acc');
put('rgsel',j.autorange?'auto':(j.range==='20mV'?'20mv':'200mv'));
put('s_mode',j.mode==='lowZ-chop'?'chop':'dc');
put('s_chop',j.chop_half_ms);put('s_settle',j.settle_ms);
put('s_aper',j.aperture_ms);put('s_azint',j.az_interval_s);
put('s_bl',j.backlight);
document.getElementById('calst').textContent='cal: '+j.cal_msg+(j.cal_busy?' (busy)':'');
document.getElementById('g0').textContent=j.gain_corr[0].toFixed(6);
document.getElementById('g1').textContent=j.gain_corr[1].toFixed(6);
document.getElementById('z0').textContent=j.zero_nv.toFixed(1)+' nV';
document.getElementById('bi').textContent=j.bias_pa.toFixed(1)+' pA servo ('+j.ib_pa.toFixed(1)+' pA Ib)';
}).catch(()=>{document.getElementById('hd').textContent='connection lost';});}
setInterval(poll,1000);poll();
cmd('net');
</script></body></html>
)rawliteral";

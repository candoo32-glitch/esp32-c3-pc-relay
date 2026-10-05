/* STEAMPUNK FOUNDRY — physical instrument interface */
(()=>{"use strict";
const $=id=>document.getElementById(id),q=s=>document.querySelector(s),qa=s=>[...document.querySelectorAll(s)];
const esc=v=>String(v??"").replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
const click=id=>$(id)?.click();
const submit=s=>{const f=q(s);if(f){if(f.requestSubmit)f.requestSubmit();else f.submit()}};
const gear=(c="")=>'<svg class="sp-svg-gear '+c+'" viewBox="0 0 100 100"><path d="M45 2h10l3 13 9 3 9-10 8 8-10 10 4 9 14 2v11l-14 3-4 9 10 10-8 8-10-10-9 4-2 14H45l-3-14-9-4-10 10-8-8 10-10-4-9-14-3V47l14-2 4-9-10-10 8-8 10 10 9-3z" fill="#80501f" stroke="#d29b43" stroke-width="2"/><circle cx="50" cy="50" r="18" fill="#21140b" stroke="#c18a38" stroke-width="3"/><circle cx="50" cy="50" r="7" fill="#d49a3e"/></svg>';
const tube=(c="")=>'<svg class="sp-svg-tube '+c+'" viewBox="0 0 70 220"><path d="M22 20Q15 28 18 43v134q-3 15 8 23h18q11-8 8-23V43q3-15-4-23z" fill="#8f6236" opacity=".25" stroke="#d4a25a" stroke-width="3"/><path d="M27 35v150M43 35v150M35 52v92" stroke="#ff9a2d" stroke-width="3"/><path d="M18 16h34M18 204h34" stroke="#754719" stroke-width="8"/><circle cx="35" cy="103" r="9" fill="#ff9b2e"/></svg>';
const gauge=(label,id,unit)=>'<div class="sp-gauge"><svg viewBox="0 0 200 200"><circle cx="100" cy="100" r="91" fill="#120d09" stroke="#a36a29" stroke-width="6"/><circle cx="100" cy="100" r="76" fill="#d7bd86" stroke="#3b2718" stroke-width="5"/><path d="M43 139A76 76 0 1 1 157 139" fill="none" stroke="#5a3a20" stroke-width="2" stroke-dasharray="2 7"/><path d="M100 100L55 72" stroke="#23170e" stroke-width="6" stroke-linecap="round"/><circle cx="100" cy="100" r="9" fill="#a96d29" stroke="#21140b" stroke-width="4"/></svg><b>'+label+'</b><strong id="'+id+'">— '+unit+'</strong></div>';
const plate=(title,sub,body,cls="")=>'<section class="sp-plate '+cls+'"><div class="sp-plate-head"><small>'+sub+'</small><b>'+title+'</b></div><div class="sp-plate-body">'+body+'</div></section>';
const anchors=ids=>{const d=document.createElement("div");d.className="sp-backend-data";ids.forEach(id=>{const e=document.createElement("span");e.id=id;d.append(e)});return d};

function frame(){
 if(q(".sp-frame"))return;
 document.body.classList.add("sp-foundry");
 const f=document.createElement("div");f.className="sp-frame";
 f.innerHTML='<div class="sp-top-tubes">'+tube("t1")+tube("t2")+tube("t3")+'</div><header class="sp-header"><div class="sp-lamp"></div><div class="sp-title"><h1>ESP32-C3</h1><h2>RELAY CONTROLLER</h2><small>AETHERIC CONTROL APPARATUS // HEADLESS CONTROL AND CONFIGURATION</small></div><div class="sp-build"><span>BUILD</span><b id="sp-build">—</b><span>UPTIME</span><b id="sp-uptime">—</b><div class="sp-clock" id="sp-clock">—</div></div></header><div class="sp-nav-host"></div><main class="sp-main"></main><div class="sp-corner gear-left">'+gear()+"</div><div class=\"sp-corner gear-right\">"+gear()+"</div>";
 document.body.prepend(f);
 const nav=q(".tabs"),main=q(".sp-main");if(nav)q(".sp-nav-host").append(nav);
 qa("[data-tab-link]").forEach(a=>{const id=a.dataset.tabLink;a.innerHTML='<i>'+({dashboard:"◉",relays:"⚙",wifi:"⌁",network:"◎",diagnostics:"ϟ",storage:"▤",system:"⚙"}[id]||"•")+'</i><span>'+({dashboard:"DASHBOARD",relays:"RELAYS",wifi:"WIFI",network:"NETWORK",diagnostics:"DIAGNOSTICS",storage:"STORAGE",system:"SYSTEM"}[id]||id.toUpperCase())+"</span>"});
 qa(".tab").forEach(t=>main.append(t));
}
function dashboard(){
 const t=$("tab-dashboard");if(!t||t.dataset.spBuilt)return;t.dataset.spBuilt=1;
 t.innerHTML='<div class="sp-dashboard"><aside class="sp-gauges">'+gauge("SIGNAL","sp-rssi","dBm")+gauge("CPU CLOCK","sp-cpu","MHz")+gauge("UPTIME","sp-up","")+'</aside><section class="sp-plate sp-central">'+
 '<div class="sp-plate-head"><small>CENTRAL TELEMETRY</small><b>SYSTEM STATUS</b></div><div class="sp-plate-body"><div class="sp-online"><i id="sp-online"></i><b id="sp-online-text">OFFLINE</b></div><div class="sp-data"><div><span>HOSTNAME</span><b id="sp-host">—</b></div><div><span>IP ADDRESS</span><b id="sp-ip">—</b></div><div><span>GATEWAY</span><b id="sp-gw">—</b></div><div><span>DNS 1</span><b id="sp-dns">—</b></div><div><span>WIFI</span><b id="sp-wifi-status">—</b></div><div><span>FIRMWARE</span><b id="sp-fw">—</b></div></div><div class="sp-blueprint">⚙<br><small>AETHERIC ENGINE</small></div></div></section><aside class="sp-side">'+
 plate("WIRELESS","RADIO",'<div class="sp-big-led" id="sp-wled"></div><b id="sp-side-ssid">—</b><span id="sp-side-rssi">—</span><div class="sp-bars"><i></i><i></i><i></i><i></i><i></i></div>')+
 plate("NETWORK","ADDRESSING",'<b id="sp-side-ip">—</b><span>IP ADDRESS</span><b id="sp-side-gw">—</b><span>GATEWAY</span>')+
 plate("ACTIVITY LOG","EVENT ROLL",'<div class="sp-log-mini">SYSTEM READY<br>WIRELESS LINK STANDBY<br>RELAY CONTROL ARMED</div>')+
 '</aside></div>';
 t.append(anchors(["dash-wifi-status","dash-ssid","dash-ip","dash-rssi","dash-channel","dash-uptime","dash-build","dash-idf","dash-cpu","dash-relay0-name","dash-relay0-state","dash-relay1-name","dash-relay1-state"]));
}
function relay(){
 const t=$("tab-relays");if(!t||t.dataset.spBuilt)return;t.dataset.spBuilt=1;
 const original=q('#tab-relays form[action="/relay/config"]');
 t.innerHTML='<div class="sp-intro"><small>RELAY WORKS</small><b>DUAL ELECTROMECHANICAL CONTROL</b><em>Physical controls below operate the existing relay configuration and API.</em></div><div class="sp-relays" id="sp-relays"></div>';
 const out=q("#sp-relays");
 [0,1].forEach(i=>{const n=$("relay"+i+"-name"),normal=$("relay"+i+"-normal"),mode=$("relay"+i+"-mode"),pulse=$("relay"+i+"-pulse");if(!n)return;
 const c=document.createElement("article");c.className="sp-relay";
 c.innerHTML='<div class="sp-coil">'+Array.from({length:20},()=>"<i></i>").join("")+'</div><div class="sp-relay-head"><h2 id="sp-rname-'+i+'">Relay '+(i+1)+'</h2><div class="sp-contact-lamp" id="sp-lamp-'+i+'">OFF</div></div><div class="sp-terminal">GPIO '+(i?6:5)+' <span>ELECTROMAGNETIC CONTACTOR</span></div><div class="sp-controls"><label>NAME<input id="sp-name-'+i+'"></label><label>NORMAL CONTACT<select id="sp-normal-'+i+'"><option value="open">OPEN</option><option value="closed">CLOSED</option></select></label><label>ACTIVATION<select id="sp-mode-'+i+'"><option value="latched">LATCHED</option><option value="pulse">PULSE</option></select></label><label>PULSE DURATION<div class="sp-dial-control"><button id="sp-minus-'+i+'" type="button">−</button><b id="sp-pulse-'+i+'">250 ms</b><button id="sp-plus-'+i+'" type="button">+</button></div></label></div><button class="sp-fire" id="sp-fire-'+i+'">ACTUATE RELAY</button><button class="sp-save" id="sp-save-'+i+'">COMMIT SETTINGS</button>';
 out.append(c);
 const sync=()=>{$("sp-rname-"+i).textContent=n.value||"Relay "+(i+1);$("sp-name-"+i).value=n.value;$("sp-normal-"+i).value=normal.value;$("sp-mode-"+i).value=mode.value;$("sp-pulse-"+i).textContent=(Number(pulse.value)||250)+" ms"};
 sync();
 $("sp-name-"+i).oninput=e=>{n.value=e.target.value;sync()};
 $("sp-normal-"+i).onchange=e=>normal.value=e.target.value;
 $("sp-mode-"+i).onchange=e=>mode.value=e.target.value;
 $("sp-minus-"+i).onclick=()=>{pulse.value=Math.max(10,(Number(pulse.value)||250)-50);sync()};
 $("sp-plus-"+i).onclick=()=>{pulse.value=Math.min(60000,(Number(pulse.value)||250)+50);sync()};
 $("sp-save-"+i).onclick=()=>original?.requestSubmit?.();
 $("sp-fire-"+i).onclick=()=>click(i?"relay-reset-button":"relay-power-button");
 });
 original?.classList.add("sp-backend");
}
function wifi(){
 const t=$("tab-wifi");if(!t||t.dataset.spBuilt)return;t.dataset.spBuilt=1;
 const save=q('form[action="/wifi/save"]'),toggle=$("wifi-toggle"),scan=$("wifi-scan-form"),recon=$("wifi-reconnect-form"),tx=q('form[action="/wifi/txpower"]');
 t.innerHTML='<div class="sp-instrument-grid">'+plate("WIRELESS TELEGRAPH","RADIO CONTROL",'<div class="sp-radio-head"><div class="sp-big-led" id="sp-wled2"></div><b id="sp-wstate">OFFLINE</b></div><div class="sp-readout"><span>SSID</span><b id="sp-wssid">—</b></div><div class="sp-readout"><span>RSSI</span><b id="sp-wrssi">—</b></div><div class="sp-readout"><span>CHANNEL</span><b id="sp-wchannel">—</b></div><button class="sp-lever" id="sp-wtoggle">WIRELESS POWER</button><button class="sp-button" id="sp-wreconnect">RECONNECT</button>')+
 plate("RADIO CABINET","CREDENTIALS",'<div id="sp-wifi-form"></div>')+
 plate("SPECTRUM SCANNER","DISCOVERY",'<div class="sp-scan-screen" id="sp-scan-screen">READY / AWAITING SCAN</div><button class="sp-button" id="sp-scan">SCAN THE ETHER</button>')+
 plate("TRANSMITTER","TX POWER",'<div id="sp-tx-form"></div>')+'</div>';
 if(save){q("#sp-wifi-form").append(save);save.classList.add("sp-backend-form")}
 if(tx){q("#sp-tx-form").append(tx);tx.classList.add("sp-backend-form")}
 $("sp-wtoggle").onclick=()=>toggle?.click();$("sp-wreconnect").onclick=()=>recon?.querySelector("button")?.click();$("sp-scan").onclick=()=>scan?.requestSubmit?.();
 [recon,scan,toggle].forEach(e=>e?.classList.add("sp-backend"));
 t.append(anchors(["wifi-status","wifi-ssid","wifi-ip","wifi-gateway","wifi-subnet","wifi-dns","wifi-rssi","wifi-channel","wifi-bssid","wifi-tx"]));
}
function network(){
 const t=$("tab-network");if(!t||t.dataset.spBuilt)return;t.dataset.spBuilt=1;const f=q('form[action="/network/save"]');
 t.innerHTML='<div class="sp-network-bay">'+plate("ADDRESSING ENGINE","NETWORK EXCHANGE",'<div class="sp-address-mode"><span>ADDRESS MODE</span><button id="sp-mode">DHCP</button></div><div id="sp-net-form"></div>')+plate("LIVE CONNECTION","TELEMETRY",'<div class="sp-network-live"><b id="sp-nip">—</b><span>IP ADDRESS</span><b id="sp-ngw">—</b><span>GATEWAY</span><b id="sp-ndns">—</b><span>DNS 1</span><b id="sp-nrssi">—</b><span>RSSI</span></div>')+'</div>';
 if(f){$("sp-net-form").append(f);f.classList.add("sp-backend-form")}
 $("sp-mode").onclick=()=>{const s=$("net-mode");if(!s)return;s.value=s.value==="static"?"dhcp":"static";s.dispatchEvent(new Event("change",{bubbles:true}));$("sp-mode").textContent=s.value.toUpperCase()};
 t.append(anchors(["net-state","net-live-ip","net-live-subnet","net-live-gateway","net-live-dns1","net-live-dns2","net-auth","net-bssid","net-rssi"]));
}
function diagnostics(){
 const t=$("tab-diagnostics");if(!t||t.dataset.spBuilt)return;t.dataset.spBuilt=1;const f=q('form[action="/diagnostics/toggle"]'),ta=$("diagnostic-console");
 t.innerHTML=plate("ENGINEER'S LOG","DIAGNOSTIC TELEGRAPH",'<div class="sp-console" id="sp-console"></div><div class="sp-diagnostic-controls"><button class="sp-button" id="sp-diag-toggle">DIAGNOSTICS</button><span>LIVE SCROLL</span></div>');
 const mirror=()=>{const c=$("sp-console");if(c){c.textContent=ta?.value||"Waiting for diagnostics…";c.scrollTop=c.scrollHeight}};setInterval(mirror,700);$("sp-diag-toggle").onclick=()=>f?.querySelector("button")?.click();f?.classList.add("sp-backend");
 t.append(anchors(["diag-state","diag-auth","diag-bssid","diag-rssi"]));
}
function simpleTabs(){
 ["tab-storage","tab-system"].forEach(id=>{const t=$(id);if(!t||t.dataset.spBuilt)return;t.dataset.spBuilt=1;t.querySelectorAll(".card").forEach(c=>c.classList.add("sp-system-panel"))});
}
function update(){
 const st=($("dash-wifi-status")?.textContent||"").trim().toUpperCase(),on=st==="CONNECTED";
 const r=parseFloat($("dash-rssi")?.textContent),cpu=parseFloat($("dash-cpu")?.textContent),up=parseFloat($("dash-uptime")?.textContent);
 const set=(id,v)=>{if($(id))$(id).textContent=v};
 set("sp-rssi",Number.isFinite(r)?Math.round(r)+" dBm":"— dBm");set("sp-cpu",Number.isFinite(cpu)?Math.round(cpu)+" MHz":"— MHz");set("sp-up",Number.isFinite(up)?Math.floor(up/3600)+"h "+String(Math.floor(up/60)%60).padStart(2,"0")+"m":"—");
 set("sp-online-text",on?"ONLINE":"OFFLINE");$("sp-online")?.classList.toggle("on",on);$("sp-wled")?.classList.toggle("on",on);$("sp-wled2")?.classList.toggle("on",on);set("sp-wstate",on?"ONLINE":"OFFLINE");
 set("sp-wssid",$("dash-ssid")?.textContent||"—");set("sp-wrssi",$("dash-rssi")?.textContent||"—");set("sp-wchannel",$("dash-channel")?.textContent||"—");set("sp-side-ssid",$("dash-ssid")?.textContent||"—");set("sp-side-rssi",$("dash-rssi")?.textContent||"—");set("sp-side-ip",$("dash-ip")?.textContent||"—");set("sp-side-gw",$("net-live-gateway")?.textContent||"—");
 set("sp-ip",$("dash-ip")?.textContent||"—");set("sp-gw",$("net-live-gateway")?.textContent||"—");set("sp-dns",$("net-live-dns1")?.textContent||"—");set("sp-fw",$("dash-build")?.textContent||"—");set("sp-nip",$("net-live-ip")?.textContent||"—");set("sp-ngw",$("net-live-gateway")?.textContent||"—");set("sp-ndns",$("net-live-dns1")?.textContent||"—");set("sp-nrssi",$("net-rssi")?.textContent||"—");set("sp-host",$("hostname")?.value||"relay-esp32");set("sp-wifi-status",st||"—");set("sp-build",$("dash-build")?.textContent||"—");set("sp-uptime",$("dash-uptime")?.textContent||"—");if($("sp-clock"))$("sp-clock").textContent=new Date().toLocaleString(undefined,{month:"short",day:"2-digit",year:"numeric",hour:"2-digit",minute:"2-digit",second:"2-digit"}).toUpperCase();
}
function init(){frame();dashboard();relay();wifi();network();diagnostics();simpleTabs();update();setInterval(update,1000)}
if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",init,{once:true});else init();
})();
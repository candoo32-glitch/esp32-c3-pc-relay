(function(){
"use strict";
const $=id=>document.getElementById(id);
let wifiCredentialsDirty=false;
let networkFormDirty=false;
let wifiPasswordEditing=false;
const esc=v=>String(v??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c]));
function text(id,v){const e=$(id);if(e)e.textContent=v??"-"}
function showTab(tab){if(!["dashboard","wifi","network","diagnostics","relays","storage","system"].includes(tab))tab="dashboard";
document.querySelectorAll(".tab").forEach(e=>e.classList.toggle("active",e.id==="tab-"+tab));
document.querySelectorAll("[data-tab-link]").forEach(e=>e.classList.toggle("active",e.dataset.tabLink===tab));
document.querySelectorAll(".return-tab").forEach(e=>e.value=tab);
location.hash=tab; return tab}
function currentTab(){const hash=(location.hash||"").slice(1);const query=new URLSearchParams(location.search).get("tab");return showTab(hash||query||"dashboard")}
function statusClass(e,v){e.className=v==="CONNECTED"?"ok":v==="OFF"?"muted":"warn";e.textContent=v}
function fmt(v,s){return v===null||v===undefined||v===""?"-":String(v)+(s||"")}
function setRelay(r,i){
 text("relay-title-"+i,r.name); text("relay-state-"+i,r.state?"ACTIVE":"NORMAL"); text("relay-normal-"+i,r.normal);
 text("relay-mode-display-"+i,r.mode); text("relay-pulse-display-"+i,r.mode==="PULSE"?r.pulse+" ms":"-");
 $("relay"+i+"-name").value=r.name; $("relay"+i+"-normal").value=r.normal.toLowerCase(); $("relay"+i+"-mode").value=r.mode.toLowerCase(); $("relay"+i+"-pulse").value=r.pulse;
 text("dash-relay"+i+"-name",r.name); $("dash-relay"+i+"-state").innerHTML=r.state?'<span class="ok">ON</span>':'OFF';
}
function render(s){
 const w=s.wifi,n=s.network,d=s.diagnostics;
 statusClass($("dash-wifi-status"),w.status); statusClass($("wifi-status"),w.status);
 text("dash-ssid",w.connected?w.ssid:"-");text("dash-ip",w.connected?w.ip:"-");text("dash-rssi",w.connected?w.rssi+" dBm":"-");text("dash-channel",w.connected?w.channel:"-");
 text("wifi-ssid",w.connected?w.ssid:"-");text("wifi-ip",w.connected?w.ip:"-");text("wifi-gateway",w.connected?w.gateway:"-");text("wifi-subnet",w.connected?w.subnet:"-");text("wifi-dns",w.connected?w.dns:"-");text("wifi-rssi",w.connected?w.rssi+" dBm":"-");text("wifi-channel",w.connected?w.channel:"-");text("wifi-bssid",w.connected?w.bssid:"-");text("wifi-tx",w.tx+" dBm");
 if(!wifiCredentialsDirty){
 $("ssid").value=w.savedSsid||"";
 $("password").value=w.savedPassword?"********":"";
 $("password").type="password";
 $("show-password").disabled=true;
 $("show-password").textContent="Show password";
}
$("wifi-toggle").textContent=w.enabled?"Turn Wi-Fi OFF":"Turn Wi-Fi ON";$("wifi-toggle").className=w.enabled?"danger":"good";$("wifi-reconnect-form").style.display=w.enabled?"block":"none";
 $("txpower").value=w.tx;
 text("net-state",w.status);text("net-auth",w.connected?w.auth:"-");text("net-bssid",w.connected?w.bssid:"-");text("net-rssi",w.connected?w.rssi+" dBm":"-");
 if(!networkFormDirty){
 $("hostname").value=n.hostname;$("net-mode").value=n.mode;$("net-ip").value=n.ip;$("net-gateway").value=n.gateway;$("net-subnet").value=n.subnet;$("net-dns1").value=n.dns1;$("net-dns2").value=n.dns2;
}
$("static-fields").style.display=$("net-mode").value==="static"?"grid":"none";
 text("diag-state",w.status);text("diag-auth",w.connected?w.auth:"-");text("diag-bssid",w.connected?w.bssid:"-");text("diag-rssi",w.connected?w.rssi+" dBm":"-");
 $("diagnostics-toggle").textContent=d.enabled?"Disable diagnostics":"Enable diagnostics";$("diagnostics-toggle").className=d.enabled?"danger":"good";
 setRelay(s.relays[0],0);setRelay(s.relays[1],1);
 text("nvs-size",s.nvs.size);$("nvs-table").innerHTML="<table><thead><tr><th>#</th><th>Namespace</th><th>Key</th><th>Type</th><th>Value</th></tr></thead><tbody>"+s.nvs.entries.map((e,i)=>"<tr><td>"+(i+1)+"</td><td>"+esc(e.namespace)+"</td><td>"+esc(e.key)+"</td><td>"+esc(e.type)+"</td><td>"+esc(e.value)+"</td></tr>").join("")+"</tbody></table><div class='muted'>"+s.nvs.entries.length+" entries. Password/token values are hidden.</div>";
 text("dash-uptime",s.system.uptime+" seconds");text("dash-build",s.system.build);text("dash-idf",s.system.idf);text("dash-cpu",s.system.cpu+" MHz");
 text("sys-build",s.system.build);text("sys-web-build",s.system.webBuild||"0");text("sys-date",s.system.date);text("sys-idf",s.system.idf);text("sys-arduino",s.system.arduino);text("sys-cpu",s.system.cpu+" MHz");text("sys-uptime",s.system.uptime+" seconds");
 $("relay-power-button").textContent=s.relays[0].name;$("relay-reset-button").textContent=s.relays[1].name;
 text("page-status",w.status); currentTab();
}
async function load(){try{const r=await fetch("/api/state",{cache:"no-store"});if(!r.ok)throw new Error(r.status);render(await r.json())}catch(e){text("page-status","UNAVAILABLE")}}
document.querySelectorAll("[data-tab-link]").forEach(e=>e.addEventListener("click",()=>showTab(e.dataset.tabLink)));
$("ssid").addEventListener("input",()=>{wifiCredentialsDirty=true});
$("password").addEventListener("focus",()=>{
 if(!wifiPasswordEditing){
   wifiPasswordEditing=true;
   wifiCredentialsDirty=true;
   $("password").value="";
   $("show-password").disabled=false;
 }
});
$("password").addEventListener("input",()=>{
 wifiPasswordEditing=true;
 wifiCredentialsDirty=true;
 $("show-password").disabled=false;
});
["hostname","net-mode","net-ip","net-gateway","net-subnet","net-dns1","net-dns2"].forEach(id=>{
 const e=$(id); e.addEventListener("input",()=>{networkFormDirty=true}); e.addEventListener("change",()=>{networkFormDirty=true});
});
$("show-password").addEventListener("click",()=>{
 const e=$("password"), visible=e.type==="text";
 if(!wifiPasswordEditing) return;
 e.type=visible?"password":"text";
 $("show-password").textContent=visible?"Show password":"Hide password";
});
window.addEventListener("hashchange",currentTab);
$("net-mode").addEventListener("change",()=>{$("static-fields").style.display=$("net-mode").value==="static"?"grid":"none"});
load();
setInterval(load,5000);
})();
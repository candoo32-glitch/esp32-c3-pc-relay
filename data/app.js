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
 $("password").value=w.passwordSaved?"********":"";
 $("password").type="password";
 $("show-password").disabled=true;
 $("show-password").textContent="Show password";
}
$("wifi-toggle").textContent=w.enabled?"Turn Wi-Fi OFF":"Turn Wi-Fi ON";$("wifi-toggle").className=w.enabled?"danger":"good";$("wifi-reconnect-form").style.display=w.enabled?"block":"none";
 $("txpower").value=w.tx;
 text("net-live-ip",w.connected?w.ip:"-");text("net-live-subnet",w.connected?w.subnet:"-");text("net-live-gateway",w.connected?w.gateway:"-");text("net-live-dns1",w.connected?n.dns1:"-");text("net-live-dns2",w.connected?n.dns2:"-");text("net-state",w.status);text("net-auth",w.connected?w.auth:"-");text("net-bssid",w.connected?w.bssid:"-");text("net-rssi",w.connected?w.rssi+" dBm":"-");
 if(!networkFormDirty){
 $("hostname").value=n.hostname;$("net-mode").value=n.mode;$("net-ip-input").value=n.ip;$("net-gateway-input").value=n.gateway;$("net-subnet-input").value=n.subnet;$("net-dns1-input").value=n.dns1;$("net-dns2-input").value=n.dns2;
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
$("ssid").addEventListener("input",()=>{wifiCredentialsDirty=true;filterSsidOptions();});$("wifi-ssid-toggle").addEventListener("click",()=>{const o=$("wifi-ssid-options");if(o.hidden){openSsidOptions()}else closeSsidOptions();});$("ssid").addEventListener("focus",()=>{if(document.querySelector(".ssid-option"))openSsidOptions();});document.addEventListener("click",e=>{const box=document.querySelector(".ssid-combobox");if(box&&!box.contains(e.target))closeSsidOptions();});
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
["hostname","net-mode","net-ip-input","net-gateway-input","net-subnet-input","net-dns1-input","net-dns2-input"].forEach(id=>{
 const e=$(id); e.addEventListener("input",()=>{networkFormDirty=true}); e.addEventListener("change",()=>{networkFormDirty=true});
});
$("show-password").addEventListener("click",()=>{
 const e=$("password"), visible=e.type==="text";
 if(!wifiPasswordEditing) return;
 e.type=visible?"password":"text";
 $("show-password").textContent=visible?"Show password":"Hide password";
});
function setPageStatus(m,k){const e=$("page-status");if(e){e.textContent=m;e.className=k||""}}
async function rd(r){const t=r.headers.get("content-type")||"";return t.includes("application/json")?await r.json():null}
async function pf(f){const r=await fetch(f.action,{method:"POST",body:new URLSearchParams(new FormData(f)),cache:"no-store"});if(!r.ok)throw Error(r.status);return rd(r)}
async function pfu(f){const r=await fetch(f.action,{method:"POST",body:new FormData(f),cache:"no-store"});if(!r.ok)throw Error(r.status);return rd(r)}
function renderScan(d){
 const e=$("wifi-scan-results"),options=$("wifi-ssid-options");
 if(!e)return;
 if(options){
   options.innerHTML="";
   const seen=new Set();
   (d&&d.networks||[]).forEach(n=>{
     const ssid=String(n.ssid||"");
     if(!ssid||seen.has(ssid))return;
     seen.add(ssid);
     const o=document.createElement("button");
     o.type="button";
     o.className="ssid-option";
     o.textContent=ssid;
     o.setAttribute("role","option");
     o.addEventListener("click",()=>{
       $("ssid").value=ssid;
       wifiCredentialsDirty=true;
       closeSsidOptions();
     });
     options.appendChild(o);
   });
 }
 if(!d||!d.networks||!d.networks.length){
   e.innerHTML='<div class="help">No networks found or scan failed.</div>';
   return
 }
 e.innerHTML='<div class="table-wrap"><table><thead><tr><th>#</th><th>SSID</th><th>RSSI</th><th>Channel</th><th>Security</th><th>BSSID</th></tr></thead><tbody>'+
   d.networks.map((n,i)=>'<tr><td>'+(i+1)+'</td><td>'+esc(n.ssid||"(hidden)")+'</td><td>'+esc(n.rssi)+' dBm</td><td>'+esc(n.channel)+'</td><td>'+esc(n.security)+'</td><td class="mono">'+esc(n.bssid)+'</td></tr>').join("")+
   '</tbody></table></div><div class="help">'+d.networks.length+' access points found.</div>';
}
function closeSsidOptions(){
 const o=$("wifi-ssid-options"),b=$("wifi-ssid-toggle"),i=$("ssid");
 if(!o||!b)return;
 o.hidden=true;
 b.setAttribute("aria-expanded","false");
 if(i)i.setAttribute("aria-expanded","false");
}
function openSsidOptions(){
 const o=$("wifi-ssid-options"),b=$("wifi-ssid-toggle"),i=$("ssid");
 if(!o||!b)return;
 o.hidden=false;
 b.setAttribute("aria-expanded","true");
 if(i)i.setAttribute("aria-expanded","true");
}
function filterSsidOptions(){
 const q=($("ssid").value||"").toLowerCase();
 document.querySelectorAll(".ssid-option").forEach(o=>{o.hidden=q!==""&&!o.textContent.toLowerCase().includes(q)});
}
async function scan(e){e.preventDefault();const b=$("wifi-scan-button"),s=$("wifi-scan-spinner"),l=$("wifi-scan-label");b.disabled=true;s.hidden=false;l.textContent="Scanning…";setPageStatus("Wi-Fi scan in progress…","warn");try{renderScan(await pf(e.currentTarget));setPageStatus("Wi-Fi scan complete","ok")}catch(x){setPageStatus("Wi-Fi scan failed","bad")}finally{b.disabled=false;s.hidden=true;l.textContent="Scan now"}}
function showUpdateStatus(message,kind){
 const box=$("software-update-status"),msg=$("software-update-message");
 if(!box||!msg)return;
 box.hidden=false;
 msg.className="notice "+(kind||"");
 msg.textContent=message||"";
}
function showUpdateDetails(d){
 const details=$("software-update-details");
 if(!details||!d)return;
 details.hidden=false;
 text("update-firmware-detail","Build "+d.currentFirmware+" → "+d.latestFirmware+(d.firmwareAvailable?" (update available)":" (current)"));
 text("update-web-detail","Build "+d.currentWeb+" → "+d.latestWeb+(d.webAvailable?" (update available)":" (current)"));
}
async function check(e){
 e.preventDefault();
 setPageStatus("Checking for updates…","warn");
 showUpdateStatus("Checking GitHub for newer firmware and web interface components…","warn");
 $("software-update-details").hidden=true;
 try{
   const r=await fetch(e.currentTarget.action,{cache:"no-store"}),d=await rd(r);
   if(!r.ok)throw new Error(d?.message||"Could not retrieve the GitHub release catalog.");
   showUpdateStatus(d.message||"Update check complete.","ok");
   showUpdateDetails(d);
   setPageStatus("Update check complete","ok");
 }catch(x){
   showUpdateStatus(x.message||"Could not retrieve the GitHub release catalog.","bad");
   setPageStatus("Update check failed","bad");
 }
}
async function latest(e){
 e.preventDefault();
 if(!confirm("Check for newer firmware and web interface components on GitHub, install any that are newer, then reboot the ESP32-C3?"))return;
 setPageStatus("Installing updates…","warn");
 showUpdateStatus("Checking for newer components and installing any updates…","warn");
 $("software-update-details").hidden=true;
 try{
   const d=await pf(e.currentTarget);
   showUpdateStatus(d?.message||"Update installed; rebooting the ESP32-C3.","ok");
   setPageStatus(d?.message||"Update installed; rebooting.","ok");
 }catch(x){
   const message=x?.message||"Update request failed or the ESP32-C3 rebooted.";
   showUpdateStatus(message,"bad");
   setPageStatus(message,"bad");
 }
}
async function upload(e,q){e.preventDefault();if(!confirm(q))return;setPageStatus("Uploading…");try{const d=await pfu(e.currentTarget);setPageStatus(d?.message||"Operation completed.","ok")}catch(x){setPageStatus("Operation failed or the ESP32-C3 rebooted.","bad")}}
document.querySelectorAll('form[method="POST"]').forEach(f=>{if(["wifi-scan-form","update-latest-form","firmware-upload-form","config-restore-form"].includes(f.id))return;f.addEventListener("submit",async e=>{e.preventDefault();try{await pf(f);setPageStatus("Saved","ok");setTimeout(load,300)}catch(x){setPageStatus("Request failed","bad")}})});
$("wifi-scan-form").addEventListener("submit",scan);$("check-update-form").addEventListener("submit",check);$("update-latest-form").addEventListener("submit",latest);$("firmware-upload-form").addEventListener("submit",e=>upload(e,"Upgrade firmware and reboot the ESP32-C3?"));$("config-restore-form").addEventListener("submit",e=>upload(e,"Restore this configuration and reboot the ESP32-C3?"));
window.addEventListener("hashchange",currentTab);
$("net-mode").addEventListener("change",()=>{$("static-fields").style.display=$("net-mode").value==="static"?"grid":"none"});
load();
setInterval(load,5000);
})();
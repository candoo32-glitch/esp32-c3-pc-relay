(function(){
"use strict";
const $=id=>document.getElementById(id);
let wifiCredentialsDirty=false;
const UI_ROOT_URL="https://raw.githubusercontent.com/candoo32-glitch/esp32-c3-pc-relay/idf-6-migration/ui";
const UI_CATALOG_URL=UI_ROOT_URL+"/catalog.json";
const UI_MANIFEST_VERSION=1;
let uiCatalogLoaded=false;
let activeExternalUi="";
let uiInitialLoad=true;
let uiInitialFallback=false;
const UI_FETCH_TIMEOUT_MS=4000;

function fetchWithTimeout(url,options={},timeoutMs=UI_FETCH_TIMEOUT_MS){
  const controller=new AbortController();
  const timer=setTimeout(()=>controller.abort(),timeoutMs);
  return fetch(url,{...options,signal:controller.signal}).finally(()=>clearTimeout(timer));
}

function uiAssetUrl(uiId,asset){
  const value=String(asset||"").trim();
  if(/^https?:\\/\\//i.test(value))return value;
  return UI_ROOT_URL+"/"+encodeURIComponent(uiId)+"/"+value.split("/").map(encodeURIComponent).join("/");
}

function removeExternalUi(keepPending=false){
  const selector=keepPending?"[data-external-ui]":"[data-external-ui],[data-ui-pending]";
  document.querySelectorAll(selector).forEach(e=>e.remove());
  document.body.classList.remove("external-ui-active","external-ui-fallback","lcars-ready");
  document.body.removeAttribute("data-lcars-tab");
  document.documentElement.removeAttribute("data-external-ui");
  document.documentElement.style.removeProperty("--lcars-active-tab");
  activeExternalUi="";
}

async function fetchExternalUiSource(url){
  /*
   * Serve external theme assets through jsDelivr. GitHub Raw is excellent
   * for repository storage, but its content-type/CORS behavior is not
   * consistent enough across Safari/WebKit for a live UI package.
   */
  const rawPrefix="https://raw.githubusercontent.com/candoo32-glitch/esp32-c3-pc-relay/idf-6-migration/";
  const cdnPrefix="https://cdn.jsdelivr.net/gh/candoo32-glitch/esp32-c3-pc-relay@idf-6-migration/";
  const sourceUrl=url.startsWith(rawPrefix)
    ? cdnPrefix+url.slice(rawPrefix.length)
    : url;
  const cacheBustedUrl=sourceUrl+(sourceUrl.includes("?")?"&":"?")+"uiCacheBust="+Date.now();
  const r=await fetchWithTimeout(cacheBustedUrl,{cache:"no-store"});
  if(!r.ok)throw new Error("UI asset HTTP "+r.status+": "+sourceUrl);
  return await r.text();
}

function installExternalUiStyle(source){
  const style=document.createElement("style");
  style.dataset.uiPending="true";
  style.textContent=source;
  document.head.appendChild(style);
  return style;
}

function promotePendingUiStyle(style){
  style.removeAttribute("data-ui-pending");
  style.dataset.externalUi="true";
}

function installExternalUiScript(url){
  return new Promise((resolve,reject)=>{
    /*
     * GitHub Raw intentionally serves repository files as text/plain with
     * nosniff. That is fine for fetch(), but it is not a reliable source for
     * a <script src> tag in Safari/WebKit. Use jsDelivr only for executable
     * theme JavaScript; the files still live in this GitHub branch.
     */
    const rawPrefix="https://raw.githubusercontent.com/candoo32-glitch/esp32-c3-pc-relay/idf-6-migration/";
    const cdnPrefix="https://cdn.jsdelivr.net/gh/candoo32-glitch/esp32-c3-pc-relay@idf-6-migration/";
    const scriptUrl=url.startsWith(rawPrefix)
      ? cdnPrefix+url.slice(rawPrefix.length)
      : url;
    const script=document.createElement("script");
    script.dataset.externalUi="true";
    script.async=false;
    script.src=scriptUrl+(scriptUrl.includes("?")?"&":"?")+"uiCacheBust="+Date.now();
    script.onload=()=>resolve(script);
    script.onerror=()=>reject(new Error("UI script could not be loaded"));
    document.head.appendChild(script);
  });
}

async function activateUi(uiId){
  const id=String(uiId||"builtin").trim();

  if(id==="builtin"){
    removeExternalUi();
    activeExternalUi="builtin";
    return true;
  }

  if(!/^[A-Za-z0-9._-]{1,63}$/.test(id))throw Error("Invalid UI id");

  let pendingStyle=null;

  try{
    const manifestUrl=UI_ROOT_URL+"/"+encodeURIComponent(id)+"/manifest.json?uiCacheBust="+Date.now();
    const r=await fetchWithTimeout(manifestUrl,{cache:"no-store"});
    if(!r.ok)throw Error("Manifest HTTP "+r.status);

    const manifest=await r.json();
    if(Number(manifest?.version||0)!==UI_MANIFEST_VERSION)throw Error("Unsupported UI manifest");
    if(String(manifest?.id||"")!==id)throw Error("UI manifest ID mismatch");
    if(String(manifest?.requires||"")!=="builtin-dom-v1")throw Error("Unsupported UI DOM contract");

    const assets=manifest.assets||{};
    if(!assets.stylesheet)throw Error("UI stylesheet missing");

    /*
     * IMPORTANT:
     * Fetch the complete new package BEFORE touching the currently active
     * theme.  The old implementation removed the active theme first and then
     * attempted to load it, which produced a white screen and also called a
     * removed loadExternalUiAsset() helper.
     */
    const stylesheetSource=await fetchExternalUiSource(uiAssetUrl(id,assets.stylesheet));
    const scriptUrl=assets.script ? uiAssetUrl(id,assets.script) : "";

    /*
     * Stage the new CSS while the old CSS is still active. The JavaScript
     * package is loaded as a real external script so Safari/WebKit and any
     * page CSP do not silently discard dynamically injected inline scripts.
     */
    pendingStyle=installExternalUiStyle(stylesheetSource);
    removeExternalUi(true);
    promotePendingUiStyle(pendingStyle);
    pendingStyle=null;

    if(scriptUrl){
      await installExternalUiScript(scriptUrl);
      window.dispatchEvent(new Event("external-ui-activate"));
    }

    activeExternalUi=id;
    document.documentElement.dataset.externalUi=id;
    document.body.classList.add("external-ui-active");
    return true;
  }catch(e){
    if(pendingStyle)pendingStyle.remove();

    /*
     * A failed switch must never leave half a theme installed.  The caller
     * keeps the saved NVS selection unchanged and the built-in DOM remains
     * usable.
     */
    if(!activeExternalUi){
      uiInitialFallback=true;
      activeExternalUi="builtin";
      document.documentElement.dataset.externalUi="builtin";
      document.body.classList.add("external-ui-fallback");
    }

    console.warn("External UI could not be loaded; keeping the currently active UI:",e);
    return false;
  }
}

async function loadUiCatalog(){
  const select=$("ui-selection");
  if(!select)return;
  try{
    const r=await fetchWithTimeout(UI_CATALOG_URL+"?uiCacheBust="+Date.now(),{cache:"no-store"});
    if(!r.ok)throw Error(r.status);
    const catalog=await r.json();
    const entries=Array.isArray(catalog?.uis)?catalog.uis:[];
    const seen=new Set(["builtin"]);
    entries.forEach(ui=>{
      const id=String(ui?.id||"").trim();
      const name=String(ui?.name||"").trim();
      if(!id||!name||seen.has(id)||!/^[A-Za-z0-9._-]{1,63}$/.test(id))return;
      seen.add(id);
      const option=document.createElement("option");
      option.value=id;
      option.textContent=name;
      select.appendChild(option);
    });
    uiCatalogLoaded=true;
    const selected=String(select.dataset.savedSelection||"builtin");
    select.value=seen.has(selected)?selected:"builtin";
  }catch(e){
    uiCatalogLoaded=false;
    select.value="builtin";
  }
}


let networkFormDirty=false;
let stateFailureCount=0;
const STATE_FAILURE_THRESHOLD=3;
let wifiPasswordEditing=false;
const esc=v=>String(v??"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c]));
function text(id,v){const e=$(id);if(e)e.textContent=v??"-"}
function showTab(tab){if(!["dashboard","wifi","network","diagnostics","relays","storage","system"].includes(tab))tab="dashboard";
document.querySelectorAll(".tab").forEach(e=>e.classList.toggle("active",e.id==="tab-"+tab));
document.querySelectorAll("[data-tab-link]").forEach(e=>e.classList.toggle("active",e.dataset.tabLink===tab));
document.querySelectorAll(".return-tab").forEach(e=>e.value=tab);
if(tab==="diagnostics")startDiagnosticPolling();else stopDiagnosticPolling();
location.hash=tab;
if(tab==="storage"){loadNvsContents();loadStorageFiles();}
return tab}
function currentTab(){const hash=(location.hash||"").slice(1);const query=new URLSearchParams(location.search).get("tab");return showTab(hash||query||"dashboard")}
function statusClass(e,v){if(!e)return;e.className="status "+(v==="CONNECTED"?"ok":v==="OFF"?"muted":"warn");e.textContent=v}
function fmt(v,s){return v===null||v===undefined||v===""?"-":String(v)+(s||"")}
function setRelay(r,i){
 text("relay-title-"+i,r.name); text("relay-state-"+i,r.state?"ACTIVE":"NORMAL"); text("relay-normal-"+i,r.normal);
 text("relay-mode-display-"+i,r.mode); text("relay-pulse-display-"+i,r.mode==="PULSE"?r.pulse+" ms":"-");
 $("relay"+i+"-name").value=r.name; $("relay"+i+"-normal").value=r.normal.toLowerCase(); $("relay"+i+"-mode").value=r.mode.toLowerCase(); $("relay"+i+"-pulse").value=r.pulse;
 text("dash-relay"+i+"-name",r.name); $("dash-relay"+i+"-state").innerHTML=r.state?'<span class="ok">ON</span>':'OFF';
}
let storageFiles=[];
let nvsEntries=[];

function formatBytes(bytes){
  const n=Number(bytes)||0;
  if(n<1024)return n+" B";
  if(n<1048576)return (n/1024).toFixed(n<10240?1:0)+" KB";
  return (n/1048576).toFixed(n<10485760?1:0)+" MB";
}

async function loadStorageFiles(){
  const e=$("storage-files");
  try{
    const r=await fetch("/api/storage/files",{cache:"no-store"});
    if(!r.ok)throw Error(r.status);
    const d=await r.json();
    storageFiles=d.files||[];
    const used=Number(d.used)||0,total=Number(d.total)||0;
    text("storage-summary-files",storageFiles.length);
    text("storage-summary-used",formatBytes(used));
    const pct=total>0?Math.min(100,used*100/total):0;
    const bar=$("storage-meter-bar");if(bar)bar.style.width=pct.toFixed(1)+"%";
    text("storage-meter-label",formatBytes(used)+" used of "+formatBytes(total)+" ("+pct.toFixed(1)+"%)");
    if(!e)return;
    if(!storageFiles.length){
      e.innerHTML='<div class="empty-state"><strong>Filesystem is empty</strong><span>Upload a Web UI asset to get started.</span></div>';
      return;
    }
    const rows=storageFiles.map(f=>{
      const path=encodeURIComponent(f.path);
      const canView=/\.(html?|css|js|json|txt|xml|svg)$/i.test(f.path);
      const ext=(f.path.split(".").pop()||"FILE").toUpperCase();
      return '<tr><td><div class="file-name mono">'+esc(f.path)+'</div><div class="file-type">'+esc(ext)+'</div></td><td class="file-size">'+formatBytes(f.size)+'</td><td class="file-actions">'+
        (canView?'<button type="button" class="secondary" data-storage-view="'+esc(f.path)+'">View</button>':"")+
        '<a href="/storage/download?path='+path+'"><button type="button" class="secondary">Download</button></a>'+
        '<button type="button" class="danger" data-storage-delete="'+esc(f.path)+'">Erase</button></td></tr>';
    }).join("");
    e.innerHTML='<table class="storage-table"><thead><tr><th>File</th><th>Size</th><th>Actions</th></tr></thead><tbody>'+rows+'</tbody></table>';
    e.querySelectorAll("[data-storage-view]").forEach(b=>b.addEventListener("click",()=>viewStorageFile(b.dataset.storageView)));
    e.querySelectorAll("[data-storage-delete]").forEach(b=>b.addEventListener("click",()=>eraseStorageFile(b.dataset.storageDelete)));
  }catch(x){
    storageFiles=[];
    text("storage-summary-files","-");text("storage-summary-used","-");
    if(e)e.innerHTML='<div class="empty-state"><strong>Web storage unavailable</strong><span>Check SPIFFS and try Refresh.</span></div>';
    setStorageStatus("Web storage unavailable.","bad");
  }
}

async function viewStorageFile(path){
  const viewer=$("storage-viewer"),title=$("storage-viewer-title"),content=$("storage-viewer-content");
  try{
    const r=await fetch("/storage/view?path="+encodeURIComponent(path),{cache:"no-store"});
    if(!r.ok)throw Error(r.status);
    const value=await r.text();
    if(title)title.textContent=path;
    if(content)content.textContent=value;
    if(viewer){viewer.hidden=false;viewer.scrollIntoView({behavior:"smooth",block:"nearest"});}
  }catch(x){setStorageStatus("File could not be viewed.","bad")}
}
function closeStorageViewer(){const viewer=$("storage-viewer");if(viewer)viewer.hidden=true}

async function eraseStorageFile(path){
  const protectedFile=/^\/(index\.html|style\.css|app\.js)$/i.test(path);
  const warning=protectedFile?"\n\nThis is a core Web UI file. Erasing it can make the normal UI unusable; Recovery Updater will still be available.":"";
  if(!confirm("Erase "+path+"? This cannot be undone."+warning))return;
  try{
    const r=await fetch("/storage/delete",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"path="+encodeURIComponent(path),cache:"no-store"});
    if(!r.ok)throw Error(r.status);
    closeStorageViewer();setStorageStatus("File erased.","ok");await loadStorageFiles();
  }catch(x){setStorageStatus("File could not be erased.","bad")}
}
function setStorageStatus(message,kind){
  const e=$("storage-status");if(!e)return;
  e.textContent=message;e.className="help "+(kind||"");
}

function renderNvsEntries(){
  const root=$("nvs-table"),query=($("nvs-filter")?.value||"").trim().toLowerCase();
  if(!root)return;
  const groups={};
  nvsEntries.forEach(e=>{
    const hay=[e.namespace,e.key,e.type,e.value].join(" ").toLowerCase();
    if(query&&!hay.includes(query))return;
    (groups[e.namespace]||(groups[e.namespace]=[])).push(e);
  });
  const names=Object.keys(groups).sort();
  if(!names.length){
    root.innerHTML='<div class="empty-state"><strong>No matching NVS entries</strong><span>Try a different filter.</span></div>';
    return;
  }
  root.innerHTML=names.map(ns=>{
    const entries=groups[ns];
    const rows=entries.map(e=>'<tr><td class="mono">'+esc(e.key)+'</td><td>'+esc(e.type)+'</td><td class="mono nvs-value">'+esc(e.value)+'</td></tr>').join("");
    return '<details class="nvs-namespace">'+'<summary><span class="nvs-namespace-name">'+esc(ns)+'</span><span class="nvs-namespace-count">'+entries.length+' '+(entries.length===1?"entry":"entries")+'</span></summary><div class="table-wrap"><table><thead><tr><th>Key</th><th>Type</th><th>Value</th></tr></thead><tbody>'+rows+'</tbody></table></div></details>';
  }).join("");
}

async function loadNvsContents(){
  const root=$("nvs-table");
  try{
    const [entriesResponse,statsResponse]=await Promise.all([
      fetch("/api/nvs",{cache:"no-store"}),
      fetch("/api/nvs/stats",{cache:"no-store"})
    ]);
    if(!entriesResponse.ok||!statsResponse.ok)throw Error("NVS unavailable");
    const n=await entriesResponse.json(),s=await statsResponse.json();
    nvsEntries=n.entries||[];
    text("nvs-size",formatBytes(n.size||0));
    text("nvs-used",s.usedEntries??"-");
    text("nvs-free",s.freeEntries??"-");
    text("nvs-namespaces",s.namespaceCount??"-");
    renderNvsEntries();
  }catch(x){
    text("nvs-size","Unavailable");text("nvs-used","-");text("nvs-free","-");text("nvs-namespaces","-");
    if(root)root.innerHTML='<div class="empty-state"><strong>NVS unavailable</strong><span>Configuration inspection could not be loaded.</span></div>';
  }
}

function render(s){
 const w=s.wifi,n=s.network,d=s.diagnostics;
 if(s.system&&s.system.theme!==undefined) applyTheme(s.system.theme);
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
 text("net-live-ip",w.connected?w.ip:"-");text("net-live-subnet",w.connected?w.subnet:"-");text("net-live-gateway",w.connected?w.gateway:"-");text("net-live-dns1",w.connected?n.dns1:"-");text("net-live-dns2",w.connected?n.dns2:"-");statusClass($("net-state"),w.status);text("net-auth",w.connected?w.auth:"-");text("net-bssid",w.connected?w.bssid:"-");text("net-rssi",w.connected?w.rssi+" dBm":"-");
 if(!networkFormDirty){
 $("hostname").value=n.hostname;$("net-mode").value=n.mode;$("net-ip-input").value=n.ip;$("net-gateway-input").value=n.gateway;$("net-subnet-input").value=n.subnet;$("net-dns1-input").value=n.dns1;$("net-dns2-input").value=n.dns2;
}
$("static-fields").style.display=$("net-mode").value==="static"?"grid":"none";
 text("diag-state",w.status);text("diag-auth",w.connected?w.auth:"-");text("diag-bssid",w.connected?w.bssid:"-");text("diag-rssi",w.connected?w.rssi+" dBm":"-");
 $("diagnostics-toggle").textContent=d.enabled?"Disable diagnostics":"Enable diagnostics";$("diagnostics-toggle").className=d.enabled?"danger":"good";
 setRelay(s.relays[0],0);setRelay(s.relays[1],1);

 text("dash-uptime",s.system.uptime+" seconds");text("dash-build",s.system.build);text("dash-idf",s.system.idf);text("dash-cpu",s.system.cpu+" MHz");
 text("sys-build",s.system.build);text("sys-web-build",s.system.webBuild||"0");const uiSelect=$("ui-selection");if(uiSelect){const selected=String(s.system.uiSelection||"builtin");uiSelect.dataset.savedSelection=selected;if(uiCatalogLoaded)uiSelect.value=[...uiSelect.options].some(o=>o.value===selected)?selected:"builtin";}text("sys-date",s.system.date);text("sys-idf",s.system.idf);text("sys-arduino",s.system.arduino);text("sys-cpu",s.system.cpu+" MHz");text("sys-uptime",s.system.uptime+" seconds");
 $("relay-power-button").textContent=s.relays[0].name;$("relay-reset-button").textContent=s.relays[1].name;
 text("page-status",w.status);
}
const THEME_NAMES=["Midnight","Ocean","Forest","Emerald","Sunset","Amber","Rose","Purple","Violet","Cyber","Slate","Coffee","Arctic","Sakura","Terminal","Solarized","Monochrome","Crimson","Indigo","Teal","Original"];
function applyTheme(value){
 const n=Math.max(0,Math.min(THEME_NAMES.length-1,Number(value)||0));
 document.body.dataset.theme=String(n);
 const button=$("theme-picker-button"),name=$("theme-picker-name");
 if(button)button.dataset.theme=String(n);
 if(name)name.textContent=THEME_NAMES[n];
 document.querySelectorAll(".theme-option").forEach(o=>{
   const selected=Number(o.dataset.themeValue)===n;
   o.classList.toggle("selected",selected);
   o.setAttribute("aria-selected",selected?"true":"false");
 });
}
function openThemeOptions(){
 const o=$("theme-options"),b=$("theme-picker-button");
 if(!o||!b)return;
 o.hidden=false;b.setAttribute("aria-expanded","true");
}
function closeThemeOptions(){
 const o=$("theme-options"),b=$("theme-picker-button");
 if(!o||!b)return;
 o.hidden=true;b.setAttribute("aria-expanded","false");
}
async function saveTheme(value){
 const status=$("theme-status");
 try{
   const r=await fetch("/system/theme",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"theme="+encodeURIComponent(value),cache:"no-store"});
   if(!r.ok)throw Error(r.status);
   applyTheme(value);
   if(status){status.textContent="Theme saved";status.className="help ok";}
 }catch(e){
   if(status){status.textContent="Theme could not be saved";status.className="help bad";}
 }
}

async function saveUiSelection(value){
 uiInitialFallback=false;
 const status=$("ui-selection-status");
 try{
   if(value!=="builtin"){
     const available=[...($("ui-selection")?.options||[])].some(o=>o.value===value);
     if(!available)throw Error("UI is not in the GitHub catalog");
   }
   if(status){status.textContent="Loading UI…";status.className="help";}
   const r=await fetch("/system/ui-selection",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"ui="+encodeURIComponent(value),cache:"no-store"});
   if(!r.ok)throw Error(r.status);
   // Persist the selection first. The browser then performs a clean page
   // reload so the selected UI is initialized from a completely fresh DOM.
   // This avoids stale theme scripts/CSS surviving a live swap.
   const select=$("ui-selection");
   if(select)select.dataset.savedSelection=String(value);
   if(status){
     status.textContent=value==="builtin"?"Built-in UI saved — reloading…":"UI saved — reloading…";
     status.className="help ok";
   }
   const reloadUrl=window.location.pathname+"?uiReload="+Date.now()+window.location.hash;
   window.location.replace(reloadUrl);
   return;
 }catch(e){
   if(status){status.textContent="UI selection failed: "+(e.message||"unknown error");status.className="help bad";}
   const select=$("ui-selection");
   if(select)select.value=String(select.dataset.savedSelection||"builtin");
 }
}
let diagnosticPollTimer=null;
let diagnosticPollBusy=false;
async function loadDiagnostics(){
 if(diagnosticPollBusy)return;
 diagnosticPollBusy=true;
 try{
   const r=await fetch("/api/diagnostics",{cache:"no-store"});
   if(!r.ok)throw Error(r.status);
   const d=await r.json();
   const area=$("diagnostic-console"),status=$("diagnostic-console-status");
   if(!area)return;
   const nearBottom=area.scrollHeight-area.scrollTop-area.clientHeight<40;
   area.value=(d.lines||[]).join("\n");
   if(nearBottom)area.scrollTop=area.scrollHeight;
   if(status)status.textContent=d.enabled?"Live — showing the latest 20 diagnostic lines":"Diagnostics are disabled";
 }catch(e){
   const status=$("diagnostic-console-status");
   if(status)status.textContent="Diagnostic stream unavailable";
 }finally{ diagnosticPollBusy=false; }
}
function startDiagnosticPolling(){
 if(diagnosticPollTimer!==null)return;
 loadDiagnostics();
 diagnosticPollTimer=setInterval(loadDiagnostics,500);
}
function stopDiagnosticPolling(){
 if(diagnosticPollTimer===null)return;
 clearInterval(diagnosticPollTimer);
 diagnosticPollTimer=null;
}
async function load(applyExternalUi=true){
 if(otaMonitorRunning||otaUpdateStarting)return;
 try{
   const r=await fetch("/api/state",{cache:"no-store"});
   if(!r.ok)throw new Error(r.status);
   const state=await r.json();
   stateFailureCount=0;
   render(state);
   if(!applyExternalUi)return;
   const selectedUi=String(state.system?.uiSelection||"builtin");
   // A failed external UI load must never block the built-in UI or cause
   // repeated GitHub requests during the normal 5-second state poll.
   if(selectedUi!=="builtin" && uiInitialFallback)return;
   if(selectedUi!==activeExternalUi){
     const loaded=await activateUi(selectedUi);
     if(!loaded && selectedUi!=="builtin"){
       const status=$("ui-selection-status");
       if(status){status.textContent="Saved UI unavailable — using Built-in";status.className="help bad";}
     }
   }
 }catch(e){
   stateFailureCount++;
   if(stateFailureCount>=STATE_FAILURE_THRESHOLD){
     setPageStatus("Web UI state unavailable — try the Recovery Updater.","bad");
   }else{
     setPageStatus("UNAVAILABLE","bad");
   }
 }
}
document.querySelectorAll("[data-tab-link]").forEach(e=>e.addEventListener("click",()=>showTab(e.dataset.tabLink)));
window.addEventListener("hashchange",()=>{const tab=(location.hash||"").slice(1);if(tab)showTab(tab)});
$("storage-refresh")?.addEventListener("click",loadStorageFiles);
$("nvs-refresh")?.addEventListener("click",loadNvsContents);
$("nvs-filter")?.addEventListener("input",renderNvsEntries);
$("storage-viewer-close")?.addEventListener("click",closeStorageViewer);
$("storage-upload-form")?.addEventListener("submit",async e=>{
  e.preventDefault();
  const form=e.currentTarget;
  const input=$("storage-file");
  const file=input?.files?.[0];
  if(!file)return;
  const target="/"+file.name.replace(/\\/g,"/");
  const existing=storageFiles.find(f=>f.path===target);
  if(existing&&!confirm("Replace "+target+"? The existing filesystem file will be erased and replaced. This cannot be undone."))return;
  try{
    setStorageStatus(existing?"Replacing file…":"Uploading…");
    const r=await fetch(form.action,{method:"POST",body:new FormData(form),cache:"no-store"});
    if(!r.ok)throw Error(r.status);
    form.reset();setStorageStatus(existing?"File replaced.":"File uploaded.","ok");await loadStorageFiles();
  }catch(x){setStorageStatus("File upload failed.","bad")}
});
$("configfile")?.addEventListener("change",e=>{
  const file=e.currentTarget.files?.[0];
  const label=$("configfile-name");
  if(label)label.textContent=file?file.name:"Choose backup file…";
});
$("theme-picker-button").addEventListener("click",()=>{const o=$("theme-options");if(o.hidden)openThemeOptions();else closeThemeOptions();});const uiSelect=$("ui-selection");if(uiSelect)uiSelect.addEventListener("change",()=>saveUiSelection(uiSelect.value));
document.querySelectorAll(".theme-option").forEach(o=>o.addEventListener("click",async()=>{closeThemeOptions();await saveTheme(o.dataset.themeValue);}));
document.addEventListener("click",e=>{const p=document.querySelector(".theme-picker");if(p&&!p.contains(e.target))closeThemeOptions();});
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
function setPageStatus(m,k){const e=$("page-status");if(!e)return; if(m==="Web UI state unavailable — try the Recovery Updater."){e.innerHTML='Web UI state unavailable — <a href="/recovery" target="_blank" rel="noopener">Open Recovery Updater</a>';e.className=k||"";return;} e.textContent=m;e.className=k||""}
function setOtaUpdateButtonDisabled(disabled){
 const b=$("update-latest-button");
 if(!b)return;
 b.disabled=disabled;
 b.textContent=disabled?"Update in progress…":"Check for and install updates";
}
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
 msg.hidden=false;
 msg.className="notice "+(kind||"");
 msg.textContent=message||"";
}
async function readOtaStatus(){
 const r=await fetch("/system/update-status",{cache:"no-store"});
 const d=await r.json();
 if(!r.ok)throw new Error(d?.message||"Could not read OTA status.");
 return d;
}
function renderOtaStatus(d){
 const progress=$("software-update-progress"),stage=$("ota-progress-stage"),info=$("ota-progress-info");
 const firmwareRow=$("ota-progress-firmware"),webRow=$("ota-progress-web");
 const firmwareBar=$("ota-firmware-bar"),webBar=$("ota-web-bar");
 const firmwareInfo=$("ota-firmware-info"),webInfo=$("ota-web-info");
 const message=$("software-update-message"),details=$("software-update-details");
 if(!progress)return;
 progress.hidden=false;
 if(details){
   details.hidden=false;
   const cf=d.currentFirmware,lf=d.latestFirmware,cw=d.currentWeb,lw=d.latestWeb;
   text("update-firmware-detail",cf>=0 ? "Build "+cf+" → "+(lf>=0?lf:"unavailable")+(lf>cf?" (update available)":lf===cf?" (current)":"") : "-");
   text("update-web-detail",cw>=0 ? "Build "+cw+" → "+(lw>=0?lw:"unavailable")+(lw>cw?" (update available)":lw===cw?" (current)":"") : "-");
 }

 const firmwareUpdatePending=d.currentFirmware>=0&&d.latestFirmware>d.currentFirmware;
 const webUpdatePending=d.currentWeb>=0&&d.latestWeb>d.currentWeb;

 if(d.component==="firmware")firmwareRow.hidden=false;
 if(d.component==="web"){
   webRow.hidden=false;
   if(firmwareUpdatePending){
     firmwareRow.hidden=false;
     firmwareBar.classList.remove("indeterminate");
     firmwareBar.style.width="100%";
     firmwareInfo.textContent="100% — firmware update complete";
   }
 }

 const bar=d.component==="web"?webBar:firmwareBar;
 const row=d.component==="web"?webRow:firmwareRow;
 const detail=d.component==="web"?webInfo:firmwareInfo;
 if(d.component){
   row.hidden=false;
   if(d.total>0){
     const complete=d.received>=d.total;
     const percent=complete?100:Math.min(100,Math.max(0,(d.received/d.total)*100));
     bar.classList.remove("indeterminate");
     bar.style.width=percent+"%";
     detail.textContent=d.received.toLocaleString()+" / "+d.total.toLocaleString()+" bytes ("+Math.round(percent)+"%)";
   }else{
     bar.classList.add("indeterminate");
     detail.textContent=d.received?d.received.toLocaleString()+" bytes received":"Waiting for download size…";
   }
 }

 let title="";
 switch(d.stage){
   case "checking": title="Checking GitHub for updates"; break;
   case "downloading": title="Downloading "+(d.component==="web"?"Web UI":"Firmware"); break;
   case "writing": title="Writing "+(d.component==="web"?"Web UI":"Firmware"); break;
   case "finalizing": title="Finalizing updates"; break;
   case "rebooting": title="Rebooting ESP32-C3"; break;
   case "complete": title="Update complete"; break;
   case "error": title="Update failed"; break;
   default: title=d.message||"Updating";
 }
 stage.textContent=title;

 if(d.stage==="error"){
   firmwareBar.classList.remove("indeterminate");
   webBar.classList.remove("indeterminate");
   message.hidden=false;
   message.className="notice bad";
   message.textContent=d.message||"OTA update failed.";
   setPageStatus("OTA update failed","bad");
 }else if(d.stage==="rebooting"||d.stage==="complete"||d.stage==="finalizing"){
   firmwareBar.classList.remove("indeterminate");
   webBar.classList.remove("indeterminate");
   if(!firmwareRow.hidden)firmwareBar.style.width="100%";
   if(!webRow.hidden)webBar.style.width="100%";
   if(d.stage==="rebooting"){
     message.hidden=false;
     message.className="notice ok";
     message.textContent=d.message||"Updates installed successfully. Rebooting…";
     setPageStatus("Rebooting…","ok");
   }else if(d.stage==="complete"){
     message.hidden=false;
     message.className="notice ok";
     message.textContent=d.message||"Update complete.";
     setPageStatus("Update complete","ok");
   }else{
     message.hidden=true;
     setPageStatus("Finalizing updates…","warn");
   }
 }else if(d.active){
   message.hidden=true;
   setPageStatus(title+"…","warn");
 }else{
   message.hidden=true;
 }
 info.textContent=d.stage==="checking" ? (d.message||"Checking GitHub for newer firmware and Web UI components…") : "";
}
let otaMonitorRunning=false;
let otaUpdateStarting=false;
let otaWasActive=false;
let otaExpectedFirmware=-1;
let otaExpectedWeb=-1;

async function monitorOtaStatus(){
 if(otaMonitorRunning)return null;
 otaMonitorRunning=true;
 otaWasActive=true;
 try{
   while(true){
     try{
       const d=await readOtaStatus();

       if(d.latestFirmware>=0)otaExpectedFirmware=d.latestFirmware;
       if(d.latestWeb>=0)otaExpectedWeb=d.latestWeb;

       if(d.stage==="idle" && otaWasActive){
         setOtaUpdateButtonDisabled(false);
         setPageStatus("ESP32-C3 rebooted; returning to the home page…","warn");
         location.replace("/");
         return d;
       }

       renderOtaStatus(d);
       if(d.stage==="error"||d.stage==="complete"){
       setOtaUpdateButtonDisabled(false);
       return d;
     }
     if(d.stage==="rebooting"){
       setOtaUpdateButtonDisabled(true);
       await waitForRebootAndReset();
       return d;
     }
     }catch(x){
       setPageStatus("ESP32-C3 connection lost — waiting for it to reconnect…","warn");
       await new Promise(resolve=>setTimeout(resolve,1000));
       continue;
     }
     await new Promise(resolve=>setTimeout(resolve,500));
   }
 }finally{
   otaMonitorRunning=false;
 }
}
async function latest(e){
 e.preventDefault();
 const button=$("update-latest-button");
 if(button?.disabled)return;
 setOtaUpdateButtonDisabled(true);
 otaUpdateStarting=true;
 setPageStatus("Installing updates…","warn");
 showUpdateStatus("", "warn");
 $("software-update-message").hidden=true;
 $("software-update-details").hidden=true;
 $("software-update-progress").hidden=false;
 const progress=$("software-update-progress"),stage=$("ota-progress-stage"),info=$("ota-progress-info");
 const firmwareRow=$("ota-progress-firmware"),webRow=$("ota-progress-web"),firmwareBar=$("ota-firmware-bar"),webBar=$("ota-web-bar");
 progress.hidden=false;
 firmwareRow.hidden=true;
 webRow.hidden=true;
 firmwareBar.style.width="0%";
 webBar.style.width="0%";
 firmwareBar.classList.add("indeterminate");
 webBar.classList.add("indeterminate");
 stage.textContent="Starting OTA";
 info.textContent="The ESP32 will continue the update independently of this browser window.";
 try{
   const r=await fetch(e.currentTarget.action,{method:"POST",cache:"no-store"});
   let d={};
   try{d=await r.json()}catch(x){}
   if(!r.ok)throw new Error(d?.message||"OTA request failed (HTTP "+r.status+").");
 }catch(x){
   otaUpdateStarting=false;
   setOtaUpdateButtonDisabled(false);
   showUpdateStatus(x.message||"Update request failed.","bad");
   setPageStatus("Update request failed","bad");
   return;
 }
 otaUpdateStarting=false;
 await monitorOtaStatus();
}
async function resumeOtaStatus(){
 try{
   const d=await readOtaStatus();
   if(d.active||["rebooting","complete","error"].includes(d.stage)){
     setOtaUpdateButtonDisabled(d.active||d.stage==="rebooting");
     $("software-update-message").hidden=true;
     $("software-update-details").hidden=false;
     $("software-update-progress").hidden=false;
     if(d.latestFirmware>=0)otaExpectedFirmware=d.latestFirmware;
     if(d.latestWeb>=0)otaExpectedWeb=d.latestWeb;
     renderOtaStatus(d);
     if(d.active)await monitorOtaStatus();
   }
 }catch(x){}
}
async function waitForRebootAndReset(){
 setPageStatus("ESP32-C3 rebooting — returning to the home page…","warn");
 for(let i=0;i<30;i++){
   await new Promise(resolve=>setTimeout(resolve,1000));
   try{
     const r=await fetch("/api/state",{cache:"no-store"});
     if(r.ok){
       location.replace("/");
       return;
     }
   }catch(x){}
 }
 location.replace("/");
}
async function upload(e,q){
 e.preventDefault();
 if(q&&!confirm(q))return;
 setPageStatus("Uploading…");
 try{
   const d=await pfu(e.currentTarget);
   setPageStatus(d?.message||"Operation completed.","ok");
   if(["firmware-upload-form","web-upload-form","config-restore-form"].includes(e.currentTarget.id)){
     await waitForRebootAndReset();
   }
 }catch(x){
   if(["firmware-upload-form","web-upload-form","config-restore-form"].includes(e.currentTarget.id)){
     await waitForRebootAndReset();
   }else{
     setPageStatus("Operation failed or the ESP32-C3 rebooted.","bad");
   }
 }
}
document.querySelectorAll('form[method="POST"]').forEach(f=>{
 if(["wifi-scan-form","update-latest-form","firmware-upload-form","web-upload-form","config-restore-form"].includes(f.id))return;
 f.addEventListener("submit",async e=>{
   e.preventDefault();
   try{
     await pf(f);
     if(f.action.endsWith("/system/reboot")){
       await waitForRebootAndReset();
       return;
     }
     setPageStatus("Saved","ok");
     setTimeout(load,300);
   }catch(x){
     if(f.action.endsWith("/system/reboot")){
       await waitForRebootAndReset();
       return;
     }
     setPageStatus("Request failed","bad");
   }
 });
});
$("wifi-scan-form").addEventListener("submit",scan);$("update-latest-form").addEventListener("submit",latest);
$("firmware-upload-form").addEventListener("submit",e=>{
  const file=$("firmware")?.files?.[0];
  if(!file)return;
  if(!/\.bin$/i.test(file.name)){e.preventDefault();setPageStatus("Firmware image must be a .bin file.","bad");return;}
  upload(e,"Upgrade the firmware with "+file.name+"? The current firmware will be replaced and the ESP32-C3 will reboot. NVS configuration will be preserved.");
});
$("web-upload-form").addEventListener("submit",e=>{
  const file=$("web-filesystem")?.files?.[0];
  if(!file)return;
  if(!/-spiffs\.bin$/i.test(file.name)){e.preventDefault();setPageStatus("Web UI image must use the -spiffs.bin filename suffix.","bad");return;}
  upload(e,"Upgrade the Web UI filesystem with "+file.name+"? The current Web UI filesystem will be replaced and the ESP32-C3 will reboot.");
});
$("config-restore-form").addEventListener("submit",e=>upload(e,"Restore this configuration and reboot the ESP32-C3?"));
$("firmware").addEventListener("change",e=>{const f=e.currentTarget.files?.[0];const n=$("firmware-name");if(n)n.textContent=f?f.name:"Choose firmware .bin…";});
$("web-filesystem").addEventListener("change",e=>{const f=e.currentTarget.files?.[0];const n=$("web-filesystem-name");if(n)n.textContent=f?f.name:"Choose Web UI -spiffs.bin…";});
window.addEventListener("hashchange",currentTab);
currentTab();
$("net-mode").addEventListener("change",()=>{$("static-fields").style.display=$("net-mode").value==="static"?"grid":"none"});
async function bootstrapPage(){
  // The built-in UI is the guaranteed local fallback. Render it first;
  // GitHub-hosted themes are strictly a background enhancement.
  await load(false);
  uiInitialLoad=false;
  document.documentElement.classList.remove("ui-boot-pending");

  // Never let GitHub availability hold the page hostage.
  try{
    await loadUiCatalog();
    await load(true);
  }catch(e){
    console.warn("External UI bootstrap failed; continuing with built-in UI:",e);
  }

  await resumeOtaStatus();
}
bootstrapPage();
setInterval(load,5000);
document.addEventListener("visibilitychange",()=>{
 if(!document.hidden)resumeOtaStatus();
});
})();
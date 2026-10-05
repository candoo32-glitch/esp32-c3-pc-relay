/* STEAMPUNK FOUNDRY interaction layer
 * Existing DOM/API remains the source of truth. This file builds a physical-machine
 * presentation and routes its controls back to the existing application controls.
 */
(()=>{
  "use strict";
  const $=id=>document.getElementById(id);
  const q=s=>document.querySelector(s);
  const qa=s=>[...document.querySelectorAll(s)];

  const click=id=>{const e=$(id);if(e)e.click()};
  const formSubmit=(form,button)=>{
    if(!form)return;
    if(typeof form.requestSubmit==="function") form.requestSubmit(button||form.querySelector("button[type=submit]")||undefined);
    else if(button) button.click();
  };
  const esc=v=>String(v??"").replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));

  function addMachinery(){
    if(q(".sp-machinery"))return;
    const m=document.createElement("div");
    m.className="sp-machinery";
    m.innerHTML='<div class="sp-tube left"></div><div class="sp-tube right"></div><div class="sp-gear g1"></div><div class="sp-gear g2"></div>';
    document.body.appendChild(m);
    const meta=document.createElement("div");
    meta.className="sp-header-meta";
    meta.innerHTML='<div class="sp-meta-build"><span>BUILD</span><b id="sp-header-build">—</b></div><div class="sp-meta-uptime"><span>UPTIME</span><b id="sp-header-uptime">—</b></div><div class="sp-meta-clock"><span id="sp-header-clock">—</span></div>';
    document.body.appendChild(meta);
  }

  function labelNavigation(){
    const labels={dashboard:"Control Room",wifi:"Wireless Telegraph",network:"Network Exchange",diagnostics:"Engineer's Log",relays:"Relay Works",storage:"Archive Cabinet",system:"Master Clock"};
    const icons={dashboard:"◉",wifi:"⌁",network:"◎",diagnostics:"✣",relays:"⚙",storage:"▤",system:"⏱"};
    qa("[data-tab-link]").forEach(a=>{
      const id=a.dataset.tabLink;
      if(labels[id])a.textContent=labels[id];
      a.dataset.spIcon=icons[id]||"•";
    });
  }

  function buildDashboard(){
    const tab=$("#tab-dashboard"), grid=tab?.querySelector(".grid");
    if(!tab||!grid||q(".sp-dashboard-machinery"))return;
    const wifiCard=grid.children[0], systemCard=grid.children[1];
    const relayCard=[...tab.children].find(e=>e.classList?.contains("card")&&e!==grid);
    const shell=document.createElement("div");
    shell.className="sp-dashboard-machinery";
    const instruments=document.createElement("div");
    instruments.className="sp-instrument-bank";
    instruments.innerHTML='<div class="sp-gauge-wrap"><div class="sp-gauge" id="sp-gauge-rssi" style="--needle:-42deg"><span class="sp-gauge-label">SIGNAL</span><span class="sp-gauge-value" id="sp-gauge-rssi-value">— dBm</span></div></div>'+
      '<div class="sp-gauge-wrap"><div class="sp-gauge" id="sp-gauge-cpu" style="--needle:-38deg"><span class="sp-gauge-label">CPU CLOCK</span><span class="sp-gauge-value" id="sp-gauge-cpu-value">— MHz</span></div></div>'+
      '<div class="sp-gauge-wrap"><div class="sp-gauge" id="sp-gauge-up" style="--needle:-20deg"><span class="sp-gauge-label">UPTIME</span><span class="sp-gauge-value" id="sp-gauge-up-value">—</span></div></div>';

    const center=document.createElement("div");
    center.className="sp-machine-screen";
    center.innerHTML='<div class="sp-screen-title"><span class="sp-led" id="sp-online-led"></span> CENTRAL AETHERIC TELEMETRY</div>';
    const dataWrap=document.createElement("div");
    dataWrap.className="sp-dashboard-data";
    dataWrap.append(wifiCard,systemCard);
    center.appendChild(dataWrap);

    const right=document.createElement("div");
    right.className="sp-machine-screen";
    right.innerHTML='<div class="sp-screen-title">RELAY STATUS</div>';
    const relayReadout=document.createElement("div");
    relayReadout.className="sp-relay-readout";
    relayReadout.innerHTML='<div class="sp-mech-control" id="sp-dash-relay0">RELAY 1 · NORMAL</div>'+
      '<div class="sp-mech-control" id="sp-dash-relay1">RELAY 2 · NORMAL</div>'+
      '<div style="margin-top:18px;color:#9f8054;font:600 .68rem Georgia,serif;line-height:1.7">Remote activation remains available from the brass relay controls above. This panel mirrors live device state.</div>';
    right.appendChild(relayReadout);
    shell.append(instruments,center,right);
    grid.replaceWith(shell);
    if(relayCard)relayCard.remove();
  }

  function buildRadioConsole(){
    const tab=$("#tab-wifi");
    if(!tab||q(".sp-radio-console"))return;
    const host=document.createElement("div");
    host.className="card sp-radio-console";
    host.innerHTML='<div class="sp-radio-face"><div class="sp-screen-title">WIRELESS TELEGRAPH</div><div class="sp-radio-lamp"><span class="sp-led" id="sp-wifi-led"></span><strong id="sp-wifi-state">OFFLINE</strong></div><div class="sp-radio-reading"><span>SSID</span><b id="sp-wifi-ssid">—</b></div><div class="sp-radio-reading"><span>RSSI</span><b id="sp-wifi-rssi">—</b></div></div>'+
      '<div class="sp-radio-control"><div class="sp-control-caption">WIRELESS POWER</div><div class="sp-lever" id="sp-wifi-lever" role="button" tabindex="0"><span></span><b>OFF</b></div></div>'+
      '<div class="sp-radio-control"><div class="sp-control-caption">SPECTRUM SCANNER</div><div class="sp-mech-control" id="sp-scan-lever">PULL TO SCAN</div><div class="help" id="sp-scan-readout">Ready.</div></div>';
    tab.insertBefore(host,tab.firstElementChild);
    const toggle=$("wifi-toggle"), reconnect=$("#wifi-reconnect-form");
    if(toggle?.parentElement)toggle.parentElement.classList.add("sp-hidden-backend");
    if(reconnect)reconnect.classList.add("sp-hidden-backend");
    const act=()=>{
      const text=(toggle?.textContent||"").toUpperCase();
      click("wifi-toggle");
      if(text.includes("OFF"))setTimeout(()=>{const b=q("#wifi-reconnect-form button");if(b)b.click()},450);
    };
    const lever=$("#sp-wifi-lever");
    lever?.addEventListener("click",act);
    lever?.addEventListener("keydown",e=>{if(e.key==="Enter"||e.key===" "){e.preventDefault();act()}});
    $("sp-scan-lever")?.addEventListener("click",()=>{
      const f=$("#wifi-scan-form");if(!f)return;
      $("sp-scan-readout").textContent="Scanning ether…";
      formSubmit(f);
      setTimeout(()=>{$("sp-scan-readout").textContent="Scan complete / awaiting next pull."},4500);
    });
  }

  function buildNetworkConsole(){
    const tab=$("#tab-network"), form=tab?.querySelector('form[action="/network/save"]');
    if(!form||q(".sp-network-console"))return;
    const panel=document.createElement("div");
    panel.className="card sp-network-console";
    panel.innerHTML='<div class="sp-screen-title">ADDRESSING ENGINE</div>'+
      '<div class="sp-network-readout"><div><span>LIVE ADDRESS</span><b id="sp-net-ip">—</b></div><div><span>GATEWAY</span><b id="sp-net-gateway">—</b></div><div><span>DNS</span><b id="sp-net-dns">—</b></div></div>'+
      '<div class="sp-mode-machine"><span>ADDRESS MODE</span><div class="sp-mode-lever" id="sp-mode-lever" role="button" tabindex="0"><i></i><b>DHCP</b></div></div>';
    tab.insertBefore(panel,tab.firstElementChild);
    const select=$("#net-mode"), lever=$("#sp-mode-lever");
    if(select)select.classList.add("sp-backend-select");
    const sync=()=>{
      const v=select?.value==="static"?"STATIC":"DHCP";
      if(lever){lever.classList.toggle("active",v==="static");lever.querySelector("b").textContent=v}
    };
    const toggle=()=>{
      if(!select)return;
      select.value=select.value==="static"?"dhcp":"static";
      select.dispatchEvent(new Event("change",{bubbles:true}));
      sync();
    };
    lever?.addEventListener("click",toggle);
    lever?.addEventListener("keydown",e=>{if(e.key==="Enter"||e.key===" "){e.preventDefault();toggle()}});
    sync();
  }

  function relayCard(id){
    const name=$("#relay"+id+"-name"), normal=$("#relay"+id+"-normal"), mode=$("#relay"+id+"-mode"), pulse=$("#relay"+id+"-pulse"), title=$("#relay-title-"+id);
    if(!name||!normal||!mode||!pulse||!title)return null;
    const card=document.createElement("div");
    card.className="sp-relay-machine";
    card.dataset.relay=id;
    card.innerHTML='<div class="sp-relay-coil"></div>'+
      '<div class="sp-relay-name" id="sp-relay-name-'+id+'">'+esc(name.value||title.textContent)+'</div>'+
      '<div class="sp-relay-state" id="sp-relay-state-'+id+'" role="button" tabindex="0">NORMAL</div>'+
      '<div class="sp-control-row"><div class="sp-mech-control" id="sp-relay-normal-'+id+'">CONTACT: OPEN</div><div class="sp-mech-control" id="sp-relay-mode-'+id+'">LATCHED</div></div>'+
      '<div class="sp-dial" id="sp-pulse-dial-'+id+'" role="slider" tabindex="0" aria-label="Pulse duration"></div>'+
      '<div class="sp-dial-value" id="sp-pulse-value-'+id+'">250 ms</div>'+
      '<input class="sp-relay-name-input" id="sp-relay-name-input-'+id+'" maxlength="32" aria-label="Relay name">'+
      '<div class="sp-mech-control sp-commit" id="sp-relay-save-'+id+'">Commit relay settings</div>';

    const sync=()=>{
      const nm=name.value||title.textContent||("Relay "+(id+1));
      const state=($("#relay-state-"+id)?.textContent||"NORMAL").trim().toUpperCase();
      const n=normal.value==="closed"?"CLOSED":"OPEN";
      const mo=mode.value==="pulse"?"PULSE":"LATCHED";
      const pv=Math.max(10,Math.min(60000,Number(pulse.value)||250));
      const ne=$("#sp-relay-name-"+id), ni=$("#sp-relay-name-input-"+id), st=$("#sp-relay-state-"+id);
      if(ne)ne.textContent=nm;
      if(ni&&document.activeElement!==ni)ni.value=nm;
      if(st){st.textContent=state==="ACTIVE"?"ACTIVATE":"NORMAL";st.classList.toggle("on",state==="ACTIVE")}
      const cn=$("#sp-relay-normal-"+id);if(cn){cn.textContent="CONTACT: "+n;cn.classList.toggle("active",n==="CLOSED")}
      const cm=$("#sp-relay-mode-"+id);if(cm){cm.textContent=mo;cm.classList.toggle("active",mo==="PULSE")}
      const pvEl=$("#sp-pulse-value-"+id);if(pvEl)pvEl.textContent=pv+" ms";
      const dial=$("#sp-pulse-dial-"+id);if(dial)dial.style.setProperty("--dial-angle",(-135+(pv-10)/59990*270)+"deg");
    };
    const setPulse=v=>{pulse.value=String(Math.round(Math.max(10,Math.min(60000,v))));sync()};
    const dial=$("#sp-pulse-dial-"+id);
    let drag=null;
    dial.addEventListener("pointerdown",e=>{drag={y:e.clientY,value:Number(pulse.value)||250};dial.setPointerCapture(e.pointerId)});
    dial.addEventListener("pointermove",e=>{if(drag)setPulse(drag.value+(drag.y-e.clientY)*75)});
    dial.addEventListener("pointerup",()=>{drag=null});
    dial.addEventListener("pointercancel",()=>{drag=null});
    dial.addEventListener("wheel",e=>{e.preventDefault();setPulse((Number(pulse.value)||250)+(e.deltaY<0?50:-50))},{passive:false});
    dial.addEventListener("keydown",e=>{if(e.key==="ArrowUp"){e.preventDefault();setPulse((Number(pulse.value)||250)+50)}if(e.key==="ArrowDown"){e.preventDefault();setPulse((Number(pulse.value)||250)-50)}});
    $("#sp-relay-normal-"+id).addEventListener("click",()=>{normal.value=normal.value==="open"?"closed":"open";sync()});
    $("#sp-relay-mode-"+id).addEventListener("click",()=>{mode.value=mode.value==="latched"?"pulse":"latched";sync()});
    $("#sp-relay-state-"+id).addEventListener("click",()=>click(id===0?"relay-power-button":"relay-reset-button"));
    $("#sp-relay-state-"+id).addEventListener("keydown",e=>{if(e.key==="Enter"||e.key===" "){e.preventDefault();click(id===0?"relay-power-button":"relay-reset-button")}});
    $("#sp-relay-name-input-"+id).addEventListener("input",e=>{name.value=e.target.value;title.textContent=e.target.value||("Relay "+(id+1))});
    $("#sp-relay-save-"+id).addEventListener("click",()=>{
      const form=q('#tab-relays form[action="/relay/config"]');
      if(form)formSubmit(form);
    });
    sync();
    return card;
  }

  function buildRelayConsole(){
    const tab=$("#tab-relays"), form=tab?.querySelector('form[action="/relay/config"]');
    if(!form||q(".sp-relay-console"))return;
    const intro=tab.querySelector(".card");
    const console=document.createElement("div");
    console.className="sp-relay-console";
    const a=relayCard(0),b=relayCard(1);
    if(a)console.appendChild(a);if(b)console.appendChild(b);
    if(intro)intro.after(console);else tab.prepend(console);
    form.classList.add("sp-hidden-backend");
  }

  function updateInstruments(){
    const status=($("#dash-wifi-status")?.textContent||"").trim().toUpperCase();
    const rssi=parseFloat($("#dash-rssi")?.textContent||"");
    const cpu=parseFloat($("#dash-cpu")?.textContent||"");
    const up=parseFloat($("#dash-uptime")?.textContent||"");
    const rssiVal=Number.isFinite(rssi)?Math.max(-100,Math.min(-20,rssi)):null;
    const cpuVal=Number.isFinite(cpu)?cpu:null;
    const upVal=Number.isFinite(up)?up:null;
    const rv=$("sp-gauge-rssi-value"),cv=$("sp-gauge-cpu-value"),uv=$("sp-gauge-up-value");
    if(rv)rv.textContent=rssiVal===null?"— dBm":Math.round(rssiVal)+" dBm";
    if(cv)cv.textContent=cpuVal===null?"— MHz":Math.round(cpuVal)+" MHz";
    if(uv){
      if(upVal===null)uv.textContent="—";
      else{const h=Math.floor(upVal/3600),m=Math.floor((upVal%3600)/60);uv.textContent=(h?String(h)+"h ":"")+String(m).padStart(2,"0")+"m"}
    }
    const rg=$("sp-gauge-rssi");if(rg&&rssiVal!==null)rg.style.setProperty("--needle",(-135+(rssiVal+100)/80*270)+"deg");
    const cg=$("sp-gauge-cpu");if(cg&&cpuVal!==null)cg.style.setProperty("--needle",(-135+Math.max(0,Math.min(240,cpuVal))/240*270)+"deg");
    const ug=$("sp-gauge-up");if(ug&&upVal!==null)ug.style.setProperty("--needle",(-135+(upVal%86400)/86400*270)+"deg");
    const led=$("sp-online-led");if(led)led.classList.toggle("on",status==="CONNECTED");
    const wf=$("sp-wifi-state"),ws=$("sp-wifi-ssid"),wr=$("sp-wifi-rssi");
    if(wf)wf.textContent=status||"OFFLINE";
    if(ws)ws.textContent=$("#dash-ssid")?.textContent||"—";
    if(wr)wr.textContent=Number.isFinite(rssi)?Math.round(rssi)+" dBm":"—";
    const wled=$("sp-wifi-led");if(wled)wled.classList.toggle("on",status==="CONNECTED");
    const wl=$("sp-wifi-lever"),wt=$("#wifi-toggle")?.textContent||"";
    if(wl){const enabled=!wt.toUpperCase().includes("TURN WI-FI ON");wl.classList.toggle("active",enabled);wl.querySelector("b").textContent=enabled?"ON":"OFF"}
    const nip=$("sp-net-ip"),ng=$("sp-net-gateway"),nd=$("sp-net-dns");
    if(nip)nip.textContent=$("#net-live-ip")?.textContent||"—";
    if(ng)ng.textContent=$("#net-live-gateway")?.textContent||"—";
    if(nd)nd.textContent=$("#net-live-dns1")?.textContent||"—";
    const hb=$("sp-header-build"),hu=$("sp-header-uptime"),hc=$("sp-header-clock");
    if(hb)hb.textContent=$("#dash-build")?.textContent||"—";
    if(hu)hu.textContent=$("#dash-uptime")?.textContent||"—";
    if(hc)hc.textContent=new Date().toLocaleString(undefined,{month:"short",day:"2-digit",year:"numeric",hour:"2-digit",minute:"2-digit",second:"2-digit"}).toUpperCase();
    const di=$("sp-dash-relay0"),d2=$("sp-dash-relay1");
    [di,d2].forEach((el,i)=>{
      if(!el)return;
      const state=($("#relay-state-"+i)?.textContent||"NORMAL").trim().toUpperCase();
      const name=$("#relay-title-"+i)?.textContent||("RELAY "+(i+1));
      el.textContent=name+" · "+state;el.classList.toggle("active",state==="ACTIVE");
    });
    [0,1].forEach(i=>{
      const card=q('.sp-relay-machine[data-relay="'+i+'"]');
      if(!card)return;
      const name=$("#relay"+i+"-name"),state=$("#relay-state-"+i),mode=$("#relay"+i+"-mode"),normal=$("#relay"+i+"-normal"),pulse=$("#relay"+i+"-pulse");
      if(card.querySelector(".sp-relay-name"))card.querySelector(".sp-relay-name").textContent=name?.value||("Relay "+(i+1));
      const st=card.querySelector(".sp-relay-state");
      if(st){const on=(state?.textContent||"").trim().toUpperCase()==="ACTIVE";st.classList.toggle("on",on);st.textContent=on?"ACTIVATE":"NORMAL"}
      const cn=card.querySelector('[id^="sp-relay-normal-"]');if(cn)cn.textContent="CONTACT: "+(normal?.value==="closed"?"CLOSED":"OPEN");
      const cm=card.querySelector('[id^="sp-relay-mode-"]');if(cm)cm.textContent=(mode?.value==="pulse"?"PULSE":"LATCHED");
      const pv=card.querySelector(".sp-dial-value");if(pv)pv.textContent=(Number(pulse?.value)||250)+" ms";
    });
  }

  function init(){
    document.body.classList.add("steampunk-active");
    document.body.dataset.externalUi="steampunk";
    addMachinery();
    labelNavigation();
    buildDashboard();
    buildRadioConsole();
    buildNetworkConsole();
    buildRelayConsole();
    updateInstruments();
    setInterval(updateInstruments,1000);
  }

  if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",init,{once:true});
  else init();
})();

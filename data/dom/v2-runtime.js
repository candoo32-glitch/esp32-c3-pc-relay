/*
 * ESP32-C3 PC Relay — DOM V2 runtime
 *
 * Optional presentation framework. It never owns V1 application logic.
 * All V2 DOM is contained below #dom-v2-root and all CSS uses the v2- prefix.
 */
(function(){
  "use strict";
  if(window.ESP32V2)return;

  const VERSION=2;
  const rootId="dom-v2-root";
  const state={data:{},listeners:new Set(),timer:null,interval:1000,online:false};
  const registry=new Map();
  const custom=new Map();
  const actions=new Map();
  const dashboards=new Map();
  let root=null;
  let activeView="";
  let currentSchema=null;

  const esc=v=>String(v==null?"":v).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[c]));
  const pathParts=p=>String(p||"").replace(/\[([^\]]+)\]/g,".$1").split(".").filter(Boolean);
  function get(obj,path,fallback){
    if(path==null||path==="")return obj;
    let v=obj;
    for(const part of pathParts(path)){
      if(v==null)return fallback;
      v=v[part];
    }
    return v==null?fallback:v;
  }
  function set(obj,path,value){
    const parts=pathParts(path); if(!parts.length)return value;
    let v=obj;
    for(let i=0;i<parts.length-1;i++){
      const k=parts[i];
      if(v[k]==null)v[k]={};
      v=v[k];
    }
    v[parts[parts.length-1]]=value;
    return value;
  }
  function bindValue(spec){
    if(spec==null)return undefined;
    if(typeof spec!=="string")return spec;
    if(spec[0]==="$")return get(state.data,spec.slice(1));
    if(spec.indexOf(".")>=0||spec.indexOf("[")>=0)return get(state.data,spec);
    return spec;
  }
  function format(v,fmt){
    if(v==null)return "";
    if(!fmt)return String(v);
    if(typeof fmt==="string"){
      if(fmt==="number")return Number(v).toLocaleString();
      if(fmt==="integer")return String(Math.round(Number(v)));
      if(fmt==="percent")return Number(v).toFixed(0)+"%";
      if(fmt==="dbm")return String(v)+" dBm";
      if(fmt==="voltage")return Number(v).toFixed(2)+" V";
      if(fmt==="celsius")return Number(v).toFixed(1)+" °C";
      if(fmt==="ms")return String(v)+" ms";
      if(fmt==="boolean")return v?"ON":"OFF";
    }
    return String(v);
  }
  function evaluate(expr){
    if(expr==null)return true;
    if(typeof expr!=="string")return !!expr;
    const m=expr.match(/^\s*([^\s]+)\s*(===|!==|==|!=|>=|<=|>|<|contains)\s*(.+?)\s*$/);
    if(!m)return !!bindValue(expr);
    const left=bindValue(m[1]), raw=m[3].replace(/^["']|["']$/g,"");
    const right=isNaN(Number(raw))?raw:Number(raw);
    switch(m[2]){
      case "===":case "==":return left==right;
      case "!==":case "!=":return left!=right;
      case ">":return Number(left)>Number(right);
      case "<":return Number(left)<Number(right);
      case ">=":return Number(left)>=Number(right);
      case "<=":return Number(left)<=Number(right);
      case "contains":return String(left??"").includes(String(right));
      default:return false;
    }
  }

  async function submitForm(url,form){
    const fields=new FormData(form);
    const hasFile=[...form.querySelectorAll("input[type=file]")].some(e=>e.files&&e.files.length);
    const body=hasFile?fields:new URLSearchParams(fields);
    const headers=hasFile?{}:{"Content-Type":"application/x-www-form-urlencoded"};
    const r=await fetch(url,{method:"POST",headers,body,cache:"no-store"});
    if(!r.ok)throw Error("V2 form HTTP "+r.status);
    return r;
  }

  async function post(url,body){
    const encoded=typeof body==="string"?body:new URLSearchParams(body||{}).toString();
    const r=await fetch(url,{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:encoded,cache:"no-store"});
    if(!r.ok)throw Error("V2 action HTTP "+r.status);
    return r;
  }

  actions.set("relay.activate",({id})=>post("/relay/action",{id,action:"activate"}));
  actions.set("relay.deactivate",({id})=>post("/relay/action",{id,action:"deactivate"}));
  actions.set("relay.toggle",async({id})=>{
    const relay=get(state.data,"relays."+id);
    return post("/relay/action",{id,action:relay&&relay.state?"deactivate":"activate"});
  });
  actions.set("wifi.toggle",()=>post("/wifi/toggle"));
  actions.set("wifi.reconnect",()=>post("/wifi/reconnect"));
  actions.set("wifi.scan",()=>post("/wifi/scan"));
  actions.set("diagnostics.toggle",()=>post("/diagnostics/toggle"));
  actions.set("system.reboot",()=>post("/system/reboot"));
  actions.set("ota.latest",()=>post("/system/update-latest"));
  actions.set("ui.select",async({id})=>{
    const result=await post("/system/ui-selection",{ui:id});
    // UI selection changes presentation layers. Always restart the page so no
    // V1/V2 DOM, stylesheet, or runtime state can survive the transition.
    window.location.reload();
    return result;
  });
  actions.set("theme.select",({id})=>post("/system/theme",{theme:id}));
  actions.set("form.submit",({url,form})=>submitForm(url,form));

  async function dispatch(name,args){
    const fn=actions.get(String(name||""));
    if(!fn)throw Error("Unknown V2 action: "+name);
    const result=await fn(args||{});
    notify();
    return result;
  }

  function notify(){for(const fn of [...state.listeners]){try{fn(state.data)}catch(e){console.warn("V2 listener:",e)}}}
  async function refresh(){
    try{
      const [stateResponse,diagnosticsResponse]=await Promise.all([
        fetch("/api/state",{cache:"no-store"}),
        fetch("/api/diagnostics",{cache:"no-store"})
      ]);
      if(!stateResponse.ok)throw Error(stateResponse.status);
      state.data=await stateResponse.json();
      if(diagnosticsResponse.ok){
        try{
          const diagnostics=await diagnosticsResponse.json();
          state.data.diagnosticsLog=Array.isArray(diagnostics?.lines)?diagnostics.lines:[];
        }catch(e){}
      }
      state.online=true;
      notify();
      return state.data;
    }catch(e){
      state.online=false;
      notify();
      return null;
    }
  }
  function startPolling(ms){
    state.interval=Math.max(250,Number(ms)||1000);
    if(state.timer)clearInterval(state.timer);
    state.timer=setInterval(refresh,state.interval);
    refresh();
  }
  function stopPolling(){if(state.timer)clearInterval(state.timer);state.timer=null}

  function cssVars(tokens){
    if(!root||!tokens)return;
    for(const [k,v] of Object.entries(tokens)){
      const name=k.startsWith("--")?k:"--v2-"+k.replace(/[^a-zA-Z0-9_-]/g,"-");
      root.style.setProperty(name,String(v));
    }
  }

  function applyAnimation(el,a){
    if(!a)return;
    const name=typeof a==="string"?a:a.name;
    if(!name)return;
    el.classList.add("v2-anim-"+String(name).replace(/[^a-zA-Z0-9_-]/g,"-"));
    if(typeof a==="object"&&a.duration)el.style.setProperty("--v2-anim-duration",String(a.duration));
  }

  function resolveAsset(src){
    if(!src)return "";
    if(/^https?:\/\//i.test(src)||src[0]==="/")return src;
    return "/dom/"+src.replace(/^\.\//,"");
  }

  async function actionFromNode(el,spec){
    if(!spec)return;
    const action=typeof spec==="string"?spec:spec.action;
    const args={...(typeof spec==="object"?spec.args||{}:{})};
    for(const [k,v] of Object.entries(args)){
      if(typeof v==="string"&&v[0]==="$")args[k]=bindValue(v);
    }
    try{
      el.setAttribute("aria-busy","true");
      await dispatch(action,args);
      if(spec&&spec.refresh!==false)await refresh();
    }catch(e){
      console.warn("V2 action failed:",action,e);
      el.dataset.v2Error="true";
    }finally{el.removeAttribute("aria-busy")}
  }

  function common(el,node){
    if(node.id)el.id="v2-"+String(node.id).replace(/[^A-Za-z0-9_-]/g,"-");
    if(node.class)el.className+=" "+String(node.class).split(/\s+/).filter(x=>x).map(x=>"v2-"+x).join(" ");
    if(node.title)el.title=String(bindValue(node.title));
    if(node.ariaLabel)el.setAttribute("aria-label",String(bindValue(node.ariaLabel)));
    if(node.visibleIf&&!evaluate(node.visibleIf))el.hidden=true;
    if(node.animate)applyAnimation(el,node.animate);
    if(node.style&&typeof node.style==="object")for(const [k,v] of Object.entries(node.style))el.style[k]=String(bindValue(v));
    if(node.on&&typeof node.on==="object")for(const [event,spec] of Object.entries(node.on))el.addEventListener(event,e=>actionFromNode(e.currentTarget,spec));
    return el;
  }

  function children(el,node){
    (node.children||node.items||[]).forEach(c=>el.appendChild(renderNode(c)));
    return el;
  }

  registry.set("container",(n)=>children(common(document.createElement("section"),n),n));
  registry.set("stack",(n)=>{
    const e=children(common(document.createElement("div"),n),n);
    e.classList.add("v2-stack");
    e.style.setProperty("--v2-gap",String(n.gap??"var(--v2-space,8px)"));
    return e;
  });
  registry.set("grid",(n)=>{
    const e=children(common(document.createElement("div"),n),n);
    e.classList.add("v2-grid");
    e.style.gridTemplateColumns=n.columns?String(n.columns):"repeat(auto-fit,minmax(180px,1fr))";
    if(n.gap!=null)e.style.gap=String(n.gap);
    if(n.rows)e.style.gridTemplateRows=String(n.rows);
    return e;
  });
  registry.set("panel",(n)=>{
    const e=children(common(document.createElement("section"),n),n);
    e.classList.add("v2-panel");
    if(n.label){
      const h=document.createElement("div");h.className="v2-panel-title";h.textContent=String(bindValue(n.label));e.prepend(h);
    }
    return e;
  });
  registry.set("text",(n)=>{
    const e=common(document.createElement(n.tag||"div"),n);
    e.classList.add("v2-text");
    e.textContent=format(bindValue(n.bind??n.value??n.text),n.format);
    return e;
  });
  registry.set("input",(n)=>{
    const e=common(document.createElement("input"),n);
    e.classList.add("v2-input");
    e.type=String(n.inputType||n.type||"text");
    if(n.name)e.name=String(n.name);
    if(n.placeholder)e.placeholder=String(bindValue(n.placeholder));
    if(n.maxLength)e.maxLength=Number(n.maxLength);
    if(n.min!=null)e.min=String(n.min);
    if(n.max!=null)e.max=String(n.max);
    if(n.step!=null)e.step=String(n.step);
    if(n.required)e.required=true;
    if(n.bind!=null){
      const value=bindValue(n.bind);
      if(value!=null)e.value=String(value);
    }else if(n.value!=null)e.value=String(bindValue(n.value));
    return e;
  });
  registry.set("select",(n)=>{
    const e=common(document.createElement("select"),n);
    e.classList.add("v2-select");
    if(n.name)e.name=String(n.name);
    (n.options||[]).forEach(o=>{
      const opt=document.createElement("option");
      if(typeof o==="string"){opt.value=o;opt.textContent=o}
      else{opt.value=String(o.value??"");opt.textContent=String(o.label??o.value??"")}
      e.appendChild(opt);
    });
    if(n.bind!=null)e.value=String(bindValue(n.bind)??"");
    return e;
  });
  registry.set("textarea",(n)=>{
    const e=common(document.createElement("textarea"),n);
    e.classList.add("v2-textarea");
    if(n.name)e.name=String(n.name);
    if(n.rows)e.rows=Number(n.rows);
    if(n.placeholder)e.placeholder=String(bindValue(n.placeholder));
    if(n.bind!=null)e.value=String(bindValue(n.bind)??"");
    else if(n.value!=null)e.value=String(bindValue(n.value)??"");
    return e;
  });
  registry.set("link",(n)=>{
    const e=common(document.createElement("a"),n);
    e.classList.add("v2-link");
    e.href=String(bindValue(n.href||"#"));
    if(n.target)e.target=String(n.target);
    e.textContent=String(bindValue(n.text||n.label||n.href||"Link"));
    return e;
  });
  registry.set("form",(n)=>{
    const e=common(document.createElement("form"),n);
    e.classList.add("v2-form");
    children(e,n);
    e.addEventListener("submit",async ev=>{
      ev.preventDefault();
      const submitter=ev.submitter;
      if(submitter)submitter.setAttribute("aria-busy","true");
      try{
        await submitForm(String(bindValue(n.action||"")),e);
        await refresh();
      }catch(err){
        console.warn("V2 form failed:",err);
        e.dataset.v2Error="true";
      }finally{
        if(submitter)submitter.removeAttribute("aria-busy");
      }
    });
    return e;
  });

  registry.set("value",(n)=>{
    const e=common(document.createElement("div"),n);
    e.classList.add("v2-value");
    if(n.label){const l=document.createElement("span");l.className="v2-label";l.textContent=String(bindValue(n.label));e.appendChild(l)}
    const v=document.createElement("span");v.className="v2-value-number";v.textContent=format(bindValue(n.bind),n.format);e.appendChild(v);
    return e;
  });
  registry.set("status",(n)=>{
    const e=common(document.createElement("div"),n);e.classList.add("v2-status");
    const value=bindValue(n.bind);
    e.dataset.state=String(n.map&&n.map[String(value)]||value||"unknown").toLowerCase();
    if(n.label){const l=document.createElement("span");l.className="v2-label";l.textContent=String(bindValue(n.label));e.appendChild(l)}
    const v=document.createElement("strong");v.textContent=n.map&&n.map[String(value)]!=null?n.map[String(value)]:format(value,n.format);e.appendChild(v);
    return e;
  });
  registry.set("button",(n)=>{
    const e=common(document.createElement("button"),n);e.type=n.submit?"submit":"button";e.classList.add("v2-button");
    e.textContent=String(bindValue(n.text??n.label??"Action"));
    if(n.action)e.addEventListener("click",()=>actionFromNode(e,n.action));
    return e;
  });
  registry.set("toggle",(n)=>{
    const e=common(document.createElement("button"),n);e.type="button";e.classList.add("v2-toggle");
    const on=!!bindValue(n.bind);e.dataset.state=on?"on":"off";e.textContent=on?(n.onText||"ON"):(n.offText||"OFF");
    if(n.action)e.addEventListener("click",()=>actionFromNode(e,n.action));
    return e;
  });
  registry.set("relay",(n)=>{
    const id=Number(n.relay??n.id??0),relay=get(state.data,"relays."+id,{});
    const e=common(document.createElement("div"),n);e.classList.add("v2-relay");
    const title=document.createElement("div");title.className="v2-relay-title";title.textContent=String(relay.name||n.label||("Relay "+(id+1)));
    const stateEl=document.createElement("strong");stateEl.className="v2-relay-state";stateEl.textContent=relay.state?"ON":"OFF";stateEl.dataset.state=relay.state?"on":"off";
    const b=document.createElement("button");b.type="button";b.className="v2-button";b.textContent=relay.state?"Deactivate":"Activate";
    b.onclick=()=>actionFromNode(b,{action:"relay.toggle",args:{id}});
    e.append(title,stateEl,b);
    return e;
  });
  registry.set("gauge",(n)=>{
    const e=common(document.createElement("div"),n);e.classList.add("v2-gauge");
    const value=Number(bindValue(n.bind))||0,min=Number(n.min??0),max=Number(n.max??100);
    const pct=Math.max(0,Math.min(100,(value-min)*100/(max-min||1)));
    const ring=document.createElement("div");ring.className="v2-gauge-ring";ring.style.setProperty("--v2-gauge-pct",pct+"%");
    const val=document.createElement("strong");val.textContent=format(value,n.format||"number");
    const label=document.createElement("span");label.className="v2-label";label.textContent=String(bindValue(n.label||""));
    ring.append(val,label);e.appendChild(ring);return e;
  });
  registry.set("meter",(n)=>{
    const e=common(document.createElement("div"),n);e.classList.add("v2-meter");
    const value=Number(bindValue(n.bind))||0,min=Number(n.min??0),max=Number(n.max??100);
    const pct=Math.max(0,Math.min(100,(value-min)*100/(max-min||1)));
    const label=document.createElement("div");label.className="v2-meter-label";label.textContent=String(bindValue(n.label||""));
    const track=document.createElement("div");track.className="v2-meter-track";
    const bar=document.createElement("div");bar.className="v2-meter-bar";bar.style.width=pct+"%";track.appendChild(bar);
    e.append(label,track);return e;
  });
  registry.set("progress",(n)=>{
    const e=common(document.createElement("div"),n);e.classList.add("v2-progress");
    const value=Number(bindValue(n.bind))||0,total=Number(bindValue(n.total))||100;
    const pct=Math.max(0,Math.min(100,value*100/(total||1)));
    const track=document.createElement("div");track.className="v2-meter-track";
    const bar=document.createElement("div");bar.className="v2-meter-bar";bar.style.width=pct+"%";track.appendChild(bar);
    const label=document.createElement("span");label.textContent=format(value,n.format)+" / "+format(total,n.totalFormat||"number");
    e.append(label,track);return e;
  });
  registry.set("log",(n)=>{
    const e=common(document.createElement("pre"),n);e.classList.add("v2-log");
    const lines=bindValue(n.bind);e.textContent=Array.isArray(lines)?lines.join("\\n"):String(lines??"");
    return e;
  });
  registry.set("image",(n)=>{
    const e=common(document.createElement("img"),n);e.classList.add("v2-image");e.src=resolveAsset(String(bindValue(n.src)||""));e.alt=String(bindValue(n.alt||""));return e;
  });
  registry.set("spacer",(n)=>{const e=common(document.createElement("div"),n);e.classList.add("v2-spacer");e.style.minHeight=String(n.height||"1rem");return e});
  registry.set("view-tabs",(n)=>{
    const e=common(document.createElement("nav"),n);e.classList.add("v2-view-tabs");
    for(const v of dashboards.keys()){const b=document.createElement("button");b.type="button";b.textContent=dashboards.get(v).title||v;b.dataset.view=v;b.onclick=()=>showView(v);e.appendChild(b)}
    return e;
  });


  // V2 utility components. These own presentation-only browser behavior and
  // talk to the same public endpoints as the protected built-in UI.
  registry.set("field",(n)=>{
    const e=common(document.createElement("label"),n);e.classList.add("v2-field");
    if(n.label){const l=document.createElement("span");l.className="v2-field-label";l.textContent=String(bindValue(n.label));e.appendChild(l)}
    (n.children||[]).forEach(c=>e.appendChild(renderNode(c)));
    return e;
  });
  registry.set("secret",(n)=>{
    const e=common(document.createElement("div"),n);e.classList.add("v2-secret");
    const input=document.createElement("input");input.className="v2-input";input.type="password";
    input.autocomplete="new-password";
    if(n.name)input.name=String(n.name);
    if(n.placeholder)input.placeholder=String(bindValue(n.placeholder));
    if(n.maxLength)input.maxLength=Number(n.maxLength);
    if(n.required)input.required=true;
    const button=document.createElement("button");button.type="button";button.className="v2-button v2-secret-toggle";
    button.textContent="SHOW";
    button.onclick=()=>{const shown=input.type==="text";input.type=shown?"password":"text";button.textContent=shown?"SHOW":"HIDE"};
    e.append(input,button);return e;
  });
  registry.set("relay-detail",(n)=>{
    const id=Number(n.relay??0),relay=get(state.data,"relays["+id+"]",{});
    const e=common(document.createElement("section"),n);e.classList.add("v2-relay-detail");
    const title=document.createElement("div");title.className="v2-relay-detail-title";title.textContent=String(relay.name||n.label||("Relay "+(id+1)));
    const grid=document.createElement("div");grid.className="v2-relay-detail-grid";
    const rows=[
      ["GPIO",n.gpio??relay.gpio??"-"],
      ["CONTACT",relay.state?"ACTIVE":"NORMAL"],
      ["NORMAL STATE",relay.normal??"-"],
      ["MODE",relay.mode??"-"],
      ["PULSE",relay.mode==="PULSE"||relay.mode==="pulse"?(String(relay.pulse??"-")+" ms"):"—"]
    ];
    rows.forEach(([k,v])=>{const a=document.createElement("span");a.className="v2-label";a.textContent=k;const b=document.createElement("strong");b.textContent=String(v);const row=document.createElement("div");row.append(a,b);grid.appendChild(row)});
    e.append(title,grid);return e;
  });
  registry.set("relay-dock",(n)=>{
    const e=common(document.createElement("aside"),n);e.classList.add("v2-relay-dock");e.setAttribute("aria-label","Relay controls");
    [0,1].forEach(id=>{
      const relay=get(state.data,"relays["+id+"]",{});
      const b=document.createElement("button");b.type="button";b.className="v2-relay-dock-button";
      b.dataset.state=relay.state?"on":"off";
      b.textContent=String(relay.name||("Relay "+(id+1)));
      const dot=document.createElement("span");dot.className="v2-relay-dock-dot";b.prepend(dot);
      b.onclick=()=>actionFromNode(b,{action:"relay.toggle",args:{id}});
      e.appendChild(b);
    });
    state.listeners.add(()=>{e.querySelectorAll(".v2-relay-dock-button").forEach((b,i)=>{const r=get(state.data,"relays["+i+"]",{});b.dataset.state=r.state?"on":"off";b.lastChild.textContent=String(r.name||("Relay "+(i+1)))})});
    return e;
  });
  registry.set("wifi-scanner",(n)=>{
    const e=common(document.createElement("section"),n);e.classList.add("v2-wifi-scanner");
    const head=document.createElement("div");head.className="v2-tool-head";
    const status=document.createElement("span");status.className="v2-tool-status";status.textContent="Ready";
    const button=document.createElement("button");button.type="button";button.className="v2-button";button.textContent="SCAN NOW";
    const list=document.createElement("div");list.className="v2-scan-results";
    const render=d=>{
      const networks=Array.isArray(d?.networks)?d.networks:[];
      list.innerHTML="";
      if(!networks.length){list.innerHTML='<div class="v2-empty">No networks found.</div>';return}
      const table=document.createElement("table");table.className="v2-tool-table";
      table.innerHTML="<thead><tr><th>SSID</th><th>RSSI</th><th>CH</th><th>SECURITY</th><th>BSSID</th></tr></thead>";
      const body=document.createElement("tbody");
      networks.forEach(x=>{const tr=document.createElement("tr");[x.ssid||"(hidden)",String(x.rssi??"—")+" dBm",x.channel??"—",x.security??"—",x.bssid??"—"].forEach(v=>{const td=document.createElement("td");td.textContent=String(v);tr.appendChild(td)});tr.onclick=()=>{const input=e.querySelector("input[data-v2-scan-ssid]");if(input)input.value=String(x.ssid||"");};body.appendChild(tr)});
      table.appendChild(body);list.appendChild(table);
      const note=document.createElement("div");note.className="v2-tool-status";note.textContent=networks.length+" access point"+(networks.length===1?"":"s")+" found";list.appendChild(note);
    };
    button.onclick=async()=>{
      button.disabled=true;status.textContent="Scanning…";
      try{const r=await fetch("/wifi/scan",{method:"POST",cache:"no-store"});if(!r.ok)throw Error(r.status);render(await r.json());status.textContent="Scan complete"}catch(err){status.textContent="Scan failed";list.innerHTML='<div class="v2-empty">Wi-Fi scan failed.</div>'}finally{button.disabled=false}
    };
    const input=document.createElement("input");input.className="v2-input";input.placeholder="Selected SSID";input.dataset.v2ScanSsid="true";if(n.name)input.name=String(n.name);
    head.append(button,status);e.append(head,input,list);return e;
  });
  registry.set("file-browser",(n)=>{
    const e=common(document.createElement("section"),n);e.classList.add("v2-file-browser");
    const head=document.createElement("div");head.className="v2-tool-head";
    const summary=document.createElement("span");summary.className="v2-tool-status";summary.textContent="Loading filesystem…";
    const refreshButton=document.createElement("button");refreshButton.type="button";refreshButton.className="v2-button";refreshButton.textContent="REFRESH";
    const body=document.createElement("div");body.className="v2-file-list";
    const viewer=document.createElement("pre");viewer.className="v2-file-viewer";viewer.hidden=true;
    const load=async()=>{
      summary.textContent="Reading SPIFFS…";
      try{
        const r=await fetch("/api/storage/files",{cache:"no-store"});if(!r.ok)throw Error(r.status);
        const d=await r.json(),files=Array.isArray(d.files)?d.files:[];
        const used=Number(d.used)||0,total=Number(d.total)||0,pct=total?Math.min(100,used*100/total):0;
        summary.textContent=files.length+" files • "+used.toLocaleString()+" / "+total.toLocaleString()+" bytes ("+pct.toFixed(1)+"%)";
        body.innerHTML="";
        if(!files.length){body.innerHTML='<div class="v2-empty">Filesystem is empty.</div>';return}
        const table=document.createElement("table");table.className="v2-tool-table";table.innerHTML="<thead><tr><th>FILE</th><th>SIZE</th><th>ACTIONS</th></tr></thead>";
        const tb=document.createElement("tbody");
        files.forEach(f=>{
          const tr=document.createElement("tr");const name=document.createElement("td");name.textContent=String(f.path);
          const size=document.createElement("td");size.textContent=Number(f.size||0).toLocaleString()+" B";
          const actions=document.createElement("td");actions.className="v2-file-actions";
          const viewable=/\.(html?|css|js|json|txt|xml|svg)$/i.test(String(f.path));
          if(viewable){const b=document.createElement("button");b.className="v2-button";b.textContent="VIEW";b.onclick=async()=>{try{const r=await fetch("/storage/view?path="+encodeURIComponent(f.path),{cache:"no-store"});if(!r.ok)throw Error(r.status);viewer.textContent=await r.text();viewer.hidden=false}catch(x){viewer.textContent="Unable to view file.";viewer.hidden=false}};actions.appendChild(b)}
          const a=document.createElement("a");a.className="v2-button";a.href="/storage/download?path="+encodeURIComponent(f.path);a.textContent="DOWNLOAD";actions.appendChild(a);
          const del=document.createElement("button");del.className="v2-button v2-danger-button";del.textContent="ERASE";del.onclick=async()=>{if(!confirm("Erase "+f.path+"? This cannot be undone."))return;try{const r=await fetch("/storage/delete",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"path="+encodeURIComponent(f.path),cache:"no-store"});if(!r.ok)throw Error(r.status);await load()}catch(x){summary.textContent="Erase failed"}};actions.appendChild(del);
          tr.append(name,size,actions);tb.appendChild(tr);
        });
        table.appendChild(tb);body.appendChild(table);
      }catch(x){summary.textContent="SPIFFS unavailable";body.innerHTML='<div class="v2-empty">Web storage could not be read.</div>'}
    };
    refreshButton.onclick=load;head.append(refreshButton,summary);e.append(head,body,viewer);load();return e;
  });
  registry.set("nvs-browser",(n)=>{
    const e=common(document.createElement("section"),n);e.classList.add("v2-nvs-browser");
    const head=document.createElement("div");head.className="v2-tool-head";
    const filter=document.createElement("input");filter.className="v2-input";filter.placeholder="Filter namespace, key, type, value…";
    const refreshButton=document.createElement("button");refreshButton.type="button";refreshButton.className="v2-button";refreshButton.textContent="REFRESH";
    const status=document.createElement("span");status.className="v2-tool-status";status.textContent="Loading NVS…";
    const body=document.createElement("div");body.className="v2-nvs-list";let entries=[];
    const render=()=>{
      const q=filter.value.trim().toLowerCase(),groups={};
      entries.forEach(x=>{if(q&&!([x.namespace,x.key,x.type,x.value].join(" ").toLowerCase().includes(q)))return;(groups[x.namespace]||(groups[x.namespace]=[])).push(x)});
      body.innerHTML="";const names=Object.keys(groups).sort();
      if(!names.length){body.innerHTML='<div class="v2-empty">No matching NVS entries.</div>';return}
      names.forEach(ns=>{const d=document.createElement("details");d.open=true;const s=document.createElement("summary");s.textContent=ns+" • "+groups[ns].length+" entr"+(groups[ns].length===1?"y":"ies");const table=document.createElement("table");table.className="v2-tool-table";table.innerHTML="<thead><tr><th>KEY</th><th>TYPE</th><th>VALUE</th></tr></thead>";const tb=document.createElement("tbody");groups[ns].forEach(x=>{const tr=document.createElement("tr");[x.key,x.type,x.value].forEach(v=>{const td=document.createElement("td");td.textContent=String(v??"");tr.appendChild(td)});tb.appendChild(tr)});table.appendChild(tb);d.append(s,table);body.appendChild(d)})};
    const load=async()=>{status.textContent="Reading NVS…";try{const [a,b]=await Promise.all([fetch("/api/nvs",{cache:"no-store"}),fetch("/api/nvs/stats",{cache:"no-store"})]);if(!a.ok||!b.ok)throw Error("NVS");const d=await a.json(),s=await b.json();entries=Array.isArray(d.entries)?d.entries:[];status.textContent=(s.namespaceCount??0)+" namespaces • "+(s.usedEntries??0)+" used • "+(s.freeEntries??0)+" free";render()}catch(x){status.textContent="NVS unavailable";body.innerHTML='<div class="v2-empty">Configuration inspection could not be loaded.</div>'}};
    filter.oninput=render;refreshButton.onclick=load;head.append(filter,refreshButton,status);e.append(head,body);load();return e;
  });
  registry.set("ota-monitor",(n)=>{
    const e=common(document.createElement("section"),n);e.classList.add("v2-ota-monitor");
    const head=document.createElement("div");head.className="v2-tool-head";
    const button=document.createElement("button");button.type="button";button.className="v2-button";button.textContent="CHECK & INSTALL UPDATES";
    const status=document.createElement("span");status.className="v2-tool-status";status.textContent="Ready";
    const details=document.createElement("div");details.className="v2-ota-details";
    const makeRow=(label)=>{const row=document.createElement("div");row.className="v2-ota-row";const title=document.createElement("strong");title.textContent=label;const track=document.createElement("div");track.className="v2-meter-track";const bar=document.createElement("div");bar.className="v2-meter-bar";track.appendChild(bar);const info=document.createElement("span");info.className="v2-tool-status";row.append(title,track,info);return {row,bar,info}};
    const fw=makeRow("FIRMWARE"),web=makeRow("WEB UI");details.append(fw.row,web.row);e.append(head,details);
    let timer=null;
    const paint=d=>{
      status.textContent=d.message||d.stage||"Updating";
      const rows=[["firmware",fw],["web",web)];
      rows.forEach(([name,row])=>{const active=d.component===name; if(active||d.stage==="complete"||d.stage==="rebooting")row.row.hidden=false});
      if(d.component){const row=d.component==="web"?web:fw;const pct=d.total>0?Math.min(100,d.received*100/d.total):0;row.bar.style.width=pct+"%";row.info.textContent=d.total>0?Math.round(pct)+"% • "+Number(d.received||0).toLocaleString()+" / "+Number(d.total).toLocaleString()+" bytes":Number(d.received||0).toLocaleString()+" bytes"}};
    const stop=()=>{if(timer){clearInterval(timer);timer=null}button.disabled=false};
    const poll=async()=>{try{const r=await fetch("/system/update-status",{cache:"no-store"});if(!r.ok)throw Error(r.status);const d=await r.json();paint(d);if(["idle","complete","error"].includes(d.stage)){stop();if(d.stage==="rebooting"){button.disabled=true;}}}catch(x){status.textContent="Waiting for ESP32…"}};
    button.onclick=async()=>{button.disabled=true;status.textContent="Starting OTA…";try{const r=await fetch("/system/update-latest",{method:"POST",cache:"no-store"});if(!r.ok)throw Error(r.status);await poll();if(!timer)timer=setInterval(poll,500)}catch(x){status.textContent="Update request failed";button.disabled=false}};
    head.append(button,status);poll();return e;
  });

  function renderNode(node){
    if(!node||typeof node!=="object")return document.createComment("invalid V2 node");
    const type=String(node.type||"container");
    const fn=custom.get(type)||registry.get(type);
    if(!fn){
      const e=document.createElement("div");e.className="v2-unsupported";e.textContent="Unsupported V2 component: "+type;return e;
    }
    try{return fn(node)}catch(e){console.warn("V2 component failed:",type,e);const x=document.createElement("div");x.className="v2-error";x.textContent="V2 component error: "+type;return x}
  }

  function registerComponent(name,renderer){if(!/^[A-Za-z0-9._-]+$/.test(name)||typeof renderer!=="function")throw Error("Invalid V2 component");custom.set(name,renderer)}

  function renderSchema(schema){
    currentSchema=schema||{};
    cssVars(currentSchema.tokens||currentSchema.theme?.tokens);
    root.classList.toggle("v2-responsive",currentSchema.responsive!==false);
    dashboards.clear();
    if(currentSchema.views&&typeof currentSchema.views==="object"){
      for(const [name,view] of Object.entries(currentSchema.views))dashboards.set(name,view||{});
    }else dashboards.set("main",currentSchema);
    const firstView=dashboards.keys().next().value||"main";
    const wanted=String(currentSchema.defaultView||firstView);
    activeView=dashboards.has(wanted)?wanted:dashboards.keys().next().value;
    showView(activeView);
  }

  function showView(name){
    if(!dashboards.has(name)||!root)return false;
    activeView=name;
    root.querySelectorAll(".v2-screen").forEach(e=>e.remove());
    const view=dashboards.get(name)||{};
    const screen=document.createElement("div");screen.className="v2-screen";
    if(view.title){const h=document.createElement("h2");h.className="v2-screen-title";h.textContent=String(view.title);screen.appendChild(h)}
    if(view.navigation){
      const nav=registry.get("view-tabs")({type:"view-tabs"});
      screen.appendChild(nav);
    }
    if(view.layout)screen.appendChild(renderNode(view.layout));
    else if(view.children)screen.appendChild(renderNode({type:"container",children:view.children}));
    root.appendChild(screen);
    return true;
  }

  async function loadSchema(url){
    const r=await fetch(url+"?v2CacheBust="+Date.now(),{cache:"no-store"});
    if(!r.ok)throw Error("V2 schema HTTP "+r.status);
    const schema=await r.json();
    renderSchema(schema);
    return schema;
  }

  function init(target){
    root=target||document.getElementById(rootId);
    if(!root)return false;
    root.dataset.runtimeVersion=String(VERSION);
    startPolling(Number(root.dataset.pollMs)||1000);
    if(root.dataset.schema)loadSchema(root.dataset.schema).catch(e=>console.warn("Optional V2 schema unavailable:",e));
    return true;
  }

  const api={
    version:VERSION,
    root:()=>root,
    state:()=>state.data,
    online:()=>state.online,
    get:(path,fallback)=>get(state.data,path,fallback),
    set,
    refresh,
    startPolling,
    stopPolling,
    dispatch,
    registerAction:(name,fn)=>actions.set(name,fn),
    registerComponent,
    registerRenderer:registerComponent,
    loadSchema,
    renderSchema,
    init,
    showView,
    resolveAsset,
    format,
    evaluate,
    components:registry,
    customComponents:custom,
    dashboards
  };
  window.ESP32V2=api;

  window.addEventListener("dom-v2-ready",()=>init(document.getElementById(rootId)));
})();

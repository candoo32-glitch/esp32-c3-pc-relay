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
  actions.set("ui.select",({id})=>post("/system/ui-selection",{ui:id}));
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
    for(const v of Object.keys(dashboards)){const b=document.createElement("button");b.type="button";b.textContent=dashboards.get(v).title||v;b.dataset.view=v;b.onclick=()=>showView(v);e.appendChild(b)}
    return e;
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

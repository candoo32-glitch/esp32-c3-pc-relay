/* LCARS Modern — optimized behavior layer
 * Decorative only. Existing application event handlers remain authoritative.
 *
 * Performance rules:
 * - Instrument shapes are created once; tab changes never rebuild the DOM.
 * - Tab switching uses one short compositor-friendly transition.
 * - No filter animation on labels/cards during navigation.
 * - Decorative observers/timers do only small, bounded work.
 */
(()=>{
  let booted=false;
  const boot=()=>{
    if(booted)return;
    booted=true;
    document.body.classList.add("lcars-ready");

    const activeTabName=()=>{
      const raw=(location.hash||"#dashboard").slice(1).split("?")[0];
      return new Set(["dashboard","wifi","network","diagnostics","relays","storage","system"]).has(raw)
        ? raw : "dashboard";
    };

    /* Dynamic LCARS instrument fields.
       These are decorative and intentionally built only once per page load.
       The right field is fixed by CSS, so there is no reason to regenerate it
       every time the user changes tabs. */
    const leftField=document.createElement("div");
    leftField.className="lcars-left-field";
    leftField.dataset.externalUi="true";
    leftField.setAttribute("aria-hidden","true");
    document.body.appendChild(leftField);

    const rightField=document.createElement("div");
    rightField.className="lcars-right-field";
    rightField.dataset.externalUi="true";
    rightField.setAttribute("aria-hidden","true");
    document.body.appendChild(rightField);

    const buildInstrumentField=()=>{
      const shapes=[
        "bar","bar","block","block","step","step","bar","block",
        "bar","corner","block","step","bar","block","bar","step",
        "corner","bar","block","bar","step","block","bar","corner"
      ];
      const colors=["orange","purple","blue","magenta"];

      const addShape=(field,index,rightSide)=>{
        const el=document.createElement("i");
        const shape=shapes[index];
        el.className="lcars-left-shape "+shape+" "+colors[Math.floor(Math.random()*colors.length)];

        const width=8+Math.floor(Math.random()*54);
        const height=3+Math.floor(Math.random()*24);
        const top=2+Math.random()*94;
        const duration=(4.5+Math.random()*5.5).toFixed(2);
        const delay=(-(Math.random()*7)).toFixed(2);
        const opacity=(.20+Math.random()*.58).toFixed(2);
        const radius=Math.floor(Math.random()*9);

        el.style.cssText=
          "--shape-w:"+width+"px;"+
          "--shape-h:"+height+"px;"+
          "--shape-top:"+top.toFixed(2)+"%;"+
          "--shape-left:"+(
            rightSide ? "0px" : (Math.random()*52).toFixed(2)+"px"
          )+";"+
          "--shape-duration:"+duration+"s;"+
          "--shape-delay:"+delay+"s;"+
          "--shape-opacity:"+opacity+";"+
          "--shape-radius:"+radius+"px;";

        if(rightSide){
          el.style.setProperty("--shape-w",(8+Math.floor(Math.random()*54))+"px");
          el.style.setProperty("--shape-top",(2+Math.random()*94).toFixed(2)+"%");
          el.style.setProperty("--shape-duration",(4.5+Math.random()*5.5).toFixed(2)+"s");
          el.style.setProperty("--shape-delay",(-(Math.random()*7)).toFixed(2)+"s");
          el.style.setProperty("--shape-opacity",(0.18+Math.random()*0.56).toFixed(2));
        }
        field.appendChild(el);
      };

      for(let i=0;i<shapes.length;i++){
        addShape(leftField,i,false);
        addShape(rightField,i,true);
      }

      /* v14 makes both instrument banks viewport-based. Keep the left field
         tall enough for the initial document without measuring it on every tab. */
      leftField.style.height=Math.max(document.documentElement.clientHeight,window.innerHeight)+"px";
    };
    buildInstrumentField();

    /* Live LCARS date/time readout. */
    const clock=document.createElement("div");
    clock.className="lcars-clock";
    clock.dataset.externalUi="true";
    clock.setAttribute("aria-label","Current date and time");
    clock.innerHTML='<div class="lcars-clock-time"></div><div class="lcars-clock-date"></div><span class="lcars-clock-rule"></span>';
    const nav=document.querySelector(".tabs");
    if(nav)nav.parentNode.insertBefore(clock,nav);

    const updateClock=()=>{
      const now=new Date();
      const t=clock.querySelector(".lcars-clock-time");
      const d=clock.querySelector(".lcars-clock-date");
      if(t)t.textContent=now.toLocaleTimeString([],{
        hour:"numeric",minute:"2-digit",second:"2-digit"
      });
      if(d)d.textContent=now.toLocaleDateString([],{
        weekday:"short",month:"short",day:"numeric",year:"numeric"
      });
    };
    updateClock();
    const tickClock=()=>{
      if(!clock.isConnected)return;
      updateClock();
      window.setTimeout(tickClock,1000);
    };
    window.setTimeout(tickClock,1000);

    /* Keep panels stable. The stylesheet already disables the old entrance
       animation, so do not create per-card animations at boot. */
    document.querySelectorAll(".card").forEach(card=>{
      card.style.removeProperty("--lcars-delay");
    });

    /* Small status pulse only when status text actually changes. */
    const statusLast=new WeakMap();
    let statusPulseBusy=false;
    const pulseStatus=(el)=>{
      if(statusPulseBusy)return;
      statusPulseBusy=true;
      el.animate(
        [{opacity:.72,transform:"translate3d(2px,0,0)"},{opacity:1,transform:"translate3d(0,0,0)"}],
        {duration:180,easing:"ease-out"}
      ).finished.finally(()=>{statusPulseBusy=false;}).catch(()=>{statusPulseBusy=false;});
    };
    const statusObserver=new MutationObserver(records=>{
      for(const record of records){
        const el=record.target.nodeType===3?record.target.parentElement:record.target;
        if(!el?.matches?.(".status,.notice,#page-status,.help.ok,.help.bad"))continue;
        const value=el.textContent||"";
        if(statusLast.get(el)===value)continue;
        statusLast.set(el,value);
        pulseStatus(el);
      }
    });
    statusObserver.observe(document.body,{subtree:true,childList:true,characterData:true});

    /* Diagnostic updates can be very frequent. Rate-limit the visual cue so
       a streaming console cannot create an animation for every incoming line. */
    const diag=document.querySelector("#diagnostic-console");
    if(diag){
      let previous=diag.value;
      let lastPulse=0;
      const poll=()=>{
        if(diag.value===previous)return;
        previous=diag.value;
        const now=performance.now();
        if(now-lastPulse<700)return;
        lastPulse=now;
        diag.animate(
          [{boxShadow:"inset 0 0 28px rgba(0,0,0,.42)"},{boxShadow:"inset 0 0 32px rgba(115,226,164,.08)"},{boxShadow:"inset 0 0 28px rgba(0,0,0,.42)"}],
          {duration:220,easing:"ease-out"}
        );
      };
      window.setInterval(poll,500);
    }

    /* Navigation identity. No DOM rebuild and no staggered card animations.
       The actual application continues to own hash navigation. */
    const activateTabVisual=()=>{
      const tab=activeTabName();
      document.body.dataset.lcarsTab=tab;
      document.documentElement.style.setProperty("--lcars-active-tab","\""+tab.toUpperCase()+"\"");

      const panel=document.getElementById("tab-"+tab);
      if(!panel)return;

      if(window.matchMedia("(prefers-reduced-motion: reduce)").matches)return;

      /* One short opacity/transform animation for the active panel only.
         transform + opacity are compositor-friendly properties. */
      panel.animate(
        [
          {opacity:.92,transform:"translate3d(0,3px,0)"},
          {opacity:1,transform:"translate3d(0,0,0)"}
        ],
        {duration:160,easing:"ease-out"}
      );
    };

    activateTabVisual();
    window.addEventListener("hashchange",activateTabVisual,{passive:true});

    /* Resize only updates the decorative left bank's height. It never rebuilds
       or randomizes the shapes. */
    let resizeTimer=0;
    window.addEventListener("resize",()=>{
      window.clearTimeout(resizeTimer);
      resizeTimer=window.setTimeout(()=>{
        leftField.style.height=Math.max(document.documentElement.clientHeight,window.innerHeight)+"px";
      },120);
    },{passive:true});

    /* Relay state becomes a visual instrument. */
    const relayVisuals=()=>{
      [0,1].forEach(i=>{
        const value=document.querySelector("#relay-state-"+i);
        const card=document.querySelector("#relay-card-"+i);
        const state=(value?.textContent||"").trim().toUpperCase();
        if(!value||!card)return;
        const active=/CLOSED|ON|ACTIVE/.test(state);
        value.classList.toggle("relay-state-active",active);
        card.classList.toggle("lcars-relay-active",active);
      });
    };
    const relayObserver=new MutationObserver(relayVisuals);
    ["#relay-state-0","#relay-state-1"].forEach(sel=>{
      const el=document.querySelector(sel);
      if(el)relayObserver.observe(el,{subtree:true,childList:true,characterData:true});
    });
    relayVisuals();

    /* Flash relay panels only when their live state changes. */
    const relaySnapshot={0:"",1:""};
    const relayPulse=()=>{
      [0,1].forEach(i=>{
        const el=document.querySelector("#relay-state-"+i);
        const card=document.querySelector("#relay-card-"+i);
        if(!el||!card)return;
        const state=(el.textContent||"").trim();
        if(relaySnapshot[i] && relaySnapshot[i]!==state){
          card.classList.remove("lcars-relay-pulse");
          void card.offsetWidth;
          card.classList.add("lcars-relay-pulse");
          window.setTimeout(()=>card.classList.remove("lcars-relay-pulse"),620);
        }
        relaySnapshot[i]=state;
      });
    };
    window.setInterval(relayPulse,750);

    /* Restrained RSSI meter. */
    const signalMeter=()=>{
      ["#wifi-rssi","#dash-rssi","#net-rssi","#diag-rssi"].forEach(sel=>{
        const el=document.querySelector(sel);
        if(!el)return;
        const n=parseInt((el.textContent||"").replace(/[^\d-]/g,""),10);
        if(!Number.isFinite(n))return;
        const quality=Math.max(0,Math.min(4,Math.round((n+90)/15)));
        const next=String(quality);
        if(el.dataset.lcarsSignal!==next)el.dataset.lcarsSignal=next;
      });
    };
    window.setInterval(signalMeter,1000);
    signalMeter();
  };

  window.addEventListener("external-ui-activate",boot,{once:true});
  if(document.readyState==="loading"){
    document.addEventListener("DOMContentLoaded",boot,{once:true});
  }else{
    boot();
  }
})();

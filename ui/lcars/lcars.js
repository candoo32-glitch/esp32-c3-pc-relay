/* LCARS Modern — behavior layer
 * Decorative only. Existing application event handlers remain authoritative.
 */
(()=>{
  const boot=()=>{
    document.body.classList.add("lcars-ready");

    /* LCARS is a control console, not a presentation animation. Keep the
       panels stable so live data and controls remain visually anchored. */
    document.querySelectorAll(".card").forEach(card=>{
      card.style.removeProperty("--lcars-delay");
    });

    const statusObserver=new MutationObserver(records=>{
      records.forEach(record=>{
        if(record.type!=="childList"&&record.type!=="characterData")return;
        const el=record.target.nodeType===3?record.target.parentElement:record.target;
        if(!el)return;
        if(el.matches?.(".status,.notice,#page-status,.help.ok,.help.bad")){
          el.animate(
            [{opacity:.55,transform:"translateX(3px)"},{opacity:1,transform:"translateX(0)"}],
            {duration:240,easing:"ease-out"}
          );
        }
      });
    });
    statusObserver.observe(document.body,{subtree:true,childList:true,characterData:true});

    document.querySelectorAll("button,select,input").forEach(el=>{
      el.addEventListener("focus",()=>el.animate(
        [{filter:"brightness(1)"},{filter:"brightness(1.14)"},{filter:"brightness(1)"}],
        {duration:360,easing:"ease-out"}
      ),{passive:true});
    });

    // Give the diagnostic console a subtle LCARS terminal scan cue when new data arrives.
    const diag=document.querySelector("#diagnostic-console");
    if(diag){
      let previous="";
      const poll=()=>{
        if(diag.value!==previous){
          previous=diag.value;
          diag.animate(
            [{boxShadow:"inset 0 0 28px rgba(0,0,0,.42)"},{boxShadow:"inset 0 0 34px rgba(115,226,164,.10)"},{boxShadow:"inset 0 0 28px rgba(0,0,0,.42)"}],
            {duration:320,easing:"ease-out"}
          );
        }
      };
      setInterval(poll,250);
    }
  };
  if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",boot,{once:true});
  else boot();


  /* Bind the decorative layer to the active tab without owning navigation. */
  const syncTabIdentity=()=>{
    const raw=(location.hash||"#dashboard").slice(1).split("?")[0];
    const allowed=new Set(["dashboard","wifi","network","diagnostics","relays","storage","system"]);
    const tab=allowed.has(raw)?raw:"dashboard";
    document.body.dataset.lcarsTab=tab;
    document.documentElement.style.setProperty("--lcars-active-tab","\""+tab.toUpperCase()+"\"");
  };
  syncTabIdentity();
  window.addEventListener("hashchange",syncTabIdentity,{passive:true});

  /* Animate the visible panel only after the existing app has selected it. */
  const animateActiveTab=()=>{
    const id="tab-"+((location.hash||"#dashboard").slice(1).split("?")[0]);
    const tab=document.getElementById(id);
    if(!tab)return;
    tab.animate(
      [{opacity:.72,transform:"translateY(7px)"},{opacity:1,transform:"translateY(0)"}],
      {duration:260,easing:"cubic-bezier(.2,.8,.2,1)"}
    );
  };
  window.addEventListener("hashchange",animateActiveTab,{passive:true});

  /* Relay state becomes a visual instrument: application text remains authoritative. */
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

  /* Flash the physical relay panels when their live state changes. */
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
  window.setInterval(relayPulse,500);

  /* Turn RSSI into a restrained communications-console signal meter. */
  const signalMeter=()=>{
    const targets=[
      document.querySelector("#wifi-rssi"),
      document.querySelector("#dash-rssi"),
      document.querySelector("#net-rssi"),
      document.querySelector("#diag-rssi")
    ];
    targets.forEach(el=>{
      if(!el)return;
      const n=parseInt((el.textContent||"").replace(/[^\d-]/g,""),10);
      if(!Number.isFinite(n))return;
      const quality=Math.max(0,Math.min(4,Math.round((n+90)/15)));
      el.dataset.lcarsSignal=String(quality);
    });
  };
  const signalObserver=new MutationObserver(signalMeter);
  signalObserver.observe(document.body,{subtree:true,childList:true,characterData:true});
  signalMeter();

})();
/* LCARS Modern — behavior layer
 * Decorative only. Existing application event handlers remain authoritative.
 */
(()=>{
  const boot=()=>{
    document.body.classList.add("lcars-ready");

    /* Dynamic LCARS left-side instrument field. The active tab controls
       the spine height so long pages get a continuous instrument structure. */
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

    const rebuildLeftField=()=>{
      const raw=(location.hash||"#dashboard").slice(1).split("?")[0];
      const tab=document.getElementById("tab-"+raw);
      const pageHeight=Math.max(
        document.documentElement.scrollHeight,
        document.body.scrollHeight,
        tab?.scrollHeight||0,
        window.innerHeight
      );
      leftField.style.height=pageHeight+"px";

      const shapes=[
        "bar","bar","block","block","step","step","bar","block",
        "bar","corner","block","step","bar","block","bar","step",
        "corner","bar","block","bar","step","block","bar","corner",
        "bar","block","step","bar","block","bar"
      ];
      const colors=["orange","purple","blue","magenta"];
      leftField.querySelectorAll(".lcars-left-shape").forEach(el=>el.remove());
      rightField.querySelectorAll(".lcars-left-shape").forEach(el=>el.remove());

      shapes.forEach((shape,index)=>{
        const el=document.createElement("i");
        el.className="lcars-left-shape "+shape+" "+colors[Math.floor(Math.random()*colors.length)];
        const width=8+Math.floor(Math.random()*54);
        const height=3+Math.floor(Math.random()*24);
        const top=2+Math.random()*94;
        const left=Math.random()*52;
        const duration=(3.2+Math.random()*6.8).toFixed(2);
        const delay=(-(Math.random()*duration)).toFixed(2);
        const opacity=(.20+Math.random()*.62).toFixed(2);
        const radius=Math.floor(Math.random()*9);
        el.style.cssText=
          "--shape-w:"+width+"px;"+
          "--shape-h:"+height+"px;"+
          "--shape-top:"+top.toFixed(2)+"%;"+
          "--shape-left:"+left.toFixed(2)+"px;"+
          "--shape-duration:"+duration+"s;"+
          "--shape-delay:"+delay+"s;"+
          "--shape-opacity:"+opacity+";"+
          "--shape-radius:"+radius+"px;";
        leftField.appendChild(el);

        const right=el.cloneNode();
        right.style.cssText=el.style.cssText;
        right.className=el.className;
        // Give the right bank its own independent geometry and timing.
        right.style.setProperty("--shape-w",(8+Math.floor(Math.random()*54))+"px");
        right.style.setProperty("--shape-top",(2+Math.random()*94).toFixed(2)+"%");
        right.style.setProperty("--shape-duration",(3.2+Math.random()*6.8).toFixed(2)+"s");
        right.style.setProperty("--shape-delay",(-(Math.random()*7)).toFixed(2)+"s");
        right.style.setProperty("--shape-opacity",(0.18+Math.random()*0.60).toFixed(2));
        rightField.appendChild(right);
      });
    };
    rebuildLeftField();


    /* Live LCARS date/time readout. It is injected into the external UI
       layer so removing LCARS also removes the clock cleanly. */
    const clock=document.createElement("div");
    clock.className="lcars-clock";
    clock.dataset.externalUi="true";
    clock.setAttribute("aria-label","Current date and time");
    clock.innerHTML='<div class="lcars-clock-time"></div><div class="lcars-clock-date"></div><span class="lcars-clock-rule"></span>';
    const nav=document.querySelector(".tabs");
    if(nav) nav.parentNode.insertBefore(clock,nav);
    const updateClock=()=>{
      const now=new Date();
      const time=now.toLocaleTimeString([],{
        hour:"numeric",
        minute:"2-digit",
        second:"2-digit"
      });
      const date=now.toLocaleDateString([],{
        weekday:"short",
        month:"short",
        day:"numeric",
        year:"numeric"
      });
      const t=clock.querySelector(".lcars-clock-time");
      const d=clock.querySelector(".lcars-clock-date");
      if(t)t.textContent=time;
      if(d)d.textContent=date;
    };
    updateClock();
    const tickClock=()=>{
      if(!clock.isConnected)return;
      updateClock();
      setTimeout(tickClock,1000);
    };
    setTimeout(tickClock,1000);

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
    requestAnimationFrame(()=>requestAnimationFrame(rebuildLeftField));
  };
  syncTabIdentity();
  window.addEventListener("hashchange",syncTabIdentity,{passive:true});
  window.addEventListener("resize",()=>rebuildLeftField(),{passive:true});
  if("ResizeObserver" in window){
    const leftResizeObserver=new ResizeObserver(()=>rebuildLeftField());
    const active=()=>document.getElementById("tab-"+((location.hash||"#dashboard").slice(1).split("?")[0]));
    const observeActive=()=>{
      leftResizeObserver.disconnect();
      const tab=active();
      if(tab)leftResizeObserver.observe(tab);
    };
    observeActive();
    window.addEventListener("hashchange",observeActive,{passive:true});
  }

  /* LCARS tab activation: the deck comes online in sections rather than
     appearing as one flat web page. Each panel gets a slightly different
     entry vector/timing, while controls remain fully interactive. */
  const animateActiveTab=()=>{
    const id="tab-"+((location.hash||"#dashboard").slice(1).split("?")[0]);
    const tab=document.getElementById(id);
    if(!tab)return;

    const sections=[...tab.querySelectorAll(".card")];
    const directions=[
      {x:-18,y:4},{x:14,y:0},{x:0,y:12},{x:10,y:-8},{x:-8,y:-6}
    ];

    tab.animate(
      [{opacity:.15},{opacity:1}],
      {duration:420,easing:"ease-out"}
    );

    sections.forEach((card,index)=>{
      const d=directions[Math.floor(Math.random()*directions.length)];
      // Keep the random transition lively, but bounded so no panel
      // ever feels slow or unpredictable.
      const delay=Math.floor(Math.random()*150)+index*42;
      const MIN_FADE_MS=360;
      const MAX_FADE_MS=520;
      const duration=MIN_FADE_MS+
        Math.floor(Math.random()*(MAX_FADE_MS-MIN_FADE_MS+1));
      card.animate(
        [
          {opacity:0,transform:"translate3d("+d.x+"px,"+d.y+"px,0) scale(.985)"},
          {opacity:1,transform:"translate3d(0,0,0) scale(1)"}
        ],
        {
          duration,
          delay,
          easing:"cubic-bezier(.16,.82,.24,1)",
          fill:"both"
        }
      );

      card.querySelectorAll(".eyebrow,.section-heading h2").forEach((label)=>{
        label.animate(
          [{opacity:0,filter:"brightness(2.2)",transform:"translateX(-6px)"},
           {opacity:1,filter:"brightness(1)",transform:"translateX(0)"}],
          {duration:280,delay:delay+90,easing:"ease-out",fill:"both"}
        );
      });
    });
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
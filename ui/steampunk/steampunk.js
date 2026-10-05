/* Steampunk decorative behavior. Application logic remains in /app.js. */
(()=> {
  let booted=false;
  const boot=()=>{
    if(booted)return;
    booted=true;
    document.body.classList.add("steampunk-ready");

    const decor=document.createElement("div");
    decor.className="sp-decor";
    decor.setAttribute("aria-hidden","true");
    decor.dataset.externalUi="true";
    decor.innerHTML='<div class="sp-pipe left"><i></i><i></i><i></i><i></i></div><div class="sp-pipe right"><i></i><i></i><i></i><i></i></div><div class="sp-gear g1"></div><div class="sp-gear g2"></div><div class="sp-gear g3"></div>';
    document.body.appendChild(decor);

    const title=document.querySelector("h1");
    if(title){
      title.textContent="ESP32-C3 RELAY CONTROLLER";
      title.dataset.steampunkTitle="true";
    }

    const subtitle=document.querySelector("body>.muted:first-of-type");
    if(subtitle)subtitle.textContent="Brassworks control console • headless relay automation";

    const stamp=document.createElement("div");
    stamp.className="sp-build-stamp";
    stamp.dataset.externalUi="true";
    stamp.innerHTML='<span>STEAMWORKS</span><strong>ESP32-C3</strong>';
    if(title)title.appendChild(stamp);

    const updateGears=()=>{
      decor.querySelectorAll(".sp-gear").forEach((g,i)=>{
        const speed=i===1?28:42;
        g.style.transform="rotate("+((Date.now()/speed)%360)+"deg)";
      });
      requestAnimationFrame(updateGears);
    };
    if(!matchMedia("(prefers-reduced-motion: reduce)").matches)requestAnimationFrame(updateGears);

    const statusPulse=()=>{
      document.querySelectorAll(".status").forEach(el=>{
        const text=(el.textContent||"").toUpperCase();
        el.classList.toggle("bad",/OFFLINE|FAILED|ERROR|DISCONNECTED/.test(text));
      });
    };
    statusPulse();
    new MutationObserver(statusPulse).observe(document.body,{subtree:true,childList:true,characterData:true});

    const relayState=()=>{
      [0,1].forEach(i=>{
        const state=document.querySelector("#relay-state-"+i);
        const button=document.querySelector("#relay-power-button");
        const button2=document.querySelector("#relay-reset-button");
        const target=i===0?button:button2;
        if(!state||!target)return;
        const on=/CLOSED|ON|ACTIVE/.test((state.textContent||"").toUpperCase());
        target.classList.toggle("good",!on);
        target.classList.toggle("bad",on);
      });
    };
    relayState();
    [0,1].forEach(i=>{
      const el=document.querySelector("#relay-state-"+i);
      if(el)new MutationObserver(relayState).observe(el,{subtree:true,childList:true,characterData:true});
    });

    const decorateCards=()=>{
      document.querySelectorAll(".card h2").forEach(h=>{
        if(!h.dataset.steampunkDecorated){
          h.dataset.steampunkDecorated="true";
          h.insertAdjacentHTML("afterbegin",'<span class="sp-rivet" aria-hidden="true">•</span> ');
        }
      });
    };
    decorateCards();
    new MutationObserver(decorateCards).observe(document.body,{subtree:true,childList:true});

    const clock=document.createElement("div");
    clock.dataset.externalUi="true";
    clock.className="sp-clock";
    clock.innerHTML='<span></span><strong></strong>';
    clock.style.cssText="position:fixed;right:24px;top:18px;z-index:130;padding:7px 12px;background:linear-gradient(#3a2718,#1b1009);border:2px solid #70451f;border-radius:6px;box-shadow:inset 0 1px #d4a35b,0 4px 10px #000;color:#dcb873;font:700 11px 'Courier New',monospace;letter-spacing:.08em;text-align:center";
    document.body.appendChild(clock);
    const tick=()=>{
      if(!clock.isConnected)return;
      const d=new Date();
      clock.querySelector("span").textContent=d.toLocaleDateString([], {month:"short",day:"2-digit",year:"numeric"});
      clock.querySelector("strong").textContent=d.toLocaleTimeString([], {hour:"2-digit",minute:"2-digit",second:"2-digit"});
    };
    tick();setInterval(tick,1000);
  };
  window.addEventListener("external-ui-activate",boot,{once:true});
  if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",boot,{once:true});else boot();
})();
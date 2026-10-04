/* THE MATRIX — lightweight digital rain
 * Decorative only. Pauses when the page is hidden and uses a modest frame rate.
 */
(()=>{
  const boot=()=>{
    document.body.dataset.externalUi="matrix";

    const canvas=document.createElement("canvas");
    canvas.className="matrix-rain";
    canvas.setAttribute("aria-hidden","true");
    document.body.prepend(canvas);

    const ctx=canvas.getContext("2d",{alpha:true});
    if(!ctx)return;

    const chars="アイウエオカキクケコサシスセソタチツテトナニヌネノ01ABCDEFGHIJKLMNOPQRSTUVWXYZ<>[]{}";
    let width=0,height=0,dpr=1,columns=0,drops=[];
    let last=0;
    const frameMs=55;

    const resize=()=>{
      dpr=Math.min(window.devicePixelRatio||1,1.5);
      width=window.innerWidth;
      height=window.innerHeight;
      canvas.width=Math.floor(width*dpr);
      canvas.height=Math.floor(height*dpr);
      canvas.style.width=width+"px";
      canvas.style.height=height+"px";
      ctx.setTransform(dpr,0,0,dpr,0,0);
      columns=Math.max(1,Math.floor(width/18));
      drops=Array.from({length:columns},()=>Math.floor(Math.random()*(height/18)));
    };

    const draw=(now)=>{
      if(document.hidden){
        last=now;
        requestAnimationFrame(draw);
        return;
      }
      if(now-last<frameMs){
        requestAnimationFrame(draw);
        return;
      }
      last=now;

      ctx.fillStyle="rgba(0,3,1,.16)";
      ctx.fillRect(0,0,width,height);
      ctx.font="14px monospace";

      for(let i=0;i<columns;i++){
        const x=i*18;
        const y=drops[i]*18;
        const bright=Math.random()>.88;
        ctx.fillStyle=bright?"rgba(183,255,202,.9)":"rgba(0,255,65,.42)";
        const ch=chars[Math.floor(Math.random()*chars.length)];
        ctx.fillText(ch,x,y);
        if(y>height && Math.random()>.975)drops[i]=0;
        else drops[i]++;
      }
      requestAnimationFrame(draw);
    };

    resize();
    window.addEventListener("resize",resize,{passive:true});
    requestAnimationFrame(draw);

    const banner=document.createElement("div");
    banner.className="matrix-terminal-banner";
    banner.textContent="SYSTEM // MATRIX_LINK";
    const cursor=document.createElement("span");
    cursor.className="matrix-cursor";
    banner.appendChild(cursor);
    document.body.appendChild(banner);
  };

  if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",boot,{once:true});
  else boot();
})();

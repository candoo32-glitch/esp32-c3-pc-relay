/* LCARS Modern — behavior layer
 * Decorative only. Existing application event handlers remain authoritative.
 */
(()=>{
  const boot=()=>{
    document.body.classList.add("lcars-ready");

    document.querySelectorAll(".card").forEach((card,i)=>{
      card.style.setProperty("--lcars-delay",(Math.min(i,18)*24)+"ms");
      card.animate(
        [{opacity:0,transform:"translateY(9px)"},{opacity:1,transform:"translateY(0)"}],
        {duration:360,delay:Math.min(i,18)*24,easing:"cubic-bezier(.2,.8,.2,1)",fill:"both"}
      );
    });

    const links=[...document.querySelectorAll("[data-tab-link]")];
    links.forEach(link=>{
      link.addEventListener("click",()=>{
        document.querySelectorAll(".tab").forEach(tab=>{
          if(!tab.hidden) tab.animate(
            [{opacity:.55,transform:"translateX(5px)"},{opacity:1,transform:"translateX(0)"}],
            {duration:220,easing:"ease-out"}
          );
        });
      },{passive:true});
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
      const observer=new MutationObserver(()=>{});
      observer.observe(diag,{childList:true,subtree:true});
      const poll=()=>{
        if(diag.value!==previous){
          previous=diag.value;
          diag.animate(
            [{boxShadow:"inset 0 0 28px rgba(0,0,0,.42)"},{boxShadow:"inset 0 0 34px rgba(115,226,164,.10)"},{boxShadow:"inset 0 0 28px rgba(0,0,0,.42)"}],
            {duration:320,easing:"ease-out"}
          );
        }
        requestAnimationFrame(poll);
      };
      requestAnimationFrame(poll);
    }
  };
  if(document.readyState==="loading")document.addEventListener("DOMContentLoaded",boot,{once:true});
  else boot();
})();

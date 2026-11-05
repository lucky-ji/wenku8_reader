// SPDX-License-Identifier: GPL-2.0-only
(()=>{
 if(location.origin!==ORIGIN)return false;
 if(window.__wenku8RememberLogin)return true;window.__wenku8RememberLogin=true;
 const touched=new WeakSet(),wired=new WeakSet();
 const apply=()=>document.querySelectorAll('form').forEach(form=>{
  // Identify the website form without ever reading username/password values.
  if(!form.querySelector('input[name="username"]')||!form.querySelector('input[name="password"]'))return;
  const field=form.querySelector('select[name="usecookie"],input[type="checkbox"][name="usecookie"]');if(!field||touched.has(field))return;
  if(!wired.has(field)){wired.add(field);field.addEventListener('change',event=>{if(event.isTrusted)touched.add(field);});}
  if(field.tagName==='SELECT' && (!field.value || Number(field.value)===0)){
   const choices=Array.from(field.options).filter(o=>!o.disabled&&Number(o.value)>0);
   const month=choices.find(o=>Number(o.value)===2592000);
   const choice=month||choices.sort((a,b)=>Number(a.value)-Number(b.value))[0];if(choice)field.value=choice.value;
  }else if(field.type==='checkbox')field.checked=true;
 });
 apply();new MutationObserver(apply).observe(document.documentElement,{subtree:true,childList:true});return true;
})();

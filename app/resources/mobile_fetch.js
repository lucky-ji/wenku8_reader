// SPDX-License-Identifier: GPL-2.0-only
(()=>{
 const c=CONFIG;
 if(location.origin!==new URL(c.url).origin)return false;
 const s={done:false,controller:new AbortController()};window.__wenku8SdkJob=s;
 (async()=>{try{
  const r=await fetch(c.url,{method:'GET',headers:c.headers,credentials:'same-origin',mode:'same-origin',cache:'no-store',redirect:c.follow?'follow':'manual',signal:s.controller.signal});
  if(r.type==='opaqueredirect'){s.error='redirect';return;}
  if(!r.status || new URL(r.url).origin!==location.origin){s.error='origin';return;}
  s.status=r.status;s.url=r.url;s.headers=Array.from(r.headers.entries());
  let length=0;const encoded=[];let carry=[];const reader=r.body&&r.body.getReader();
  if(!reader){s.error='network';return;}
  for(;;){const x=await reader.read();if(x.done)break;
   length+=x.value.length;if(length>c.limit){s.error='size';await reader.cancel();return;}
   for(let i=0;i<x.value.length;i++){carry.push(x.value[i]);if(carry.length===24576){encoded.push(btoa(String.fromCharCode.apply(null,carry)));carry=[];}}
  }
  if(carry.length)encoded.push(btoa(String.fromCharCode.apply(null,carry)));s.body=encoded.join('');
 }catch(e){s.error='network';}finally{s.done=true;}})();return true;
})()

// SPDX-License-Identifier: GPL-2.0-only
(()=>{
 const origin=ORIGIN,base=new URL(origin);
 if(location.origin!==base.origin)return false;
 if(window.__wenku8NavigationGuard)return true;
 const canonical=value=>{
  try{const u=new URL(value||location.href,document.baseURI);
   const alias=(base.hostname==='www.wenku8.net'&&u.hostname==='wenku8.net')||(base.hostname==='wenku8.net'&&u.hostname==='www.wenku8.net');
   if((u.hostname!==base.hostname&&!alias)||u.username||u.password)return null;
   if(u.protocol==='http:'){if(u.port||base.port)return null;}
   else if(u.protocol!=='https:'||u.port!==base.port)return null;
   return base.origin+u.pathname+u.search+u.hash;
  }catch(e){return null;}
 };
 const fix=element=>{
  if(!element||!element.getAttribute)return;
  let attribute;
  if(['A','AREA','BASE'].includes(element.tagName))attribute='href';
  else if(element.tagName==='FORM')attribute='action';
  else if(['BUTTON','INPUT'].includes(element.tagName)&&element.hasAttribute('formaction'))attribute='formaction';
  if(attribute){const old=element.getAttribute(attribute),url=canonical(old);
   if(url){if(old!==url)element.setAttribute(attribute,url);
    const target=attribute==='formaction'?'formtarget':'target';
    if(element.hasAttribute(target)&&element.getAttribute(target)!=='_self')element.setAttribute(target,'_self');
   }
  }
 };
 const scan=root=>{fix(root);if(root.querySelectorAll)root.querySelectorAll('base[href],a[href],area[href],form,button[formaction],input[formaction]').forEach(fix);};
 // Resolve a legacy <base> first so relative links retain their original path.
 document.querySelectorAll('base[href]').forEach(fix);scan(document);
 document.addEventListener('click',event=>{const a=event.target&&event.target.closest&&event.target.closest('a[href],area[href]');if(a)fix(a);},true);
 const beforeSubmit=event=>{fix(event.target);if(event.submitter)fix(event.submitter);};
 document.addEventListener('submit',beforeSubmit,true);
 document.addEventListener('submit',beforeSubmit,false);
 const submit=HTMLFormElement.prototype.submit;
 HTMLFormElement.prototype.submit=function(){fix(this);return submit.apply(this,arguments);};
 window.open=function(url){const target=canonical(url);if(target){location.assign(target);return window;}return null;};
 new MutationObserver(records=>{for(const record of records){if(record.type==='attributes')fix(record.target);else record.addedNodes.forEach(scan);}})
  .observe(document.documentElement,{subtree:true,childList:true,attributes:true,attributeFilter:['href','action','target','formaction','formtarget']});
 window.__wenku8NavigationGuard=true;return true;
})();

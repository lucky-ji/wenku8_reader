// SPDX-License-Identifier: GPL-2.0-only
(()=>{
 if(location.origin!==ORIGIN)return false;
 if(window.__wenku8LoginUI){window.__wenku8LoginUI();return true;}
 const apply=()=>{
  const form=Array.from(document.querySelectorAll('form')).find(f=>f.querySelector('input[name="username"]')&&f.querySelector('input[name="password"]'));
  if(!form)return; // Keep site verification/challenge pages functional.
  if(!document.getElementById('wenku8-login-ui')){
   const style=document.createElement('style');style.id='wenku8-login-ui';
   style.textContent=`[data-wenku8-login-hidden="1"]{display:none!important}body{margin:0!important;background:#f8fafc!important;color:#233044!important;font-size:16px!important}
    [data-wenku8-login-path="1"]{max-width:100%!important;min-width:0!important;box-sizing:border-box!important}table[data-wenku8-login-path="1"]{width:100%!important}
    form[data-wenku8-login-form="1"]{max-width:440px!important;margin:16px auto!important;padding:16px!important;box-sizing:border-box!important;background:#fff!important;border-radius:16px}
    form input[name="username"],form input[name="password"],form input[name="checkcode"]{max-width:100%!important;box-sizing:border-box!important;min-height:44px!important;border:1px solid #ced7e3!important;border-radius:9px!important;padding:8px!important;font-size:16px!important}
    form button:not([type]),form input[type="submit"],form button[type="submit"]{min-height:44px!important;border:0!important;border-radius:10px!important;background:#315b46!important;color:white!important;padding:10px 24px!important;font-size:16px!important}`;
   (document.head||document.documentElement).appendChild(style);
   let viewport=document.querySelector('meta[name="viewport"]');if(!viewport){viewport=document.createElement('meta');viewport.name='viewport';(document.head||document.documentElement).appendChild(viewport);}viewport.content='width=device-width, initial-scale=1';
  }
  form.setAttribute('data-wenku8-login-form','1');
  for(let node=form;node&&node.setAttribute;node=node.parentElement){
   node.setAttribute('data-wenku8-login-path','1');const parent=node.parentElement;if(!parent||parent.tagName==='HTML')break;
   for(const sibling of Array.from(parent.children)){
    if(sibling===node||['SCRIPT','STYLE','LINK','META'].includes(sibling.tagName))continue;
    if((sibling.matches&&sibling.matches('iframe[src*="challenges.cloudflare.com"],.cf-turnstile'))||(sibling.querySelector&&sibling.querySelector('iframe[src*="challenges.cloudflare.com"],.cf-turnstile')))continue;
    sibling.setAttribute('data-wenku8-login-hidden','1');
   }
  }
  // Keep submit and verification controls; reset/account links do not belong
  // in the focused in-app login flow. Never inspect credential field values.
  form.querySelectorAll('input[type="reset"],input[type="button"],button[type="reset"],button[type="button"]').forEach(control=>{
   const behavior=(control.getAttribute('onclick')||'')+' '+(control.getAttribute('name')||'')+' '+(control.getAttribute('id')||'');
   const caption=(control.textContent||'')+' '+(control.getAttribute('value')||'');
   const verification=/checkcode|captcha|turnstile/i.test(behavior)||/验证码|看不清|换一张|验证/.test(caption)||control.querySelector('img[src*="checkcode"],img[src*="captcha"]');
   const auxiliary=(control.getAttribute('type')||'').toLowerCase()==='reset'||/注册|重置|清空|忘记密码|找回密码/.test(caption)||/register|regnew|lostpass|resetform/i.test(behavior);
   if(auxiliary&&!verification)control.setAttribute('data-wenku8-login-hidden','1');
  });
  form.querySelectorAll('a').forEach(link=>{
   const behavior=(link.getAttribute('href')||'')+' '+(link.getAttribute('onclick')||'');
   const captcha=/checkcode|captcha/i.test(behavior)||/验证码|看不清|换一张/.test(link.textContent||'')||link.querySelector('img[src*="checkcode"],img[src*="captcha"]');
   if(!captcha)link.setAttribute('data-wenku8-login-hidden','1');
  });
 };
 window.__wenku8LoginUI=apply;apply();new MutationObserver(apply).observe(document.documentElement,{subtree:true,childList:true});return true;
})();

#pragma once

namespace xenon::extension_manager {
// Fixed presentation for Xenon's own manager WebUI only. This has no native
// bridge, privileged calls, website styling, or caller-supplied script/data.
inline constexpr const char* ui_script=R"JS((()=>{
  if(location.protocol!=='chrome:'||location.hostname!=='extensions')return;
  const observed=new WeakSet();
  const normalizeLink=event=>{
    const link=event.composedPath().find(node=>node instanceof HTMLAnchorElement);
    if(!link)return;
    let url;try{url=new URL(link.href);}catch{return;}
    if(url.origin==='https://chromewebstore.google.com'||
       (url.origin==='https://chrome.google.com'&&url.pathname.startsWith('/webstore/')))
      link.target='_self';
  };
  const observer=new MutationObserver(()=>visit(document));
  const visit=root=>{
    if(!observed.has(root)){
      observed.add(root);observer.observe(root,{childList:true,subtree:true});
      root.addEventListener('click',normalizeLink,true);
    }
    const host=root.host?.localName;
    if(['extensions-sidebar','cr-drawer','cr-toolbar'].includes(host)&&!root.querySelector('#xenon-extension-branding')){
      const style=document.createElement('style');style.id='xenon-extension-branding';
      style.textContent=host==='extensions-sidebar'?
        '#web-store-icon { display:none !important; }':host==='cr-drawer'?
        'slot[name="header-icon"] { display:none !important; }':
        'slot[name="product-logo"] { display:none !important; }';
      root.appendChild(style);
    }
    for(const node of root.querySelectorAll('extensions-manager,extensions-sidebar,extensions-toolbar,cr-drawer,cr-toolbar'))
      if(node.shadowRoot)visit(node.shadowRoot);
  };
  visit(document);
  for(const name of ['extensions-manager','extensions-sidebar','extensions-toolbar','cr-drawer','cr-toolbar'])
    customElements.whenDefined(name).then(()=>visit(document));
})())JS";
}

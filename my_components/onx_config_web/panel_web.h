#pragma once
// Extra script for the configuration page on Room Companion touch panels. The
// knob's page is unchanged; on panels this adapts its wording and preview,
// enables the panel-local controls in the entity picker, and adds the
// dashboard stream settings. Empty on the knob.
namespace esphome::onx_config_web {
#ifdef USE_ROOM_COMPANION
static const char PANEL_JS[] = R"ONXPANEL(
(()=>{
 'use strict';
 globalThis.ONX_PANEL=true;
 const $=id=>document.getElementById(id);

 // Wording: this page is shared with the knob.
 const swap=s=>s
  .replace(/OpenNextion Knob/g,'OpenNextion Room Companion')
  .replace(/Knob sensitivity/g,'Swipe sensitivity')
  .replace(/press the knob/g,'touch the screen')
  .replace(/the physical knob/g,'the panel')
  .replace(/\bKnob\b/g,'Panel')
  .replace(/\bknob\b/g,'panel');
 const fix=node=>{
  if(node.nodeType===3){const v=swap(node.nodeValue);if(v!==node.nodeValue)node.nodeValue=v;return}
  if(node.nodeType!==1||node.tagName==='SCRIPT'||node.tagName==='STYLE')return;
  for(const a of ['title','aria-label','placeholder']){const v=node.getAttribute&&node.getAttribute(a);if(v){const n=swap(v);if(n!==v)node.setAttribute(a,n)}}
  for(const child of node.childNodes)fix(child);
 };
 document.title=swap(document.title);fix(document.body);
 new MutationObserver(list=>{for(const m of list){if(m.type==='characterData')fix(m.target);else m.addedNodes.forEach(fix)}})
  .observe(document.body,{subtree:true,childList:true,characterData:true});

 // The panel shows the knob layout in a square area above its icon strip.
 const style=document.createElement('style');
 style.textContent='.knob{border-radius:18px!important}';
 document.head.append(style);

 // Same request protocol as the main page.
 async function rpc(values){
  const post=await fetch('/knob/api',{method:'POST',cache:'no-store',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Onx-Token':window.ONX_TOKEN},body:new URLSearchParams(values)});
  const queued=await post.json();if(!post.ok||queued.error)throw Error(queued.error||'The panel could not complete this request');
  for(let i=0;i<60;i++){
   await new Promise(r=>setTimeout(r,150));
   const response=await fetch('/knob/result?ticket='+queued.ticket,{cache:'no-store'});
   if(response.status!==202){const body=await response.json();if(body.error)throw Error(body.error);return body}
  }
  throw Error('The panel did not confirm this change.');
 }

 // Dashboard stream settings, below Device settings.
 const anchor=$('settings-form');
 if(!anchor)return;
 const form=document.createElement('form');
 form.className='panel';form.id='panel-dashboard';
 form.innerHTML='<h2>Home Assistant dashboard</h2>'+
  '<p class="hint">Show an interactive Home Assistant dashboard on the panel. A Remote WebView server renders it and streams the picture; '+
  'native screens still appear for Assist and notifications. Add <strong>Dashboard</strong> as a control (listed under “On this panel”) '+
  'to open it from the icon strip, or use the Dashboard switch in Home Assistant. Hold the top-left corner of the screen for a second to return.</p>'+
  '<div class="field-grid">'+
  '<label class="field">Remote WebView server<input id="dash-server" maxlength="96" placeholder="192.168.1.10:8081" autocomplete="off"></label>'+
  '<label class="field">Zoom<input id="dash-scale" type="number" min="0.5" max="4" step="0.25"></label>'+
  '</div>'+
  '<label class="field">Dashboard address<input id="dash-url" maxlength="256" placeholder="http://homeassistant.local:8123/room-panel/0" autocomplete="off"></label>'+
  '<p class="hint">Design the dashboard for the panel’s size, without animations: these screens update a full frame in roughly a tenth of a second. '+
  'Changing the server or zoom needs a restart.</p>'+
  '<p class="hint" id="dash-status" role="status"></p>'+
  '<div class="settings-actions"><button class="primary" type="submit">Save dashboard</button></div>';
 anchor.after(form);
 const show=state=>{
  $('dash-server').value=state.server||'';$('dash-url').value=state.url||'';$('dash-scale').value=state.scale||1;
  $('dash-status').textContent='Stream: '+state.status+(state.restart_required?' · restart the panel to apply the new server or zoom':'');
 };
 rpc({op:'panel_state'}).then(show).catch(e=>{$('dash-status').textContent=e.message});
 form.onsubmit=async event=>{
  event.preventDefault();
  const button=form.querySelector('button');button.disabled=true;
  try{
   show(await rpc({op:'panel_save',data:JSON.stringify({server:$('dash-server').value.trim(),url:$('dash-url').value.trim(),scale:parseFloat($('dash-scale').value)||1})}));
   $('dash-status').textContent='Saved. '+$('dash-status').textContent;
  }catch(e){$('dash-status').textContent=e.message}
  finally{button.disabled=false}
 };
})();
)ONXPANEL";
#else
static const char PANEL_JS[] = "";
#endif
}  // namespace esphome::onx_config_web

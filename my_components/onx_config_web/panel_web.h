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
 document.querySelectorAll('.controls dt').forEach((n,i)=>{n.textContent=['Swipe','Tap','Edge swipe'][i]||n.textContent});
 new MutationObserver(list=>{for(const m of list){if(m.type==='characterData')fix(m.target);else m.addedNodes.forEach(fix)}})
  .observe(document.body,{subtree:true,childList:true,characterData:true});

 // Rectangular panel previews retain the real aspect ratio.
 const style=document.createElement('style');
 style.textContent='.knob{border-radius:18px!important;box-sizing:content-box;width:calc(100% - 16px);max-width:240px;height:auto!important;aspect-ratio:2/3!important}.knob svg{width:100%;height:100%}.panel-page-layout{margin:18px 0;padding:16px;border:1px solid var(--line);border-radius:8px}.panel-page-layout select{width:100%}';
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

 // Pages compose configured entities: subscriptions remain owned by slots.
 let pageConfigs=[],pageError='',editorSlots=[],editorActive=0,editorPreset=false,redraw=()=>{};
 const drafts=new Map();
 const pageFor=entity=>drafts.get(entity)||pageConfigs.find(p=>p.entity===entity)||{entity,layout:'single',members:[]};
 const editor=document.createElement('section');editor.className='panel-page-layout';editor.id='panel-page-layout';
 editor.innerHTML='<h3>Page layout</h3><p class="hint">Use the full rectangular screen. Add and save entities in the slots first, then group them on this page. Tap an item to open its controls; swipe to change pages.</p><label class="field">Layout<select id="page-kind"><option value="single">One control</option><option value="two_controls">Two controls</option><option value="control_sensors">One control + three sensors</option></select></label><div id="page-members"></div><button type="button" id="save-page-layout">Save page layout</button><p class="hint" id="page-layout-status" role="status"></p>';
 $('slot-panel-display')?.append(editor);
 const labelOf=s=>s.name||s.entity;
 function renderPageEditor(){
  const slot=editorSlots[editorActive];editor.hidden=editorPreset||!slot?.entity;if(!slot?.entity)return;
  const p=pageFor(slot.entity);$('page-kind').value=p.layout;
  for(const option of $('page-kind').options)option.disabled=option.value!=='single'&&/^(sensor|binary_sensor)\./.test(slot.entity);
  const options=p.layout==='control_sensors'?editorSlots.filter(s=>/^(sensor|binary_sensor)\./.test(s.entity)):editorSlots.filter(s=>s.entity&&!/^(sensor|binary_sensor)\./.test(s.entity));
  const count=p.layout==='two_controls'?1:p.layout==='control_sensors'?3:0;
  $('page-members').replaceChildren();
  for(let i=0;i<count;i++){
   const label=document.createElement('label');label.className='field';label.textContent=p.layout==='two_controls'?'Second control':'Sensor '+(i+1);
   const select=document.createElement('select');select.setAttribute('aria-label',label.textContent);select.dataset.member=i;
   select.add(new Option('Choose an entity',''));
   for(const s of options)if(s.entity!==slot.entity)select.add(new Option(labelOf(s),s.entity));
   if(p.members[i]&&!options.some(s=>s.entity===p.members[i]))select.add(new Option('Missing: '+p.members[i],p.members[i]));
   select.value=p.members[i]||'';select.onchange=()=>{const d=structuredClone(pageFor(slot.entity));d.members[i]=select.value;drafts.set(slot.entity,d);redraw();$('page-layout-status').textContent='Page layout not saved.'};
   label.append(select);$('page-members').append(label);
  }
  $('page-layout-status').textContent=pageError|| (drafts.has(slot.entity)?'Page layout not saved.':'Layout saved separately from entity settings.');
 }
 $('page-kind').onchange=()=>{const entity=editorSlots[editorActive]?.entity;if(!entity)return;drafts.set(entity,{entity,layout:$('page-kind').value,members:[]});renderPageEditor();redraw()};
 globalThis.ONX_PANEL_EDITOR=(slots,active,busy,refresh,isPreset=false)=>{
  editorSlots=slots;editorActive=active;editorPreset=isPreset;redraw=refresh;renderPageEditor();
  $('save-page-layout').disabled=busy||isPreset||!slots[active]?.entity;
 };
 $('save-page-layout').onclick=async()=>{
  const entity=editorSlots[editorActive]?.entity;if(!entity)return;
  const p=pageFor(entity),count=p.layout==='two_controls'?1:p.layout==='control_sensors'?3:0;
  const members=p.members.slice(0,count);
  if(members.length!==count||members.some(m=>!m)||new Set([entity,...members]).size!==count+1){$('page-layout-status').textContent='Choose a different entity for every position.';return}
  $('save-page-layout').disabled=true;
  try{const result=await rpc({op:'panel_page_save',data:JSON.stringify({...p,members})});pageConfigs=result.pages||[];drafts.delete(entity);pageError='';renderPageEditor();$('page-layout-status').textContent='Page layout saved.';redraw()}
  catch(error){$('page-layout-status').textContent=error.message}
  finally{$('save-page-layout').disabled=false}
 };
 rpc({op:'panel_pages'}).then(result=>{pageConfigs=result.pages||[];renderPageEditor();redraw()}).catch(error=>{pageError=error.message;renderPageEditor()});
 globalThis.ONX_PANEL_GEOMETRY=(kind,w,h,detail=false)=>{
  const top=56,bottom=detail?12:52,gap=8,pad=12,body=h-top-bottom;
  const rects=[{x:pad,y:top,w:w-pad*2,h:body}];
  if(kind==='two_controls'){const half=Math.floor((body-gap)/2);rects[0].h=half;rects.push({x:pad,y:top+half+gap,w:w-pad*2,h:body-half-gap})}
  if(kind==='control_sensors'){const main=Math.floor((body-gap)*55/100),row=Math.floor((body-main-gap*3)/3);rects[0].h=main;for(let i=0;i<3;i++)rects.push({x:pad,y:top+main+gap+i*(row+gap),w:w-pad*2,h:row})}
  return rects;
 };
 const ns='http://www.w3.org/2000/svg';
 const node=(tag,attrs={})=>{const n=document.createElementNS(ns,tag);for(const [key,value]of Object.entries(attrs))n.setAttribute(key,value);return n};
 globalThis.ONX_PANEL_PREVIEW=(svg,slot,view,views,screen,settings)=>{
  const {width:w,height:h}=globalThis.ONX_PANEL_SIZE||{width:320,height:480},detail=screen!=='main';
  svg.setAttribute('viewBox',`0 0 ${w} ${h}`);svg.closest('.knob').style.aspectRatio=`${w}/${h}`;
  svg.closest('.knob').style.setProperty('aspect-ratio',`${w}/${h}`,'important');
  svg.setAttribute('aria-label',(slot.name||slot.entity||'Empty page')+' rectangular panel preview');
  svg.replaceChildren(node('rect',{width:w,height:h,fill:'#101418'}));
  const text=(s,x,y,size,width,color='#ffffff')=>{
   const t=node('text',{x,y:y+size*.85,'font-size':size,fill:color,'font-family':'Arial, sans-serif'});
   let value=String(s||'');const limit=Math.max(2,Math.floor(width/(size*.56)));if(value.length>limit)value=value.slice(0,limit-1)+'…';t.textContent=value;svg.append(t);
  };
  const icon=(name,x,y,size)=>{if(/^mdi:[a-z0-9-]+$/.test(name||''))svg.append(node('image',{href:'https://api.iconify.design/mdi/'+name.slice(4)+'.svg?color=%23ffffff',x,y,width:size,height:size}))};
  if(!slot.entity){text('Choose an entity',24,h/2,24,w-48);$('preview-status').textContent='Choose what this page displays.';return}
  // The media player has its own full-height artwork and transport view.
  if(detail&&view?.layout==='media'){
   const scale=w/240,cover=Math.max(48,Math.min(w-24,Math.floor(h*.4),h-56-Math.round(64*scale)-Math.round(12*scale)-20-84)),textY=56+cover+12,barY=h-Math.round(64*scale)-Math.round(12*scale)-20;
   if(view.artwork)try{const url=new URL(view.artwork);if(['http:','https:'].includes(url.protocol))svg.append(node('image',{href:url.href,x:(w-cover)/2,y:56,width:cover,height:cover,preserveAspectRatio:'xMidYMid meet'}))}catch{}
   const lines=view.lines||[];text(lines[2]?.text||slot.name||slot.entity,12,textY,22,w-24);text(lines[3]?.text||'',12,textY+28,18,w-24,'#9c9c9e');text(lines[1]?.text||'',12,textY+52,16,w-24,'#d2d2d2');
   svg.append(node('rect',{x:20,y:barY,width:w-40,height:5,rx:2,fill:'#414850'}));if(view.progress>=0)svg.append(node('rect',{x:20,y:barY,width:(w-40)*view.progress/1000,height:5,rx:2,fill:settings.media_color||'#62b0ff'}));
   text('◀       '+(view.playing?'Ⅱ':'▶')+'       ▶',40,h-Math.round(64*scale)-Math.round(12*scale)+12,28,w-80);
  }else{
   const p=detail?{layout:'single',members:[]}:pageFor(slot.entity),ids=[slot.entity,...p.members],rects=globalThis.ONX_PANEL_GEOMETRY(p.layout,w,h,detail);
   for(const [i,r]of rects.entries()){
    const base=views.find(v=>v.entity===ids[i]),v=!detail&&base?{...base,...base.carousel}:base;
    const compact=p.layout==='control_sensors'&&i>0,lines=v?.lines||[];
    svg.append(node('rect',{x:r.x,y:r.y,width:r.w,height:r.h,rx:8,fill:'#232323','data-panel-item':i}));
    const title=i===0?(slot.name||lines[4]?.text||ids[i]):(editorSlots.find(s=>s.entity===ids[i])?.name||lines[4]?.text||ids[i]||'Choose an entity');
    const small=14*w/240,normal=24*w/240,large=32*w/240,valueSize=compact?(r.h>=normal+8?normal:small):(r.h<150?normal:large);
    text(title,r.x+12,r.y+(compact?(r.h-small)/2:12),small,compact?r.w*.55-12:p.layout==='two_controls'?r.w-64:r.w-24);
    text(lines[1]?.text||(ids[i]?'Waiting for data':'—'),r.x+(compact?r.w*.55:12),r.y+(compact?(r.h-valueSize)/2:r.h*(v?.layout==='graph'?.18:p.layout==='two_controls'?.35:.49)),valueSize,compact?r.w*.45-12:r.w-24);
    let context=/^(sensor|binary_sensor)\./.test(ids[i]||'')&&lines[4]?.text&&lines[4]?.text!==title?lines[4].text:lines[2]?.text||'';
    if(detail&&lines[0]?.text)context=lines[0].text+(context?' · '+context:'');
    if(!compact){text(context,r.x+12,r.y+r.h*.67,small,r.w-24,'#d2d2d2');if(detail)text(lines[3]?.text||'',r.x+12,r.y+r.h-48,small,r.w-24,'#d2d2d2');if(v?.layout!=='graph')icon(v?.icon,r.x+r.w-(p.layout==='two_controls'?40:54),r.y+(p.layout==='two_controls'?12:28),p.layout==='two_controls'?26:40)}
    if(detail&&v?.layout==='graph'){
     const points=v.graph||[],now=v.graph_now||0,values=points.map(p=>p.value),lo=Math.min(...values),hi=Math.max(...values),scale=w/240;
     const left=r.x+(r.w-186*scale)/2,top=r.y+r.h*.38,plot=42*scale,pw=143*scale,ph=64*scale;
     for(const off of [0,ph/2,ph])svg.append(node('line',{x1:left+plot,y1:top+off,x2:left+185*scale,y2:top+off,stroke:'#344653'}));
     text(v.graph_max_label||'',left,top,12*scale,plot);text(v.graph_min_label||'',left,top+ph-14*scale,12*scale,plot);
     for(let j=1;j<points.length;j++){const a=points[j-1],b=points[j];if(b.t-a.t>1200)continue;const x=p=>left+185*scale-(now-p.t)*pw/43200,y=p=>top+(hi>lo?ph-(p.value-lo)*ph/(hi-lo):ph/2);svg.append(node('line',{x1:x(a),y1:y(a),x2:x(b),y2:y(b),stroke:settings.media_color||'#62b0ff','stroke-width':2}))}
    }
    if(detail&&v?.value_ring>=0){svg.append(node('rect',{x:r.x+12,y:r.y+r.h-16,width:r.w-24,height:4,fill:'#414850'}));svg.append(node('rect',{x:r.x+12,y:r.y+r.h-16,width:(r.w-24)*v.value_ring/1000,height:4,fill:settings.media_color||'#62b0ff'}))}
   }
   if(!detail)for(const item of view?.carousel?.navigator||[])icon(item.icon,w/2+item.offset*54-10,h-32,20);
  }
  icon('mdi:monitor-dashboard',16,12,28);icon('mdi:bell-outline',w-44,12,28);
  $('preview-status').textContent=view?'Live rectangular panel view':'Save configured entities to load live readings.';
 };

 // Dashboard stream settings, below Device settings.
 const anchor=$('settings-form');
 if(!anchor)return;
 const form=document.createElement('form');
 form.className='panel';form.id='panel-dashboard';
 form.innerHTML='<h2>Home Assistant dashboard</h2>'+
  '<p class="hint">Show an interactive Home Assistant dashboard on the panel. A Remote WebView server renders it and streams the picture; '+
  'native screens still appear for Assist and notifications. Add <strong>Dashboard</strong> as a control (listed under “On this panel”) '+
  'to open it from the icon strip, or use the Dashboard switch in Home Assistant. Swipe left or right to return.</p>'+
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

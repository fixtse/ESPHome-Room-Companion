#pragma once
// Extra script for the configuration page on the Room Companion touch panels:
// the rectangular preview, page layouts, orientation labels, the panel-local
// controls in the entity picker and the dashboard stream settings.
namespace esphome::onx_config_web {
#ifdef USE_ROOM_COMPANION
static const char PANEL_JS[] = R"ONXPANEL(
(()=>{
 'use strict';
 globalThis.ONX_PANEL=true;
 const $=id=>document.getElementById(id);

 const style=document.createElement('style');
 // The page stylesheet draws the device frame; drawPreview sets its aspect ratio.
 style.textContent='.screen-frame{aspect-ratio:2/3}';
 document.head.append(style);

 // Same request protocol as the main page.
 async function rpc(values){
  const post=await fetch('/panel/api',{method:'POST',cache:'no-store',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Onx-Token':window.ONX_TOKEN},body:new URLSearchParams(values)});
  const queued=await post.json();if(!post.ok||queued.error)throw Error(queued.error||'The panel could not complete this request');
  for(let i=0;i<60;i++){
   await new Promise(r=>setTimeout(r,150));
   const response=await fetch('/panel/result?ticket='+queued.ticket,{cache:'no-store'});
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
 // Mirrors onx_panel_layout::geometry (portrait and landscape).
 globalThis.ONX_PANEL_GEOMETRY=(kind,w,h,detail=false)=>{
  const top=detail?12:40,bottom=detail?12:52,gap=8,pad=12,body=h-top-bottom,inner=w-pad*2;
  const rects=[{x:pad,y:top,w:inner,h:body}];
  if(w>h){
   if(kind==='two_controls'){const half=Math.floor((inner-gap)/2);rects[0].w=half;rects.push({x:pad+half+gap,y:top,w:inner-half-gap,h:body})}
   if(kind==='control_sensors'){const main=Math.floor((inner-gap)*58/100),row=Math.floor((body-gap*2)/3),x=pad+main+gap;rects[0].w=main;for(let i=0;i<3;i++)rects.push({x,y:top+i*(row+gap),w:inner-main-gap,h:i===2?body-2*(row+gap):row})}
   return rects;
  }
  if(kind==='two_controls'){const half=Math.floor((body-gap)/2);rects[0].h=half;rects.push({x:pad,y:top+half+gap,w:inner,h:body-half-gap})}
  if(kind==='control_sensors'){const main=Math.floor((body-gap)*55/100),row=Math.floor((body-main-gap*3)/3);rects[0].h=main;for(let i=0;i<3;i++)rects.push({x:pad,y:top+main+gap+i*(row+gap),w:inner,h:row})}
  return rects;
 };
 // Accent tints, as onx_display_settings::mix().
 const mix=(a,b,weight)=>{const p=c=>parseInt(c.slice(1),16),x=p(a),y=p(b);let out=0;for(const shift of [0,8,16]){const ca=(x>>shift)&255,cb=(y>>shift)&255;out|=Math.floor((ca*weight+cb*(100-weight)+50)/100)<<shift}return '#'+out.toString(16).padStart(6,'0')};
 // The panel reports its native (portrait) size; the selected orientation turns it.
 let nativeSize=null;
 rpc({op:'panel_screen'}).then(r=>{if(r.width&&r.height){nativeSize={width:r.width,height:r.height};redraw()}}).catch(()=>{});
 globalThis.ONX_PANEL_SCREEN=settings=>{
  const base=globalThis.ONX_PANEL_SIZE||nativeSize||{width:320,height:480},turned=['90','270'].includes(String(settings?.orientation||'0'));
  const portrait={width:Math.min(base.width,base.height),height:Math.max(base.width,base.height)};
  return turned?{width:portrait.height,height:portrait.width}:portrait;
 };
 const ns='http://www.w3.org/2000/svg';
 const node=(tag,attrs={})=>{const n=document.createElementNS(ns,tag);for(const [key,value]of Object.entries(attrs))n.setAttribute(key,value);return n};
 globalThis.ONX_PANEL_PREVIEW=(svg,slot,view,views,screen,settings)=>{
  const {width:w,height:h}=globalThis.ONX_PANEL_SCREEN(settings),detail=screen!=='main';
  const background=settings?.background_color||'#101418',rgb=parseInt(background.slice(1),16),light=((rgb>>16)*299+((rgb>>8)&255)*587+(rgb&255)*114)>150000;
  const foreground=light?'#161b22':'#ffffff',surface=mix(light?'#000000':'#ffffff',background,8),controlColor=mix(light?'#000000':'#ffffff',background,13),trackColor=mix(light?'#000000':'#ffffff',background,23),mutedColor=mix(foreground,background,68);
  const accent=settings?.media_color||'#62b0ff',strong=mix(accent,'#000000',80),soft=mix(accent,background,28);
  svg.setAttribute('viewBox',`0 0 ${w} ${h}`);
  svg.closest('.screen-frame').style.setProperty('aspect-ratio',`${w}/${h}`,'important');
  svg.closest('.screen-frame').style.setProperty('max-width',w>h?'360px':'240px','important');
  svg.setAttribute('aria-label',(slot.name||slot.entity||'Empty page')+' rectangular panel preview');
  svg.replaceChildren(node('rect',{width:w,height:h,fill:background}));
  const text=(s,x,y,size,width,color=foreground,anchor='start')=>{
   const t=node('text',{x,y:y+size*.85,'font-size':size,fill:color,'font-family':'Arial, sans-serif','text-anchor':anchor});
   let value=String(s||'');const limit=Math.max(2,Math.floor(width/(size*.56)));if(value.length>limit)value=value.slice(0,limit-1)+'…';t.textContent=value;svg.append(t);
  };
  const icon=(name,x,y,size,color=foreground)=>{if(/^mdi:[a-z0-9-]+$/.test(name||''))svg.append(node('image',{href:'https://api.iconify.design/mdi/'+name.slice(4)+'.svg?color='+encodeURIComponent(color),x,y,width:size,height:size}))};
  if(!slot.entity){text('Choose an entity',24,h/2,24,w-48);$('preview-status').textContent='Choose what this page displays.';return}
  const P=n=>n*Math.min(w,h)/320,muted=mutedColor,control=controlColor;
  const rect=(x,y,width,height,fill=surface,radius=P(14),attrs={})=>svg.appendChild(node('rect',{x,y,width,height,rx:radius,fill,...attrs}));
  const onColor=color=>{const c=parseInt(color.slice(1),16);return ((c>>16)*299+((c>>8)&255)*587+(c&255)*114)>150000?'#161b22':'#ffffff'};
  const button=(caption,x,y,width,height,fill=control)=>{rect(x,y,width,height,fill,height/2,{'data-panel-control':caption});text(caption,x+width/2,y+(height-P(20))/2,P(20),width-P(12),onColor(fill),'middle')};
  const reading=(value,unit='')=>value!==undefined&&value!==null&&value!==''&&!['unknown','unavailable','None'].includes(String(value))?String(value)+unit:'—';
  const condition=s=>({'partlycloudy':'Partly cloudy','clear-night':'Clear','lightning-rainy':'Storm','snowy-rainy':'Sleet'}[s]||String(s||'').replaceAll('_',' ').replace(/^./,c=>c.toUpperCase()));
  const tile=(label,value,x,y,width,height)=>{rect(x,y,width,height);const room=width-P(20),fit=(s,size)=>Math.max(P(12),Math.min(P(size),room/(String(s).length*.56)));text(label,x+P(10),y+P(8),fit(label,16),room,muted);text(value,x+P(10),y+P(30),fit(value,20),room)};
  const artwork=(url,x,y,size,radius=P(10))=>{
   rect(x,y,size,size,control,radius);
   if(url)try{const u=new URL(url);if(['http:','https:'].includes(u.protocol)){
    const id='art-'+svg.querySelectorAll('clipPath').length,clip=node('clipPath',{id});clip.append(node('rect',{x,y,width:size,height:size,rx:radius}));svg.append(clip);
    svg.append(node('image',{href:u.href,x,y,width:size,height:size,preserveAspectRatio:'xMidYMid meet','clip-path':`url(#${id})`,'data-panel-artwork':''}));return;
   }}catch{}
   icon('mdi:music',x+size*.3,y+size*.3,size*.4,muted);
  };
  const weather=(v,title,r,isDetail)=>{
   const d=v?.panel||{},pad=P(16),x=r.x+pad,inner=r.w-2*pad,forecast=d.forecast||[];
   text(title,x,r.y+pad,P(isDetail?20:16),inner,mutedColor);
   const temp=reading(d.temperature,'°'),tempWidth=inner*.62,size=Math.min(P(isDetail&&w<h?85:r.h>P(240)&&w<h?85:43),tempWidth/(temp.length*.56)),top=r.y+pad+P(isDetail?24:20);
   text(temp,x,top,size,tempWidth);
   const iconSize=Math.min(P(64),inner*.30);icon(v?.icon||'mdi:weather-cloudy',x+inner-iconSize,top,iconSize);
   let y=top+size+P(8);
   text(condition(d.state)||v?.lines?.[0]?.text||'Weather',x,y,P(20),inner);y+=P(26);
   text('Feels '+reading(d.feels_like,'°'),x,y,P(16),inner,muted);y+=P(28);
   const stats=[['Humidity',reading(d.humidity,'%')],['Wind',reading(d.wind,d.wind_unit?' '+d.wind_unit:'')],['Rain',reading(forecast[0]?.rain,'%')]];
   if(isDetail){
    const wide=w>h,statW=(inner-P(16))/3;
    if(wide){for(const [label,value]of stats){text(label,x,y,P(16),inner/2,muted);text(value,x+inner/2,y,P(20),inner/2);y+=P(44)}}
    else{stats.forEach(([label,value],i)=>tile(label,value,x+i*(statW+P(8)),y,statW,P(64)));y+=P(76)}
    const fx=wide?w*.46+P(12):x,fy=wide?P(16):y,fw=wide?w-fx-P(16):inner,fh=h-fy-P(16);
    rect(fx,fy,fw,fh,surface,P(14),{'data-weather-forecast':''});
    if(!forecast.length)text(d.forecast_pending?'Loading forecast…':'Forecast not available',fx+P(14),fy+P(14),P(16),fw-P(28),muted);
    forecast.slice(0,5).forEach((day,i)=>{const yy=fy+P(12)+i*Math.min(P(46),(fh-P(24))/Math.min(5,forecast.length));text(i===0?'Today':new Date(day.date).toLocaleDateString(undefined,{weekday:'short'}),fx+P(12),yy,P(16),fw*.3);text(reading(day.low,'°')+'  '+reading(day.high,'°'),fx+fw*.42,yy,P(16),fw*.55,mutedColor)});
   }else{
    const items=[...stats.slice(0,2),['Pressure',reading(d.pressure,d.pressure_unit?' '+d.pressure_unit:'')],['Visibility',reading(d.visibility,d.visibility_unit?' '+d.visibility_unit:'')],['UV index',reading(d.uv)],['Feels like',reading(d.feels_like,'°')]].filter(([,value])=>value!=='—');
    const rows=Math.min(3,Math.floor((r.y+r.h-pad-y+P(8))/P(64))),tw=(inner-P(8))/2;
    if(rows>0)items.slice(0,rows*2).forEach(([label,value],i)=>tile(label,value,x+(i%2)*(tw+P(8)),y+Math.floor(i/2)*P(64),tw,P(56)));
    else text(items.slice(0,2).map(([label,value])=>label+' '+value).join(' · '),x,r.y+r.h-pad-P(16),P(16),inner,mutedColor);
   }
  };
  const mainCard=(v,title,entity,r,compact)=>{
   const d=v?.panel||{},domain=(entity||'').split('.')[0],pad=P(14),x=r.x+pad,inner=r.w-2*pad;
   rect(r.x,r.y,r.w,r.h,surface,P(compact?14:18),{'data-panel-item':entity});
   if(!compact&&domain==='weather'){weather(v,title,r,false);return}
   if(compact){icon(v?.icon,x,r.y+(r.h-P(30))/2,P(30),accent);text(title,x+P(42),r.y+(r.h-P(16))/2,P(16),inner*.6-P(42));text(v?.lines?.[1]?.text||'—',r.x+r.w-pad,r.y+(r.h-P(20))/2,P(20),inner*.4,foreground,'end');return}
   if(domain==='media_player'){
    const art=Math.max(P(48),Math.min(P(104),r.h-pad*2-P(60),r.w*.36)),tx=x+art+P(12),tw=r.x+r.w-pad-tx;
    artwork(v?.artwork,x,r.y+pad,art);text(title,tx,r.y+pad,P(16),tw,muted);text(d.title||v?.lines?.[2]?.text||condition(d.state),tx,r.y+pad+P(22),P(24),tw);text(d.artist||v?.lines?.[3]?.text||'',tx,r.y+pad+P(50),P(16),tw,mutedColor);
    const yy=r.y+r.h-pad-P(48);button('◀',x,yy,P(48),P(48));button(v?.playing?'Ⅱ':'▶',r.x+r.w/2-P(48),yy,P(96),P(48),accent);button('▶',r.x+r.w-pad-P(48),yy,P(48),P(48));return;
   }
   icon(v?.icon,x,r.y+pad,P(36),accent);text(title,x+P(56),r.y+pad,P(20),inner-P(56)-(domain==='light'?P(62):0));
   const yy=r.y+r.h-pad-P(48),on=['on','playing','open'].includes(d.state)||v?.lines?.[1]?.text==='On';
   if(['light','switch','input_boolean','fan','automation'].includes(domain))button(on?'On':'Off',r.x+r.w-pad-P(54),r.y+pad,P(54),P(30),on?accent:control);
   if(domain==='light'){
    text(on?'On':'Off',x+P(56),r.y+pad+P(28),P(16),inner-P(56),muted);
    if(d.dimmable!==false){const pct=Number(d.brightness??0);text(pct+'%',x,r.y+pad+P(76),P(43),inner);rect(x,yy,inner,P(40),control,P(12),{'data-brightness-slider':''});rect(x,yy,inner*Math.max(0,Math.min(100,pct))/100,P(40),accent,P(12))}return;
   }
   if(domain==='climate'){
    text(condition(d.state)+' · now '+reading(d.current,'°'),x+P(56),r.y+pad+P(28),P(16),inner-P(56),accent);
    text('Target',r.x+r.w/2,yy-P(38),P(16),inner,muted,'middle');text(reading(d.temperature,'°'),r.x+r.w/2,yy-P(12),P(43),inner-P(110),foreground,'middle');button('−',x,yy,P(48),P(48));button('+',r.x+r.w-pad-P(48),yy,P(48),P(48));return;
   }
   text(v?.lines?.[1]?.text||reading(d.state),x,r.y+pad+P(74),P(43),inner);
   if(['scene','script','button','input_button'].includes(domain))button('Run',x,yy,inner,P(48));
  };
  // The media player: artwork, title, artist, volume, progress and transport,
  // as onx_panel_ui::sync_media (no header; artwork beside the column in landscape).
  if(detail&&slot.entity.startsWith('weather.')){
   weather(view,slot.name||slot.entity,{x:0,y:0,w:w>h?w*.46:w,h},true);
  }else if(detail&&slot.entity.startsWith('sensor.')){
   const d=view?.panel||{},pad=P(16),inner=w-pad*2,lines=view?.lines||[],points=view?.graph||[];
   text(slot.name||lines[4]?.text||slot.entity,pad,pad,P(24),inner);
   text(lines[0]?.text||'Sensor',pad,pad+P(30),P(16),inner,muted);
   text(lines[1]?.text||'—',pad,P(82),P(64),inner);
   if(view?.layout==='graph'){
    const labels=['1 h','12 h','24 h','7 d'],range=d.history_range||43200;
    labels.forEach((label,i)=>button(label,pad+i*(inner+P(8))/4,P(174),(inner-P(24))/4,P(36),range===[3600,43200,86400,604800][i]?soft:control));
    const gx=pad,gy=P(224),gw=inner,gh=Math.max(P(60),h-gy-P(110));rect(gx,gy,gw,gh,surface,P(12),{'data-sensor-graph':''});
    if(points.length>1){const vals=points.map(p=>p.value),lo=Math.min(...vals),hi=Math.max(...vals),now=view.graph_now;
     for(let j=1;j<points.length;j++){const xy=p=>[gx+P(14)+(1-(now-p.t)/range)*(gw-P(28)),gy+P(12)+(hi>lo?1-(p.value-lo)/(hi-lo):.5)*(gh-P(24))],a=xy(points[j-1]),b=xy(points[j]);svg.append(node('line',{x1:a[0],y1:a[1],x2:b[0],y2:b[1],stroke:accent,'stroke-width':2}))}
     const tw=(inner-P(16))/3;[['Min',lo],['Avg',vals.reduce((a,b)=>a+b,0)/vals.length],['Max',hi]].forEach(([label,value],i)=>tile(label,Number(value.toFixed(1)).toString(),pad+i*(tw+P(8)),h-P(80),tw,P(64)));
    }else text('No recorded history',gx+P(14),gy+P(14),P(16),gw-P(28),muted);
   }
  }else if(detail&&view?.layout==='media'){
   const scale=Math.min(w,h)/240,top=Math.round(12*scale),rowH=Math.round(64*scale),rowGap=Math.round(12*scale),wide=w>h;
   const cover=wide?Math.max(48,Math.min(h-2*top,Math.floor(w*45/100))):Math.max(48,Math.min(w-48,h-top-12-84-28-rowH-rowGap));
   const colX=wide?top+cover+Math.round(12*scale):0,colW=wide?w-colX-Math.round(8*scale):w,cx=colX+colW/2,coverX=wide?top:(w-cover)/2,textY=wide?top+Math.round(8*scale):top+cover+12;
   artwork(view.artwork,coverX,top,cover,P(18));
   if(view.panel?.actions?.length)button('☷',w-P(52),top,P(40),P(40));
   const lines=view.lines||[];
   text(lines[2]?.text||slot.name||slot.entity,cx,textY,22*scale*.75,colW-24,foreground,'middle');
   text(lines[3]?.text||'',cx,textY+28,18*scale*.75,colW-24,mutedColor,'middle');
   text(lines[1]?.text||'',cx,textY+52,16*scale*.75,colW-24,foreground,'middle');
   const barY=h-rowH-rowGap-20,barW=colW-Math.round(40*scale);
   svg.append(node('rect',{x:cx-barW/2,y:barY,width:barW,height:5,rx:2,fill:trackColor,'data-media-bar':''}));
   if(view.progress>=0)svg.append(node('rect',{x:cx-barW/2,y:barY,width:barW*view.progress/1000,height:5,rx:2,fill:accent}));
   // Previous, play/pause and next spaced evenly across the column.
   const sizes=[44,56,44].map(v=>Math.round(v*scale)),space=(colW-sizes.reduce((a,b)=>a+b,0))/4,cy=h-rowGap-rowH/2;
   let x=colX+space;
   for(const [i,size]of sizes.entries()){
    const button=node('rect',{x,y:cy-size/2,width:size,height:size,rx:size/2,fill:i===1?strong:controlColor,'data-media-button':['previous','play','next'][i]});
    svg.append(button);
    const glyph=i===0?'mdi:skip-previous':i===2?'mdi:skip-next':view.playing?'mdi:pause':'mdi:play',g=size*.55;
    icon(glyph,x+(size-g)/2,cy-g/2,g,onColor(i===1?strong:controlColor));x+=size+space;
   }
  }else if(detail&&/^(light|climate|switch|input_boolean|script|automation|scene|button|input_button|fan)\./.test(slot.entity)){
   mainCard(view,slot.name||slot.entity,slot.entity,{x:0,y:0,w,h},false);
   const d=view?.panel||{};
   if(slot.entity.startsWith('automation.'))button('Run',P(16),h-P(74),w-P(32),P(48),strong);
   if(slot.entity.startsWith('script.')&&d.state==='on')button('Stop script',P(16),h-P(136),w-P(32),P(48));
   (d.actions||[]).slice(0,2).forEach((name,i)=>button(name,P(16)+i*(w-P(24))/2,h-P(194),(w-P(40))/2,P(40)));
  }else{
   const p=detail?{layout:'single',members:[]}:pageFor(slot.entity),ids=[slot.entity,...p.members],rects=globalThis.ONX_PANEL_GEOMETRY(p.layout,w,h,detail);
   const base=Math.min(w,h)/240;
   for(const [i,r]of rects.entries()){
    const found=views.find(v=>v.entity===ids[i]),v=!detail&&found?{...found,...found.carousel}:found;
    const compact=p.layout==='control_sensors'&&i>0,lines=v?.lines||[];
    const cardTitle=i===0?(slot.name||v?.lines?.[4]?.text||ids[i]):(editorSlots.find(s=>s.entity===ids[i])?.name||v?.lines?.[4]?.text||ids[i]||'Choose an entity');
    if(!detail){mainCard(v,cardTitle,ids[i],r,compact);continue}
    svg.append(node('rect',{x:r.x,y:r.y,width:r.w,height:r.h,rx:8,fill:surface,'data-panel-item':i}));
    const title=i===0?(slot.name||lines[4]?.text||ids[i]):(editorSlots.find(s=>s.entity===ids[i])?.name||lines[4]?.text||ids[i]||'Choose an entity');
    const small=14*base,normal=24*base,large=32*base,narrow=p.layout==='two_controls'||r.w<220;
    const stacked=compact&&r.w<220&&r.h>=small+normal+16,side=compact&&!stacked;
    const valueSize=compact?(stacked||r.h>=normal+8?normal:small):(r.h<150||r.w<220?normal:large);
    text(title,r.x+12,r.y+(side?(r.h-small)/2:stacked?8:12),small,side?r.w*.55-12:narrow&&!compact?r.w-64:r.w-24);
    text(lines[1]?.text||(ids[i]?'Waiting for data':'—'),r.x+(side?r.w*.55:12),r.y+(side?(r.h-valueSize)/2:stacked?r.h-valueSize*1.2-8:r.h*(v?.layout==='graph'?.18:p.layout==='two_controls'?.35:.49)),valueSize,side?r.w*.45-12:r.w-24);
    let context=/^(sensor|binary_sensor)\./.test(ids[i]||'')&&lines[4]?.text&&lines[4]?.text!==title?lines[4].text:lines[2]?.text||'';
    if(detail&&lines[0]?.text)context=lines[0].text+(context?' · '+context:'');
    if(!compact){text(context,r.x+12,r.y+r.h*.67,small,r.w-24,mutedColor);if(v?.layout!=='graph')icon(v?.icon,r.x+r.w-(narrow?40:54),r.y+(narrow?12:28),narrow?26:40)}
    if(detail&&v?.layout==='graph'){
     const points=v.graph||[],now=v.graph_now||0,values=points.map(p=>p.value),lo=Math.min(...values),hi=Math.max(...values),scale=base;
     const left=r.x+(r.w-186*scale)/2,top=r.y+r.h*.38,plot=42*scale,pw=143*scale,ph=64*scale;
     for(const off of [0,ph/2,ph])svg.append(node('line',{x1:left+plot,y1:top+off,x2:left+185*scale,y2:top+off,stroke:'#344653'}));
     text(v.graph_max_label||'',left,top,12*scale,plot);text(v.graph_min_label||'',left,top+ph-14*scale,12*scale,plot);
     for(let j=1;j<points.length;j++){const a=points[j-1],b=points[j];if(b.t-a.t>1200)continue;const x=p=>left+185*scale-(now-p.t)*pw/43200,y=p=>top+(hi>lo?ph-(p.value-lo)*ph/(hi-lo):ph/2);svg.append(node('line',{x1:x(a),y1:y(a),x2:x(b),y2:y(b),stroke:accent,'stroke-width':2}))}
    }
    if(detail&&v?.value_ring>=0){svg.append(node('rect',{x:r.x+12,y:r.y+r.h-16,width:r.w-24,height:4,fill:trackColor}));svg.append(node('rect',{x:r.x+12,y:r.y+r.h-16,width:(r.w-24)*v.value_ring/1000,height:4,fill:accent}))}
   }
   if(!detail){
    text(new Date().toLocaleTimeString(undefined,{hour:'2-digit',minute:'2-digit'}),P(16),P(8),P(16),P(90),mutedColor);
    // Bottom bar: the selected control sits on an accent-tinted marker.
    const spacing=Math.round(54*base*.75),iconSize=Math.round(20*base),y=h-22;
    svg.append(node('rect',{x:w/2-16*base,y:y-16*base,width:32*base,height:32*base,rx:16*base,fill:soft,'data-nav-marker':''}));
    for(const item of view?.carousel?.navigator||[])icon(item.icon,w/2+item.offset*spacing-iconSize/2,y-iconSize/2,iconSize,item.offset===0?accent:foreground);
    // Bell only on the main view, without a background.
    icon('mdi:bell-outline',w-38,8,24,foreground);
   }
  }
  $('preview-status').textContent=view?'Live rectangular panel view':'Save configured entities to load live readings.';
 };

 // Orientation: these panels have no accelerometer, so it is chosen here and
 // applied with a restart (the screen size changes).
 const orientation=$('orientation');
 if(orientation){
  const hint=document.createElement('p');hint.className='hint';hint.id='orientation-hint';
  hint.textContent='Saving a new orientation restarts the panel. Pages, media and the bottom bar adapt to portrait or landscape.';
  orientation.closest('label')?.after(hint);
 }

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
 // Touch panels have no physical button: the press action becomes a shortcut
 // button on the control's own screen, and there are no hold gestures.
 const say=(selector,text)=>{const n=document.querySelector(selector);if(n)n.textContent=text};
 const hideNode=n=>{if(n)n.hidden=true};
 say('#slot-tab-action','Shortcut');say('#slot-tab-gestures','Swipes');
 const actionPane=$('slot-panel-action');
 if(actionPane){
  hideNode(actionPane.querySelector('.help-links'));
  const intro=document.createElement('p');intro.className='hint';
  intro.textContent='Add a button at the bottom of this control’s screen that runs a script or automation. Tapping the card on the main view still opens the screen; the shortcut never replaces it.';
  actionPane.prepend(intro);
  say('#action-binding h3','Shortcut button');say('#choose-action','Choose script or automation');say('#default','Remove shortcut');
  hideNode($('none'));
  // Only replace the caption text node: the span also holds the checkbox.
  const responses=$('gesture-pressResponses');
  const caption=responses?[...responses.parentNode.childNodes].find(n=>n.nodeType===3&&n.textContent.trim()):null;
  if(caption)caption.textContent=' Show the returned value after the shortcut runs';
  say('#action-limit','This control’s screen has its own buttons, so it cannot have a shortcut.');
 }
 const gesturePane=$('slot-panel-gestures');
 if(gesturePane){
  for(const id of ['gesture-holdCw','gesture-holdCcw'])hideNode($(id)?.closest('article'));
  const heads=gesturePane.querySelectorAll('.binding h3');
  if(heads[0])heads[0].textContent='Swipe up on its screen';if(heads[1])heads[1].textContent='Swipe down on its screen';
  const hint=gesturePane.querySelector('.help-links + .hint');
  if(hint)hint.textContent='Swipe anywhere on this control’s screen outside its buttons. Leave empty to keep the normal adjustment shown in the preview, or enter none to ignore the swipe.';
 }
 // The page uses the panel's accent colour (updates while choosing one).
 const colorValue=$('color-value');
 if(colorValue)new MutationObserver(()=>{const v=colorValue.textContent.trim();if(/^#[0-9a-f]{6}$/i.test(v))document.documentElement.style.setProperty('--accent',v)}).observe(colorValue,{childList:true,characterData:true,subtree:true});
 // Hold progress belonged to the physical button.
 const cue=$('hold-cue')?.closest('label');
 if(cue){hideNode(cue);if(cue.nextElementSibling?.classList.contains('field-note'))hideNode(cue.nextElementSibling)}
 // Keep the shortcut wording after the main page re-renders.
 const baseEditor=globalThis.ONX_PANEL_EDITOR;
 globalThis.ONX_PANEL_EDITOR=(slots,active,...rest)=>{
  baseEditor(slots,active,...rest);
  const action=slots[active]?.action;
  if(!action||action==='none')say('#action-name','No shortcut');
  say('#action-id',action&&action!=='none'?action:'Nothing is added to the screen.');
 };
})();
)ONXPANEL";
#else
static const char PANEL_JS[] = "";
#endif
}  // namespace esphome::onx_config_web

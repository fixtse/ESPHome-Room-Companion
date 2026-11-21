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

 // Pages: the carousel shows pages, each with its own icon and name. A page
 // holds 1, 2 or 4 entities by placement (full, split, large + three compact);
 // there is no "main entity". Every placed entity is its own configuration
 // entry with its own shortcut, gestures and quick actions (40 in total), and
 // the entry editor below the page edits whichever position is selected.
 // Wire format (firmware: onx_panel_pages.h):
 //   panel_pages -> {pages:[{name,icon,layout,entities:[...]}]}
 //   panel_pages_save data=JSON {pages:[...]} (whole list: order, add, delete)
 // Presets carry their own page list; it is edited here and stored with the preset.
 const LAYOUTS={single:{n:1,label:'Full',hint:'One entity',slots:['Full screen']},two_controls:{n:2,label:'Split',hint:'Two entities',slots:['First','Second']},control_sensors:{n:4,label:'Large + 3',hint:'Three compact sensors',slots:['Large','Compact 1','Compact 2','Compact 3']},action_grid:{label:'Action grid',hint:'Adjustable rows and columns',n:4}};
 const gridLayout='action_grid',isGrid=p=>p.layout===gridLayout;
 const lead=p=>p.entities.find(Boolean)||'';
 const position=(p,i)=>isGrid(p)?'Row '+(Math.floor(i/p.columns)+1)+', column '+(i%p.columns+1):LAYOUTS[p.layout].slots[i];
 const artId=/^[0-9a-f]{14}$/;
 const axis=n=>Number.isInteger(n)&&n>=1&&n<=4?n:2;
 const DEFAULT_ICONS={sensor:'chart-line',binary_sensor:'checkbox-marked-circle-outline',media_player:'music-note',weather:'weather-partly-cloudy',climate:'thermostat',timer:'timer-outline',light:'lightbulb',switch:'toggle-switch',cover:'window-shutter',fan:'fan',script:'script-text',automation:'robot',number:'numeric',input_number:'numeric',select:'format-list-bulleted',input_select:'format-list-bulleted',button:'gesture-tap-button',input_button:'gesture-tap-button',scene:'palette'};
 const MAX_ENTRIES=40,ICON_RE=/^mdi:[a-z0-9-]{1,76}$/;
 const compact=e=>/^(sensor|binary_sensor)\./.test(e);
 const isTimer=e=>/^(timer|sensor)\./.test(e);
 let pageConfigs=[],savedPages='[]',panelPages='[]',pagesLoaded=false,pageError='',editorSlots=[],editorActive=-1,editorEntity='',editorBusy=false,editorTarget=null,selectedPage=0,selectedPos=0,timerEntity='',view='page',saving=false,redraw=()=>{};
 const app=()=>globalThis.ONX_APP;
 // The main-view preview shows the selected page, else the page this entity leads.
 const pageFor=entity=>{const sel=pageConfigs[selectedPage],p=view==='page'&&sel?.entities.includes(entity)?sel:pageConfigs.find(p=>lead(p)===entity);return p?{...p,ids:p.entities}:{layout:'single',ids:[entity]}};
 const artSide=96,artChunk=4800,artData=new Map(),artUrls=new Map(),artLoaded=new Set(),artPending=new Set();
 function rgb565Url(encoded){
  const bytes=atob(encoded.replace(/-/g,'+').replace(/_/g,'/'));
  const motion=bytes.startsWith('ONXART01')&&bytes.length===artSide*artSide*2;
  const offset=motion?16:0,side=motion?40:Math.sqrt(bytes.length/2);
  if(side!==40&&side!==artSide)throw Error('Invalid image data.');
  const c=document.createElement('canvas');c.width=c.height=side;
  const ctx=c.getContext('2d'),pixels=ctx.createImageData(side,side);
  for(let i=0;i<side*side;i++){
   const px=bytes.charCodeAt(offset+i*2)|bytes.charCodeAt(offset+i*2+1)<<8;
   pixels.data.set([Math.round((px>>11)*255/31),Math.round((px>>5&63)*255/63),Math.round((px&31)*255/31),255],i*4);
  }
  ctx.putImageData(pixels,0,0);return c.toDataURL();
 }
 async function downloadImage(id){
  let offset=0,data='';
  while(true){
   const part=await rpc({op:'panel_art_get',data:id,offset});
   if(typeof part.data!=='string')throw Error('Image is unavailable.');
   data+=part.data;
   if(part.total===undefined||part.next===part.total)return data;
   if(part.next<=offset||part.next>artSide*artSide*2)throw Error('Invalid image data.');
   offset=part.next;
  }
 }
 function artUrl(id){
  if(!artId.test(id||''))return '';
  if(!artUrls.has(id)&&!artLoaded.has(id)){
   artLoaded.add(id);
   downloadImage(id).then(data=>{
    artUrls.set(id,rgb565Url(data));artData.set(id,data);drawn.canvas=null;delete drawn.slots;touch();
   }).catch(()=>{});
  }
  return artUrls.get(id)||'';
 }
 const positionArt=(p,i)=>p?.artwork?.[i]||'';
 function updateArt(p,i,id){
  if(!p.artwork&&!id)return;
  p.artwork??=Array(p.entities.length).fill('');
  p.artwork[i]=id;
  if(!p.artwork.some(Boolean))delete p.artwork;
 }
 const iconOnly=(p,i)=>p?.icon_only?.[i]===true;
 // Grid-key icon animations, per entity on the page: pulse, blink or spin while it is on.
 const EFFECTS=['none','pulse','blink','spin'];
 const animationOf=(p,e)=>EFFECTS.includes(p?.animation?.[e])?p.animation[e]:'none';
 function setAnimation(p,e,value){
  if(!p||!e)return;
  if(value==='none'){if(p.animation){delete p.animation[e];if(!Object.keys(p.animation).length)delete p.animation}return}
  (p.animation??={})[e]=value;
 }
 function animationField(p,grid,ids){
  if(!grid||!p?.animation||typeof p.animation!=='object')return {};
  const out=Object.fromEntries(Object.entries(p.animation).filter(([e,v])=>ids.includes(e)&&EFFECTS.includes(v)&&v!=='none'));
  return Object.keys(out).length?{animation:out}:{};
 }
 function setIconOnly(p,i,value){
  if(!p.icon_only&&!value)return;
  p.icon_only??=Array(p.entities.length).fill(false);
  p.icon_only[i]=value;
  if(!p.icon_only.some(Boolean))delete p.icon_only;
 }
 const artFields=(p,n)=>p.artwork?.some(Boolean)?{artwork:Array.from({length:n},(_,i)=>positionArt(p,i))}:{};
 const el=(tag,cls,text)=>{const n=document.createElement(tag);if(cls)n.className=cls;if(text!==undefined)n.textContent=text;return n};
 const svgIcon=d=>{const s=document.createElementNS('http://www.w3.org/2000/svg','svg');s.setAttribute('viewBox','0 0 24 24');s.setAttribute('aria-hidden','true');s.innerHTML='<path d="'+d+'"/>';return s};
 const tool=(label,d,danger)=>{const b=el('button',danger?'danger':'');b.type='button';b.title=label;b.setAttribute('aria-label',label);b.append(svgIcon(d));return b};
 const mdiUrl=name=>'https://api.iconify.design/mdi/'+name.slice(4)+'.svg';
 const setIcon=(node,name)=>{if(ICON_RE.test(name||''))node.style.setProperty('--icon','url("'+mdiUrl(name)+'")');else node.style.removeProperty('--icon');return node};
 const mdi=name=>{const s=el('span','mdi');s.setAttribute('aria-hidden','true');return setIcon(s,name)};
 function displayArt(node,id){
  const url=artUrl(id);
  if(url)node.style.setProperty('--icon','url("'+url+'")');
  node.classList.toggle('art-preview',!!url);
  return node;
 }
 const pageGlyph=p=>displayArt(mdi(pageIcon(p)),p.carousel_artwork);
 const entry=e=>e?editorSlots.find(s=>s.entity===e):undefined;
 const nameOf=e=>entry(e)?.name||app()?.item(e)?.name||e;
 const slotOf=e=>e?editorSlots.findIndex(s=>s.entity===e):-1;
 const iconOf=e=>entry(e)?.icon||app()?.item(e)?.icon||'mdi:'+(DEFAULT_ICONS[(e||'').split('.')[0]]||'shape-outline');
 const pageTitle=(p,i)=>p?.name||(p?nameOf(lead(p)):'')||'Page '+(i+1);
 const pageIcon=p=>p.icon==='entity'?iconOf(lead(p)):p.icon||(isGrid(p)?'mdi:view-grid-outline':lead(p)?iconOf(lead(p)):'mdi:view-dashboard-outline');
 const dirtyPages=()=>JSON.stringify(pageConfigs)!==savedPages;
 const status=t=>{$('page-layout-status').textContent=t||''};
 const singles=list=>list.filter(s=>s.entity&&s.timerHidden!=='1').map(s=>({name:'',icon:'',layout:'single',entities:[s.entity]}));
 const normalize=list=>(Array.isArray(list)?list:[]).map(p=>{
  const layout=Object.hasOwn(LAYOUTS,p?.layout)?p.layout:'single',rows=axis(p?.rows),columns=axis(p?.columns);
  const grid=layout===gridLayout,n=grid?rows*columns:LAYOUTS[layout].n;
  const ids=Array.isArray(p?.entities)?p.entities:[p?.entity,...(p?.members||[])];
  return {name:String(p?.name||''),icon:String(p?.icon||''),layout,...(grid?{rows,columns}:{}),entities:Array.from({length:n},(_,i)=>String(ids[i]||'')),...artFields(p,n),...(grid&&p?.icon_only?.some(x=>x===true)?{icon_only:Array.from({length:n},(_,i)=>iconOnly(p,i))}:{}),...(artId.test(p?.carousel_artwork||'')?{carousel_artwork:p.carousel_artwork}:{}),...animationField(p,grid,ids.slice(0,n))};
 });
 const used=()=>editorSlots.filter(s=>s.entity).length;
 // Re-render through the app so save/discard states and the preview follow.
 const touch=()=>{const a=app();if(a?.refresh)a.refresh();else{renderPageEditor();redraw()}};
 const current=()=>view==='timer'?timerEntity:pageConfigs[selectedPage]?.entities[selectedPos]||'';
 // Point the entry editor at the selected position; false when nothing changed.
 function focusMember(){const i=slotOf(current()),a=app();if(a&&i>=0&&i!==editorActive){a.select(i);return true}return false}
 function selectPage(i){view='page';selectedPage=i;const p=pageConfigs[i];selectedPos=Math.max(0,p?p.entities.findIndex(Boolean):0);status('');if(!focusMember())touch()}
 function selectPos(pos){view='page';selectedPos=pos;status('');if(!focusMember())touch()}
 function place(page,pos,item){
  const e=item.entity;if(page.entities.includes(e)&&page.entities[pos]!==e){status(nameOf(e)+' is already on this page.');return false}
  // The same entity may appear on several pages: one entry, one subscription.
  const other=!page.entities.slice(0,pos).some(Boolean)&&pageConfigs.find(p=>p!==page&&lead(p)===e);if(other){status(nameOf(e)+' already leads “'+(other.name||nameOf(e))+'”. Place it in another position.');return false}
  if(pos>0&&page.layout==='control_sensors'&&!compact(e)){status('Compact positions show sensors and binary sensors.');return false}
  try{if(slotOf(e)<0)app()?.add(item)}catch(error){status(error.message);return false}
  const old=page.entities[pos];page.entities[pos]=e;if(old!==e){updateArt(page,pos,'');setIconOnly(page,pos,false);if(old)release(old)}
  status('');view='page';selectedPage=pageConfigs.indexOf(page);selectedPos=pos;if(!focusMember())touch();
  return true;
 }
 // ---- moving entities between pages
 // A placed entity drags as {page,pos}; over a page in the list it opens that
 // page after a short hover, and the list end makes a new single page.
 const MEMBER='application/x-onx-member',ENTITY='application/x-onx-entity';
 let dragged=null;  // {page,pos}: dataTransfer is unreadable during dragover
 const dragKinds=ev=>[...ev.dataTransfer.types];
 const carries=ev=>dragKinds(ev).includes(MEMBER)||dragKinds(ev).includes(ENTITY);
 function endDrag(){dragged=null;document.body.classList.remove('dragging-entity')}
 document.addEventListener('dragend',endDrag);document.addEventListener('drop',()=>setTimeout(endDrag));
 // Take the entity off its page, then place it; put it back if placing fails.
 function moveMember(from,page,pos){
  const src=pageConfigs[from.page],e=src?.entities[from.pos];if(!e)return false;
  if(src===page&&from.pos===pos)return false;
  const art=positionArt(src,from.pos),icon=iconOnly(src,from.pos);
  src.entities[from.pos]='';updateArt(src,from.pos,'');setIconOnly(src,from.pos,false);
  if(place(page,pos,{entity:e,name:nameOf(e),icon:iconOf(e)})){if(art)updateArt(page,pos,art);if(icon)setIconOnly(page,pos,true);touch();return true}
  src.entities[from.pos]=e;updateArt(src,from.pos,art);setIconOnly(src,from.pos,icon);touch();return false;
 }
 function dropOnNewPage(ev){
  ev.preventDefault();const item=ev.dataTransfer.getData(ENTITY),from=dragKinds(ev).includes(MEMBER)?dragged:null;endDrag();
  const page={name:'',icon:'',layout:'single',entities:['']};pageConfigs.push(page);
  const ok=from?moveMember(from,page,0):item?place(page,0,JSON.parse(item)):false;
  if(!ok){pageConfigs.splice(pageConfigs.indexOf(page),1);touch()}
 }
 function newPageDrop(){
  const z=el('div','page-drop-new','Drop here for a new page');
  z.ondragover=ev=>{if(!carries(ev))return;ev.preventDefault();z.classList.add('drop-target')};
  z.ondragleave=()=>z.classList.remove('drop-target');
  z.ondrop=ev=>{z.classList.remove('drop-target');dropOnNewPage(ev)};
  return z;
 }
 // Entries stay while a page or the timer list still uses them.
 function release(e){const i=slotOf(e);if(e&&i>=0&&editorSlots[i].timerHidden!=='1'&&!pageConfigs.some(p=>p.entities.includes(e)))app()?.clear(i)}
 function removeAt(page,pos){const e=page.entities[pos];page.entities[pos]='';updateArt(page,pos,'');setIconOnly(page,pos,false);release(e);touch()}
 function pickFor(page,pos){const small=pos>0&&page.layout==='control_sensors';app()?.pick(x=>!small||compact(x),item=>place(page,pos,item))}
 function cell(page,pos,r,w,h){
  const e=page.entities[pos]||'',small=pos>0&&page.layout==='control_sensors',role=position(page,pos);
  const c=el('div','page-cell'+(small?' compact':'')+(isGrid(page)?' action-key'+(iconOnly(page,pos)?' icon-only':''):'')+(e?'':' empty'));c.dataset.member=pos;
  c.style.cssText='left:'+r.x/w*100+'%;top:'+r.y/h*100+'%;width:'+r.w/w*100+'%;height:'+r.h/h*100+'%';
  c.setAttribute('aria-current',String(view==='page'&&pos===selectedPos));
  const main=el('button','cell-main');main.type='button';main.disabled=editorBusy;
  if(e){const url=artUrl(positionArt(page,pos)),image=url?el('img','mdi art-preview'):mdi(iconOf(e));
   if(url){image.src=url;image.alt=''}
   main.append(image,el('strong','',nameOf(e)),el('small','',e));main.setAttribute('aria-label',role+': '+nameOf(e));main.onclick=()=>selectPos(pos)}
  else{main.append(mdi('mdi:plus-circle-outline'),el('strong','',small?'Add a sensor':isGrid(page)?'':'Add entity'));main.setAttribute('aria-label','Add an entity to '+role);main.onclick=()=>{selectPos(pos);pickFor(page,pos)}}
  c.append(el('small','cell-tag',role),main);
  if(e){
   const tools=el('div','cell-tools'),swap=tool('Replace '+nameOf(e),'M4 7h13l-3-3M20 17H7l3 3'),rm=tool('Remove '+nameOf(e)+' from this page','m6 6 12 12M18 6 6 18',true);
   swap.onclick=()=>{selectPos(pos);pickFor(page,pos)};rm.onclick=()=>removeAt(page,pos);swap.disabled=rm.disabled=editorBusy;tools.append(swap,rm);if(!isGrid(page))c.append(tools);
   c.draggable=!editorBusy;c.ondragstart=ev=>{const pg=pageConfigs.indexOf(page);dragged={page:pg,pos};ev.dataTransfer.setData('application/x-onx-cell',String(pos));ev.dataTransfer.setData(MEMBER,JSON.stringify(dragged));ev.dataTransfer.effectAllowed='move';document.body.classList.add('dragging-entity')};
  }
  c.ondragover=ev=>{ev.preventDefault();c.classList.add('drop')};c.ondragleave=()=>c.classList.remove('drop');
  c.ondrop=ev=>{ev.preventDefault();c.classList.remove('drop');const member=dragKinds(ev).includes(MEMBER)?dragged:null,from=ev.dataTransfer.getData('application/x-onx-cell'),item=ev.dataTransfer.getData('application/x-onx-entity');
   // From another page: move it here (into an empty position, or replacing).
   if(member&&pageConfigs[member.page]!==page){endDrag();moveMember(member,page,pos);return}
   if(from!==''){const f=+from,a=page.entities[f],b=page.entities[pos]||'';if(f===pos)return;if(page.layout==='control_sensors'&&((pos>0&&a&&!compact(a))||(f>0&&b&&!compact(b)))){status('Compact positions show sensors and binary sensors.');return}const art=positionArt(page,f),dest=positionArt(page,pos),fromIcon=iconOnly(page,f),toIcon=iconOnly(page,pos);page.entities[f]=b;page.entities[pos]=a;updateArt(page,f,dest);updateArt(page,pos,art);setIconOnly(page,f,toIcon);setIconOnly(page,pos,fromIcon);selectedPos=pos;status('');touch()}
   else if(item)place(page,pos,JSON.parse(item))};
  return c;
 }
 // Dropping a placed entity outside the screen removes it.
 $('page-grid').addEventListener('dragend',ev=>{if(ev.dataTransfer.dropEffect==='none'&&ev.target.isConnected&&ev.target.dataset?.member!==undefined){const p=pageConfigs[selectedPage];if(p)removeAt(p,+ev.target.dataset.member)}});
 let lastSettings={};
 const screen=()=>globalThis.ONX_PANEL_SCREEN?.(lastSettings)||{width:320,height:480};
 function thumb(kind){
  const {width:w,height:h}=screen(),s=document.createElementNS('http://www.w3.org/2000/svg','svg');s.setAttribute('viewBox','0 0 '+w+' '+h);s.setAttribute('aria-hidden','true');if(w>h)s.classList.add('wide');
  s.innerHTML='<rect class="frame" x="5" y="5" width="'+(w-10)+'" height="'+(h-10)+'" rx="36"/>'+globalThis.ONX_PANEL_GEOMETRY(kind,w,h).map(r=>'<rect x="'+r.x+'" y="'+r.y+'" width="'+r.w+'" height="'+r.h+'" rx="18"/>').join('');return s;
 }
 function renderLayouts(p){
  $('page-kind').replaceChildren(...Object.entries(LAYOUTS).map(([value,l])=>{
   const label=el('label','layout-option'),input=el('input');input.type='radio';input.name='page-kind';input.value=value;input.checked=p.layout===value;input.disabled=editorBusy||saving;
   input.onchange=()=>setLayout(value);const text=el('span');text.append(el('b','',l.label),el('small','',l.hint));label.append(input,thumb(value),text);return label}));
 }
 function setLayout(kind){
  const p=pageConfigs[selectedPage];if(!p||p.layout===kind)return;
  const n=LAYOUTS[kind].n,dropped=p.entities.slice(n).filter(Boolean);
  p.layout=kind;
  if(isGrid(p)){p.rows=2;p.columns=2}else{delete p.rows;delete p.columns;delete p.icon_only;delete p.animation}
  p.entities=Array.from({length:n},(_,i)=>p.entities[i]||'');
  if(p.artwork)p.artwork=p.artwork.slice(0,n).concat(Array(Math.max(0,n-p.artwork.length)).fill(''));
  // Compact positions only take sensors; anything else moves off the page.
  if(kind==='control_sensors')for(let i=1;i<n;i++)if(p.entities[i]&&!compact(p.entities[i])){dropped.push(p.entities[i]);p.entities[i]='';updateArt(p,i,'')}
  const names=dropped.map(nameOf);for(const e of dropped)release(e);selectedPos=Math.min(selectedPos,n-1);
  status(dropped.length?names.join(', ')+(dropped.length===1?' was':' were')+' removed from this page.':'');if(!focusMember())touch();
 }
 function resizeGrid(){
  const p=pageConfigs[selectedPage];if(!p||!isGrid(p))return;
  const rows=+$('grid-rows').value,columns=+$('grid-columns').value,old=p.entities,oldColumns=p.columns;
  const selectedRow=Math.floor(selectedPos/oldColumns),selectedColumn=selectedPos%oldColumns;
  p.rows=rows;p.columns=columns;
  p.entities=Array.from({length:rows*columns},(_,i)=>i%columns<oldColumns?old[Math.floor(i/columns)*oldColumns+i%columns]||'':'');
  selectedPos=Math.min(selectedRow,rows-1)*columns+Math.min(selectedColumn,columns-1);
  if(p.icon_only){
   const oldFlags=p.icon_only;
   p.icon_only=Array.from({length:rows*columns},(_,i)=>i%columns<oldColumns&&oldFlags[Math.floor(i/columns)*oldColumns+i%columns]===true);
   if(!p.icon_only.some(Boolean))delete p.icon_only;
  }
  if(p.artwork){
   const oldArt=p.artwork;
   p.artwork=Array.from({length:rows*columns},(_,i)=>i%columns<oldColumns?oldArt[Math.floor(i/columns)*oldColumns+i%columns]||'':'');
   if(!p.artwork.some(Boolean))delete p.artwork;
  }
  const dropped=old.filter(e=>e&&!p.entities.includes(e)),names=dropped.map(nameOf);
  for(const e of dropped)release(e);
  status(names.length?'Removed from the grid: '+names.join(', ')+'.':'');
  if(!focusMember())touch();
 }
 $('grid-rows').onchange=$('grid-columns').onchange=resizeGrid;
 function renderCanvas(p){
  const {width:w,height:h}=screen(),grid=$('page-grid');grid.className='page-grid'+(w>h?' wide':'');grid.style.aspectRatio=w+'/'+h;
  const top=el('div','page-chrome top');top.append(el('span','',new Date().toLocaleTimeString(undefined,{hour:'2-digit',minute:'2-digit'})),mdi('mdi:bell-outline'));
  const bottom=el('div','page-chrome bottom');for(const [i,q] of pageConfigs.entries()){const m=pageGlyph(q);if(i===selectedPage)m.classList.add('here');bottom.append(m)}
  grid.replaceChildren(top,...globalThis.ONX_PANEL_GEOMETRY(p.layout,w,h,false,p.rows,p.columns).map((r,i)=>cell(p,i,r,w,h)),bottom);
  drawLiveCanvas();
 }
 // The page as the panel draws it, under the editable positions.
 function drawLiveCanvas(){
  const grid=$('page-grid'),p=view==='page'?pageConfigs[selectedPage]:null;
  if(!grid||!p||!globalThis.ONX_PANEL_PREVIEW)return;
  let svg=grid.querySelector('svg.page-live');
  if(!svg){svg=document.createElementNS('http://www.w3.org/2000/svg','svg');svg.classList.add('page-live');svg.setAttribute('aria-hidden','true');grid.prepend(svg)}
  const e=lead(p),a=app(),views=a?.views?.()||[];
  // Hide the plain cell text only while live data is actually drawn; until
  // then (or if it is unavailable) the cells keep showing their entity.
  const liveView=e?views.find(v=>v.entity===e):null;
  grid.classList.toggle('live',!!liveView);
  if(!liveView){svg.replaceChildren();return}
  const slot=(a?.slots?.()||[]).find(s=>s.entity===e)||{entity:e,name:''};
  globalThis.ONX_PANEL_PREVIEW(svg,slot,liveView,views,'main',a?.settings?.()||lastSettings);
 }
 function pageButton(p,i,savedList){
  const b=el('button','page-item');b.type='button';const meta=(isGrid(p)?p.rows+' × '+p.columns+' grid':LAYOUTS[p.layout].label)+' · '+p.entities.filter(Boolean).length+'/'+p.entities.length;
  const box=el('span','slot-icon');box.append(pageGlyph(p));const text=el('span','item-text');text.append(el('span','',pageTitle(p,i)),el('small','',meta));
  b.append(el('b','',String(i+1)),box,text);
  const dirty=JSON.stringify(savedList[i])!==JSON.stringify(p)||p.entities.some(e=>{const j=slotOf(e);return j>=0&&app()?.dirtyAt?.(j)});if(dirty)b.append(el('span','dirty-dot','●'));
  b.title=pageTitle(p,i);b.setAttribute('aria-label','Page '+(i+1)+': '+pageTitle(p,i)+(dirty?', unsaved':''));b.setAttribute('aria-current',String(view==='page'&&i===selectedPage));b.disabled=editorBusy;b.onclick=()=>selectPage(i);
  b.draggable=!editorBusy;b.ondragstart=ev=>{ev.dataTransfer.setData('application/x-onx-page',String(i));ev.dataTransfer.effectAllowed='move'};
  let openTimer=0;
  b.ondragover=ev=>{
   if(carries(ev)){ev.preventDefault();b.classList.add('drop-hover');if(!openTimer&&i!==selectedPage)openTimer=setTimeout(()=>{openTimer=0;if(document.body.classList.contains('dragging-entity')||dragKinds(ev).includes(ENTITY))selectPage(i)},550);return}
   if(![...ev.dataTransfer.types].includes('application/x-onx-page'))return;ev.preventDefault();b.classList.add('drop-target')};
  b.ondragleave=()=>{b.classList.remove('drop-target','drop-hover');clearTimeout(openTimer);openTimer=0};
  b.ondrop=ev=>{b.classList.remove('drop-target','drop-hover');clearTimeout(openTimer);openTimer=0;if(carries(ev)){ev.preventDefault();return}const f=ev.dataTransfer.getData('application/x-onx-page');if(f==='')return;ev.preventDefault();movePage(+f,i)};return b;
 }
 function movePage(from,to){if(from===to||to<0||to>=pageConfigs.length)return;const [m]=pageConfigs.splice(from,1);pageConfigs.splice(to,0,m);selectedPage=to;view='page';touch()}
 // Rebuild a list only when what it shows changed, so a re-render cannot
 // swap a button out from under a click.
 const drawn={};
 function patch(id,key,build){if(drawn[id]===key)return;drawn[id]=key;$(id).replaceChildren(...build())}
 function renderPageEditor(){
  refreshDockOptions();
  const savedList=JSON.parse(savedPages),editor=$('page-editor'),locked=editorBusy||saving;
  if(view==='page'&&selectedPage>=pageConfigs.length)selectedPage=Math.max(0,pageConfigs.length-1);
  if(view==='timer'&&!editorSlots.some(s=>s.entity===timerEntity&&s.timerHidden==='1')){view='page'}
  const names=pageConfigs.map(p=>p.entities.map(e=>e&&nameOf(e)+'|'+iconOf(e)+'|'+(app()?.dirtyAt?.(slotOf(e))?1:0)));
  patch('slots',JSON.stringify([pageConfigs,savedPages,names,view,selectedPage,locked,pagesLoaded,pageError,!!editorTarget]),()=>[...(pageConfigs.length?pageConfigs.map((p,i)=>pageButton(p,i,savedList)):[el('p','empty-note',pagesLoaded||editorTarget?'No pages yet.':pageError||'Loading pages…')]),newPageDrop()]);
  $('slot-capacity').textContent=used()+'/'+MAX_ENTRIES+' entities';$('slot-capacity').title=used()+' of '+MAX_ENTRIES+' entity entries in use (pages and timers)';
  const timers=editorSlots.filter(s=>s.entity&&s.timerHidden==='1');
  patch('timer-list',JSON.stringify([timers.map(t=>t.entity+'|'+nameOf(t.entity)),view,timerEntity,locked]),()=>timers.map(t=>{
   const li=el('li'),open=el('button'),box=el('span','slot-icon');open.type='button';box.append(mdi(iconOf(t.entity)));
   const text=el('span','item-text');text.append(el('span','',nameOf(t.entity)),el('small','',t.entity));open.append(box,text);open.setAttribute('aria-current',String(view==='timer'&&timerEntity===t.entity));open.disabled=locked;
   open.onclick=()=>{view='timer';timerEntity=t.entity;status('');if(!focusMember())touch()};
   const rm=tool('Remove timer '+nameOf(t.entity),'m6 6 12 12M18 6 6 18');rm.className='icon-button';rm.dataset.timer=t.entity;rm.disabled=locked;rm.onclick=()=>removeTimer(t.entity);li.append(open,rm);return li}));
  for(const id of ['add-page','add-first-page','add-timer'])$(id).disabled=locked;
  const p=view==='page'?pageConfigs[selectedPage]:null;
  $('pages-empty').hidden=!(view==='page'&&!p);
  for(const id of ['page-head','page-layouts','page-canvas'])$(id).hidden=!p;
  $('grid-size').hidden=!p||!isGrid(p);
  if(p){
   if(isGrid(p)){$('grid-rows').value=p.rows;$('grid-columns').value=p.columns;$('grid-rows').disabled=$('grid-columns').disabled=locked}
   const name=$('page-name');if(document.activeElement!==name)name.value=p.name;name.placeholder=lead(p)?nameOf(lead(p)):'Living room';name.disabled=locked;
   displayArt(setIcon($('page-icon-preview'),pageIcon(p)),p.carousel_artwork);$('page-icon-button').setAttribute('aria-label','Carousel icon: '+(p.carousel_artwork?'custom image':p.icon==='entity'?'first entity icon':p.icon||'default icon')+'. Choose another');$('page-icon-button').disabled=locked;
   $('page-up').disabled=locked||selectedPage===0;$('page-down').disabled=locked||selectedPage>=pageConfigs.length-1;$('delete-page').disabled=locked;
   const {width:w,height:h}=screen();
   const kindKey=JSON.stringify([p.layout,locked,w,h]);if(drawn.kind!==kindKey){drawn.kind=kindKey;renderLayouts(p)}
   const canvasKey=JSON.stringify([p,pageConfigs.map(pageIcon),selectedPage,selectedPos,view,locked,w,h,p.entities.map(e=>e&&nameOf(e)+'|'+iconOf(e))]);
   if(drawn.canvas!==canvasKey){drawn.canvas=canvasKey;renderCanvas(p)}
  }
  const e=current(),i=slotOf(e),member=!!e&&i>=0&&i===editorActive;
  editor.classList.toggle('no-member',!member);
  $('member-head').hidden=!member;$('member-empty').hidden=member||!(p&&!e);
  $('member-icon-only').hidden=!member||!p||!isGrid(p);
  $('member-icon-only-value').disabled=locked;$('member-icon-only-value').checked=iconOnly(p,selectedPos);
  $('member-animation').hidden=!member||!p||!isGrid(p);
  {const fx=animationOf(p,e),sample=$('member-animation-sample');$('member-animation-value').disabled=locked;$('member-animation-value').checked=fx!=='none';
   if(member){setIcon(sample,iconOf(e));sample.className='mdi fx-sample'+(fx!=='none'?' fx-'+fx:'')}}
  if(member){
   const preview=view==='page'&&artUrl(positionArt(p,selectedPos));
   setIcon($('member-icon'),iconOf(e));
   if(preview)$('member-icon').style.setProperty('--icon','url("'+preview+'")');
   $('member-icon').classList.toggle('art-preview',!!preview);
   $('member-icon-button').hidden=view==='timer';$('member-icon-button').disabled=locked;$('member-role').textContent=view==='timer'?'Registered timer':position(p,selectedPos)+' · page '+(selectedPage+1);
   $('member-name').textContent=nameOf(e);$('member-id').textContent=e;$('member-replace').hidden=view==='timer';
   $('member-remove').textContent=view==='timer'?'Remove timer':'Remove from page';$('member-replace').disabled=$('member-remove').disabled=locked;
  }
  $('slot-title').textContent=(view==='timer'?nameOf(timerEntity):p?pageTitle(p,selectedPage):'Pages')+(editorTarget?' · '+editorTarget.name:'');
  if(pageError&&!pagesLoaded&&!editorTarget)status(pageError);
 }
 function removeTimer(e){const i=slotOf(e);if(i<0)return;app()?.setTimer(i,false);release(e);if(timerEntity===e)view='page';touch()}
 $('add-timer').onclick=()=>app()?.pick(isTimer,item=>{try{let i=slotOf(item.entity);if(i<0)i=app().add(item);app().setTimer(i,true);view='timer';timerEntity=item.entity;if(!focusMember())touch()}catch(error){status(error.message)}});
 const addPage=()=>{pageConfigs.push({name:'',icon:'',layout:'single',entities:['']});selectPage(pageConfigs.length-1)};
 $('add-page').onclick=$('add-first-page').onclick=addPage;
 $('delete-page').onclick=()=>{const p=pageConfigs[selectedPage];if(!p)return;const title=pageTitle(p,selectedPage);pageConfigs.splice(selectedPage,1);for(const e of p.entities)release(e);status('“'+title+'” deleted. Discard changes to restore it.');selectPage(Math.max(0,selectedPage-1))};
 $('page-up').onclick=()=>movePage(selectedPage,selectedPage-1);$('page-down').onclick=()=>movePage(selectedPage,selectedPage+1);
 $('page-name').oninput=()=>{const p=pageConfigs[selectedPage];if(p){p.name=$('page-name').value;touch()}};
 $('member-animation-value').onchange=()=>{const p=pageConfigs[selectedPage];if(p&&isGrid(p)){setAnimation(p,p.entities[selectedPos],$('member-animation-value').checked?'pulse':'none');touch()}};
 $('member-icon-only-value').onchange=()=>{const p=pageConfigs[selectedPage];if(p&&isGrid(p)){setIconOnly(p,selectedPos,$('member-icon-only-value').checked);touch()}};
 $('member-replace').onclick=()=>{const p=pageConfigs[selectedPage];if(p)pickFor(p,selectedPos)};
 $('member-remove').onclick=()=>{if(view==='timer'){removeTimer(timerEntity);return}const p=pageConfigs[selectedPage];if(p)removeAt(p,selectedPos)};

 // Icon picker: search all Material Design Icons with live previews.
 const SUGGESTED=['home','sofa','bed','silverware-fork-knife','desk','television','lightbulb','ceiling-light','lamp','fan','thermometer','water-percent','weather-partly-cloudy','music','speaker','garage','shower','flower','door','window-shutter','power-plug','washing-machine','coffee','gamepad-variant','briefcase','robot-vacuum','car','shield-home','leaf','baby-face-outline'].map(n=>'mdi:'+n);
 let iconNames=null,iconLoad=null,iconQuery=0,iconLimit=96,iconList=[];
 function loadIcons(){
  if(iconNames)return Promise.resolve(iconNames);
  iconLoad??=fetch('https://api.iconify.design/collection?prefix=mdi',{cache:'force-cache'}).then(r=>r.ok?r.json():Promise.reject(Error('HTTP '+r.status))).then(d=>{const all=new Set(d.uncategorized||[]);for(const list of Object.values(d.categories||{}))for(const n of list)all.add(n);iconNames=[...all].sort();return iconNames}).catch(()=>{iconLoad=null;return null});
  return iconLoad;
 }
 async function searchIcons(q){
  const terms=q.toLowerCase().replace(/^mdi:/,'').split(/[\s_]+/).filter(Boolean);
  const names=await loadIcons();
  if(names){const exact=terms.join('-'),score=n=>n===exact?0:n.startsWith(exact)?1:n.split('-').includes(terms[0])?2:3;return names.filter(n=>terms.every(t=>n.includes(t))).sort((a,b)=>score(a)-score(b)||a.length-b.length||a.localeCompare(b)).map(n=>'mdi:'+n)}
  // Without the full list, fall back to Iconify's search.
  const r=await fetch('https://api.iconify.design/search?prefix=mdi&limit=999&query='+encodeURIComponent(terms.join(' ')));if(!r.ok)throw Error('Icon search is unavailable.');return ((await r.json()).icons||[]).filter(n=>ICON_RE.test(n));
 }
 function focusIcon(name){$('icon-focus').classList.remove('art-preview');setIcon($('icon-focus'),name);$('icon-focus-name').textContent=name||'Entity icon'}
 let artTarget=null,cropImage=null,cropRequest=0,cropFrames=[];
 // Decode individual GIF and WebP frames with the browser's still-image
 // decoders. This also works on a panel served over local HTTP.
 async function motionFrames(file){
  const bytes=new Uint8Array(await file.arrayBuffer()),view=new DataView(bytes.buffer);
  const ascii=(at,n)=>String.fromCharCode(...bytes.subarray(at,at+n));
  const u24=at=>bytes[at]|bytes[at+1]<<8|bytes[at+2]<<16;
  const gif=file.type==='image/gif';
  const width=gif?view.getUint16(6,true):1+u24(24),height=gif?view.getUint16(8,true):1+u24(27);
  if(!width||!height||width*height>4000000)throw Error('Choose an animation up to 4 megapixels.');
  const canvas=document.createElement('canvas');canvas.width=width;canvas.height=height;
  const ctx=canvas.getContext('2d'),frames=[];
  async function frame(parts,type,x,y,w,h,dispose,replace,restore=false){
   const before=restore?ctx.getImageData(x,y,w,h):null;
   const bitmap=await createImageBitmap(new Blob(parts,{type}));
   try{
    if(replace)ctx.clearRect(x,y,w,h);
    ctx.drawImage(bitmap,x,y);
    frames.push(await createImageBitmap(canvas));
    if(before)ctx.putImageData(before,x,y);
    else if(dispose)ctx.clearRect(x,y,w,h);
   }finally{bitmap.close()}
  }
  try{
   if(gif){
    if(ascii(0,3)!=='GIF')throw Error('Invalid GIF.');
    const table=bytes[10]&128?3*(2<<(bytes[10]&7)):0;
    let at=13+table,control=null;
    const endBlocks=start=>{
     let pos=start;
     while(pos<bytes.length){const n=bytes[pos++];if(!n)return pos;pos+=n}
     throw Error('Invalid GIF frame.');
    };
    while(at<bytes.length&&frames.length<3){
     const kind=bytes[at++];
     if(kind===0x3b)break;
     if(kind===0x21){
      const label=bytes[at++],end=endBlocks(at);
      if(label===0xf9&&end-at===6)control=bytes.slice(at-2,end);
      at=end;continue;
     }
     if(kind!==0x2c||at+9>=bytes.length)throw Error('Invalid GIF frame.');
     const x=view.getUint16(at,true),y=view.getUint16(at+2,true),w=view.getUint16(at+4,true),h=view.getUint16(at+6,true);
     if(!w||!h||x+w>width||y+h>height)throw Error('Invalid GIF frame dimensions.');
     const head=bytes.slice(0,13+table),descriptor=bytes.slice(at-1,at+9);
     head[6]=w&255;head[7]=w>>8;head[8]=h&255;head[9]=h>>8;descriptor.fill(0,1,5);
     const payload=at+9+(bytes[at+8]&128?3*(2<<(bytes[at+8]&7)):0);
     const end=endBlocks(payload+1),disposal=control?(control[3]>>2)&7:0;
     await frame([head,control||new Uint8Array(),descriptor,bytes.slice(at+9,end),Uint8Array.of(0x3b)],file.type,x,y,w,h,disposal===2,false,disposal===3);
     at=end;control=null;
    }
   }else{
    if(ascii(0,4)!=='RIFF'||ascii(8,4)!=='WEBP'||ascii(12,4)!=='VP8X')throw Error('Choose an animated WebP or GIF.');
    for(let at=12;at+8<=bytes.length&&frames.length<3;){
     const size=view.getUint32(at+4,true),start=at+8,end=start+size;
     if(end>bytes.length)throw Error('Invalid WebP chunk.');
     if(ascii(at,4)==='ANMF'&&size>=24){
      const x=2*u24(start),y=2*u24(start+3),w=1+u24(start+6),h=1+u24(start+9),flags=bytes[start+15];
      if(x+w>width||y+h>height)throw Error('Invalid WebP frame dimensions.');
      const header=new Uint8Array(30),hv=new DataView(header.buffer);
      header.set([82,73,70,70],0);header.set([87,69,66,80,86,80,56,88],8);
      hv.setUint32(4,22+end-start-16,true);hv.setUint32(16,10,true);
      header[20]=16;
      for(let i=0;i<3;i++){header[24+i]=(w-1)>>(8*i);header[27+i]=(h-1)>>(8*i)}
      await frame([header,bytes.slice(start+16,end)],file.type,x,y,w,h,flags&1,flags&2);
     }
     at=end+(size&1);
    }
   }
   if(frames.length<2)throw Error('Choose an animation with at least two frames.');
   return frames;
  }catch(error){frames.forEach(f=>f.close());throw error}
 }

 function closeIconPicker(){const button=artTarget?.pos>=0?'member-icon-button':'page-icon-button';$('icon-picker').close();$(button).focus()}
 function chooseIcon(name){const p=pageConfigs[selectedPage];if(p){p.icon=name;delete p.carousel_artwork;touch()}closeIconPicker()}
 function crop(canvas){
  if(!cropImage)return;
  const ctx=canvas.getContext('2d'),side=Math.min(cropImage.width,cropImage.height)/+$('art-zoom').value;
  const x=(cropImage.width-side)*+$('art-x').value/100,y=(cropImage.height-side)*+$('art-y').value/100;
  ctx.fillStyle='#101418';ctx.fillRect(0,0,canvas.width,canvas.height);
  ctx.drawImage(cropImage,x,y,side,side,0,0,canvas.width,canvas.height);
 }
 function cropPixels(source,side){
  const old=cropImage;cropImage=source;
  const c=document.createElement('canvas');c.width=c.height=side;crop(c);cropImage=old;
  const pixels=c.getContext('2d').getImageData(0,0,side,side).data;
  const result=new Uint8Array(side*side*2);
  for(let i=0;i<pixels.length;i+=4){
   const px=(pixels[i]>>3)<<11|(pixels[i+1]>>2)<<5|pixels[i+2]>>3;
   result[i/2]=px;result[i/2+1]=px>>8;
  }
  return result;
 }
 function encodeArt(bytes){
  let hash=14695981039346656037n;
  for(const byte of bytes)hash=BigInt.asUintN(64,(hash^BigInt(byte))*1099511628211n);
  return {id:((hash&0x00ffffffffffffffn)|1n).toString(16).padStart(14,'0'),data:btoa(String.fromCharCode(...bytes))};
 }
 function cropData(previous,role){
  const pixels=previous?atob(previous.replace(/-/g,'+').replace(/_/g,'/')):'';
  const packed=pixels.startsWith('ONXART01')&&pixels.length===artSide*artSide*2;
  if(role==='off'&&!packed)return encodeArt(cropPixels(cropImage,artSide));
  const out=new Uint8Array(artSide*artSide*2), frameSize=3200;
  if(packed)out.set(Uint8Array.from(pixels,c=>c.charCodeAt(0)));
  else{
   out.set(new TextEncoder().encode('ONXART01'));
   if(pixels){
    const side=Math.sqrt(pixels.length/2);
    for(let y=0;y<40;y++)for(let x=0;x<40;x++){
     const src=(Math.floor(y*side/40)*side+Math.floor(x*side/40))*2,dst=16+(y*40+x)*2;
     out[dst]=pixels.charCodeAt(src);out[dst+1]=pixels.charCodeAt(src+1);
    }
   }else out.set(cropPixels(cropImage,40),16);
  }
  if(role==='off')out.set(cropPixels(cropImage,40),16);
  else if(role==='on'){out[8]=1;out.set(cropPixels(cropImage,40),16+frameSize)}
  else{
   out[9]=cropFrames.length;
   cropFrames.forEach((frame,i)=>out.set(cropPixels(frame,40),16+(2+i)*frameSize));
  }
  return encodeArt(out);
 }
 function setPositionIcon(id){
  if(!artTarget)return;
  if(artTarget.pos>=0)updateArt(artTarget.page,artTarget.pos,id);
  else if(id){artTarget.page.carousel_artwork=id;artTarget.page.icon=''}
  else{delete artTarget.page.carousel_artwork;artTarget.page.icon=''}
  touch();closeIconPicker();
 }
 function closeCropFrames(){
  if(cropFrames.length)cropFrames.forEach(f=>f.close());else cropImage?.close();
  cropFrames=[];cropImage=null;
 }
 function clearCrop(){cropRequest++;closeCropFrames();$('art-file').value='';$('art-crop').hidden=true;$('art-status').textContent=''}
 for(const id of ['art-zoom','art-x','art-y'])$(id).oninput=()=>crop($('art-canvas'));
 $('art-file').onchange=async()=>{
  const file=$('art-file').files[0],mine=++cropRequest;
  $('art-status').textContent='';$('art-crop').hidden=true;
  closeCropFrames();
  if(!file)return;
  try{
   if(!['image/png','image/jpeg','image/webp','image/gif'].includes(file.type)||file.size>10*1024*1024)
    throw Error('Choose a PNG, JPEG, WebP or GIF up to 10 MB.');
   const animate=$('art-role').value==='press';
   if(animate&&!['image/gif','image/webp'].includes(file.type))throw Error('Choose an animated GIF or WebP.');
   const frames=animate?await motionFrames(file):[];
   const bitmap=frames[0]||await createImageBitmap(file);
   if(mine!==cropRequest){if(frames.length)frames.forEach(f=>f.close());else bitmap.close();return}
   cropFrames=frames;
   if(!bitmap.width||!bitmap.height||bitmap.width*bitmap.height>32000000){bitmap.close();throw Error('Choose an image with no more than 32 megapixels.')}
   cropImage=bitmap;
   $('art-zoom').value=1;$('art-x').value=$('art-y').value=50;
   crop($('art-canvas'));$('art-crop').hidden=false;
  }catch(error){if(mine===cropRequest)$('art-status').textContent=error.message}
 };
 $('art-role').onchange=clearCrop;
 $('art-use').onclick=async()=>{
  if(!cropImage||!artTarget)return;
  const target=artTarget,mine=cropRequest,role=$('art-role').value,button=$('art-use');
  button.disabled=true;
  try{
   const id=target.pos>=0?positionArt(target.page,target.pos):'';
   const old=id?(artData.get(id)||await downloadImage(id)):'';
   if(mine!==cropRequest||artTarget!==target)return;
   const {id:next,data}=cropData(old,role);
   artData.set(next,data);artPending.add(next);artUrls.set(next,rgb565Url(data));setPositionIcon(next);
  }catch(error){if(mine===cropRequest)$('art-status').textContent=error.message}
  finally{button.disabled=false}
 };
 $('member-icon-button').onclick=()=>{
  const p=pageConfigs[selectedPage],entity=p?.entities[selectedPos];if(!entity)return;
  artTarget={page:p,pos:selectedPos};clearCrop();$('art-role').value='off';$('art-role').querySelector('[value=press]').disabled=!isGrid(p);$('art-role-field').hidden=false;$('icon-entity').hidden=true;
  $('icon-picker-title').textContent='Choose a position icon';$('icon-picker-context').textContent='Shown for '+nameOf(entity)+' on this page.';
  $('icon-source').hidden=true;$('mdi-picker-body').hidden=true;$('art-picker-body').hidden=false;
  const id=positionArt(p,selectedPos);focusIcon(iconOf(entity));
  const url=artUrl(id);if(url){$('icon-focus').style.setProperty('--icon','url("'+url+'")');$('icon-focus').classList.add('art-preview')}
  if(id)$('icon-focus-name').textContent='Custom image';
  $('icon-default').disabled=!id;$('icon-picker').showModal();$('art-file').focus();
 };
 function drawIcons(){
  const p=pageConfigs[selectedPage],grid=$('icon-grid'),shown=iconList.slice(0,iconLimit);
  grid.replaceChildren(...shown.map(name=>{const b=el('button');b.type='button';b.setAttribute('role','option');b.dataset.icon=name;b.title=name;b.setAttribute('aria-selected',String(p?.icon===name));b.append(mdi(name),el('small','',name.slice(4)));b.onclick=()=>chooseIcon(name);b.onfocus=b.onmouseenter=()=>focusIcon(name);return b}));
  if(!shown.length)grid.append(el('p','empty','No icons match. Try a shorter word, or type a full name such as mdi:sofa-outline.'));
  $('icon-more').hidden=iconList.length<=iconLimit;
 }
 async function renderIcons(){
  const mine=++iconQuery,q=$('icon-search').value.trim().toLowerCase(),p=pageConfigs[selectedPage];iconLimit=96;
  let list,caption;
  if(!q){list=[p?.icon,...(p?.entities||[]).filter(Boolean).map(iconOf),...SUGGESTED];caption='Suggestions'}
  else{$('icon-count').textContent='Searching…';try{list=await searchIcons(q)}catch(error){list=[];$('icon-count').textContent=error.message}caption='Results for “'+q+'”';if(ICON_RE.test(q)&&!list.includes(q))list.unshift(q);else if(ICON_RE.test('mdi:'+q)&&!list.includes('mdi:'+q)&&!iconNames)list.unshift('mdi:'+q)}
  if(mine!==iconQuery)return;
  iconList=[...new Set(list.filter(n=>ICON_RE.test(n||'')))];$('icon-caption').textContent=caption;$('icon-count').textContent=q?iconList.length+' icon'+(iconList.length===1?'':'s'):'';drawIcons();
 }
 let iconDebounce;
 $('icon-search').oninput=()=>{clearTimeout(iconDebounce);iconDebounce=setTimeout(renderIcons,150)};
 $('icon-search').onkeydown=ev=>{if(ev.key==='ArrowDown'){ev.preventDefault();$('icon-grid').querySelector('button')?.focus()}else if(ev.key==='Enter'){ev.preventDefault();const first=$('icon-grid').querySelector('button');if(first)first.click()}};
 $('icon-grid').onkeydown=ev=>{
  const items=[...$('icon-grid').querySelectorAll('button')],i=items.indexOf(document.activeElement);if(i<0)return;
  const cols=Math.max(1,items.filter(b=>b.offsetTop===items[0].offsetTop).length),step={ArrowRight:1,ArrowLeft:-1,ArrowDown:cols,ArrowUp:-cols}[ev.key];if(!step)return;ev.preventDefault();
  const n=i+step;if(n<0){$('icon-search').focus();return}items[Math.min(items.length-1,n)]?.focus();
 };
 function iconMode(upload){
  $('mdi-picker-body').hidden=upload;$('art-picker-body').hidden=!upload;
  $('browse-icon').setAttribute('aria-pressed',String(!upload));$('upload-icon').setAttribute('aria-pressed',String(upload));
  $(upload?'art-file':'icon-search').focus();
 }
 $('browse-icon').onclick=()=>iconMode(false);$('upload-icon').onclick=()=>iconMode(true);
 $('icon-more').onclick=()=>{iconLimit+=96;drawIcons()};
 $('icon-entity').onclick=()=>chooseIcon('entity');
 $('icon-default').onclick=()=>artTarget?setPositionIcon(''):chooseIcon('');
 $('icon-picker').addEventListener('close',()=>{clearCrop();artTarget=null});
 $('close-icon-picker').onclick=()=>$('icon-picker').close();
 $('icon-picker').addEventListener('click',ev=>{if(ev.target!==$('icon-picker'))return;const r=ev.target.getBoundingClientRect();if(ev.clientX<r.left||ev.clientX>r.right||ev.clientY<r.top||ev.clientY>r.bottom)$('icon-picker').close()});
 $('page-icon-button').onclick=()=>{
  const p=pageConfigs[selectedPage];if(!p)return;
  artTarget={page:p,pos:-1};clearCrop();$('art-role').value='off';$('art-role-field').hidden=true;$('icon-entity').hidden=false;$('icon-entity').disabled=!lead(p)||p.icon==='entity';$('icon-source').hidden=false;$('icon-picker-title').textContent='Choose a carousel icon';
  $('icon-picker-context').textContent='Shown in the bottom bar for “'+pageTitle(p,selectedPage)+'”.';
  $('icon-search').value='';focusIcon(pageIcon(p));displayArt($('icon-focus'),p.carousel_artwork);if(p.carousel_artwork)$('icon-focus-name').textContent='Custom image';$('icon-default').disabled=!(p.icon||p.carousel_artwork);renderIcons();loadIcons();$('icon-picker').showModal();iconMode(!!p.carousel_artwork);
 };

 const imageIds=pages=>[...new Set(pages.flatMap(p=>[p.carousel_artwork,...(p.artwork||[])]).filter(Boolean))];
 async function uploadImages(pages){
  for(const id of imageIds(pages)){
   if(!artPending.has(id))continue;
   const data=artData.get(id);
   const payload=data.replace(/\+/g,'-').replace(/\//g,'_'),length=artChunk*4/3;
   let result;
   if(data.length===4268)result=await rpc({op:'panel_art_put',data:payload});
   else for(let i=0;i<payload.length;i+=length){
    const offset=i*3/4;
    result=await rpc({op:'panel_art_put',q:id,offset,data:payload.slice(i,i+length)});
    if(i+length<payload.length&&result.next!==offset+artChunk)throw Error('The panel could not receive the image.');
   }
   if(result?.id!==id)throw Error('The panel could not verify the image.');
  }
 }
 globalThis.ONX_PAGE_ART={
  async export(pages){
   const result={};
   for(const id of imageIds(pages)){
    const data=artData.get(id)||await downloadImage(id);
    rgb565Url(data);result[id]=data;
   }
   return result;
  },
  import(data,pages){
   if(data===undefined)return;
   if(!data||typeof data!=='object'||Array.isArray(data)||Object.keys(data).length>40)throw Error('Invalid image collection.');
   for(const id of imageIds(pages))if(Object.hasOwn(data,id)){
    if(!artId.test(id)||typeof data[id]!=='string'||![4268,artSide*artSide*8/3].includes(data[id].length))throw Error('Invalid page image.');
    artUrls.set(id,rgb565Url(data[id]));artData.set(id,data[id]);artPending.add(id);
   }
  },
  upload:uploadImages
 };
 // Save: entries first (pages reference them), then the page list.
 async function savePages(){
  const leads=new Set();
  for(const [i,p] of pageConfigs.entries()){
   const first=lead(p),missing=isGrid(p)?!first:p.entities.some(e=>!e);
   if(missing||leads.has(first)){
    view='page';selectedPage=i;selectedPos=Math.max(0,p.entities.findIndex(e=>missing?!e:e===first));touch();
    status(missing?(isGrid(p)?'Place at least one action on the grid.':'Fill every position on “'+pageTitle(p,i)+'”, or choose a smaller layout.'):'Each page needs a different first assigned entity.');
    return false;
   }
   leads.add(first);
  }
  if(pageConfigs.some(p=>p.icon&&p.icon!=='entity'&&!ICON_RE.test(p.icon))){status('Icons use the mdi:name form, for example mdi:sofa.');return false}
  const a=app();if(!a)return false;const target=editorTarget,list=structuredClone(pageConfigs);
  try{await uploadImages(list)}catch(error){status(error.message);a.message?.(error.message,true);return false}
  if(target)target.pages=list;
  if(!await a.saveAll())return false;
  if(target){savedPages=JSON.stringify(pageConfigs);touch();return true}
  if(!dirtyPages()){a.message?.('Changes saved.');touch();return true}
  saving=true;renderPageEditor();
  try{const result=await rpc({op:'panel_pages_save',data:JSON.stringify({pages:list})});pageConfigs=normalize(result.pages);savedPages=panelPages=JSON.stringify(pageConfigs);pageError='';status('');a.message?.('Changes saved.');return true}
  catch(error){status(error.message);a.message?.(error.message+' The pages were not changed.',true);return false}
  finally{saving=false;touch()}
 }
 // Presets edit their own page list; the panel's list comes from the firmware.
 function loadTarget(target){
  editorTarget=target;
  if(target){pageConfigs=target.pages?normalize(target.pages):target.isNew?[]:singles(editorSlots);savedPages=JSON.stringify(pageConfigs)}
  else{pageConfigs=JSON.parse(panelPages);savedPages=panelPages}
  view='page';selectedPage=0;selectedPos=Math.max(0,pageConfigs[0]?.entities.findIndex(Boolean)??0);status('');
 }
 // Bridge for config_app.h: import/export, dirty state, discard and save.
 globalThis.ONX_PAGES={get:()=>pageConfigs,restore:list=>{pageConfigs=structuredClone(list);selectedPage=Math.max(0,Math.min(selectedPage,pageConfigs.length-1));selectedPos=Math.max(0,pageConfigs[selectedPage]?.entities.findIndex(Boolean)??0);renderPageEditor();redraw()},saved:()=>JSON.parse(panelPages),set:list=>{pageConfigs=normalize(list);selectPage(0)},dirty:dirtyPages,discard:()=>{pageConfigs=JSON.parse(savedPages);selectedPos=Math.max(0,pageConfigs[selectedPage]?.entities.findIndex(Boolean)??0);status('')},save:savePages};
 globalThis.ONX_PANEL_EDITOR=(slots,active,busy,refresh,target=null)=>{
  editorSlots=slots;editorBusy=busy;redraw=refresh;
  const switched=(target||null)!==editorTarget;if(switched)loadTarget(target||null);
  // Follow the app when it selects another entry (initial load, preset switch).
  const e=slots[active]?.entity||'';
  if(!switched&&(active!==editorActive||e!==editorEntity)&&e&&current()!==e){
   const own=pageConfigs[selectedPage]?.entities.indexOf(e)??-1,page=own>=0?selectedPage:pageConfigs.findIndex(p=>p.entities.includes(e));
   if(page>=0){view='page';selectedPage=page;selectedPos=pageConfigs[page].entities.indexOf(e)}else if(slots[active].timerHidden==='1'){view='timer';timerEntity=e}
  }
  editorActive=active;editorEntity=e;
  if(!focusMember())renderPageEditor();
 };
 rpc({op:'panel_pages'}).then(result=>{panelPages=JSON.stringify(normalize(result.pages));pagesLoaded=true;if(!editorTarget){pageConfigs=JSON.parse(panelPages);savedPages=panelPages;selectedPage=0;selectedPos=Math.max(0,pageConfigs[0]?.entities.findIndex(Boolean)??0)}if(!focusMember())touch()}).catch(error=>{pageError='Could not load pages: '+error.message;renderPageEditor()});
 // Mirrors onx_panel_layout::geometry (portrait and landscape).
 globalThis.ONX_PANEL_GEOMETRY=(kind,w,h,detail=false,rows=2,columns=2)=>{
  const top=detail?12:40,bottom=detail?12:52,gap=8,pad=12,body=h-top-bottom,inner=w-pad*2;
  if(kind===gridLayout){
   const usableW=w-16-gap*(columns-1),usableH=body-6-4-gap*(rows-1);  // As onx_panel_layout.h (GRID_TOP 6, GRID_BOTTOM 4).
   return Array.from({length:rows*columns},(_,i)=>{
    const c=i%columns,r=Math.floor(i/columns),x=Math.floor(c*usableW/columns),y=Math.floor(r*usableH/rows);
    return {x:8+x+c*gap,y:top+6+y+r*gap,w:Math.floor((c+1)*usableW/columns)-x,h:Math.floor((r+1)*usableH/rows)-y};
   });
  }
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
 // The centre editor shows the main page; the side preview shows the opened entity.
 {const pick=$('preview-screen');if(pick){pick.value='detail';const label=pick.closest('label');if(label)label.hidden=true}}
 rpc({op:'panel_screen'}).then(r=>{if(r.width&&r.height){nativeSize={width:r.width,height:r.height};redraw()}}).catch(()=>{});
 globalThis.ONX_PANEL_SCREEN=settings=>{
  const base=globalThis.ONX_PANEL_SIZE||nativeSize||{width:320,height:480},turned=['90','270'].includes(String(settings?.orientation||'0'));
  const portrait={width:Math.min(base.width,base.height),height:Math.max(base.width,base.height)};
  return turned?{width:portrait.height,height:portrait.width}:portrait;
 };
 const ns='http://www.w3.org/2000/svg';
 const node=(tag,attrs={})=>{const n=document.createElementNS(ns,tag);for(const [key,value]of Object.entries(attrs))n.setAttribute(key,value);return n};
 globalThis.ONX_PANEL_PREVIEW=(svg,slot,view,views,screen,settings)=>{
  if(settings&&settings.orientation!==lastSettings.orientation){lastSettings=settings;renderPageEditor()}else lastSettings=settings||{};
  // A port of onx_panel_detail.h: the same geometry, fonts, colours and rules,
  // drawn as SVG. Keep the two in step when either changes.
  const {width:W,height:H}=globalThis.ONX_PANEL_SCREEN(settings),detail=screen!=='main',wide=W>H;
  const frame=svg.closest('.screen-frame'),status=t=>{if(svg.id==='device-preview')$('preview-status').textContent=t};
  if(frame){frame.style.setProperty('aspect-ratio',`${W}/${H}`,'important');frame.style.setProperty('max-width',wide?'360px':'240px','important')}
  svg.setAttribute('viewBox',`0 0 ${W} ${H}`);
  svg.setAttribute('aria-label',(slot.name||slot.entity||'Empty page')+' rectangular panel preview');
  // Colours (onx_display_settings).
  const BG=settings?.background_color||'#101418',lum=c=>{const n=parseInt(c.slice(1),16);return ((n>>16)*299+((n>>8)&255)*587+(n&255)*114)},lightBg=lum(BG)>150000;
  const TEXT=lightBg?'#161b22':'#ffffff',CARD=mix(lightBg?'#000000':'#ffffff',BG,8),CONTROL=mix(lightBg?'#000000':'#ffffff',BG,13),TRACK=mix(lightBg?'#000000':'#ffffff',BG,23),MUTED=mix(TEXT,BG,68),SOFT=MUTED;
  const ACCENT=settings?.media_color||'#62b0ff',STRONG=mix(ACCENT,'#000000',80),TINT=mix(ACCENT,BG,28),INSET=CONTROL;
  const ink=bg=>lum(bg)>150000?'#161b22':'#ffffff';
  svg.replaceChildren(node('rect',{width:W,height:H,fill:BG}));
  // Pixel and font metrics. Fonts are the firmware's ESPHome fonts: the base
  // size times font_scale (1.3333 on the 3.5", 1 on the 2.8"), so 16/20/24 px
  // on the 3.5" and 12/15/18 px on the 2.8". Line metrics are MiSans' at the
  // 3.5" sizes, scaled with the layout like P().
  const scale=Math.min(W,H)/320,fontScale=Math.min(W,H)/240,P=v=>Math.trunc(v*Math.min(W,H)/320);
  const font=(size,lh,base,weight=500)=>({size:Math.round(size*fontScale),lh:Math.round(lh*scale),base:Math.round(base*scale),weight});
  const F={small:font(12,21,17),label:font(12,21,17),body:font(15,27,21),title:font(18,32,26,700),big:font(32,57,45),hero:font(64,113,89),clock:font(14,25,20,700),
    symbol:Math.min(W,H)>=320?{size:28,lh:31,base:24,weight:400}:{size:20,lh:22,base:17,weight:400}};  // symbol_font: Montserrat 28 (3.5"), 20 (2.8").
  const line=f=>f.lh,family='MiSans, "Inter", "Segoe UI", Arial, sans-serif';
  const ruler=document.createElement('canvas').getContext('2d');
  const width=(s,f)=>{ruler.font=`${f.weight} ${f.size}px ${family}`;return Math.ceil(ruler.measureText(String(s)).width)};
  const fit=(s,w,fonts)=>{let last=fonts[0];for(const f of fonts){last=f;if(width(s,f)<=w)return f}return last};
  // Word wrap into at most n lines, the last truncated with an ellipsis (LV_LABEL_LONG_DOT).
  const wrap=(s,f,w,n)=>{
   const out=[];let rest=String(s??'');
   while(rest&&out.length<n){
    if(width(rest,f)<=w||out.length===n-1){
     if(width(rest,f)<=w){out.push(rest);rest='';break}
     let cut=rest;while(cut&&width(cut+'…',f)>w)cut=cut.slice(0,-1);out.push(cut+'…');rest='';break;
    }
    let cut=rest.length;while(cut>1&&width(rest.slice(0,cut),f)>w)cut--;
    const space=rest.slice(0,cut).lastIndexOf(' ');if(space>0&&cut<rest.length)cut=space;
    out.push(rest.slice(0,cut).trimEnd());rest=rest.slice(cut).trimStart();
   }
   return out;
  };
  const linesFor=(s,f,w,max)=>Math.max(1,Math.min(max,wrap(s,f,w,max).length));
  let target=svg;
  const add=(n)=>{target.append(n);return n};
  const box=(x,y,w,h,fill,r=0,attrs={})=>add(node('rect',{x,y,width:Math.max(0,w),height:Math.max(0,h),rx:Math.min(r,Math.max(0,w)/2,Math.max(0,h)/2),fill,...attrs}));
  const text=(s,f,color,x,y,w=-1,align='left',n=1)=>{
   if(s===undefined||s===null||s==='')return;
   const rows=w>0?wrap(s,f,w,n):[String(s)],anchor=align==='center'?'middle':align==='right'?'end':'start',ax=align==='center'?x+w/2:align==='right'?x+w:x;
   rows.forEach((row,i)=>{const t=add(node('text',{x:ax,y:y+i*f.lh+f.base,'font-size':f.size,'font-weight':f.weight,fill:color,'font-family':family,'text-anchor':anchor}));t.textContent=row});
  };
  // Icons: the entity's icon, or LVGL symbols drawn with their MDI equivalents.
  const SYMBOL={power:'mdi:power',play:'mdi:play',pause:'mdi:pause',prev:'mdi:skip-previous',next:'mdi:skip-next',minus:'mdi:minus',plus:'mdi:plus',list:'mdi:format-list-bulleted',close:'mdi:close',stop:'mdi:stop',ok:'mdi:check',volume:'mdi:volume-high'};
  let currentArt='';
  const glyph=(name,cx,cy,size,color)=>{
   if(currentArt){add(node('image',{href:currentArt,x:cx-size/2,y:cy-size/2,width:size,height:size}));return}
   if(/^mdi:[a-z0-9-]+$/.test(name||''))add(node('image',{href:'https://api.iconify.design/mdi/'+name.slice(4)+'.svg?color='+encodeURIComponent(color),x:cx-size/2,y:cy-size/2,width:size,height:size}));
  };
  const viewOf=e=>views.find(v=>v.entity===e);
  const iconFor=e=>viewOf(e)?.icon||iconOf(e)||'mdi:help-circle-outline';
  const icon=(e,cx,cy,size,color)=>glyph(iconFor(e),cx,cy,size,color);
  // LVGL symbols fill more of their font size than an MDI glyph fills its box.
  // Per glyph: how much larger the MDI box must be than the font size to match
  // the LVGL (FontAwesome) symbol's drawn size.
  const SYMBOL_SCALE={stop:1.3,play:1.75,pause:1.6,prev:1.4,next:1.4,power:1.25,minus:1.3,plus:1.3,volume:1.2,list:1.2,close:1.3,ok:1.3};
  const symbol=(name,cx,cy,color)=>glyph(SYMBOL[name],cx,cy,Math.round(F.symbol.size*(SYMBOL_SCALE[name]||1.2)),color);
  const button=(x,y,w,h,r,bg,caption,f,fg,attrs={})=>{if(bg===ACCENT||bg===STRONG)fg=ink(bg);box(x,y,w,h,bg,r,attrs);if(caption)text(caption,f,fg,x+P(4),y+(h-f.lh)/2,w-P(8),'center')};
  const circle=(cx,cy,size,bg,sym,fg,attrs={})=>{add(node('circle',{cx,cy,r:size/2,fill:bg,...attrs}));if(bg===ACCENT||bg===STRONG)fg=ink(bg);symbol(sym,cx,cy,fg)};
  const chips=(r,labels,sel)=>{if(!labels.length)return;const gap=P(8),n=labels.length,w=Math.trunc((r.w-gap*(n-1))/n);labels.forEach((l,i)=>{const on=i===sel;button(r.x+i*(w+gap),r.y,w,r.h,r.h/2,on?TINT:CONTROL,l,F.small,on?ACCENT:SOFT,on?{stroke:ACCENT,'stroke-width':P(2)}:{})})};
  const row=(r,name,value,bg=CARD)=>{box(r.x,r.y,r.w,r.h,bg,P(10));const pad=P(12),lh=F.small.lh;text(name,F.small,MUTED,r.x+pad,r.y+(r.h-lh)/2,r.w/2-pad);text(value,F.small,TEXT,r.x+r.w/2,r.y+(r.h-lh)/2,r.w/2-pad,'right')};
  const tile=(r,name,value,bg=CARD)=>{box(r.x,r.y,r.w,r.h,bg,P(12));const pad=P(10),iw=r.w-2*pad,gap=Math.max(0,Math.trunc((r.h-F.label.lh-F.body.lh)/3));text(name,F.label,MUTED,r.x+pad,r.y+gap,iw);text(value,fit(value,iw,[F.body,F.small]),TEXT,r.x+pad,r.y+r.h-gap-F.body.lh,iw)};
  const header=(r,name,context,reserve=0)=>{const w=r.w-reserve,f=fit(name,w,[F.title,F.body]);text(name,f,TEXT,r.x,r.y,w);if(context)text(context,F.small,MUTED,r.x,r.y+f.lh+P(2),w)};
  const level=(x,y,w,h,fr)=>{box(x,y,w,h,CONTROL,h/2);if(!isFinite(fr))return;fr=Math.max(0,Math.min(1,fr));if(fr>0)box(x,y,Math.max(h,w*fr),h,ACCENT,h/2)};
  const arc=(cx,cy,size,aw,from,to,color,round=true)=>{ // Degrees clockwise from 3 o'clock, as LVGL.
   const r=(size-aw)/2,rad=a=>a*Math.PI/180,sweep=((to-from)%360+360)%360||360;
   if(sweep>=359.9){add(node('circle',{cx,cy,r,fill:'none',stroke:color,'stroke-width':aw}));return}
   const a0=rad(from),a1=rad(from+sweep);
   add(node('path',{d:`M ${cx+r*Math.cos(a0)} ${cy+r*Math.sin(a0)} A ${r} ${r} 0 ${sweep>180?1:0} 1 ${cx+r*Math.cos(a1)} ${cy+r*Math.sin(a1)}`,fill:'none',stroke:color,'stroke-width':aw,'stroke-linecap':round?'round':'butt'}));
  };
  // Number formatting (onx_detail_view::compact, fmt, pct, label).
  const num=v=>{const n=parseFloat(v);return isFinite(n)?n:NaN};
  const compact=v=>{const n=num(v);if(!isFinite(n))return String(v??'');let s=n.toFixed(2).replace(/0+$/,'').replace(/\.$/,'');return s==='-0'?'0':s};
  const fmt=(v,d=-1)=>!isFinite(v)?'—':d<0?compact(v):Number(v).toFixed(d);
  const pct=v=>isFinite(v)?Math.round(v)+'%':'—';
  const label=s=>{s=String(s??'').replaceAll('_',' ');return s?s[0].toUpperCase()+s.slice(1):s};
  const heroSafe=s=>!!s&&/^[0-9.,: -]+$/.test(s),heroText=s=>heroSafe(String(s).replaceAll('°',''));
  const options=s=>{try{const v=JSON.parse(String(s||'[]').replaceAll("'",'"'));return Array.isArray(v)?v.map(String):[]}catch{return String(s||'').split(',').map(x=>x.trim().replace(/^\[|\]$/g,'').replace(/^'|'$/g,'')).filter(Boolean)}};
  const conditionText=s=>({partlycloudy:'Partly cloudy','clear-night':'Clear','lightning-rainy':'Storm','snowy-rainy':'Sleet'}[s]||label(s));
  const present=(v,unit)=>v===undefined||v===null||v===''||['None','unknown','unavailable'].includes(String(v))?'':compact(v)+(unit?(unit==='%'||unit==='°'?unit:' '+unit):'');
  const readingOf=(v,unit)=>v===undefined||v===null||v===''||['None','unknown','unavailable'].includes(String(v))?'—':compact(v)+unit;
  const hvacLabel=m=>m==='heat_cool'?'Auto':m==='fan_only'?'Fan':label(m);
  // As onx_sensor_view::relative.
  const relative=(t,now)=>{if(!t||now<1577836800)return '—';const delta=t-now,sec=Math.abs(delta);if(sec<60)return delta<0?'just now':'in less than a minute';
   const c=sec>=86400?Math.floor((sec+1800)/86400):sec>=3600?Math.floor((sec+120)/3600):Math.floor((sec+30)/60),u=(sec>=86400?'day':sec>=3600?'hour':'minute')+(c!==1?'s':'');
   const a=Math.max(1,c)+' '+u;return delta<0?a+' ago':'in '+a};
  const ago=(then,now)=>{const m=Math.max(0,Math.round((now-then)/60));return m<1?'just now':m<60?m+' min ago':m<1440?Math.round(m/60)+' h ago':Math.round(m/1440)+' d ago'};
  // The panel's time pattern: %H:%M, or %I:%M with an optional AM/PM (onx_display_settings::time_pattern).
  const clock=t=>{const d=new Date(t*1000),twelve=String(settings?.hour_format)==='12',h=twelve?(d.getHours()%12||12):d.getHours(),m=String(d.getMinutes()).padStart(2,'0');
   return String(h).padStart(2,'0')+':'+m+(twelve&&String(settings?.show_ampm??settings?.ampm)!=='false'&&settings?.show_ampm!==false&&settings?.show_ampm!==0&&settings?.show_ampm!=='0'?(d.getHours()<12?' AM':' PM'):'')};
  // A large reading with a smaller unit on its baseline.
  const reading=(value,unit,x,y,w,hero=true)=>{
   const u=!unit?'':(unit==='%'||unit==='°'?unit:' '+unit),uw=u?width(u,F.title):0;
   const f=hero&&heroSafe(value)&&width(value,F.hero)+uw<=w?F.hero:width(value,F.big)+uw<=w?F.big:F.title;
   const vw=Math.min(width(value,f),w-uw);text(value,f,TEXT,x,y,vw+1);
   if(u)text(u,F.title,SOFT,x+vw+P(2),y+f.base-F.title.base,w-vw-P(2));return f.lh;
  };
  // History charts (draw_chart): a line with optional fill, or 24 columns.
  const chart=(points,now,range,x,y,w,h,color,fill,bars=false)=>{
   if(!points||points.length<2||w<P(24)||h<P(12))return null;
   const vals=points.map(p=>p.value),lo=Math.min(...vals),hi=Math.max(...vals),start=now-range,span=Math.max(hi-lo,1e-3),gap=Math.max(1200,range/24);
   if(bars){
    const N=24,sum=Array(N).fill(0),n=Array(N).fill(0);
    for(const p of points){if(p.t<start||p.t>now)continue;const b=Math.min(N-1,Math.floor((p.t-start)*N/range));sum[b]+=p.value;n[b]++}
    const top=Math.max(hi,1e-3),slot=Math.max(1,Math.trunc(w/N)),bw=Math.max(2,Math.trunc(slot*2/3));
    for(let b=0;b<N;b++){if(!n[b])continue;const bh=Math.max(2,Math.trunc(Math.max(0,sum[b]/n[b])/top*h));box(x+Math.trunc(b*w/N)+slot/2-bw/2,y+h-bh,bw,bh,color,0)}
    return {lo,hi};
   }
   const inset=Math.max(2,Math.trunc(h/12)),px=p=>x+Math.max(0,Math.min(1,(p.t-start)/range))*w,py=v=>y+inset+(hi-v)/span*(h-2*inset);
   let d='';points.forEach((p,i)=>{d+=(i&&p.t-points[i-1].t<=gap?'L':'M')+px(p).toFixed(1)+' '+py(p.value).toFixed(1)+' '});
   if(fill){let a='';for(let i=1;i<points.length;i++){const p0=points[i-1],p1=points[i];if(p1.t-p0.t>gap)continue;a+=`M${px(p0)} ${py(p0.value)} L${px(p1)} ${py(p1.value)} L${px(p1)} ${y+h} L${px(p0)} ${y+h} Z `}if(a)add(node('path',{d:a,fill:color,'fill-opacity':.2}))}
   add(node('path',{d,fill:'none',stroke:color,'stroke-width':Math.max(2,P(3)),'stroke-linecap':'round','stroke-linejoin':'round'}));
   return {lo,hi};
  };
  // The sensor detail graph (onx_sensor_graph): grid lines, min/max labels and the trace.
  const sensorGraph=(points,now,range,x,y,w,h)=>{
   if(!points||points.length<1)return;
   const vals=points.map(p=>p.value),low=Math.min(...vals),high=Math.max(...vals),axis=v=>Math.abs(v)>=100?Math.round(v).toString():String(Math.round(v*10)/10);
   const lab=[axis(low),axis(high)],left=Math.min(Math.round(66*scale),Math.max(width(lab[0],F.small),width(lab[1],F.small))+5),right=w-1,bottom=h-1;
   for(const yy of [0,Math.trunc(bottom/2),bottom])add(node('line',{x1:x+left,y1:y+yy,x2:x+right,y2:y+yy,stroke:'#344653','stroke-width':1}));
   text(lab[1],F.small,MUTED,x,y,left-5,'right');text(lab[0],F.small,MUTED,x,y+h-F.small.lh,left-5,'right');
   let d='';points.forEach((p,i)=>{const px=x+right-(now-p.t)*(right-left)/range,py=y+(high>low?bottom-(p.value-low)*bottom/(high-low):bottom/2);d+=(i&&p.t-points[i-1].t<=Math.max(1200,range/24)?'L':'M')+px.toFixed(1)+' '+py.toFixed(1)+' '});
   add(node('path',{d,fill:'none',stroke:ACCENT,'stroke-width':2}));
  };
  const artwork=(url,x,y,size,r)=>{
   const id='art-'+Math.random().toString(36).slice(2),clip=node('clipPath',{id});clip.append(node('rect',{x,y,width:size,height:size,rx:r}));add(clip);
   box(x,y,size,size,CONTROL,r);
   try{const u=new URL(url);if(['http:','https:'].includes(u.protocol)){add(node('image',{href:u.href,x,y,width:size,height:size,preserveAspectRatio:'xMidYMid slice','clip-path':`url(#${id})`,'data-panel-artwork':''}));return true}}catch{}
   return false;
  };
  const transport=(cx,cy,side,main,gap,playing)=>{circle(cx-main/2-gap-side/2,cy,side,INSET,'prev',TEXT,{'data-media-button':'previous'});circle(cx,cy,main,ACCENT,playing?'pause':'play',TEXT,{'data-media-button':'play'});circle(cx+main/2+gap+side/2,cy,side,INSET,'next',TEXT,{'data-media-button':'next'})};
  const progressBar=(v,x,y,w,remaining,lab)=>{
   const bar=P(6),pr=v?.progress;if(!(pr>=0))return;  // The panel hides the bar until there is a position.
   box(x,y,w,bar,remaining?CONTROL:TRACK,bar/2,{'data-media-bar':''});
   if(pr>=0){box(x,y,Math.max(bar,w*pr/1000),bar,ACCENT,bar/2);const d=num(v?.panel?.duration),s=isFinite(d)?d*pr/1000:NaN,dur=t=>isFinite(t)?Math.floor(t/60)+':'+String(Math.floor(t%60)).padStart(2,'0'):'';
    if(isFinite(d)){const ly=y+bar+(lab??P(remaining?4:6));text(dur(s),F.small,MUTED,x,ly,w/2);text(remaining?'-'+dur(d-s):dur(d),F.small,MUTED,x+w/2,ly,w/2,'right')}}
  };
  const now=Date.now()/1000;
  const history=v=>({points:(v?.graph||[]).map(p=>({t:p.t,value:p.value})).filter(p=>isFinite(p.value)),now:v?.graph_now||now,range:v?.panel?.history_range||43200});
  const rangeLabel=r=>({3600:'1 h',86400:'24 h',604800:'7 d'}[r]||'12 h');
  if(!slot.entity){text('Choose an entity',F.body,TEXT,P(24),H/2,W-P(48));status('Choose what this page displays.');return}

  // ---------------------------------------------------------------- details
  const layout=()=>{const pad=P(16);if(wide){const vw=Math.trunc(W*48/100),x=vw+P(12),cw=W-x-pad;return{vis:{x:pad,y:pad,w:vw-pad,h:H-2*pad},head:{x,y:pad,w:cw,h:P(48)},ctl:{x,y:pad+P(60),w:cw,h:H-2*pad-P(60)}}}
   const vis={x:pad,y:P(76),w:W-2*pad,h:Math.trunc(H*50/100)},y=vis.y+vis.h+P(12);return{head:{x:pad,y:pad,w:W-2*pad,h:P(48)},vis,ctl:{x:pad,y,w:W-2*pad,h:H-pad-y}}};
  const bottomOf=r=>r.y+r.h;
  const buildLight=(l,e,d,title)=>{
   const on=!!d.light_on&&d.available!==false,dimmable=d.dimmable!==false,lvl=on?Number(d.brightness??0):0,modes=[];
   if(dimmable)modes.push(0);if(d.supports_ct)modes.push(1);if(d.supports_rgb)modes.push(2);
   let mode=Number(d.light_mode??0);if(!modes.includes(mode))mode=modes.length?modes[0]:-1;
   const pad=P(16),barH=P(56),info=wide?{x:l.head.x,y:pad,w:l.head.w,h:H-2*pad}:{x:pad,y:pad,w:W-2*pad,h:H-2*pad};
   text(title,fit(title,info.w,[F.title,F.body]),TEXT,info.x,info.y,info.w);
   let y=info.y+F.title.lh+P(10);
   text(d.available===false?'—':dimmable?(on?pct(lvl):'Off'):(on?'On':'Off'),F.big,TEXT,info.x,y,info.w,'center');y+=F.big.lh+P(12);
   const bs=P(44),gap=P(6),n=modes.length>1?modes.length:0,barW=P(8)*2+bs+(n?P(17)+n*bs+(n-1)*gap:0),barX=info.x+(info.w-barW)/2,barY=bottomOf(l.ctl)-barH;
   box(barX,barY,barW,barH,CARD,barH/2);const by=barY+(barH-bs)/2;
   circle(barX+P(8)+bs/2,by+bs/2,bs,on?STRONG:CONTROL,'power',on?ink(STRONG):SOFT);
   if(n){let x=barX+P(8)+bs+P(8);box(x,barY+(barH-P(28))/2,1,P(28),TRACK,0);x+=P(9);
    for(const m of modes){const sel=m===mode;add(node('circle',{cx:x+bs/2,cy:by+bs/2,r:bs/2,fill:sel?CONTROL:CARD,...(sel?{stroke:TEXT,'stroke-width':P(2)}:{})}));glyph(['mdi:brightness-6','mdi:thermometer','mdi:palette'][m],x+bs/2,by+bs/2,P(24),TEXT);x+=bs+gap}}
   // Colour presets above the mode bar.
   const sw=[];if(d.available!==false&&(d.supports_rgb||d.supports_ct)){const lo=d.kelvin_min||2000,hi=d.kelvin_max>lo?d.kelvin_max:6500;for(const k of [2000,3500,5000,6500])sw.push(kelvinRgb(Math.min(hi,Math.max(lo,k))));if(d.supports_rgb)sw.push('#7FACFF','#D796FF','#FF9EF3','#FF6E54','#FF3B30','#34C759','#0A84FF','#FFD60A')}
   let controlBottom=barY-P(16);
   if(sw.length&&!wide){const ss=P(40),sg=P(12),rh=ss+P(8),ry=barY-P(12)-rh,total=sw.length*(ss+sg)-sg;let x=Math.max(pad,(W-total)/2);
    for(const c of sw){add(node('circle',{cx:x+ss/2,cy:ry+P(4)+ss/2,r:ss/2,fill:c,stroke:TRACK,'stroke-width':1,'data-light-preset':''}));x+=ss+sg}controlBottom=ry-P(12)}
   const vis=wide?l.vis:{x:pad,y,w:W-2*pad,h:controlBottom-y};
   if(d.available===false||vis.h<P(60))return;
   if(mode<0){const s=Math.min(vis.w,vis.h,P(150));circle(vis.x+vis.w/2,vis.y+vis.h/2,s,on?STRONG:CONTROL,'power',on?TEXT:SOFT);return}
   if(mode===2){const s=Math.min(vis.w,vis.h,P(220)),x0=vis.x+(vis.w-s)/2,y0=vis.y+(vis.h-s)/2;
    // Hue around (red at 3 o'clock, clockwise), saturation outwards to a white centre.
    const r0=s/2,cxw=x0+r0,cyw=y0+r0,g=add(node('g',{'data-light-wheel':''}));
    for(let a=0;a<360;a+=5){const a0=a*Math.PI/180,a1=(a+5.6)*Math.PI/180;g.append(node('path',{d:`M${cxw} ${cyw} L${cxw+r0*Math.cos(a0)} ${cyw+r0*Math.sin(a0)} A${r0} ${r0} 0 0 1 ${cxw+r0*Math.cos(a1)} ${cyw+r0*Math.sin(a1)} Z`,fill:hsv(a+2.5,100)}))}
    const wid='wh'+Math.random().toString(36).slice(2),rg=node('radialGradient',{id:wid});rg.append(node('stop',{offset:0,'stop-color':'#fff'}),node('stop',{offset:1,'stop-color':'#fff','stop-opacity':0}));g.append(rg,node('circle',{cx:cxw,cy:cyw,r:r0,fill:`url(#${wid})`}));
    const ms=P(30),r=s/2,a=Number(d.hue??0)*Math.PI/180,rr=(on?Math.max(0,Math.min(100,Number(d.saturation??100))):0)/100*(r-ms/4);
    add(node('circle',{cx:x0+r+Math.cos(a)*rr,cy:y0+r+Math.sin(a)*rr,r:ms/2,fill:hsv(Number(d.hue??0),on?Number(d.saturation??100):0),stroke:'#ffffff','stroke-width':P(3)}));return}
   const sw2=Math.min(P(124),vis.w),sh=vis.h,sx=vis.x+(vis.w-sw2)/2,sy=vis.y;
   if(mode===1){const lo=d.kelvin_min||2200,hi=d.kelvin_max>lo?d.kelvin_max:6500,id='ct'+Math.random().toString(36).slice(2),g=node('linearGradient',{id,x1:0,y1:0,x2:0,y2:1});
    g.append(node('stop',{offset:0,'stop-color':kelvinRgb(hi)}),node('stop',{offset:1,'stop-color':kelvinRgb(lo)}));add(g);box(sx,sy,sw2,sh,`url(#${id})`,P(30));
    const k=d.kelvin||(lo+hi)/2,hh=P(6),hy=Math.min(sh-hh-P(8),Math.max(P(8),Math.trunc(sh*(1-(k-lo)/(hi-lo)))-hh/2));box(sx+sw2/4,sy+hy,sw2/2,hh,'#161B22',hh/2,{stroke:'#ffffff','stroke-width':P(2)});return}
   const id='clip'+Math.random().toString(36).slice(2),clip=node('clipPath',{id});clip.append(node('rect',{x:sx,y:sy,width:sw2,height:sh,rx:P(30)}));add(clip);
   box(sx,sy,sw2,sh,CONTROL,P(30),{'data-brightness-slider':''});const fh=Math.trunc(sh*Math.max(0,Math.min(100,lvl))/100);
   box(sx,sy+sh-fh,sw2,fh,ACCENT,0,{'clip-path':`url(#${id})`});const kh=P(5),kw=Math.trunc(sw2*30/100);box(sx+(sw2-kw)/2,sy+Math.min(sh-kh-P(8),Math.max(P(8),sh-fh+P(12))),kw,kh,ink(ACCENT),kh/2,{'fill-opacity':.7});
  };
  const buildClimate=(l,e,d,title)=>{
   const mode=d.hvac||d.state;let tgt=num(d.temperature);const range=!isFinite(tgt)&&isFinite(num(d.low))&&isFinite(num(d.high)),unit=d.unit||'°';  // As build_climate.
   const lo=isFinite(d.minimum)?d.minimum:7,hi=isFinite(d.maximum)&&d.maximum>lo?d.maximum:35;
   let context=hvacLabel(mode);if(d.preset&&!['None','none'].includes(d.preset))context+=' · '+label(d.preset);header(l.head,title,context);
   const size=Math.min(l.vis.w,l.vis.h,P(260)),cx=l.vis.x+l.vis.w/2,ay=l.vis.y+(l.vis.h-size)/2,cy=ay+size/2,aw=P(16);
   arc(cx,cy,size,aw,135,45,CONTROL);const v=isFinite(tgt)?tgt:lo,frac=Math.max(0,Math.min(1,(v-lo)/(hi-lo)));if(frac>0)arc(cx,cy,size,aw,135,135+270*frac,mode==='off'?TRACK:ACCENT);
   if(!range&&isFinite(tgt)&&(d.features&1)){const a=(135+270*frac)*Math.PI/180,r=(size-aw)/2;add(node('circle',{cx:cx+r*Math.cos(a),cy:cy+r*Math.sin(a),r:aw/2+P(4),fill:TEXT}))}
   const big=range?fmt(num(d.target_low))+'–'+fmt(num(d.target_high))+'°':fmt(tgt)+'°';
   text(big,F.big,TEXT,cx-size/2,cy-F.big.lh/2-P(8),size,'center');text('Now '+fmt(num(d.current))+' '+unit,F.small,SOFT,cx-size/2,cy+F.big.lh/2,size,'center');
   const bs=P(56),by=ay+size-bs/2-P(6);circle(cx-size/2+bs/2,by,bs,CONTROL,'minus',TEXT);circle(cx+size/2-bs/2,by,bs,CONTROL,'plus',TEXT);
   const modes=options(d.modes).slice(0,4),opts=[['Preset',d.presets,d.preset_mode,16],['Swing',d.swing_modes,d.swing_mode,32],['Fan',d.fan_modes,d.fan_mode,8]].filter(o=>(d.features&o[3])&&options(o[1]).length);
   const rows=(modes.length>1?1:0)+opts.length,gap=rows>=3?P(6):P(10),ch=rows>0?Math.min(P(44),Math.trunc((l.ctl.h-(rows-1)*gap)/rows)):P(44);let y=l.ctl.y;
   if(modes.length>1){chips({x:l.ctl.x,y,w:l.ctl.w,h:ch},modes.map(hvacLabel),modes.indexOf(mode));y+=ch+gap}
   for(const o of opts){row({x:l.ctl.x,y,w:l.ctl.w,h:ch},o[0],label(o[2])+'  ›');y+=ch+gap}
  };
  const buildFan=(l,e,d,title)=>{
   const on=(d.hvac||d.state)==='on',lvl=num(d.percentage),speeds=(d.features&1)&&d.step>0,n=speeds?Math.max(1,Math.min(8,Math.round(100/d.step))):0,filled=on&&isFinite(lvl)&&n?Math.max(0,Math.min(n,Math.ceil(lvl*n/100-0.01))):0;
   let context=on?'On':'Off';if(on&&n)context+=' · Speed '+filled+' of '+n;const pw=P(52);header(l.head,title,context,pw+P(8));
   circle(l.head.x+l.head.w-pw/2,l.head.y+pw/2-P(2),pw,on?STRONG:CONTROL,'power',on?TEXT:SOFT);
   text(on&&isFinite(lvl)?pct(lvl):(on?'On':'Off'),F.big,TEXT,l.vis.x,l.vis.y+P(8),l.vis.w,'center');
   if(n){const gap=P(10),bw=Math.trunc((l.vis.w-gap*(n-1))/n),base=bottomOf(l.vis)-P(8),top=l.vis.y+F.big.lh+P(28),span=base-top;for(let i=0;i<n;i++){const h=Math.trunc(span*(i+2)/(n+1));box(l.vis.x+i*(bw+gap),base-h,bw,h,i<filled?ACCENT:CONTROL,P(10))}}
   const rh=P(48),gap=P(10);let y=l.ctl.y;const addRow=(name,value)=>{if(y+rh>bottomOf(l.ctl)+P(4))return;row({x:l.ctl.x,y,w:l.ctl.w,h:rh},name,value);y+=rh+gap};
   if(d.features&2)addRow('Oscillate',d.oscillating==='true'?'On':'Off');
   if(d.features&4)addRow('Direction',label(d.direction||'forward')+'  ›');
   if((d.features&8)&&options(d.presets).length)addRow('Preset',label(!d.preset_mode||d.preset_mode==='None'?'none':d.preset_mode)+'  ›');
  };
  const buildCover=(base,e,d,title)=>{
   const raw=slot.coverPresets??d.cover_presets??'15,25,50,75',enabled=(slot.coverPresetsEnabled??d.cover_presets_enabled??'1')!=='0';
   const presets=enabled&&(d.features&4)?String(raw).split(',').map(v=>v.trim()).filter(v=>/^\d{1,3}$/.test(v)&&Number(v)<=100).slice(0,4).map(Number):[];
   const l={...base,vis:{...base.vis},ctl:{...base.ctl}};
   if(presets.length&&!wide){const extra=P(12)+F.small.lh+P(6)+P(40);l.vis.h-=extra;l.ctl.y-=extra;l.ctl.h+=extra}
   const position=num(d.position),known=isFinite(position),moving=['opening','closing'].includes(d.state);header(l.head,title,label(d.state)+(known?' · '+pct(position):''));
   const fw=Math.min(P(200),l.vis.w),fh=l.vis.h-F.big.lh-P(12),fx=l.vis.x+(l.vis.w-fw)/2,fy=l.vis.y,border=P(6);
   box(fx,fy,fw,fh,CONTROL,P(10));box(fx+border,fy+border,fw-2*border,fh-2*border,'#1B2733',P(4));
   const inner=fh-2*border,iw=fw-2*border,closed=known?Math.trunc(inner*(100-Math.max(0,Math.min(100,position)))/100):inner/2;
   box(fx+border,fy+border,iw,closed,'#4A525B',0);for(let y=P(10);y<closed-P(6);y+=P(14))box(fx+border,fy+border+y,iw,P(2),'#3A4148',0);box(fx+border,fy+border+Math.max(0,closed-P(4)),iw,P(4),ACCENT,0);
   text(known?pct(position):label(d.state),F.big,TEXT,l.vis.x,fy+fh+P(8),l.vis.w,'center');
   const all=[['Open',1],['Stop',8],['Close',2]].filter(b=>!d.features||(d.features&b[1]));if(!all.length)return;
   const gap=P(10),n=all.length,bw=Math.trunc((l.ctl.w-gap*(n-1))/n),bh=Math.min(P(56),l.ctl.h);all.forEach((b,i)=>button(l.ctl.x+i*(bw+gap),l.ctl.y,bw,bh,P(14),b[0]==='Stop'&&moving?STRONG:CONTROL,b[0],F.body,TEXT));
   if(presets.length){const cols=Math.min(wide&&W<400?2:4,presets.length),pg=P(8),ph=P(40),y=l.ctl.y+bh+P(12),pw=Math.trunc((l.ctl.w-(cols-1)*pg)/cols);
    text('Positions',F.small,MUTED,l.ctl.x,y,l.ctl.w);const top=y+F.small.lh+P(6);
    presets.forEach((v,i)=>{const py=top+Math.floor(i/cols)*(ph+pg);if(py+ph>bottomOf(l.ctl))return;const on=known&&Math.abs(position-v)<.51;button(l.ctl.x+(i%cols)*(pw+pg),py,pw,ph,ph/2,on?TINT:CONTROL,v+'%',F.small,on?ACCENT:SOFT,on?{stroke:ACCENT,'stroke-width':P(2)}:{})});
   }
  };
  const buildToggle=(l,e,d,title,v)=>{
   const on=d.state==='on';header(l.head,title,d.available!==false?(on?'On':'Off'):'Unavailable');
   const size=Math.min(P(150),l.vis.w,l.vis.h-F.big.lh-P(16)),cx=l.vis.x+l.vis.w/2,cy=l.vis.y+(l.vis.h-F.big.lh-P(12))/2;
   circle(cx,cy,size,on?STRONG:CONTROL,'power',on?TEXT:SOFT);text(on?'On':'Off',F.big,TEXT,l.vis.x,cy+size/2+P(10),l.vis.w,'center');
   if(d.power===undefined&&!(v?.graph?.length))return;const th=P(56),ty=wide?l.ctl.y:Math.min(l.ctl.y,cy+size/2+P(10)+F.big.lh+P(10));
   tile({x:l.ctl.x,y:ty,w:l.ctl.w,h:th},d.power_unit==='kWh'?'Energy':'Power',d.power!==undefined?d.power+' '+(d.power_unit||''):'—');
   const gy=ty+th+P(10),gb=H-P(24);if(gb-gy>=P(60)){const hst=history(v);sensorGraph(hst.points,hst.now,hst.range,l.ctl.x,gy,l.ctl.w,gb-gy)}
  };
  const buildAction=(l,e,d,title)=>{
   const script=e.startsWith('script.'),scene=e.startsWith('scene.'),automation=e.startsWith('automation.'),running=script&&d.state==='on';
   header(l.head,title,(script?'Script':automation?'Automation':scene?'Scene':'Button')+(running?' · Running…':''));
   // As build_action: the 2.8" uses a smaller button without "Tap to run" and gives the room to the history.
   const compact=Math.min(W,H)<320&&!wide;
   if(compact){l.vis.h=Math.min(P(120),l.vis.w)+P(8);l.ctl.y=l.vis.y+l.vis.h+P(12);l.ctl.h=H-P(16)-l.ctl.y}
   const size=compact?Math.min(P(120),l.vis.w):Math.min(P(170),l.vis.w,l.vis.h-P(40)),cx=l.vis.x+l.vis.w/2,cy=compact?l.vis.y+l.vis.h/2:l.vis.y+(l.vis.h-P(30))/2;
   circle(cx,cy,size,STRONG,'play',TEXT,running?{stroke:ACCENT,'stroke-width':P(6)}:{});
   if(!compact||running){if(compact)l.ctl.y+=F.body.lh;text(running?'Running…':'Tap to run',F.body,SOFT,l.vis.x,cy+size/2+(compact?P(4):P(12)),l.vis.w,'center')}
   if(automation){const en=d.state==='on';button(l.ctl.x,l.ctl.y,l.ctl.w,P(48),P(16),CONTROL,en?'Enabled · Tap to disable':'Disabled · Tap to enable',F.body,TEXT)}
   else if(running)button(l.ctl.x,l.ctl.y,l.ctl.w,P(48),P(16),CONTROL,'Stop script',F.body,TEXT);
   // Run history, as build_action: last run, a 24 h strip and the recent runs.
   if(automation)return;
   const log=Array.isArray(d.changes)?d.changes:[];let last=log.length?log[0].at:0;
   if(!last&&!script){const t=Date.parse(d.state);if(isFinite(t))last=Math.floor(t/1000)}
   const x=l.ctl.x,w=l.ctl.w,bottom=bottomOf(l.ctl);let y=l.ctl.y+(running?P(48)+P(14):0);
   if(y+F.body.lh>bottom)return;
   text(last?'Last run '+relative(last,now):'No runs recorded yet',F.body,TEXT,x,y,w);y+=F.body.lh+P(12);
   const sh=P(16);
   if(now>1577836800&&y+F.small.lh*2+sh+P(8)<=bottom){
    text('Runs, last 24 h',F.small,MUTED,x,y,w);y+=F.small.lh+P(2);box(x,y,w,sh,CARD,P(4));
    for(const run of log)if(run.at<=now&&now-run.at<86400)box(x+Math.trunc((86400-(now-run.at))*(w-P(4))/86400),y,P(4),sh,ACCENT,P(2));
    y+=sh+P(4);text('-24 h',F.small,MUTED,x,y,w/2);text('now',F.small,MUTED,x+w/2,y,w/2,'right');y+=F.small.lh+P(14);
   }
   const rh=F.body.lh+P(6);if(!log.length||y+F.small.lh+rh>bottom)return;
   text('Recent runs',F.small,MUTED,x,y,w);y+=F.small.lh+P(4);
   for(const run of log){if(y+rh>bottom)break;text(clock(run.at),F.body,TEXT,x,y,w/2);text(relative(run.at,now),F.body,SOFT,x+w/2,y,w/2,'right');y+=rh}
  };
  const binaryText=(kind,s)=>{const on=s==='on';if(!['on','off'].includes(s))return label(s);if(['door','window','opening','garage_door'].includes(kind))return on?'Open':'Closed';if(['motion','occupancy','presence'].includes(kind))return on?'Detected':'Clear';if(kind==='moisture')return on?'Wet':'Dry';if(kind==='lock')return on?'Unlocked':'Locked';if(kind==='connectivity')return on?'Connected':'Disconnected';return on?'On':'Off'};
  // As build_event: the last event type in a ring, how long ago, and the log.
  const eventAgo=stamp=>{const t=Date.parse(stamp);if(!isFinite(t)||now<1577836800)return '';const sec=Math.max(0,Math.floor(now-t/1000));
   return sec<45?'now':sec<3600?Math.max(1,Math.floor(sec/60))+' min ago':sec<86400?Math.floor(sec/3600)+' h ago':sec<172800?'yesterday':Math.floor(sec/86400)+' days ago'};
  const buildEvent=(l,e,d,title)=>{
   const kind=d.sensor_class||'',never=!d.state||d.state==='unknown';
   header(l.head,title,kind==='doorbell'?'Doorbell':kind==='motion'?'Motion events':kind==='button'?'Remote button':'Events');
   const size=Math.min(P(170),l.vis.w,l.vis.h-P(40)),cx=l.vis.x+l.vis.w/2,cy=l.vis.y+(l.vis.h-P(30))/2;
   add(node('circle',{cx,cy,r:size/2-P(1.5),fill:never?CARD:TINT,stroke:never?CONTROL:ACCENT,'stroke-width':P(3)}));
   text(never?'No events':label(d.event_type||'event'),F.body,never?TEXT:ACCENT,cx-size/2+P(8),cy-F.body.lh/2,size-P(16),'center');
   if(!never)text(eventAgo(d.state),F.small,MUTED,l.vis.x,cy+size/2+P(10),l.vis.w,'center');
   const c=Array.isArray(d.changes)?d.changes:[];
   if(!c.length){text('New events appear here while the panel is running',F.small,MUTED,l.ctl.x,l.ctl.y,l.ctl.w);return}
   const rh=P(36),gap=P(6);let y=l.ctl.y;for(const item of c){if(y+rh>bottomOf(l.ctl)+P(4))break;row({x:l.ctl.x,y,w:l.ctl.w,h:rh},label(item.state),clock(item.at),'transparent');y+=rh+gap}
  };
  // As build_timers for one Home Assistant timer helper (portrait): name and
  // state, a countdown ring, then Stop / Pause / +1 min or Start.
  const countdown=s=>{if(!isFinite(s)||s<0)return '--:--';const t=Math.ceil(s),two=n=>String(n).padStart(2,'0');return t>=3600?Math.floor(t/3600)+':'+two(Math.floor(t/60)%60)+':'+two(t%60):two(Math.floor(t/60))+':'+two(t%60)};
  const seconds=v=>{const p=String(v||'').split(':').map(Number);return p.length&&p.every(isFinite)?p.reduce((a,b)=>a*60+b,0):NaN};
  const buildTimer=(e,d,title,v)=>{
   const t=v.timer||{},state=t.state||d.state||'idle',total=seconds(t.duration);
   const left=state==='paused'?seconds(t.remaining):state==='active'?num(t.deadline_seconds_left):total;
   const ready=isFinite(left),ringing=ready&&state==='active'&&left<=0;
   let context=t.voice?'Voice timer':'Timer';if(!ready)context+=' · syncing';else if(ringing)context='Time is up';
   else if(state==='active'){const f=clock(now+Math.round(left));if(f)context+=' · finishes '+f}else context+=state==='paused'?' · paused':' · idle';
   const pad=P(16),col={x:pad,y:pad,w:W-2*pad,h:H-2*pad},nf=fit(title,col.w,[F.title,F.body]);
   text(title,nf,TEXT,col.x,col.y,col.w);text(context,F.small,ringing?ACCENT:MUTED,col.x,col.y+nf.lh,col.w);
   const y=col.y+nf.lh+F.small.lh+P(14),controls=[];
   if(ready&&(state==='active'||state==='paused'))controls.push(['stop','Stop',false],[state==='active'?'pause':'play',state==='active'?'Pause':'Resume',true],['plus','1 min',false]);
   else if(ready)controls.push(['play',ringing?'Restart':'Start',true]);
   const bh=P(64),gap=P(8),controlsY=bottomOf(col)-(controls.length?bh:0);
   const ring={x:pad,y,w:W-2*pad,h:(controls.length?controlsY-P(14):controlsY)-y},size=Math.max(P(100),Math.min(ring.w,ring.h,P(250)));
   const cx=ring.x+ring.w/2,ay=ring.y+(ring.h-size)/2,cy=ay+size/2,fr=ringing?1:total>0&&ready?Math.max(0,Math.min(1,left/total)):0;
   arc(cx,cy,size,P(14),0,359.9,CONTROL,false);if(fr>0)arc(cx,cy,size,P(14),270,270+359.9*fr,state==='paused'?SOFT:ACCENT);
   const value=!ready?'--:--':ringing?'0:00':countdown(left),vf=fit(value,size-2*P(14)-P(16),[F.big,F.title]);
   const sub=ringing?'Time is up':total>0?'of '+countdown(total):'',block=vf.lh+(sub?F.small.lh:0),vy=ay+(size-block)/2;
   text(value,vf,TEXT,cx-size/2,vy,size,'center');if(sub)text(sub,F.small,ringing?ACCENT:MUTED,cx-size/2,vy+vf.lh-P(6),size,'center');
   const n=controls.length,bw=n?Math.trunc((col.w-gap*(n-1))/n):0;
   controls.forEach(([sym,caption,primary],i)=>{const x=col.x+i*(bw+gap),w=i===n-1?col.w-(n-1)*(bw+gap):bw,fg=primary?'#101418':TEXT;
    box(x,controlsY,w,bh,primary?ACCENT:CONTROL,P(14));const total=Math.trunc(F.symbol.lh*3/4)+P(2)+F.small.lh,top=controlsY+(bh-total)/2;
    symbol(sym,x+w/2,top-P(4)+F.symbol.lh/2,fg);text(caption,F.small,fg,x,top+Math.trunc(F.symbol.lh*3/4)+P(2),w,'center')});
  };
  const buildBinary=(l,e,d,title)=>{
   const kind=d.sensor_class||'',on=d.state==='on';header(l.head,title,kind?label(kind)+' sensor':'Binary sensor');
   const size=Math.min(P(170),l.vis.w,l.vis.h-P(40)),cx=l.vis.x+l.vis.w/2,cy=l.vis.y+(l.vis.h-P(30))/2;
   add(node('circle',{cx,cy,r:size/2-P(1.5),fill:on?TINT:CARD,stroke:on?ACCENT:CONTROL,'stroke-width':P(3)}));text(binaryText(kind,d.state),F.body,on?ACCENT:TEXT,cx-size/2+P(8),cy-F.body.lh/2,size-P(16),'center');
   const c=d.changes||[];if(c.length)text(ago(c[0].at,now),F.small,MUTED,l.vis.x,cy+size/2+P(10),l.vis.w,'center');
   if(!c.length){text('Changes appear here while the panel is running',F.small,MUTED,l.ctl.x,l.ctl.y,l.ctl.w);return}
   const rh=P(36),gap=P(6);let y=l.ctl.y;for(const item of c){if(y+rh>bottomOf(l.ctl)+P(4))break;row({x:l.ctl.x,y,w:l.ctl.w,h:rh},binaryText(kind,item.state),clock(item.at),'transparent');y+=rh+gap}
  };
  const buildSensor=(l,e,d,title,v)=>{
   const kind=d.sensor_class||'';let context=kind?label(kind):'Sensor';if(d.power!==undefined||v?.lines?.[4]?.text&&v.lines[4].text!==title&&v.layout!=='graph')context=context;
   const nl=linesFor(title,F.title,l.head.w,2);text(title,F.title,TEXT,l.head.x,l.head.y,l.head.w,'left',nl);let y=l.head.y+nl*F.title.lh+P(2);
   text(context,F.small,MUTED,l.head.x,y,l.head.w);y+=F.small.lh+P(10);
   const value=num(d.state),numeric=isFinite(value),area={x:l.vis.x,y:wide?l.vis.y:y,w:l.vis.w,h:wide?l.vis.h:bottomOf(l.ctl)-y};y=area.y;
   if(numeric)y+=reading(compact(d.state),d.unit,area.x,y,area.w);else{const n=linesFor(v?.lines?.[1]?.text||d.state,F.big,area.w,3);text(v?.lines?.[1]?.text||d.state,F.big,TEXT,area.x,y,area.w,'left',n);y+=n*F.big.lh}
   const graph=v?.layout==='graph',hst=history(v);
   if(hst.points.length<2&&graph){text('Collecting history…',F.small,MUTED,area.x,y,area.w);y+=F.small.lh}
   if(!graph&&numeric&&d.unit==='%'&&value>=0&&value<=100){y+=P(14);level(area.x,y,area.w,P(14),value/100);y+=P(14)+P(6);text('0',F.small,MUTED,area.x,y);text('100%',F.small,MUTED,area.x,y,area.w,'right');y+=F.small.lh}
   y+=P(10);
   if(graph){const r=hst.range;chips({x:area.x,y,w:area.w,h:P(36)},['1 h','12 h','24 h','7 d'],r===3600?0:r===86400?2:r===604800?3:1);y+=P(46)}
   const stats=hst.points.length>=2,th=P(52),bottom=wide?{x:l.ctl.x,y:bottomOf(l.ctl)-th,w:l.ctl.w,h:th}:{x:area.x,y:bottomOf(area)-th,w:area.w,h:th};
   if(graph){const g=wide?{x:l.ctl.x,y:l.ctl.y,w:l.ctl.w,h:(stats?bottom.y-P(12):bottomOf(l.ctl))-l.ctl.y}:{x:area.x,y,w:area.w,h:(stats?bottom.y-P(8):bottomOf(area))-y};
    if(g.h>=P(80)){box(g.x,g.y,g.w,g.h,CARD,P(12),{'data-sensor-graph':''});sensorGraph(hst.points,hst.now,hst.range,g.x+P(12),g.y+P(12),g.w-P(24),g.h-P(40));
     text(rangeLabel(hst.range),F.small,MUTED,g.x+P(10),g.y+g.h-F.small.lh-P(6));text('now',F.small,MUTED,g.x+P(10),g.y+g.h-F.small.lh-P(6),g.w-P(20),'right')}}
   if(stats){const vals=hst.points.map(p=>p.value),gap=P(10),tw=Math.trunc((bottom.w-2*gap)/3);tile({x:bottom.x,y:bottom.y,w:tw,h:th},'Min',fmt(Math.min(...vals),1));tile({x:bottom.x+tw+gap,y:bottom.y,w:tw,h:th},'Avg',fmt(vals.reduce((a,b)=>a+b,0)/vals.length,1));tile({x:bottom.x+2*(tw+gap),y:bottom.y,w:bottom.w-2*(tw+gap),h:th},'Max',fmt(Math.max(...vals),1))}
  };
  const buildNumber=(l,e,d,title)=>{
   const value=num(d.value??d.state),lo=isFinite(d.minimum)?d.minimum:0,hi=isFinite(d.maximum)&&d.maximum>lo?d.maximum:100,step=d.step>0?d.step:1;
   header(l.head,title,fmt(lo)+' – '+fmt(hi)+' · step '+fmt(step));text(fmt(value),F.big,TEXT,l.vis.x,l.vis.y+Math.trunc(l.vis.h/5),l.vis.w,'center');
   if(d.unit)text(d.unit,F.body,SOFT,l.vis.x,l.vis.y+Math.trunc(l.vis.h/5)+F.big.lh,l.vis.w,'center');
   const sy=l.vis.y+Math.trunc(l.vis.h*64/100),hit=P(48),kw=P(28),tw=l.vis.w-kw,frac=isFinite(value)?Math.max(0,Math.min(1,(value-lo)/(hi-lo))):0,ty=sy-hit/2+(hit-P(8))/2;
   box(l.vis.x+kw/2,ty,tw,P(8),TRACK,P(4));box(l.vis.x+kw/2,ty,tw*frac,P(8),ACCENT,P(4));add(node('circle',{cx:l.vis.x+tw*frac+kw/2,cy:sy,r:kw/2,fill:TEXT}));
   text(fmt(lo),F.small,MUTED,l.vis.x,sy+hit/2);text(fmt(hi),F.small,MUTED,l.vis.x,sy+hit/2,l.vis.w,'right');
   const bs=P(56),cy=l.ctl.y+bs/2;circle(l.ctl.x+bs/2,cy,bs,CONTROL,'minus',TEXT);circle(l.ctl.x+l.ctl.w-bs/2,cy,bs,CONTROL,'plus',TEXT);
   if(l.ctl.h>=bs+P(28))text('Swipe up / down also steps by '+fmt(step),F.small,MUTED,l.ctl.x,l.ctl.y+bs+P(8),l.ctl.w,'center');
  };
  const buildSelect=(l,e,d,title)=>{
   const opts=options(d.options),current=d.value??d.state;header(l.head,title,opts.length+' options');
   const r=wide?l.ctl:{x:l.vis.x,y:l.vis.y,w:l.vis.w,h:H-P(16)-l.vis.y},bh=P(52),gap=P(8),sel=opts.indexOf(current),top=sel>=0&&sel*(bh+gap)+bh>r.h?sel*(bh+gap)+bh-r.h:0;
   const id='list'+Math.random().toString(36).slice(2),clip=node('clipPath',{id});clip.append(node('rect',{x:r.x,y:r.y,width:r.w,height:r.h}));add(clip);
   const g=add(node('g',{'clip-path':`url(#${id})`}));const prev=target;target=g;
   opts.forEach((o,i)=>{const y=r.y+i*(bh+gap)-top,on=o===current;box(r.x,y,r.w,bh,on?TINT:CARD,P(12),on?{stroke:ACCENT,'stroke-width':P(2)}:{});if(on)symbol('ok',r.x+r.w-P(14)-P(13),y+bh/2,ACCENT);text(o,F.body,on?ACCENT:TEXT,r.x+P(16),y+(bh-F.body.lh)/2,r.w-P(64))});target=prev;
  };
  const buildWeather=(e,d,title)=>{
   const pad=P(16),left=wide?{x:pad,y:pad,w:Math.trunc(W*46/100)-pad,h:H-2*pad}:{x:pad,y:pad,w:W-2*pad,h:H-2*pad},is=P(46),fc=d.forecast||[];
   text(title,F.body,SOFT,left.x,left.y,left.w-is-P(8));let y=left.y+F.body.lh;
   const temp=readingOf(d.temperature,'°'),feels='Feels '+readingOf(d.feels_like,'°'),range=fc.length?'H '+readingOf(fc[0].high,'°')+'  L '+readingOf(fc[0].low,'°'):'';
   const hero=!wide&&heroText(temp),tf=hero?F.hero:F.big,tw=Math.min(width(temp,tf),Math.trunc(left.w*55/100));text(temp,tf,TEXT,left.x-(hero?P(3):0),y,tw+P(4));
   const sx=left.x+tw+P(14),sw=left.x+left.w-sx,n=range?3:2;let sy=y+Math.trunc((tf.lh-F.body.lh-(n-1)*F.small.lh)/2);
   {const top=Math.max(P(4),left.y-P(8)),size=Math.max(P(24),Math.min(is,sy-P(4)-top));icon(e,left.x+left.w-size/2,top+size/2,size,TEXT)}
   text(conditionText(d.state),F.body,TEXT,sx,sy,sw);sy+=F.body.lh;text(feels,F.small,MUTED,sx,sy,sw);if(range)text(range,F.small,MUTED,sx,sy+F.small.lh,sw);
   y+=tf.lh+P(6);const stats=[['Humidity',readingOf(d.humidity,'%')],['Wind',readingOf(d.wind,d.wind_unit?' '+d.wind_unit:'')],['Rain',fc.length?readingOf(fc[0].rain,'%'):'—']],gap=P(8),th=P(64);
   if(wide){const rh=P(40);for(const s of stats){if(y+rh>bottomOf(left))break;row({x:left.x,y,w:left.w,h:rh},s[0],s[1]);y+=rh+P(6)}}
   else{const w3=Math.trunc((left.w-2*gap)/3);for(let i=0;i<3;i++)tile({x:left.x+i*(w3+gap),y,w:i===2?left.w-2*(w3+gap):w3,h:th},stats[i][0],stats[i][1]);y+=th+P(12)}
   const list=wide?{x:Math.trunc(W*46/100)+P(12),y:pad,w:W-Math.trunc(W*46/100)-P(12)-pad,h:H-2*pad}:{x:pad,y,w:W-2*pad,h:H-pad-y};
   box(list.x,list.y,list.w,list.h,CARD,P(14),{'data-weather-forecast':''});
   if(!fc.length){text(d.forecast_pending?'Loading forecast…':'Forecast not available',F.small,MUTED,list.x+P(14),list.y+P(14),list.w-P(28));return}
   const lows=fc.map(f=>num(f.low)).filter(isFinite),highs=fc.map(f=>num(f.high)).filter(isFinite),lo=Math.min(...lows),hi=Math.max(...highs);
   const nowDate=new Date(),today=nowDate.getFullYear()+'-'+String(nowDate.getMonth()+1).padStart(2,'0')+'-'+String(nowDate.getDate()).padStart(2,'0'),day=iso=>String(iso).slice(0,10)===today?'Today':new Date(String(iso).slice(0,10)+'T12:00:00').toLocaleDateString(undefined,{weekday:'short'});
   const inner=list.h-P(8),cnt=Math.max(1,Math.min(fc.length,5,Math.trunc(inner/(F.small.lh+P(12))))),rh=Math.trunc(inner/cnt),ip=P(14);
   let dw=0,nw=0,cw=0;for(const f of fc.slice(0,cnt)){dw=Math.max(dw,width(day(f.date),F.small));nw=Math.max(nw,width(readingOf(f.low,'°'),F.small),width(readingOf(f.high,'°'),F.small));cw=Math.max(cw,width(conditionText(f.condition),F.small))}
   dw+=P(10);nw+=P(2);const spare=list.w-2*ip-dw-2*nw-2*P(8)-P(44);cw=wide?0:Math.max(0,Math.min(Math.max(0,spare),cw+P(8)));if(cw<P(48))cw=0;
   const bx=ip+dw+cw+nw+P(8),bw=list.w-ip-nw-P(8)-bx,bar=P(6);
   fc.slice(0,cnt).forEach((f,i)=>{const top=list.y+P(4)+i*rh,ry=top+(rh-F.small.lh)/2;if(i>0)box(list.x+ip,top,list.w-2*ip,1,CONTROL,0);
    text(day(f.date),F.small,TEXT,list.x+ip,ry,dw);if(cw)text(conditionText(f.condition),F.small,MUTED,list.x+ip+dw,ry,cw-P(6));
    text(readingOf(f.low,'°'),F.small,MUTED,list.x+ip+dw+cw,ry,nw,'right');text(readingOf(f.high,'°'),F.small,TEXT,list.x+list.w-ip-nw,ry,nw,'right');
    const by=top+rh/2-bar/2;box(list.x+bx,by,bw,bar,CONTROL,bar/2);const a=num(f.low),b=num(f.high);if(isFinite(a)&&isFinite(b)&&hi>lo){const x0=Math.trunc(bw*(a-lo)/(hi-lo)),x1=Math.trunc(bw*(b-lo)/(hi-lo));box(list.x+bx+x0,by,Math.max(bar,x1-x0),bar,ACCENT,bar/2)}});
  };
  const buildMedia=(l,e,d,title,v,quick)=>{
   const pad=P(16),sideArt=wide?Math.min(H-2*pad,Math.trunc(W*42/100)):0,x=wide?pad+sideArt+P(16):pad,w=wide?W-x-pad:W-2*pad,reserve=quick?P(44)+P(8):0;
   const playing=v?.playing||d.state==='playing',playback=['playing','paused','buffering','on'].includes(d.state)||v?.playing;let bottom=bottomOf(l.ctl);const tight=Math.min(W,H)<320&&!wide;  // As build_media: tighter controls on the 2.8".
   if(quick&&!wide)bottom=H-pad;
   if(d.features&4){const vh=P(48),vy=bottom-vh,p=Math.max(0,Math.min(100,Math.round(num(d.volume)*100)||0));
    // As build_media: whenever volume is supported.
    box(x,vy,w,vh,CARD,P(14),{'data-media-volume':''});const iw=Math.round(F.symbol.size*1.07);symbol('volume',x+P(12)+iw/2,vy+vh/2,SOFT);  // As build_media: glyph, then a gap.
    const nw=width('100',F.body)+P(4),sx=x+P(12)+iw+P(14),sw=w-(sx-x)-nw-P(24),th=P(6),ks=P(22);text(String(p),F.body,TEXT,x+w-P(12)-nw,vy+(vh-F.body.lh)/2,nw,'right');
    box(sx,vy+(vh-th)/2,sw,th,TRACK,th/2);box(sx,vy+(vh-th)/2,Math.max(th,sw*p/100),th,ACCENT,th/2);add(node('circle',{cx:sx+Math.max(ks/2,Math.min(sw-ks/2,sw*p/100)),cy:vy+vh/2,r:ks/2,fill:TEXT}));bottom=vy-(tight?P(8):P(14))}
   const bh=P(64),by=bottom-bh;transport(x+w/2,by+bh/2,P(52),bh,P(28),playing);
   const bar=P(6),progH=bar+(tight?P(3):P(6))+F.small.lh,progY=by-(tight?-P(2):P(16))-progH;progressBar(v,x,progY,w,false,tight?P(3):undefined);
   // The media view's context/hint lines carry the title and artist too (onx_detail_view).
   const ln=v?.lines||[],name=playback&&(d.title||ln[2]?.text)?(d.title||ln[2].text):playback?label(d.state):'Nothing playing',sub=playback?d.artist||ln[3]?.text||'':'',tf=fit(name,w,[F.title,F.body]);
   const subY=progY-(tight?P(6):P(12))-F.body.lh,titleY=subY-tf.lh;text(name,tf,TEXT,x,titleY,w,'center');if(sub)text(sub,F.body,MUTED,x,subY,w,'center');
   const room=titleY-(tight?P(8):P(12))-pad,ly=playback&&Array.isArray(d.lyrics)?d.lyrics:null;
   // As build_media: synced lyrics take the artwork's column; the current line
   // in full, the previous and next lines in the rows left.
   if(ly){const ry=wide?pad:pad+reserve,rh=wide?H-2*pad:room-reserve,rx=wide?pad:x,rw=wide?sideArt:w,lp=P(12),lw=rw-2*lp,lh=rh-2*lp;
    box(rx,ry,rw,rh,CARD,P(14),{'data-media-lyrics':''});
    // As build_media: the cover, out of focus and darkened, behind the lyrics.
    try{const u=new URL(v?.artwork);if(['http:','https:'].includes(u.protocol)){const id='lyr-'+Math.random().toString(36).slice(2),clip=node('clipPath',{id});clip.append(node('rect',{x:rx,y:ry,width:rw,height:rh,rx:P(14)}));add(clip);
     const g=add(node('g',{'clip-path':`url(#${id})`}));g.append(node('image',{href:u.href,x:rx-P(20),y:ry-P(20),width:rw+P(40),height:rh+P(40),preserveAspectRatio:'none',style:`filter:blur(${P(18)}px)`,'data-lyrics-backdrop':''}));
     add(node('rect',{x:rx,y:ry,width:rw,height:rh,rx:P(14),fill:BG,'fill-opacity':0.5}))}}catch{}
    const gap=P(8),sl=F.small.lh,rows=(s,f,n)=>s&&n>0?wrap(s,f,lw,n).length:0,cur=ly[1]||'';let cf=[F.title,F.body].find(f=>{const n=rows(cur,f,24);return n<=2&&n*f.lh<=lh-2*(gap+sl)})||F.small;if(rows(cur,cf,24)*cf.lh>lh-2*(gap+sl)&&rows(cur,F.body,24)*F.body.lh<=lh)cf=F.body;
    const ch=Math.min(lh,rows(cur,cf,24)*cf.lh),cy=Math.trunc((lh-ch)/2),top=ry+lp,space=Math.trunc((lh-ch)/2)-gap,n=Math.trunc(space/sl);text(cur,cf,TEXT,rx+lp,top+cy,lw,'center',24);
    const ph=rows(ly[0],F.small,n)*sl;if(ph)text(ly[0],F.small,MUTED,rx+lp,top+cy-gap-ph,lw,'center',n);
    if(rows(ly[2],F.small,n))text(ly[2],F.small,SOFT,rx+lp,top+cy+ch+gap,lw,'center',n);return}
   const art=wide?sideArt:Math.max(P(48),Math.min(P(220),w-2*reserve,room)),ax=wide?pad:(W-art)/2,ay=wide?(H-art)/2:pad+Math.max(0,(room-art)/2);
   if(!artwork(v?.artwork,ax,ay,art,P(14)))icon(e,ax+art/2,ay+art/2,Math.trunc(art*40/100),playing?ACCENT:SOFT);
  };
  const renderDetail=()=>{
   const e=slot.entity,v=view,d={...(v?.panel||{})},title=slot.name||v?.lines?.[4]?.text||e,l=layout(),quick=(d.actions||[]).length;
   if(v?.playing!==undefined&&e.startsWith('media_player.')&&!d.state)d.state=v.playing?'playing':'idle';
   if(!v){header(l.head,title,'Waiting for initial data');text('…',F.big,MUTED,l.vis.x,l.vis.y+l.vis.h/3,l.vis.w,'center');return}
   if(d.received===false||d.available===false){const wait=d.received===false;header(l.head,title,wait?'Waiting for initial data':'Unavailable');text(wait?'…':'—',F.big,MUTED,l.vis.x,l.vis.y+l.vis.h/3,l.vis.w,'center');return}
   if(v.timer||e.startsWith('timer.')){buildTimer(e,d,title,v);return}
   if(quick){const bs=P(44);circle(W-P(16)-bs/2,P(16)+bs/2,bs,CONTROL,'list',TEXT,{'data-quick-actions':''});l.head.w-=bs+P(8)}
   if(e.startsWith('light.'))buildLight(l,e,d,title);
   else if(e.startsWith('climate.'))buildClimate(l,e,d,title);
   else if(e.startsWith('fan.'))buildFan(l,e,d,title);
   else if(e.startsWith('cover.'))buildCover(l,e,d,title);
   else if(/^(automation|scene|script|button|input_button)\./.test(e))buildAction(l,e,d,title);
   else if(e.startsWith('binary_sensor.'))buildBinary(l,e,d,title);
   else if(e.startsWith('event.'))buildEvent(l,e,d,title);
   else if(e.startsWith('sensor.'))buildSensor(l,e,d,title,v);
   else if(/^(number|input_number)\./.test(e))buildNumber(l,e,d,title);
   else if(/^(select|input_select)\./.test(e))buildSelect(l,e,d,title);
   else if(e.startsWith('weather.'))buildWeather(e,d,title);
   else if(e.startsWith('media_player.'))buildMedia(l,e,d,title,v,quick);
   else buildToggle(l,e,d,title,v);
  };

  // ------------------------------------------------------------------ cards
  const titled=(t,x,y,w,ctxColor=MUTED)=>{const f=fit(t.name,w,[F.body,F.small]);text(t.name,f,TEXT,x,y,w);let b=y+f.lh;if(t.context){text(t.context,F.small,ctxColor,x,b,w);b+=F.small.lh}return b};
  const chipIcon=(e,x,y,s,on)=>{add(node('circle',{cx:x+s/2,cy:y+s/2,r:s/2,fill:on?TINT:INSET}));icon(e,x+s/2,y+s/2,Math.trunc(s*56/100),on?ACCENT:SOFT)};
  const toggle=(x,y,on)=>{const w=P(54),h=P(30),k=h-P(8);box(x,y,w,h,on?ACCENT:TRACK,h/2,{'data-panel-control':'toggle'});box(on?x+w-k-P(4):x+P(4),y+P(4),k,k,on?'#101418':SOFT,k/2)};
  const cardWeather=(t,d)=>{
   // As card_weather: the 2.8" (compact) trades the hero reading for more readings and the forecast.
   const compact=Math.min(W,H)<320,r=t.r,pad=compact?P(14):P(16),is=compact?Math.min(P(64),Math.trunc(r.h/4)):Math.min(P(84),Math.trunc(r.h/3));text(t.name,F.small,SOFT,r.x+pad,r.y+pad,r.w-2*pad-is);let y=r.y+pad+F.small.lh;
   const temp=present(d.temperature,'°')||'—',hero=!compact&&r.h>=P(240)&&heroText(temp),tf=hero?F.hero:F.big;text(temp,tf,TEXT,r.x+pad-(hero?P(3):0),y-(hero?P(6):0),r.w-2*pad-is);
   y+=tf.lh-(hero?P(14):P(2));text(conditionText(d.state),F.body,TEXT,r.x+pad,y,r.w-2*pad-is);y+=F.body.lh+(compact?P(10):P(14));icon(t.entity,r.x+r.w-pad-is/2,r.y+pad+is/2+P(8),is,TEXT);
   const stats=[['Humidity',present(d.humidity,'%')],['Wind',present(d.wind,d.wind_unit)],['Pressure',present(d.pressure,d.pressure_unit)],['Visibility',present(d.visibility,d.visibility_unit)],['UV index',present(d.uv,'')],['Feels like',present(d.feels_like,'°')]].filter(s=>s[1]);
   const gap=compact?P(6):P(8),th=compact?P(50):P(56),tw=Math.trunc((r.w-2*pad-gap)/2),rows=Math.min(Math.ceil(stats.length/2),Math.trunc((r.y+r.h-pad-y+gap)/(th+gap)));
   for(let i=0;i<Math.min(stats.length,rows*2);i++)tile({x:r.x+pad+(i%2)*(tw+gap),y:y+Math.trunc(i/2)*(th+gap),w:tw,h:th},stats[i][0],stats[i][1],INSET);
   const fc=Array.isArray(d.forecast)?d.forecast:[];
   if(compact&&fc.length){
    const n=new Date(),today=n.getFullYear()+'-'+String(n.getMonth()+1).padStart(2,'0')+'-'+String(n.getDate()).padStart(2,'0');
    const day=iso=>String(iso).slice(0,10)===today?'Today':new Date(String(iso).slice(0,10)+'T12:00:00').toLocaleDateString('en-US',{weekday:'short'});
    const deg=v=>present(v,'°')||'—',rh=F.small.lh+P(4),dw=Math.trunc(r.w*22/100),vw=Math.trunc(r.w*30/100);
    let fy=y+rows*(th+gap)+(rows?P(2):0);
    for(const f of fc){if(fy+rh>r.y+r.h-pad+P(4))break;text(day(f.date),F.small,TEXT,r.x+pad,fy,dw);text(conditionText(f.condition),F.small,MUTED,r.x+pad+dw,fy,r.w-2*pad-dw-vw);
     text(deg(f.low)+' / '+deg(f.high),F.small,TEXT,r.x+r.w-pad-vw,fy,vw,'right');fy+=rh}
   }
   if(rows<=0&&stats.length&&r.y+r.h-pad-y+P(14)>=F.small.lh)text(stats.slice(0,2).map(s=>s[0]+' '+s[1]).join('  ·  '),F.small,MUTED,r.x+pad,y-P(8),r.w-2*pad);
  };
  const cardLight=(t,d)=>{
   const r=t.r,on=!!d.light_on&&d.available!==false,dimmable=d.dimmable!==false,lvl=on?Number(d.brightness??0):0;
   const head={...t,context:d.available===false?'Unavailable':on?'On':'Off'};if(on&&d.kelvin>0&&d.supports_ct)head.context+=' · '+d.kelvin+' K';
   const pad=P(14),cs=P(44),tx=pad+cs+P(12);chipIcon(t.entity,r.x+pad,r.y+pad,cs,on);titled(head,r.x+tx,r.y+pad+P(2),r.w-tx-pad-P(62));
   if(d.available!==false)toggle(r.x+r.w-pad-P(54),r.y+pad+P(7),on);
   const sh=P(40),sy=r.h-pad-sh,vy=pad+cs+P(8);
   if(!dimmable){if(sy-vy>=F.big.lh)text(on?'On':'Off',F.big,TEXT,r.x+pad,r.y+vy,r.w-2*pad);return}
   if(sy-vy>=F.big.lh)text(pct(lvl),F.big,TEXT,r.x+pad,r.y+vy,r.w-2*pad);
   const sw=r.w-2*pad,id='lc'+Math.random().toString(36).slice(2),clip=node('clipPath',{id});clip.append(node('rect',{x:r.x+pad,y:r.y+sy,width:sw,height:sh,rx:P(12)}));add(clip);
   box(r.x+pad,r.y+sy,sw,sh,INSET,P(12),{'data-brightness-slider':''});const fw=Math.trunc(sw*Math.max(0,Math.min(100,lvl))/100);box(r.x+pad,r.y+sy,fw,sh,ACCENT,0,{'clip-path':`url(#${id})`});
   box(r.x+pad+Math.max(P(4),Math.min(sw-P(8),fw-P(9))),r.y+sy+P(9),P(4),sh-P(18),'#101418',P(2));
  };
  const cardClimate=(t,d)=>{
   const r=t.r,mode=d.hvac||d.state,off=mode==='off',tgt=num(d.temperature),range=!isFinite(tgt)&&isFinite(num(d.low))&&isFinite(num(d.high));
   const head={...t,context:hvacLabel(mode)+(isFinite(num(d.current))?' · now '+fmt(num(d.current))+'°':'')},pad=P(14),cs=P(44),tx=pad+cs+P(12);
   chipIcon(t.entity,r.x+pad,r.y+pad,cs,!off);titled(head,r.x+tx,r.y+pad+P(2),r.w-tx-pad,off?MUTED:ACCENT);
   const value=range?fmt(num(d.target_low))+'–'+fmt(num(d.target_high))+'°':isFinite(tgt)?fmt(tgt)+'°':'—',bs=P(52),by=r.h-pad-bs;if(by<pad+cs)return;
   const big=r.h>=P(260)&&heroText(value),vf=big?F.hero:F.big,cy=big?Math.trunc((pad+cs+by+bs)/2):by+bs/2;
   const labelY=cy-vf.lh/2-F.small.lh+P(6);if(labelY>=pad+cs)text('Target',F.small,MUTED,r.x,r.y+labelY,r.w,'center');  // As card_climate: no overlap with the header.
   text(value,vf,TEXT,r.x,r.y+cy-vf.lh/2+P(4),r.w,'center');
   if(d.available===false||!(d.features&1))return;circle(r.x+pad+bs/2,r.y+by+bs/2,bs,INSET,'minus',TEXT);circle(r.x+r.w-pad-bs/2,r.y+by+bs/2,bs,INSET,'plus',TEXT);
  };
  const cardMedia=(t,d,v)=>{
   const r=t.r,pad=P(14),bh=P(48),playing=v?.playing||d.state==='playing',by=r.h-pad-bh,bar=P(6),progH=bar+P(4)+F.small.lh,progY=by-P(10)-progH,metaH=F.small.lh+F.title.lh+F.small.lh;
   const tall=progY-pad-metaH-P(8)>=P(140),art=tall?Math.min(r.w-2*pad,progY-pad-metaH-P(18)):Math.max(P(48),Math.min(P(104),progY-pad-P(10),Math.trunc(r.w*36/100))),ax=tall?(r.w-art)/2:pad;
   if(currentArt||!artwork(t.artwork?'':v?.artwork,r.x+ax,r.y+pad,art,P(10))){if(!currentArt)box(r.x+ax,r.y+pad,art,art,INSET,P(10));icon(t.entity,r.x+ax+art/2,r.y+pad+art/2,Math.min(Math.trunc(art*46/100),P(96)),playing?ACCENT:SOFT)}
   const tx=tall?pad:pad+art+P(12),tw=r.w-tx-pad,track=d.title&&!['off','idle'].includes(d.state);let y=tall?pad+art+P(8):pad+P(2);
   text(t.name,F.small,MUTED,r.x+tx,r.y+y,tw);y+=F.small.lh;const name=track?d.title:label(d.state||'idle'),f=fit(name,tw,[F.title,F.body]);text(name,f,TEXT,r.x+tx,r.y+y,tw);y+=f.lh;
   if(track&&d.artist)text(d.artist,F.small,SOFT,r.x+tx,r.y+y,tw);
   if(!tall&&t.ring>=0&&pad+art-P(4)>y+F.small.lh)level(r.x+tx,r.y+pad+art-P(4),tw,P(4),t.ring/1000);
   if(by<pad+art||d.available===false)return;
   if(progY>=(tall?y:pad+art)&&v?.progress>=0)progressBar(v,r.x+pad,r.y+progY,r.w-2*pad,true);
   transport(r.x+r.w/2,r.y+by+bh/2,bh-P(8),bh,P(22),playing);
  };
  const cardGeneric=(t,d,v)=>{
   const r=t.r,e=t.entity,domain=e.split('.')[0],pad=P(14),cs=P(44),tx=pad+cs+P(12),available=d.available!==false&&!!v;
   const switchable=available&&['switch','input_boolean','automation','fan','siren'].includes(domain),runnable=available&&['scene','script','button','input_button'].includes(domain),pv=num(d.state),hst=history(v);
   if(available&&!switchable&&d.unit==='%'&&isFinite(pv)&&pv>=0&&pv<=100&&r.h<P(300)&&r.h>=P(110)){
    const tw=r.w-2*pad,nl=linesFor(t.name,F.title,tw,2);text(t.name,F.title,TEXT,r.x+pad,r.y+pad,tw,'left',nl);
    const top=pad+nl*F.title.lh+P(10),avail=r.h-pad-top,size=Math.min(avail,Math.trunc(r.w*34/100)),aw=Math.max(P(8),Math.trunc(size/10)),cx=r.x+pad+size/2,cy=r.y+top+avail/2;
    arc(cx,cy,size,aw,0,360,CONTROL,false);if(pv>0)arc(cx,cy,size,aw,270,270+Math.min(pv,99.99)*3.6,ACCENT);
    const val=fmt(pv,pv<10?1:0)+'%',vf=fit(val,size-2*aw-P(8),[F.big,F.title,F.body,F.small]);text(val,vf,TEXT,r.x+pad,cy-vf.lh/2,size,'center');
    const nx=pad+size+P(14),nw=r.w-nx-pad;let y=top;if(t.context){const cl=linesFor(t.context,F.body,nw,2);text(t.context,F.body,SOFT,r.x+nx,r.y+y,nw,'left',cl);y+=cl*F.body.lh+P(8)}
    const gh=r.h-pad-y;if(gh>=P(24))chart(hst.points,hst.now,hst.range,r.x+nx,r.y+y,nw,gh,ACCENT,false);return;
   }
   chipIcon(e,r.x+pad,r.y+pad,cs,t.on);titled(t,r.x+tx,r.y+pad+P(2),r.w-tx-pad-(switchable?P(62):0));
   if(switchable)toggle(r.x+r.w-pad-P(54),r.y+pad+P(7),d.state==='on');
   let bottom=r.h-pad;const top=pad+cs+P(10),strip=P(16)+2*F.small.lh+P(14);let room=bottom-top-F.body.lh-P(10);if(room-strip>=P(140))room-=strip;
   if(runnable&&room>=P(140)){const s=Math.min(P(150),room-P(16),r.w-2*pad),cy=top+P(8)+Math.trunc((room-P(16))/2);circle(r.x+r.w/2,r.y+cy,s,ACCENT,'play',TEXT,{stroke:CONTROL,'stroke-width':P(8)});
    const runs=(d.changes||[]),last=runs.length?runs[0].at:0,ty=cy+s/2+P(10);text(last?'Last run '+ago(last,now):'Run',F.body,TEXT,r.x+pad,r.y+ty,r.w-2*pad,'center');
    const sh=P(16),sy=r.h-pad-sh-F.small.lh-P(4);if(sy>=ty+F.body.lh+P(8)+F.small.lh){text('Runs, last 24 h',F.small,MUTED,r.x+pad,r.y+sy-F.small.lh-P(2),r.w-2*pad);const sw=r.w-2*pad;box(r.x+pad,r.y+sy,sw,sh,INSET,P(4));
     for(const run of runs){if(run.at>now||now-run.at>=86400)continue;box(r.x+pad+Math.trunc((86400-(now-run.at))*(sw-P(4))/86400),r.y+sy,P(4),sh,ACCENT,P(2))}
     text('-24 h',F.small,MUTED,r.x+pad,r.y+sy+sh+P(4),sw/2);text('now',F.small,MUTED,r.x+pad+sw/2,r.y+sy+sh+P(4),sw/2,'right')}return}
   if(runnable&&bottom-P(48)>=pad+cs+P(8)){bottom-=P(48);button(r.x+pad,r.y+bottom,r.w-2*pad,P(48),P(24),INSET,'Run',F.body,TEXT);bottom-=P(10)}
   const bar=t.ring>=0&&!runnable;if(bar)bottom-=P(12)+P(10);const vy=pad+cs+P(10);
   if(bottom-vy<F.big.lh){if(bar&&bottom+P(10)>vy)level(r.x+pad,r.y+bottom+P(10),r.w-2*pad,P(12),t.ring/1000);return}
   const numeric=isFinite(num(d.state))&&['sensor','number','input_number'].includes(domain),value=numeric?compact(d.state):t.value,unit=numeric?d.unit:'';
   if(domain==='switch'&&available&&d.power!==undefined){
    const state=d.state==='on'?'On':'Off',caption=F.small.lh+P(4),sf=r.h-pad-vy-F.big.lh-P(10)-caption>=P(48)?F.big:F.title,label=r.h-pad-vy-sf.lh-P(8)-caption>=P(40);
    text(state,sf,TEXT,r.x+pad,r.y+vy,width(state,sf)+1);const sx=pad+width(state,sf)+P(10);
    text(d.power+' '+(d.power_unit||'')+(d.power_unit==='W'?' now':''),F.body,SOFT,r.x+sx,r.y+vy+sf.lh-F.body.lh-(sf===F.big?P(4):0),r.w-sx-pad);
    const gy=vy+sf.lh+P(8),gb=r.h-pad-(label?caption:0);if(gb-gy>=P(20)&&chart(hst.points,hst.now,hst.range,r.x+pad,r.y+gy,r.w-2*pad,gb-gy,ACCENT,false,true)&&label)text((d.power_unit==='kWh'?'Energy':'Power')+' · '+rangeLabel(hst.range),F.small,MUTED,r.x+pad,r.y+gb+P(4),r.w-2*pad);return;
   }
   if(available&&!runnable){const big=bottom-vy>=F.hero.lh+F.small.lh+P(120)&&numeric&&heroSafe(value),vh=big?F.hero.lh:F.big.lh,sy=vy+vh,gy=sy+F.small.lh+P(8);
    if(hst.points.length>=2&&bottom-gy>=P(44)){const delta=hst.points.at(-1).value-hst.points[0].value,ch=chart(hst.points,hst.now,hst.range,r.x+pad,r.y+gy,r.w-2*pad,bottom-gy,ACCENT,true);
     if(ch){reading(value,unit,r.x+pad,r.y+vy,r.w-2*pad,big);text(rangeLabel(hst.range)+'  ·  min '+fmt(ch.lo,1)+'  ·  max '+fmt(ch.hi,1),F.small,MUTED,r.x+pad,r.y+sy,r.w-2*pad);
      if(Math.abs(delta)>=0.05)text((delta>0?'+':'-')+fmt(Math.abs(delta),1),F.small,ACCENT,r.x+pad,r.y+sy,r.w-2*pad,'right');if(bar)level(r.x+pad,r.y+bottom+P(10),r.w-2*pad,P(12),t.ring/1000);return}}}
   const hero=bottom-vy>=F.hero.lh&&numeric&&heroSafe(value),vh=hero?F.hero.lh:F.big.lh;reading(value,unit,r.x+pad,r.y+bottom-vh,r.w-2*pad,hero);
   if(bar)level(r.x+pad,r.y+bottom+P(10),r.w-2*pad,P(12),t.ring/1000);
  };
  const cardRow=(t,d,v)=>{
   const r=t.r,pad=P(14),is=P(24),binary=t.entity.startsWith('binary_sensor.');icon(t.entity,r.x+pad+is/2,r.y+r.h/2,is,ACCENT);
   const numeric=d.available!==false&&isFinite(num(d.state)),value=numeric?compact(d.state):t.value,unit=numeric?d.unit||'':'',uw=unit?width(unit,F.small)+P(4):0,vf=F.body,vw=Math.min(width(value,vf)+1,r.w/2),vx=r.w-pad-uw-vw,nx=pad+is+P(12),vy=(r.h-vf.lh)/2;
   let end=vx-P(8);const sw=Math.min(P(64),Math.trunc(r.w/5)),sh=Math.min(P(22),r.h-P(12)),hst=history(v);
   if(numeric&&end-nx-sw-P(8)>=P(64)){if(chart(hst.points,hst.now,hst.range,r.x+end-sw,r.y+(r.h-sh)/2,sw,sh,ACCENT,false))end-=sw+P(10)}
   else if(binary&&(d.changes||[]).length){const a=ago(d.changes[0].at,now),aw=width(a,F.small)+1;if(end-nx-aw-P(8)>=P(64)){text(a,F.small,MUTED,r.x+end-aw,r.y+(r.h-F.small.lh)/2,aw);end-=aw+P(10)}}
   text(t.name,F.small,SOFT,r.x+nx,r.y+(r.h-F.small.lh)/2,end-nx);text(value,vf,binary?ACCENT:TEXT,r.x+vx,r.y+vy,vw);if(uw)text(unit,F.small,MUTED,r.x+vx+vw+P(4),r.y+vy+vf.lh-F.small.lh-P(2),uw);
  };
  // As animate_icon on the panel: only while the entity is on.
  const animateKey=(t,p)=>{const fx=animationOf(p,t.entity),img=target.lastElementChild;if(t.on&&fx!=='none'&&img?.tagName==='image'){img.classList.add('fx-'+fx);img.dataset.animation=fx}};
  const cardKey=(t,d,i,p)=>{
   const r=t.r,ready=d.available!==false&&!!viewOf(t.entity);box(r.x,r.y,r.w,r.h,t.entity?(t.on?TINT:CONTROL):mix(CARD,BG,40),P(14),{'data-panel-item':i});if(!t.entity)return;
   const color=t.on?ACCENT:ready?TEXT:MUTED,only=iconOnly(p,i);
   if(only){if(currentArt){add(node('image',{href:currentArt,x:r.x,y:r.y,width:r.w,height:r.h,preserveAspectRatio:'xMidYMid slice',style:'clip-path:inset(0 round '+P(14)+'px)'}));if(t.on)box(r.x+1,r.y+1,r.w-2,r.h-2,'none',P(14),{stroke:ACCENT,'stroke-width':P(2)})}else{icon(t.entity,r.x+r.w/2,r.y+r.h/2,Math.trunc(Math.min(r.w,r.h)*86/100),color);animateKey(t,p)}return}
   const pad=P(4),nh=F.small.lh;icon(t.entity,r.x+r.w/2,r.y+(r.h-nh-pad)/2,Math.min(P(44),Math.trunc(Math.min(r.w,r.h)*40/100)),color);animateKey(t,p);text(t.name,F.small,color,r.x+pad,r.y+r.h-nh-pad,r.w-2*pad,'center');
  };
  const renderMain=()=>{
   const p=pageFor(slot.entity),ids=p.ids,rects=globalThis.ONX_PANEL_GEOMETRY(p.layout,W,H,false,p.rows,p.columns),grid=p.layout===gridLayout;
   const tiles=rects.map((r,i)=>{const e=ids[i]||'',v=viewOf(e),c=v?{...v,...v.carousel}:null,lines=c?.lines||[],d=v?.panel||{};
    const name=e===slot.entity?(slot.name||lines[4]?.text||e):(editorSlots.find(s=>s.entity===e)?.name||v?.lines?.[4]?.text||e);
    let context=lines[2]?.text||'';if(/^(sensor|binary_sensor)\./.test(e)&&lines[4]?.text&&lines[4].text!==name)context=lines[4].text;
    const st=d.state,on=d.available!==false&&(['on','playing','open','opening','active'].includes(st)||(e.startsWith('climate.')&&st&&st!=='off'));
    return {entity:e,name,value:lines[1]?.text||'',context,ring:c?.value_ring??-1,on,r,compact:p.layout==='control_sensors'&&i>0,key:grid,v,d,art:artUrl(positionArt(p,i))}});
   if(tiles.length){const cb=tiles[0].r.y;  // As refresh_carousel_clock: every main page, grids included.
   if(cb>=F.clock.lh)text(clock(now),F.clock,TEXT,P(16),(cb-F.clock.lh)/2,W/4)}
   // Status icons (Wi-Fi, Home Assistant) and the bell.
   const ui=Math.min(W,H)>=320?1.3333:1,si=Math.round(20*ui),hdr=tiles[0]?.r.y||Math.round(40*ui),sy=Math.max(Math.round(8*ui),hdr/2-Math.round(2*ui));const shape=(d,vw,vh,cx,cy,sc)=>add(node('path',{d,fill:TEXT,transform:'translate('+(cx-vw*sc/2)+' '+(cy-vh*sc/2)+') scale('+sc+')'}));shape('M8.09 12.44C7.71 12.07 7.52 11.62 7.52 11.08C7.52 10.55 7.71 10.10 8.09 9.73C8.47 9.36 8.94 9.17 9.5 9.17C10.05 9.17 10.52 9.36 10.90 9.73C11.28 10.10 11.47 10.55 11.47 11.08C11.47 11.62 11.28 12.07 10.90 12.44C10.52 12.81 10.05 13 9.5 13C8.94 13 8.47 12.81 8.09 12.44ZM5.02 8.67L3.36 7.03C4.14 6.28 5.05 5.68 6.10 5.24C7.15 4.80 8.28 4.58 9.5 4.58C10.71 4.58 11.84 4.81 12.89 5.25C13.94 5.70 14.85 6.30 15.63 7.07L13.97 8.67C13.39 8.11 12.71 7.67 11.95 7.36C11.18 7.04 10.37 6.88 9.5 6.88C8.62 6.88 7.81 7.04 7.04 7.36C6.28 7.67 5.60 8.11 5.02 8.67ZM1.66 5.42L0 3.82C1.21 2.62 2.63 1.68 4.25 1.01C5.87 0.33 7.62 0 9.5 0C11.37 0 13.12 0.33 14.74 1.01C16.36 1.68 17.78 2.62 19 3.82L17.33 5.42C16.32 4.44 15.14 3.68 13.80 3.12C12.46 2.57 11.03 2.29 9.5 2.29C7.96 2.29 6.53 2.57 5.19 3.12C3.85 3.68 2.67 4.44 1.66 5.42Z',19,13,W/2-Math.round(13*ui),sy,ui);shape('M14.33 6.74L8.16 0.57C7.98 0.39 7.74 0.29 7.50 0.29C7.25 0.29 7.01 0.39 6.83 0.57L0.66 6.74C0.29 7.11 0 7.83 0 8.34V13.97C0 14.48 0.42 14.90 0.93 14.90H6.70L4.16 12.37C4.03 12.41 3.89 12.44 3.75 12.44C3.04 12.44 2.46 11.86 2.46 11.15C2.46 10.45 3.04 9.87 3.75 9.87C4.45 9.87 5.03 10.45 5.03 11.15C5.03 11.30 5.00 11.44 4.96 11.57L6.93 13.55V6.30C6.72 6.20 6.54 6.04 6.41 5.83C6.28 5.63 6.21 5.39 6.21 5.15C6.21 4.45 6.79 3.87 7.5 3.87C8.20 3.87 8.78 4.45 8.78 5.15C8.78 5.39 8.71 5.63 8.58 5.83C8.45 6.04 8.27 6.20 8.06 6.30V11.38L10.02 9.42C9.96 9.23 9.95 9.02 9.98 8.82C10.01 8.62 10.09 8.44 10.21 8.27C10.33 8.11 10.49 7.98 10.67 7.89C10.84 7.80 11.04 7.75 11.25 7.75C11.95 7.75 12.53 8.32 12.53 9.03C12.53 9.74 11.95 10.31 11.25 10.31C11.09 10.31 10.94 10.28 10.80 10.23L8.06 12.98V14.91H14.06C14.57 14.91 15 14.48 15 13.97V8.34C15 7.83 14.70 7.11 14.33 6.74V6.74Z',15,15,W/2+Math.round(13*ui),sy,ui);
   glyph('mdi:bell-outline',W-4-22,sy,P(26),TEXT);
   tiles.forEach((t,i)=>{currentArt=t.art;
    if(t.key){cardKey(t,t.d,i,p);currentArt='';return}
    box(t.r.x,t.r.y,t.r.w,t.r.h,CARD,t.compact?P(14):P(18),{'data-panel-item':t.entity});
    if(!t.entity){currentArt='';return}
    const ready=!!t.v&&t.d.available!==false;
    if(t.compact)cardRow(t,t.d,t.v);
    else if(ready&&t.entity.startsWith('weather.'))cardWeather(t,t.d);
    else if(ready&&t.entity.startsWith('light.'))cardLight(t,t.d);
    else if(ready&&t.entity.startsWith('climate.'))cardClimate(t,t.d);
    else if(ready&&t.entity.startsWith('media_player.'))cardMedia({...t,artwork:t.art},t.d,t.v);
    else cardGeneric(t,t.d,t.v);
    currentArt='';});
   // The dock: the selected page on an accent-tinted marker. Strip constants
   // from boards/onx3248g035.yaml and onx2432g028.yaml (the same on both:
   // centre 22 px above the bottom, 32 px marker, 20 px icons at ui_scale).
   const cy=H-22,is=Math.round(20*ui),marker=32;
   const dock=view?.carousel?.navigator||[];
   dock.forEach((item,i)=>{
    const x=Math.floor(W*(2*i+1)/(2*dock.length)),selected=item.selected??item.offset===0;
    const page=pageConfigs.find(q=>lead(q)===item.entity)||(selected?pageConfigs[selectedPage]:null);
    if(selected)add(node('circle',{cx:x,cy,r:marker/2,fill:TINT,'data-nav-marker':''}));
    currentArt=artUrl(page?.carousel_artwork);glyph(page?pageIcon(page):item.icon,x,cy,is,selected?ACCENT:TEXT);currentArt='';
   });
  };
  if(detail)renderDetail();else renderMain();
  status(view?'Live rectangular panel view':'Save configured entities to load live readings.');
  if(svg.id==='device-preview')drawLiveCanvas();
 };
 // Colour helpers shared by the light preview (onx_light::kelvin / hsv).
 function kelvinRgb(k){const t=k/100;let r,g,b;if(t<=66){r=255;g=99.47*Math.log(t)-161.12;b=t<=19?0:138.52*Math.log(t-10)-305.04}else{r=329.7*Math.pow(t-60,-0.1332);g=288.12*Math.pow(t-60,-0.0755);b=255}const c=v=>Math.max(0,Math.min(255,Math.round(v))).toString(16).padStart(2,'0');return '#'+c(r)+c(g)+c(b)}
 function hsv(h,s){s=Math.max(0,Math.min(100,s))/100;const f=n=>{const k=(n+h/60)%6;return 1-s*Math.max(0,Math.min(k,4-k,1))};const c=v=>Math.round(v*255).toString(16).padStart(2,'0');return '#'+c(f(5))+c(f(3))+c(f(1))}

 // Orientation: these panels have no accelerometer, so it is chosen here and
 // applied with a restart (the screen size changes).
 const orientation=$('orientation');
 if(orientation){
  const hint=document.createElement('p');hint.className='hint';hint.id='orientation-hint';
  hint.textContent='Saving a new orientation restarts the panel. Pages, media and the bottom bar adapt to portrait or landscape.';
  orientation.closest('label')?.after(hint);
 }

 // Dashboard stream settings belong with the Home Assistant connection.
 const anchor=$('page-connection');
 if(!anchor)return;
 const form=document.createElement('form');
 form.className='panel';form.id='panel-dashboard';
 form.innerHTML='<h2>Home Assistant dashboard</h2>'+
  '<p class="hint">Show an interactive Home Assistant dashboard on the panel. A Remote WebView server renders it and streams the picture; '+
  'native screens still appear for Assist and notifications. A dashboard icon appears automatically when an address is set. You can also add <strong>Dashboard</strong> as a control '+
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
 anchor.append(form);
 const show=state=>{
  $('dash-server').value=state.server||'';$('dash-url').value=state.url||'';$('dash-scale').value=state.scale||1;

  $('dash-status').textContent='Stream: '+state.status+(state.restart_required?' · restart the panel to apply the new server or zoom':'');
 };
 var savedDockPin='dashboard';
 function refreshDockOptions(reset=false){
  const select=$('dock-pin');if(!select)return;
  const value=(reset?savedDockPin:select.value)||savedDockPin||'dashboard';
  const entries=[['none','None (dashboard scrolls with dock)'],['dashboard','Streamed dashboard']];
  JSON.parse(panelPages).forEach((p,i)=>{const entity=lead(p);if(entity)entries.push([entity,pageTitle(p,i)])});
  if(!entries.some(([id])=>id===value))entries.push([value,value+' (page unavailable)']);
  select.replaceChildren(...entries.map(([id,label])=>new Option(label,id)));
  select.value=value;
  const summary=$('dock-summary');if(summary)summary.textContent=select.selectedOptions[0]?.textContent||'';
 }
 function showDock(state){
  savedDockPin=state.dock_pin||'dashboard';refreshDockOptions(true);
  $('dash-toolbar-side').value=state.toolbar_side||'right';
 }
 $('dock-settings-form').onsubmit=async event=>{
  event.preventDefault();
  const button=event.currentTarget.querySelector('button');button.disabled=true;
  try{
   showDock(await rpc({op:'panel_save',data:JSON.stringify({dock_pin:$('dock-pin').value,toolbar_side:$('dash-toolbar-side').value})}));
   $('dock-status').textContent='Saved.';
  }catch(error){$('dock-status').textContent=error.message}
  finally{button.disabled=false}
 };
 rpc({op:'panel_state'}).then(state=>{show(state);showDock(state)}).catch(e=>{$('dash-status').textContent=e.message});
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
 const applyAccent=()=>{
  const v=colorValue?.textContent.trim();if(!/^#[0-9a-f]{6}$/i.test(v))return;
  const rgb=[1,3,5].map(i=>parseInt(v.slice(i,i+2),16)/255).map(c=>c<=0.04045?c/12.92:((c+0.055)/1.055)**2.4);
  const luminance=rgb[0]*0.2126+rgb[1]*0.7152+rgb[2]*0.0722;
  document.documentElement.style.setProperty('--accent',v);
  document.documentElement.style.setProperty('--accent-ink',luminance>0.179?'#000':'#fff');
 };
 if(colorValue){applyAccent();new MutationObserver(applyAccent).observe(colorValue,{childList:true,characterData:true,subtree:true})}
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

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
  return {name:String(p?.name||''),icon:String(p?.icon||''),layout,...(grid?{rows,columns}:{}),entities:Array.from({length:n},(_,i)=>String(ids[i]||'')),...artFields(p,n),...(grid&&p?.icon_only?.some(x=>x===true)?{icon_only:Array.from({length:n},(_,i)=>iconOnly(p,i))}:{}),...(artId.test(p?.carousel_artwork||'')?{carousel_artwork:p.carousel_artwork}:{})};
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
  const e=item.entity;if(page.entities.includes(e)&&page.entities[pos]!==e){status(nameOf(e)+' is already on this page.');return}
  // The same entity may appear on several pages: one entry, one subscription.
  const other=!page.entities.slice(0,pos).some(Boolean)&&pageConfigs.find(p=>p!==page&&lead(p)===e);if(other){status(nameOf(e)+' already leads “'+(other.name||nameOf(e))+'”. Place it in another position.');return}
  if(pos>0&&page.layout==='control_sensors'&&!compact(e)){status('Compact positions show sensors and binary sensors.');return}
  try{if(slotOf(e)<0)app()?.add(item)}catch(error){status(error.message);return}
  const old=page.entities[pos];page.entities[pos]=e;if(old!==e){updateArt(page,pos,'');setIconOnly(page,pos,false);if(old)release(old)}
  status('');view='page';selectedPage=pageConfigs.indexOf(page);selectedPos=pos;if(!focusMember())touch();
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
   c.draggable=!editorBusy;c.ondragstart=ev=>{ev.dataTransfer.setData('application/x-onx-cell',String(pos));ev.dataTransfer.effectAllowed='move'};
  }
  c.ondragover=ev=>{ev.preventDefault();c.classList.add('drop')};c.ondragleave=()=>c.classList.remove('drop');
  c.ondrop=ev=>{ev.preventDefault();c.classList.remove('drop');const from=ev.dataTransfer.getData('application/x-onx-cell'),item=ev.dataTransfer.getData('application/x-onx-entity');
   if(from!==''){const f=+from,a=page.entities[f],b=page.entities[pos]||'';if(f===pos)return;if(page.layout==='control_sensors'&&((pos>0&&a&&!compact(a))||(f>0&&b&&!compact(b)))){status('Compact positions show sensors and binary sensors.');return}const art=positionArt(page,f),dest=positionArt(page,pos),fromIcon=iconOnly(page,f),toIcon=iconOnly(page,pos);page.entities[f]=b;page.entities[pos]=a;updateArt(page,f,dest);updateArt(page,pos,art);setIconOnly(page,f,toIcon);setIconOnly(page,pos,fromIcon);selectedPos=pos;status('');touch()}
   else if(item)place(page,pos,JSON.parse(item))};
  return c;
 }
 // Dropping a placed entity outside the screen removes it.
 $('page-grid').addEventListener('dragend',ev=>{if(ev.dataTransfer.dropEffect==='none'&&ev.target.dataset?.member!==undefined){const p=pageConfigs[selectedPage];if(p)removeAt(p,+ev.target.dataset.member)}});
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
  if(isGrid(p)){p.rows=2;p.columns=2}else{delete p.rows;delete p.columns;delete p.icon_only}
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
 }
 function pageButton(p,i,savedList){
  const b=el('button','page-item');b.type='button';const meta=(isGrid(p)?p.rows+' × '+p.columns+' grid':LAYOUTS[p.layout].label)+' · '+p.entities.filter(Boolean).length+'/'+p.entities.length;
  const box=el('span','slot-icon');box.append(pageGlyph(p));const text=el('span','item-text');text.append(el('span','',pageTitle(p,i)),el('small','',meta));
  b.append(el('b','',String(i+1)),box,text);
  const dirty=JSON.stringify(savedList[i])!==JSON.stringify(p)||p.entities.some(e=>{const j=slotOf(e);return j>=0&&app()?.dirtyAt?.(j)});if(dirty)b.append(el('span','dirty-dot','●'));
  b.title=pageTitle(p,i);b.setAttribute('aria-label','Page '+(i+1)+': '+pageTitle(p,i)+(dirty?', unsaved':''));b.setAttribute('aria-current',String(view==='page'&&i===selectedPage));b.disabled=editorBusy;b.onclick=()=>selectPage(i);
  b.draggable=!editorBusy;b.ondragstart=ev=>{ev.dataTransfer.setData('application/x-onx-page',String(i));ev.dataTransfer.effectAllowed='move'};
  b.ondragover=ev=>{if(![...ev.dataTransfer.types].includes('application/x-onx-page'))return;ev.preventDefault();b.classList.add('drop-target')};b.ondragleave=()=>b.classList.remove('drop-target');
  b.ondrop=ev=>{b.classList.remove('drop-target');const f=ev.dataTransfer.getData('application/x-onx-page');if(f==='')return;ev.preventDefault();movePage(+f,i)};return b;
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
  patch('slots',JSON.stringify([pageConfigs,savedPages,names,view,selectedPage,locked,pagesLoaded,pageError,!!editorTarget]),()=>pageConfigs.length?pageConfigs.map((p,i)=>pageButton(p,i,savedList)):[el('p','empty-note',pagesLoaded||editorTarget?'No pages yet.':pageError||'Loading pages…')]);
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
 globalThis.ONX_PAGES={saved:()=>JSON.parse(panelPages),set:list=>{pageConfigs=normalize(list);selectPage(0)},dirty:dirtyPages,discard:()=>{pageConfigs=JSON.parse(savedPages);selectedPos=Math.max(0,pageConfigs[selectedPage]?.entities.findIndex(Boolean)??0);status('')},save:savePages};
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
   const usableW=w-16-gap*(columns-1),usableH=body+8-gap*(rows-1);
   return Array.from({length:rows*columns},(_,i)=>{
    const c=i%columns,r=Math.floor(i/columns),x=Math.floor(c*usableW/columns),y=Math.floor(r*usableH/rows);
    return {x:8+x+c*gap,y:top+y+r*gap,w:Math.floor((c+1)*usableW/columns)-x,h:Math.floor((r+1)*usableH/rows)-y};
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
  let currentArt='';
  const icon=(name,x,y,size,color=foreground)=>{if(currentArt){svg.append(node('image',{href:currentArt,x,y,width:size,height:size}));return}if(/^mdi:[a-z0-9-]+$/.test(name||''))svg.append(node('image',{href:'https://api.iconify.design/mdi/'+name.slice(4)+'.svg?color='+encodeURIComponent(color),x,y,width:size,height:size}))};
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
    if(currentArt)icon(v?.icon,x,r.y+pad,art);else artwork(v?.artwork,x,r.y+pad,art);text(title,tx,r.y+pad,P(16),tw,muted);text(d.title||v?.lines?.[2]?.text||condition(d.state),tx,r.y+pad+P(22),P(24),tw);text(d.artist||v?.lines?.[3]?.text||'',tx,r.y+pad+P(50),P(16),tw,mutedColor);
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
   const p=detail?{layout:'single',ids:[slot.entity]}:pageFor(slot.entity),ids=p.ids,rects=globalThis.ONX_PANEL_GEOMETRY(p.layout,w,h,detail,p.rows,p.columns);
   const base=Math.min(w,h)/240;
   for(const [i,r]of rects.entries()){
    currentArt=detail?'':artUrl(positionArt(p,i));
    const found=views.find(v=>v.entity===ids[i]),v=!detail&&found?{...found,...found.carousel}:found;
    const compact=p.layout==='control_sensors'&&i>0,lines=v?.lines||[];
    const cardTitle=ids[i]===slot.entity?(slot.name||v?.lines?.[4]?.text||ids[i]):(editorSlots.find(s=>s.entity===ids[i])?.name||v?.lines?.[4]?.text||ids[i]||'Choose an entity');
    if(p.layout===gridLayout){
     const on=['on','playing','heat','cool'].includes(v?.panel?.state),color=on?accent:foreground,only=iconOnly(p,i),full=only&&currentArt,g=only?Math.min(r.w,r.h)*.86:Math.min(P(44),Math.min(r.w,r.h)*.4);
     rect(r.x,r.y,r.w,r.h,ids[i]?(on?control:surface):mix(surface,background,40),P(9),{'data-panel-item':i});
     if(ids[i]){icon(v?.icon||iconOf(ids[i]),r.x+(r.w-g)/2,r.y+(r.h-(only?0:P(16))-g)/2,g,color);
      if(full){
       const image=svg.lastElementChild;
       for(const [key,value] of Object.entries({x:r.x,y:r.y,width:r.w,height:r.h,preserveAspectRatio:'xMidYMid slice'}))image.setAttribute(key,value);
       image.style.clipPath='inset(0 round '+P(9)+'px)';
       if(on)rect(r.x+1,r.y+1,r.w-2,r.h-2,'none',P(9),{stroke:accent,'stroke-width':P(2)});
      }
      if(!only)text(cardTitle,r.x+r.w/2,r.y+r.h-P(17),P(12),r.w-P(8),color,'middle')}
     continue;
    }
    if(!detail){mainCard(v,cardTitle,ids[i],r,compact);continue}
    svg.append(node('rect',{x:r.x,y:r.y,width:r.w,height:r.h,rx:8,fill:surface,'data-panel-item':i}));
    const title=ids[i]===slot.entity?(slot.name||lines[4]?.text||ids[i]):(editorSlots.find(s=>s.entity===ids[i])?.name||lines[4]?.text||ids[i]||'Choose an entity');
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
   currentArt='';
  if(!detail){
    text(new Date().toLocaleTimeString(undefined,{hour:'2-digit',minute:'2-digit'}),P(16),P(8),P(16),P(90),mutedColor);
    // Bottom bar: the selected control sits on an accent-tinted marker.
    const spacing=Math.round(54*base*.75),iconSize=Math.round(20*base),y=h-22;
    svg.append(node('rect',{x:w/2-16*base,y:y-16*base,width:32*base,height:32*base,rx:16*base,fill:soft,'data-nav-marker':''}));
    const nav=view?.carousel?.navigator||[];
    for(const item of nav){
     const page=pageConfigs.find(p=>lead(p)===item.entity)||(item.offset===0?pageConfigs[selectedPage]:null);
     currentArt=artUrl(page?.carousel_artwork);
     icon(page?pageIcon(page):item.icon,w/2+item.offset*spacing-iconSize/2,y-iconSize/2,iconSize,item.offset===0?accent:foreground);
    }
    currentArt='';
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
  'native screens still appear for Assist and notifications. A dashboard icon appears automatically when an address is set. You can also add <strong>Dashboard</strong> as a control '+
  'to open it from the icon strip, or use the Dashboard switch in Home Assistant. Swipe left or right to return.</p>'+
  '<div class="field-grid">'+
  '<label class="field">Remote WebView server<input id="dash-server" maxlength="96" placeholder="192.168.1.10:8081" autocomplete="off"></label>'+
  '<label class="field">Zoom<input id="dash-scale" type="number" min="0.5" max="4" step="0.25"></label>'+
  '</div>'+
  '<label class="field">Dashboard address<input id="dash-url" maxlength="256" placeholder="http://homeassistant.local:8123/room-panel/0" autocomplete="off"></label>'+
  '<label class="field">Pinned dock item<select id="dock-pin"></select></label>'+
  '<label class="field">Pinned item position<select id="dash-toolbar-side"><option value="left">Left of toolbar</option><option value="right">Right of toolbar</option></select></label>'+
  '<p class="hint">Design the dashboard for the panel’s size, without animations: these screens update a full frame in roughly a tenth of a second. '+
  'Changing the server or zoom needs a restart.</p>'+
  '<p class="hint" id="dash-status" role="status"></p>'+
  '<div class="settings-actions"><button class="primary" type="submit">Save dashboard</button></div>';
 anchor.after(form);
 const show=state=>{
  $('dash-server').value=state.server||'';$('dash-url').value=state.url||'';$('dash-scale').value=state.scale||1;
  savedDockPin=state.dock_pin||'dashboard';refreshDockOptions(true);
  $('dash-toolbar-side').value=state.toolbar_side||'right';
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
 }
 rpc({op:'panel_state'}).then(show).catch(e=>{$('dash-status').textContent=e.message});
 form.onsubmit=async event=>{
  event.preventDefault();
  const button=form.querySelector('button');button.disabled=true;
  try{
   show(await rpc({op:'panel_save',data:JSON.stringify({server:$('dash-server').value.trim(),url:$('dash-url').value.trim(),scale:parseFloat($('dash-scale').value)||1,...( $('dock-pin').value!=='dashboard'||savedDockPin!=='dashboard'?{dock_pin:$('dock-pin').value}:{}),toolbar_side:$('dash-toolbar-side').value})}));
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

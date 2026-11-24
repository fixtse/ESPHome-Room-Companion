#pragma once
namespace esphome::onx_config_web {
static const char CONFIG_APP_JS[] = R"ONXJS(
(()=>{
'use strict';
const $=id=>document.getElementById(id),ha=new OnxHA.Client(),DRAFT='onx.config.draft.v1';
const domains=[['','All'],['sensor','Sensors'],['binary_sensor','Binary sensors'],['light','Lights'],['switch','Switches'],['fan','Fan'],['number','Number'],['input_number','Input Number'],['select','Select'],['input_select','Input Select'],['scene','Scene'],['button','Button'],['input_button','Input Button'],['siren','Sirens'],['event','Events'],['climate','Climate'],['media_player','Media players'],['timer','Timers'],['weather','Weather'],['cover','Covers'],['actions','Automations & scripts']];
// Keep the rail preference across browser restarts, without delaying startup.
const railToggle=$('rail-toggle');
function setRailCollapsed(collapsed){
 document.body.classList.toggle('rail-collapsed',collapsed);
 railToggle.setAttribute('aria-expanded',String(!collapsed));
 railToggle.setAttribute('aria-label',collapsed?'Expand sidebar':'Collapse sidebar');
 railToggle.title=collapsed?'Expand sidebar':'Collapse sidebar';
 try{localStorage.setItem('onx.sidebar.collapsed',String(collapsed))}catch{}
}
try{setRailCollapsed(localStorage.getItem('onx.sidebar.collapsed')==='true')}catch{setRailCollapsed(false)}
railToggle.onclick=()=>setRailCollapsed(!document.body.classList.contains('rail-collapsed'));
for(const button of document.querySelectorAll('.tabs button'))button.title=button.textContent.trim();
let discoveringHA=false,discoverRounds=0;
const HA_LOGO='<svg viewBox="0 0 40 40" width="28" height="28" aria-hidden="true"><path fill="#46BDF4" d="M18.1 3.3a2.7 2.7 0 0 1 3.7 0l14.5 14.2a6.8 6.8 0 0 1 2 4.6v13.3c0 1.2-1 2.1-2.2 2.1H21.4v-4.6l6.8-6.7a3.1 3.1 0 1 0-1.9-2l-4.9 4.9V16.7a3 3 0 1 0-2.9 0v17.6l-4.9-4.8a3.1 3.1 0 1 0-1.9 1.9l6.3 6.1H3.8c-1.2 0-2.1-1-2.1-2.1V22.1c0-1.7.7-3.4 1.9-4.6z"/></svg>';
// Scan with the panel's mDNS (the query runs asynchronously on the device), retrying while the tab stays open.
async function discoverHA(auto=false){
 if(discoveringHA||document.hidden)return;if(!auto)discoverRounds=0;discoveringHA=true;
 const button=$('ha-discover'),status=$('ha-discovery-status'),results=$('ha-discovery-results'),scanner=$('ha-scanner');
 const stop=text=>{scanner.classList.remove('loading');button.disabled=false;status.textContent=text};
 scanner.classList.add('loading');button.disabled=true;status.textContent='Looking for Home Assistant on the panel’s network…';
 let response;
 try{
  response=await rpc({op:'ha_discover',q:'refresh'});
  for(let attempt=0;response.status==='searching'&&attempt<40;attempt++){await new Promise(resolve=>setTimeout(resolve,250));response=await rpc({op:'ha_discover'})}
 }catch{discoveringHA=false;stop('Discovery is unavailable. Enter your Home Assistant address below.');return}
 discoveringHA=false;
 const seen=new Set();results.replaceChildren();
 for(const item of response.instances||[]){
  let url;try{url=OnxHA.baseUrl(item.url)}catch{continue}
  const key=item.uuid||url;if(seen.has(key))continue;seen.add(key);
  const choice=document.createElement('button');choice.type='button';choice.className='ha-result';choice.innerHTML=HA_LOGO;
  const text=document.createElement('span');text.append(element('b',item.name||'Home Assistant'),element('small',url+(item.version?' · '+item.version:'')));
  const chev=element('span','›');chev.className='chev';chev.setAttribute('aria-hidden','true');choice.append(text,chev);
  choice.onclick=()=>{$('ha-url').value=url;$('ha-connect').click()};results.append(choice);
 }
 if(response.status==='unavailable'){stop('This panel cannot search the network. Enter the address below.');return}
 if(seen.size){stop(seen.size===1?'Found 1 Home Assistant. Select it to sign in.':'Found '+seen.size+' Home Assistant servers. Select one to sign in.');return}
 if(!$('page-connection').hidden&&++discoverRounds<5){status.textContent='Still looking… Make sure Home Assistant is on the same network.';setTimeout(()=>{if(!$('page-connection').hidden)discoverHA(true)},1500);return}
 stop('No Home Assistant found automatically. Search again or enter its address below.');
}
$('ha-discover').onclick=()=>discoverHA(false);
// ImmichFrame: discovery (the panel's mDNS search, as for Home Assistant) and
// the idle-screen photo settings, saved through the panel's entities.
let discoveringIF=false;
// force: the search button (always a new search). Otherwise the panel returns
// what an earlier search found and only searches when it has nothing yet.
async function discoverIF(force=false){
 if(discoveringIF)return;discoveringIF=true;
 const button=$('if-discover'),status=$('if-discovery-status'),results=$('if-discovery-results'),scanner=$('if-scanner');
 const stop=text=>{scanner.classList.remove('loading');button.disabled=false;status.textContent=text;discoveringIF=false};
 scanner.classList.add('loading');button.disabled=true;status.textContent='Looking for ImmichFrame on the panel’s network…';
 let response;
 try{response=await rpc({op:'if_discover',...(force?{q:'refresh'}:{})});if(!force&&response.status==='ready'&&!(response.instances||[]).length)response=await rpc({op:'if_discover',q:'refresh'});for(let i=0;response.status==='searching'&&i<40;i++){await new Promise(r=>setTimeout(r,250));response=await rpc({op:'if_discover'})}}
 catch{stop('Discovery is unavailable. Enter the ImmichFrame address below.');return}
 results.replaceChildren();const seen=new Set();
 for(const item of response.instances||[]){
  const url=String(item.url||'').replace(/\/+$/,'');if(!/^https?:\/\/[^\s/?#@]+/.test(url)||seen.has(url))continue;seen.add(url);
  const choice=document.createElement('button');choice.type='button';choice.className='ha-result';
  choice.innerHTML='<svg viewBox="0 0 24 24" width="28" height="28" aria-hidden="true"><rect x="3" y="4" width="18" height="16" rx="3" fill="#4250AF"/><circle cx="9" cy="10" r="2" fill="#fff"/><path d="m21 16-5-5-8 8h10a3 3 0 0 0 3-3Z" fill="#fff" fill-opacity=".85"/></svg>';
  const text=document.createElement('span');text.append(element('b',item.name||'ImmichFrame'),element('small',url+(item.version?' · '+item.version:'')));
  const chev=element('span','›');chev.className='chev';chev.setAttribute('aria-hidden','true');choice.append(text,chev);
  choice.onclick=()=>{$('if-url').value=url;$('if-status').textContent='Selected '+(item.name||'ImmichFrame')+'. Save to use it.';$('if-save').focus()};results.append(choice);
 }
 if(response.status==='unavailable'){stop('This panel cannot search the network. Enter the address below.');return}
 stop(seen.size?(seen.size===1?'Found 1 ImmichFrame server. Select it, then save.':'Found '+seen.size+' ImmichFrame servers. Select one, then save.'):'No ImmichFrame found automatically. Search again or enter its address below.');
}
// Idle screen page: the mode (0 clock, 1 dashboard, 2 ImmichFrame) and the
// ImmichFrame settings go through the "immich" RPC; the clock, weather and
// timing fields are device settings and save with them.
let ifSaved=null,changingIF=false;
const idleMode=()=>document.querySelector('[name="idle-mode"]:checked')?.value||'0';
function idleSections(){const mode=idleMode();for(const e of document.querySelectorAll('#page-photos [data-idle]'))e.hidden=!e.dataset.idle.split(' ').includes(mode)}
const DETAIL_NAMES={date:'Date',location:'Place',album:'Album',people:'People',description:'Description'};
function ifCard(){
 const url=ifSaved?.url||'',show=!!url&&!changingIF;$('if-connected').hidden=!show;$('if-setup').hidden=show;$('if-cancel-change').hidden=!(changingIF&&url);
 if(!show){if(idleMode()==='2'&&!discoveringIF&&!$('if-discovery-results').children.length)void discoverIF();return}
 $('if-link').href=url;$('if-link').textContent=url.replace(/^https?:\/\//,'');
 const facts=[['Photo fit',$('if-fit').selectedOptions[0]?.textContent||''],['Clock',['Large clock','Small clock','Photo only'][+(ifSaved.overlay||0)]||'Large clock'],['New photo every',(ifSaved.interval||'20')+' s'],['Details',String(ifSaved.details||'').split(',').filter(Boolean).map(d=>DETAIL_NAMES[d]||d).join(', ')||'None']];
 $('if-facts').replaceChildren(...facts.map(([k,v])=>{const d=document.createElement('div');d.append(element('dt',k),element('dd',v));return d}));
 const pill=$('if-pill');pill.textContent='Checking…';delete pill.dataset.state;
 const ctl=new AbortController(),timer=setTimeout(()=>ctl.abort(),5000);
 fetch(url+'/',{mode:'no-cors',signal:ctl.signal,cache:'no-store'}).then(()=>{pill.textContent='Reachable';pill.dataset.state='ok'},()=>{pill.textContent='Not reachable from this browser';pill.dataset.state='off'}).finally(()=>clearTimeout(timer));
}
async function loadIF(){
 try{const s=await rpc({op:'immich_state'});if(s.error)throw Error(s.error);ifSaved=s;changingIF=false;
  $('if-url').value=s.url||'';const mode=['0','1','2'].includes(s.idle)?s.idle:'0';for(const r of document.querySelectorAll('[name="idle-mode"]'))r.checked=r.value===mode;
  $('if-fit').value=s.fit||'0';$('if-interval').value=s.interval||'20';
  const overlay=['0','1','2'].includes(s.overlay)?s.overlay:'0';for(const r of document.querySelectorAll('[name="if-overlay"]'))r.checked=r.value===overlay;
  $('if-split').checked=s.split==='1';$('if-fade').checked=s.fade==='1';
  const on=new Set(String(s.details||'').split(',').filter(Boolean));for(const c of document.querySelectorAll('[data-detail]'))c.checked=on.has(c.dataset.detail);
  idleSections();ifCard();
 }catch(error){$('if-status').textContent='Could not read the idle screen settings: '+error.message}
}
$('if-discover').onclick=()=>discoverIF(true);
for(const r of document.querySelectorAll('[name="idle-mode"]'))r.onchange=()=>{idleSections();ifCard();$('if-status').textContent='Not saved yet.'};
$('if-change').onclick=()=>{changingIF=true;ifCard();$('if-url').focus()};
$('if-cancel-change').onclick=()=>{changingIF=false;$('if-url').value=ifSaved?.url||'';ifCard()};
$('if-remove').onclick=()=>{changingIF=true;$('if-url').value='';ifCard();$('if-status').textContent='Save to remove the server.'};
$('idle-form').addEventListener('input',event=>{if(event.target.closest('#if-section')||event.target.name==='idle-mode'){$('if-status').textContent='Not saved yet.';return}settingsInput()});
$('idle-form').onsubmit=async event=>{
 event.preventDefault();const url=$('if-url').value.trim().replace(/\/+$/,''),status=$('if-status'),mode=idleMode();
 if(url&&!/^https?:\/\/[^\s/?#@]+(:\d+)?(\/[^\s?#@]*)?$/.test(url)){status.textContent='Enter the address with http:// or https://, for example http://192.168.1.20:8080.';$('if-url').focus();return}
 if(mode==='2'&&!url){status.textContent='Choose an ImmichFrame server or enter its address.';$('if-url').focus();return}
 const details=[...document.querySelectorAll('[data-detail]')].filter(c=>c.checked).map(c=>c.dataset.detail);
 if(details.length>3){status.textContent='Choose at most three photo details.';return}
 const interval=Math.min(600,Math.max(5,Math.round((+$('if-interval').value||20)/5)*5));$('if-interval').value=interval;
 const overlay=document.querySelector('[name="if-overlay"]:checked')?.value||'0';
 const data=[url,mode,$('if-fit').value,$('if-split').checked?'1':'0',details.join(','),$('if-fade').checked?'1':'0',String(interval),overlay].join('\n');
 $('if-save').disabled=true;status.textContent='Saving…';
 try{const r=await rpc({op:'immich',data});if(r.error)throw Error(r.error);
  if(initialized&&dirtySettings()&&!await saveSettings())throw Error('The clock and weather settings were not saved. Check the message at the top and try again.');
  status.textContent='Idle screen saved.';await loadIF()}
 catch(error){status.textContent=error.message}finally{$('if-save').disabled=false}
};
document.querySelector('.tabs [data-page="photos"]')?.addEventListener('click',()=>loadIF());

const copy=value=>JSON.parse(JSON.stringify(value));
const emptyGestures=()=>({cw:'',ccw:'',holdCw:'',holdCcw:'',feedback:'',popup:'1',responses:'0',pressResponses:'0',events:'0',step:'1'});
const empty=()=>({entity:'',name:'',area:'',action:'',actionName:'',typeOption:'',icon:'',iconOverride:'0',secondary:'',secondaryText:'{value}',sensorGraph:'0',timerHidden:'0',coverPresets:'',coverPresetsEnabled:'1',gestures:emptyGestures(),playlists:[]});
let MAX_SLOTS=250;let pagePick=null;
let visibleSlots=1,editTarget=null;  // null: the panel; {name,isNew}: a saved or new preset
let slots=Array.from({length:MAX_SLOTS},empty),saved=copy(slots),active=0,mode='entity',domain='',next=-1,revision=0,debounce,busy=false,initialized=false,device={},settings={},savedSettings={},savedWeather='',recovery=null,lastLoaded=null,statusBusy=false,previewBusy=false,liveViews=[],overlayTimer=0,lastPreviewAt=0,slotTab='display',playlistTarget=-1,gestureTarget='cw';
function element(tag,text,className){const e=document.createElement(tag);e.textContent=text;if(className)e.className=className;return e}
function message(text,error=false){$('status').textContent=text;$('status').classList.toggle('error',error)}
// Long saves/imports: a progress bar under the status line, and a hold that
// pauses the background preview/status polling so it does not compete with
// the save for the panel's small request queue.
let work=0;
function progress(done=0,total=0){const s=$('status');s.classList.toggle('working',total>0);s.style.setProperty('--p',total?Math.max(4,Math.round(100*done/total))+'%':'0%')}
async function hold(fn){work++;try{return await fn()}finally{if(!--work)progress()}}
function safeRead(key){try{return JSON.parse(sessionStorage.getItem(key)||'null')}catch{return null}}
function dirtySlot(i){return JSON.stringify(slots[i])!==JSON.stringify(saved[i])}
function dirtySettings(){return JSON.stringify(settings)!==JSON.stringify(savedSettings)}
function hasChanges(){return slots.some((_,i)=>dirtySlot(i))||dirtySettings()}
// Pages (panel_web.h) are saved together with the entries they use.
const pagesDirty=()=>!!globalThis.ONX_PAGES?.dirty?.();
function unsavedLabel(){const n=slots.filter((_,i)=>dirtySlot(i)).length,p=pagesDirty();return n||p?'Unsaved changes'+(n?' · '+n+' entit'+(n===1?'y':'ies'):'')+(p?' · pages':''):''}
function saveStates(){trackUndo();const any=slots.some((_,i)=>dirtySlot(i))||pagesDirty();$('save').disabled=busy||!initialized||!dirtySlot(active);$('save-all').disabled=busy||!initialized||!(any||editTarget?.isNew);$('discard').disabled=busy||!any;$('undo').disabled=busy||!undoStack.length;$('unsaved-count').textContent=unsavedLabel()}
function persistDraft(){if(!initialized||editTarget)return;if(recovery&&hasChanges()){recovery=null;$('draft-notice').hidden=true}if(recovery)return;try{if(hasChanges())sessionStorage.setItem(DRAFT,JSON.stringify({schema:2,slots,settings}));else sessionStorage.removeItem(DRAFT)}catch{message('This browser could not store a recovery draft. Save your changes before closing.',true)}}
// In-page alert dialog in the style of shadcn/ui AlertDialog. Resolves to true/false,
// or to the entered text (null when cancelled) when `input` is given.
function ask({title,text='',ok='Continue',cancel='Cancel',danger=false,input=null}){
 const dialog=$('ask'),field=$('ask-input'),error=$('ask-error'),okButton=$('ask-ok');
 $('ask-title').textContent=title;$('ask-text').textContent=text;$('ask-text').hidden=!text;
 okButton.textContent=ok;$('ask-cancel').textContent=cancel;okButton.classList.toggle('danger-action',danger);okButton.classList.toggle('primary',!danger);
 $('ask-field').hidden=!input;error.hidden=true;error.textContent='';
 if(input){$('ask-label').textContent=input.label||'Name';field.value=input.value||'';field.placeholder=input.placeholder||''}
 return new Promise(resolve=>{
  const finish=value=>{dialog.onclose=null;$('ask-form').onsubmit=null;$('ask-cancel').onclick=null;if(dialog.open)dialog.close();resolve(value)};
  $('ask-cancel').onclick=()=>finish(input?null:false);
  dialog.onclose=()=>finish(input?null:false);
  dialog.oncancel=event=>{event.preventDefault();finish(input?null:false)};
  $('ask-form').onsubmit=event=>{event.preventDefault();if(!input){finish(true);return}const value=field.value.trim(),problem=input.validate?.(value);if(problem){error.textContent=problem;error.hidden=false;field.focus();return}finish(value)};
  dialog.showModal();(input?field:okButton).focus();if(input)field.select()})}
function presetNameProblem(value,except=''){if(!value)return 'Enter a name.';if(value.length>32||/[\u0000-\u001f\u007f]/.test(value))return 'Use 1-32 characters.';if(value.toLowerCase()==='custom')return '“Custom” is reserved for controls that match no preset.';if(value.toLowerCase()!==except.toLowerCase()&&(presets.presets||[]).some(item=>item.name.toLowerCase()===value.toLowerCase()))return 'A preset with this name already exists.';return ''}
function actionLabel(action){return !action?'Device default':action==='none'?'No action':action==='open_playlists'?'Open playlists':action.startsWith('script.')?'Run script':'Run automation'}
function nativePress(entity){return /^(fan|number|input_number|select|input_select|scene|button|input_button)\./.test(entity)}
function choices(slot){const d=slot.entity.split('.')[0];return d==='light'?['Light (On/Off)','Dimmable Light','Color Temperature Light','RGB Light']:d==='switch'?['Switch','Plug']:d==='cover'?['Cover (Preset Off)','Cover (Preset On)']:['Automatic']}
function slotPayload(index,slot){return {op:'save',slot:index,entity:slot.entity,action:slot.action,name:slot.name,area:slot.area,action_name:slot.actionName,type:slot.typeOption,icon:slot.icon,icon_override:slot.iconOverride,secondary:slot.secondary,secondary_text:slot.secondaryText,sensor_graph:slot.sensorGraph,timer_hidden:slot.timerHidden,cover_presets:slot.coverPresets,cover_presets_enabled:slot.coverPresetsEnabled,gestures:JSON.stringify(slot.gestures),playlists:JSON.stringify(slot.playlists)}}
async function fetchJson(url,options={}){
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),10000);
 try{const response=await fetch(url,{...options,cache:'no-store',signal:controller.signal});const body=await response.json();if(!response.ok||body.error){const e=Error(body.error||'The panel could not complete this request');e.busy=response.status===429;throw e}return {response,body}}
 catch(error){if(error.name==='AbortError')throw Error('The panel did not respond. Check its Wi-Fi connection and retry.');throw error}
 finally{clearTimeout(timer)}
}
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
// Queue a request, retrying while the panel's queue is full, then poll for the
// result: quickly at first (most requests finish in tens of ms), backing off.
async function rpc(values){
 let queued;
 for(let attempt=0;;attempt++){try{({body:queued}=await fetchJson('/panel/api',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Onx-Token':window.ONX_TOKEN},body:new URLSearchParams(values)}));break}catch(error){if(!error.busy||attempt>=10)throw error;await sleep(200+150*attempt)}}
 const until=Date.now()+30000;
 for(let delay=60;Date.now()<until;delay=Math.min(delay*1.5,400)){await sleep(delay);const {response,body}=await fetchJson('/panel/result?ticket='+queued.ticket);if(response.status!==202)return body}
 throw Error('The panel did not confirm this change. Refresh status before retrying.');
}
globalThis.ONX_RPC=rpc;
let changingHA=false;function haView(){const auth=ha.read('onx.ha.auth'),show=!!auth&&!changingHA;$('ha-connected').hidden=!show;$('ha-setup').hidden=show;$('ha-cancel-change').hidden=!(auth&&changingHA);if(!show)return;const link=$('ha-link');link.href=auth.base;link.textContent=auth.base.replace(/^https?:\/\//,'').replace(/\/$/,'');const panel=!!device.ha_connected,pill=$('ha-pill');pill.textContent=panel&&ha.ready?'Connected':panel?'Panel connected':'Panel not connected';pill.dataset.state=panel&&ha.ready?'ok':panel?'warn':'off';const facts=$('ha-facts');facts.replaceChildren();for(const [k,v] of [['Panel → Home Assistant',panel?'Connected (ESPHome API)':'Not connected'],['Browser → Home Assistant',ha.ready?'Entity list loaded':'Signed in'],['Entities available',ha.ready?String(ha.items.length):'—'],['Last refreshed',lastLoaded?lastLoaded.toLocaleTimeString():'—']]){const row=document.createElement('div');row.append(element('dt',k),element('dd',v));facts.append(row)}}$('ha-change').onclick=()=>{changingHA=true;haView();void discoverHA(false)};$('ha-cancel-change').onclick=()=>{changingHA=false;haView()};function browserStatus(){const loggedIn=!!ha.read('onx.ha.auth');$('browser-status').textContent='Browser → Home Assistant: '+(ha.ready?'entity list loaded':loggedIn?'signed in':'disconnected');$('browser-status').dataset.ok=String(ha.ready);haView();$('catalogue-time').textContent=lastLoaded?'Entity list loaded '+lastLoaded.toLocaleTimeString()+'. '+ha.items.length+' available entities.':'No entity list loaded in this browser.'}
function deviceStatus(online){$('device-status').textContent=online?'Panel: reachable':'Panel: unreachable';$('device-status').dataset.ok=String(online);$('ha-status').textContent='Panel → Home Assistant: '+(!online?'unknown':device.ha_connected?'connected':'not connected');$('ha-status').dataset.ok=String(online&&device.ha_connected);if(typeof haView==='function')haView();const info=$('device-info');info.replaceChildren();for(const [key,value] of [['Model','Room Companion'],['Firmware',device.firmware||'Unknown'],['Uptime',Number.isFinite(device.uptime_ms)?Math.floor(device.uptime_ms/60000)+' minutes':'Unknown'],['Wi-Fi signal',Number.isFinite(device.wifi_rssi_dbm)?Math.round(device.wifi_rssi_dbm)+' dBm':'Unavailable on this firmware'],['Wi-Fi disconnects',device.wifi_disconnects_since_boot??'Unknown'],['Last reset',device.reset_reason_text||'Unknown'],['Internal heap',Number.isFinite(device.internal_heap_free)?Math.round(device.internal_heap_free/1024)+' KB free / '+Math.round(device.internal_heap_minimum/1024)+' KB minimum':'Unavailable on this firmware'],['Artwork',device.artwork_error||device.artwork_status||'Unknown'],['Wi-Fi',online?(device.wifi_connected?'connected':'disconnected'):'unknown'],['Home Assistant',online?(device.ha_connected?'connected':'disconnected'):'unknown']])info.append(element('dt',key),element('dd',value))}
function page(name){document.querySelectorAll('.tabs [data-page]').forEach(b=>{const section=$('page-'+b.dataset.page);if(section)section.hidden=b.dataset.page!==name});if(name==='connection'&&(!ha.read('onx.ha.auth')||changingHA))void discoverHA(true);document.querySelectorAll('[data-page]').forEach(e=>{if(e.dataset.page===name)e.setAttribute('aria-current','page');else e.removeAttribute('aria-current')})}
document.querySelectorAll('[data-page]').forEach(e=>e.onclick=()=>page(e.dataset.page));
const svgNS='http://www.w3.org/2000/svg',measure=document.createElement('canvas').getContext('2d');
function svgNode(tag,attrs={}){const node=document.createElementNS(svgNS,tag);for(const [key,value] of Object.entries(attrs))node.setAttribute(key,String(value));return node}
function fitted(text,size,width){measure.font=size+'px Arial';let out=String(text);if(measure.measureText(out).width<=width)return out;while(out&&measure.measureText(out+'…').width>width)out=out.slice(0,-1);return out+'…'}
function drawPreview(slot){
 clearTimeout(overlayTimer);
 const svg=$('device-preview');let view=liveViews.find(item=>item.entity===slot.entity);
 if(globalThis.ONX_PANEL_PREVIEW){globalThis.ONX_PANEL_PREVIEW(svg,slot,view,liveViews,$('preview-screen').value,settings);return}
 if(view&&$('preview-screen').value==='main')view={...view,...view.carousel};
 svg.replaceChildren(svgNode('circle',{cx:120,cy:120,r:120,fill:'#000'}));
 svg.setAttribute('aria-label',(slot.name||slot.entity||'Empty slot')+' device display preview');
 const hex=color=>'#'+Number(color??0xffffff).toString(16).padStart(6,'0');
 const text=(content,y,size,width=196,color=0xffffff,x=0,wrap=false,backing=false)=>{
  const words=String(content).split(' '),rows=wrap?['']:String(content).split('\n');
  if(wrap){measure.font=size+'px Arial';for(const word of words){const i=rows.length-1,candidate=(rows[i]+' '+word).trim();if(measure.measureText(candidate).width>width&&rows[i]&&rows.length<2)rows.push(word);else rows[i]=candidate}}
  for(const [i,row] of rows.slice(0,2).entries()){
   const shown=fitted(row,size,backing?width-12:width),top=y+i*(size+2);
   if(backing&&shown){measure.font=size+'px Arial';const w=Math.min(width,measure.measureText(shown).width+12);svg.append(svgNode('rect',{x:120+x-w/2,y:top,width:w,height:size+4,rx:7,fill:'#14171d',opacity:.7}));}
   const line=svgNode('text',{x:120+x,y:top+size*.85+(backing?2:0),'text-anchor':'middle','font-size':size,fill:hex(color)});line.textContent=shown;svg.append(line);
  }
 };
 if(!view){text(slot.entity?'Save to load':'Choose an entity',88,18);if(slot.entity)text('live device data',112,16);$('preview-status').textContent=slot.entity?'No live view for this unsaved entity.':'Choose what this slot displays.';return}
 if(view.layout==='graph'){
  const points=view.graph||[],now=view.graph_now||0,values=points.map(p=>p.value),low=Math.min(...values),high=Math.max(...values);
  const bottom=view.graph_min_label??'',top=view.graph_max_label??'';
  measure.font='12px Arial';const gutter=Math.min(66,Math.max(measure.measureText(bottom).width,measure.measureText(top).width)+5),left=27+gutter;
  if(points.length)for(const [label,y] of [[top,104],[bottom,156]]){const node=svgNode('text',{x:left-5,y:y+10,'text-anchor':'end','font-size':12,fill:'#d2d2d2'});node.textContent=label;svg.append(node);}
  for(const y of [104,136,168])svg.append(svgNode('line',{x1:left,y1:y,x2:213,y2:y,stroke:'#344653','stroke-width':1}));
  for(let i=1;i<points.length;i++){const a=points[i-1],b=points[i];if(b.t-a.t>1200)continue;
   const x=p=>213-(now-p.t)*(186-gutter-1)/43200,y=p=>104+(high>low?64-(p.value-low)*64/(high-low):32);
   svg.append(svgNode('line',{x1:x(a),y1:y(a),x2:x(b),y2:y(b),stroke:settings.media_color||'#62B0FF','stroke-width':2}));}
 }
 if(view.icon&&/^mdi:[a-z0-9-]+$/.test(view.icon)){
  const size=view.layout==='media'?80:view.icon_size;
  const icon=svgNode('image',{href:'https://api.iconify.design/mdi/'+view.icon.slice(4)+'.svg?color=%23ffffff',x:120+(view.icon_x||0)-size/2,y:view.icon_y,width:size,height:size});svg.append(icon);
 }
 if(view.layout==='media'&&view.artwork){try{const url=new URL(view.artwork);if(['http:','https:'].includes(url.protocol))svg.append(svgNode('image',{href:url.href,x:70,y:18,width:100,height:100,preserveAspectRatio:'xMidYMid meet'}))}catch{}}
 const ringValue=view.layout==='media'?view.progress:view.value_ring;
 if(ringValue>=0){
  svg.append(svgNode('circle',{cx:120,cy:120,r:118,fill:'none',stroke:'#344653','stroke-width':4}));
  const ring=svgNode('circle',{cx:120,cy:120,r:118,fill:'none',stroke:view.layout==='light'?hex(view.accent):settings.media_color||hex(view.ring_color??0x62b0ff),'stroke-width':4,'stroke-linecap':'round',pathLength:1000,'stroke-dasharray':ringValue+' '+(1000-ringValue),transform:'rotate(-90 120 120)',class:'preview-ring','data-playing':String(view.layout==='media'&&!!view.playing),opacity:view.layout!=='media'||view.playing?1:.6});svg.append(ring);
 }
 if(view.layout==='light'&&view.selector_value>=0){
  const rgb=view.light_mode===2,start=rgb?0:135,sweep=rgb?360:270;
  const point=angle=>[120+114*Math.cos(angle*Math.PI/180),120+114*Math.sin(angle*Math.PI/180)];
  for(let i=0;i<36;i++){
   const a=point(start+i*sweep/36),b=point(start+(i+1)*sweep/36),t=i/35;
   const color=rgb?'hsl('+(i*10)+',100%,50%)':'rgb('+[255+(220-255)*t,179+(235-179)*t,71+(255-71)*t].map(Math.round).join(',')+')';
   svg.append(svgNode('path',{d:'M'+a.join(' ')+' A114 114 0 0 1 '+b.join(' '),fill:'none',stroke:color,'stroke-width':10}));
  }
  const p=point(start+sweep*view.selector_value/1000);svg.append(svgNode('circle',{cx:p[0],cy:p[1],r:7,fill:'#fff',stroke:'#111','stroke-width':2}));
 }
 if(view.layout==='media'&&view.overlay&&view.overlay_path){
  const paint=(kind,path)=>{const group=svgNode('g',{'data-transport-overlay':kind});group.append(svgNode('circle',{cx:120,cy:68,r:24,fill:'#000',opacity:.7}),svgNode('path',{d:path,transform:'translate(104 52)',fill:'#fff'}));svg.append(group);return group};
  const group=paint(view.overlay,view.overlay_path);
  if(view.overlay!=='pause'&&view.overlay_ms>0)overlayTimer=setTimeout(()=>{if(!group.isConnected)return;group.remove();if(view.paused&&view.pause_path)paint('pause',view.pause_path)},view.overlay_ms);
 }
 if(view.layout==='carousel')for(const [i,item] of (view.navigator||[]).entries()){
  if(!item.visible)continue;
  const angle=(item.offset||0)*17*Math.PI/180,x=120+102*Math.sin(angle),y=120+102*Math.cos(angle);
  if(item.selected)svg.append(svgNode('circle',{cx:x,cy:y,r:12,fill:'#20384E'}));
  if(/^mdi:[a-z0-9-]+$/.test(item.icon||''))svg.append(svgNode('image',{href:'https://api.iconify.design/mdi/'+item.icon.slice(4)+'.svg?color='+encodeURIComponent(item.selected?'#62B0FF':'#ffffff'),x:x-9,y:y-9,width:18,height:18,opacity:item.selected?1:.55}));
 }
 for(const [i,original] of (view.lines||[]).entries()){const line=original;const name=i===4;text(name&&!slot.secondary?(slot.name||line.text):line.text,line.y,line.size,line.width,line.color,line.x||0,name&&view.layout!=='media',false)}
 $('preview-status').textContent=view.available?'Live device view':'Device view · waiting for entity data';
}
async function refreshPreview(){if(work||previewBusy||busy||!initialized||document.hidden||$('entity-picker').open)return;previewBusy=true;lastPreviewAt=Date.now();try{const data=await rpc({op:'preview',entity:slots[active].entity});globalThis.ONX_PANEL_SIZE={width:data.panel_width||320,height:data.panel_height||480};{const fresh=Array.isArray(data.slots)?data.slots:[],got=new Set(fresh.map(v=>v.entity)),keep=new Set(slots.map(s=>s.entity).filter(Boolean));liveViews=fresh.concat(liveViews.filter(v=>!got.has(v.entity)&&keep.has(v.entity)))}drawPreview(slots[active])}catch{$('preview-status').textContent='Live preview unavailable. Check the panel connection.'}finally{previewBusy=false}}
function preview(){const s=slots[active],d=s.entity.split('.')[0];drawPreview(s);
 const rotates={light:'Brightness, or the selected White / Colour mode',cover:'Position',climate:'Target temperature',fan:'Speed',number:'Value by its step',input_number:'Value by its step',select:'Previous / next option',input_select:'Previous / next option',media_player:'Volume'};
 $('rotate-hint').textContent=s.gestures.cw||s.gestures.ccw||s.gestures.events==='1'?'Run your swipe actions':s.entity?(rotates[d]||'Nothing to adjust'):'—';
 const touches={light:'Brightness slider, power, white and colour swatches',cover:'Drag the blind; Open / Stop / Close',climate:'Target arc, − / +, HVAC modes, preset / fan / swing',fan:'Power, speed bars, oscillate / direction / preset',select:'Tap an option in the list',input_select:'Tap an option in the list',number:'Slider and − / +',input_number:'Slider and − / +',scene:'Large run button',script:'Large run button',button:'Large press button',input_button:'Large press button',switch:'Large on / off toggle',input_boolean:'Large on / off toggle',automation:'Large on / off toggle',media_player:'Artwork, play / pause, previous / next',sensor:'Value, 12 h trend and min / avg / max',binary_sensor:'State and recent changes',weather:'Conditions and 5-day forecast',timer:'Countdown; timers add +1 min, Pause and Cancel'};
 const shortcut=s.action&&s.action!=='none'&&s.action!=='open_playlists'?' · Shortcut: '+(s.actionName||actionLabel(s.action)):'';$('press-hint').textContent=!s.entity?'—':(touches[d]||'Large on / off toggle')+shortcut;
}
function iconButton(label,kind){
 const b=element('button','','icon-button');b.type='button';b.title=label;b.setAttribute('aria-label',label);
 const paths={up:'M12 19V5m-6 6 6-6 6 6',down:'M12 5v14m-6-6 6 6 6-6',remove:'m6 6 12 12M18 6 6 18'};
 const svg=svgNode('svg',{viewBox:'0 0 24 24','aria-hidden':'true',focusable:'false'});svg.append(svgNode('path',{d:paths[kind]}));b.append(svg);return b;
}
function markSlotChanges(text){
 persistDraft();saveStates();message(text);
}
function shortPlaylistName(value){
 let out='';const encoder=new TextEncoder();for(const char of value){if(encoder.encode(out+char).length>48)break;out+=char}return out;
}
function renderEditorTabs(){
 const media=!!slots[active].entity;$('slot-tab-playlists').hidden=!media;
 if(slotTab==='playlists'&&!media)slotTab='display';
 for(const button of document.querySelectorAll('[data-slot-tab]')){
  const chosen=button.dataset.slotTab===slotTab;button.setAttribute('aria-selected',String(chosen));button.tabIndex=chosen?0:-1;button.disabled=busy;
  $('slot-panel-'+button.dataset.slotTab).hidden=!chosen;
 }
}
for(const button of document.querySelectorAll('[data-slot-tab]')){
 button.onclick=()=>{slotTab=button.dataset.slotTab;renderEditorTabs()};
 button.onkeydown=event=>{
  if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;event.preventDefault();
  const tabs=[...document.querySelectorAll('[data-slot-tab]')].filter(b=>!b.hidden&&!b.disabled),index=tabs.indexOf(button);
  const next=event.key==='Home'?0:event.key==='End'?tabs.length-1:(index+(event.key==='ArrowRight'?1:-1)+tabs.length)%tabs.length;
  if(tabs[next]){slotTab=tabs[next].dataset.slotTab;renderEditorTabs();tabs[next].focus()}
 };
}
function renderPlaylists(){
 const s=slots[active];$('playlist-options').hidden=!s.entity;$('playlist-count').textContent=s.playlists.length+' of 8';
 $('playlist-entries').replaceChildren();
 if(!s.playlists.length)$('playlist-entries').append(element('p','Add a quick action, then choose the script or automation it runs.','empty'));
 s.playlists.forEach((entry,index)=>{
  const row=element('article','','playlist-row'),head=element('div','','playlist-row-head'),tools=element('div','','playlist-row-tools');
  row.setAttribute('aria-label','Quick action '+(index+1));head.append(element('span',String(index+1),'playlist-order'));
  for(const [label,delta,icon] of [['Move up',-1,'up'],['Move down',1,'down'],['Remove',0,'remove']]){
   const b=iconButton(label+' quick action '+(index+1),icon);if(!delta)b.classList.add('danger');
   b.disabled=busy||!initialized||(delta<0&&index===0)||(delta>0&&index===s.playlists.length-1);
   b.onclick=()=>{if(delta)[s.playlists[index],s.playlists[index+delta]]=[s.playlists[index+delta],s.playlists[index]];else s.playlists.splice(index,1);changed()};tools.append(b);
  }
  head.append(tools);row.append(head);
  const fields=element('div','','playlist-fields'),nameField=element('label','Display name','field'),name=element('input','');
  name.value=entry.name;name.maxLength=48;name.placeholder='Use the chosen action’s name';name.setAttribute('aria-label','Quick action '+(index+1)+' name');name.disabled=busy||!initialized;
  name.oninput=()=>{entry.name=name.value;markSlotChanges('Quick action changes not saved.')};nameField.append(name);
  const action=element('div','','playlist-action'),selected=ha.items.find(i=>i.entity===entry.action);
  action.append(element('span','Runs','field-caption'),element('strong',entry.action?(selected?.name||entry.action):'No action selected'));
  if(entry.action)action.append(element('small',entry.action));
  const choose=element('button',entry.action?'Change action':'Choose action');choose.type='button';choose.dataset.choosePlaylist=String(index);
  choose.setAttribute('aria-label','Choose action for quick action '+(index+1));choose.disabled=busy||!initialized;choose.onclick=()=>selectMode('playlist',index);action.append(choose);
  fields.append(nameField,action);row.append(fields);$('playlist-entries').append(row);
 });
 $('add-playlist').disabled=busy||!initialized||s.playlists.length>=8;
}
$('add-playlist').onclick=()=>selectMode('playlist',slots[active].playlists.length);
function isActionPicker(){return mode==='action'||mode==='playlist'||mode==='gesture'}
function selectedPickerEntity(){const s=slots[active];return mode==='gesture'?s.gestures[gestureTarget]:mode==='gestureFeedback'?s.gestures.feedback:mode==='playlist'?s.playlists[playlistTarget]?.action:mode==='secondary'?s.secondary:mode==='action'?s.action:s.entity}
function closePicker(){if($('entity-picker').open)$('entity-picker').close()}
$('close-picker').onclick=closePicker;
$('entity-picker').addEventListener('click',event=>{if(event.target!==$('entity-picker'))return;const r=event.target.getBoundingClientRect();if(event.clientX<r.left||event.clientX>r.right||event.clientY<r.top||event.clientY>r.bottom)closePicker()});
function slotIcon(slot){
 const holder=element('span','','slot-icon');holder.setAttribute('aria-hidden','true');
 const domain=slot.entity.split('.')[0];
 const paths={
  sensor:'M3 3v18h18v-2H5V3H3m4 12 4-4 4 3 5-7-2-1-4 5-3-3-5 5 1 2Z',
  media_player:'M3 4v14h7v2h4v-2h7V4H3m2 2h14v10H5V6m4 2v6l6-3-6-3Z',
  weather:'M6 10a6 6 0 0 1 11-3 5 5 0 0 1 0 10H6a4 4 0 0 1 0-8v1m0 2a2 2 0 0 0 0 4h11a3 3 0 0 0 0-6h-1l-1-2a4 4 0 0 0-7 3v1H6Z',
  climate:'M10 3a3 3 0 0 1 6 0v10a5 5 0 1 1-6 0V3m2 0v11l-1 1a3 3 0 1 0 4 0l-1-1V3a1 1 0 0 0-2 0Z',
  timer:'M9 1h6v2H9V1m3 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18m0 2a7 7 0 1 1 0 14 7 7 0 0 1 0-14m-1 2h2v6h-2V8Z'
 };
 const fallback=svgNode('svg',{viewBox:'0 0 24 24',focusable:'false'});
 fallback.append(svgNode('path',{d:paths[domain]||'M4 4h6v6H4V4m10 0h6v6h-6V4M4 14h6v6H4v-6m10 0h6v6h-6v-6Z'}));holder.append(fallback);
 const defaults={sensor:'chart-line',binary_sensor:'checkbox-marked-circle-outline',media_player:'music-note',weather:'weather-partly-cloudy',climate:'thermostat',timer:'timer-outline',light:'lightbulb',switch:'toggle-switch',cover:'window-shutter',fan:'fan',script:'script-text',automation:'robot',number:'numeric',input_number:'numeric',select:'format-list-bulleted',input_select:'format-list-bulleted',button:'gesture-tap-button',input_button:'gesture-tap-button',scene:'palette'};
 const icon=slot.icon||ha.items.find(item=>item.entity===slot.entity)?.icon||'mdi:'+(defaults[domain]||'view-grid-outline');
 if(slot.entity && /^mdi:[a-z0-9-]{1,76}$/.test(icon)){
  const img=document.createElement('img');img.alt='';img.width=20;img.height=20;img.hidden=true;img.decoding='async';
  img.onload=()=>{fallback.style.display='none';img.hidden=false};img.onerror=()=>img.remove();
  img.src='https://api.iconify.design/mdi/'+icon.slice(4)+'.svg?color=%2394a3b8';holder.append(img);
 }
 return holder;
}
// Undo: snapshots of the unsaved slots and pages, taken whenever a render
// finds them changed. Rapid edits (typing) collapse into one step.
let undoStack=[],undoKey=null,undoAt=0,undoTarget;
const undoState=()=>JSON.stringify({s:slots,p:globalThis.ONX_PAGES?.get?.()??null});
function trackUndo(){if(!initialized)return;const key=undoState();if(undoTarget!==editTarget||undoKey===null){undoTarget=editTarget;undoStack=[];undoKey=key;return}if(key===undoKey)return;const now=Date.now();if(!(now-undoAt<700&&undoStack.length))undoStack.push(undoKey);if(undoStack.length>50)undoStack.shift();undoAt=now;undoKey=key}
function undo(){if(busy)return;const prev=undoStack.pop();if(!prev)return;const v=JSON.parse(prev);slots=v.s;if(v.p)globalThis.ONX_PAGES?.restore?.(v.p);undoKey=prev;undoAt=0;changed();search();message(undoStack.length?'Undone. Undo again to go further back.':'Undone.')}
function render(){trackUndo();
 const icon=slots[active].icon;$('icon-hint').textContent=!slots[active].entity?'':icon?'Home Assistant icon: '+icon:'Using the entity default. Sign in and refresh the entity list to sync Home Assistant icons.';
 visibleSlots=Math.max(visibleSlots,active+1,slots.reduce((last,s,i)=>s.entity?i+1:last,1));
 $('add-slot').disabled=busy||!initialized||visibleSlots>=MAX_SLOTS;
 if(!globalThis.ONX_PANEL)$('slot-capacity').textContent=slots.filter(s=>s.entity).length+' configured';
 if(!globalThis.ONX_PANEL)$('slots').replaceChildren(...slots.slice(0,visibleSlots).map((s,i)=>{const b=element('button','');b.append(element('b',String(i+1)),slotIcon(s),element('span',s.name||s.entity||'Empty slot'));b.title=s.name||s.entity||'Empty slot';if(dirtySlot(i))b.append(element('span','●','dirty-dot'));b.setAttribute('aria-label','Slot '+(i+1)+': '+(s.name||s.entity||'Empty')+(dirtySlot(i)?', unsaved':''));b.setAttribute('aria-current',String(i===active));b.disabled=busy;b.onclick=()=>{active=i;slotTab='display';playlistTarget=-1;render();search();refreshPreview()};return b}));
 const s=slots[active],restricted=nativePress(s.entity),count=slots.filter((_,i)=>dirtySlot(i)).length;
 $('slot-title').textContent='Slot '+(active+1)+(editTarget?' · '+editTarget.name:'');
 $('action-binding').querySelector('h3').textContent=s.entity.startsWith('media_player.')?'When idle':'When pressed';
 renderEditorTabs();renderPlaylists();renderGestures();renderPresetCard();
 const switchPower=s.entity.startsWith('switch.');$('sensor-options').hidden=!(s.entity.startsWith('sensor.')||switchPower);$('secondary-heading').textContent=switchPower?'Power & energy':'Secondary reading';$('choose-secondary').textContent=switchPower?'Choose power or energy sensor':'Choose secondary entity';$('secondary-help').textContent=switchPower?'Choose a power sensor in W or energy sensor in kWh. Its reading appears beside On/Off on the main view; opening the switch shows its 12-hour graph.':'Replaces the sensor name below the value. Use {value}, {unit}, {relative}, {time}, {date}, or {datetime}.';$('sensor-presentation').closest('label').hidden=switchPower;$('secondary-text').closest('label').hidden=switchPower;$('secondary-entity').value=s.secondary;$('secondary-text').value=s.secondaryText;$('sensor-presentation').value=s.sensorGraph;
 $('cover-options').hidden=!s.entity.startsWith('cover.');
 $('cover-presets-enabled').checked=s.coverPresetsEnabled!=='0';
 if(document.activeElement!==$('cover-presets'))$('cover-presets').value=s.coverPresets??(s.entity.startsWith('cover.')?'15,25,50,75':'');
 $('cover-presets').disabled=busy||s.coverPresetsEnabled==='0';
 $('entity-name').textContent=s.name||s.entity||'Empty slot';$('entity-id').textContent=s.entity;$('display-name').value=s.name;
 $('action-name').textContent=s.actionName||actionLabel(s.action);$('action-id').textContent=s.action&&s.action!=='none'?s.action:s.action==='none'?'Ignore a short press':'Use the entity’s normal controls';
 if(s.entity)$('action-id').textContent=s.action==='open_playlists'?(s.playlists.length?'Open the quick actions list':'Add entries in the Quick actions tab first'):s.action&&s.action!=='none'?s.action:s.action==='none'?'Do nothing when idle':'Choose an automation or script to run when nothing is playing';
 if(restricted&&s.action)$('action-id').textContent='This saved action is not used by the device. Clear it with Default.';
 $('open-playlists').hidden=!s.entity;$('open-playlists').disabled=busy||!s.entity;
 $('gesture-pressResponses').disabled=busy||restricted||!/^(script|automation)\./.test(s.action);
 $('action-limit').hidden=!restricted;$('choose-action').disabled=busy||restricted||!s.entity;$('none').disabled=busy||restricted||!s.entity;$('default').disabled=busy||!s.entity;
 saveStates();
 $('move-up').disabled=busy||active===0;$('move-down').disabled=busy||active===MAX_SLOTS-1;$('duplicate').disabled=busy||!s.entity||!slots.some(s=>!s.entity);
 for(const id of ['choose-secondary','secondary-entity','secondary-text','sensor-presentation','choose-entity','clear','display-name','import','export','ha-connect','ha-disconnect','discard-settings'])$(id).disabled=busy||!initialized;
 $('save-settings').disabled=busy||!initialized||!dirtySettings();
 $('display-binding').classList.toggle('active',slotTab==='display');$('action-binding').classList.toggle('active',slotTab==='action');$('action-hint').hidden=!isActionPicker();$('search-title').textContent=isActionPicker()?'Automations & scripts':mode==='secondary'?'Secondary entities':'Available entities';$('picker-title').textContent=mode==='gesture'?'Choose a gesture action':mode==='gestureFeedback'?'Choose a feedback entity':mode==='playlist'?'Choose a quick action':mode==='action'?'Choose a press action':mode==='secondary'?'Choose a secondary entity':mode==='page'?'Place an entity on this page':'Choose a display entity';$('picker-context').textContent=mode==='playlist'?(slots[active].playlists[playlistTarget]?.name||'Playlist '+(playlistTarget+1)):mode==='page'?'':slots[active].name||slots[active].entity;
 $('filters').replaceChildren(...(isActionPicker()?[['actions','All actions'],['automation','Automations'],['script','Scripts']]:domains).map(([value,title])=>{const b=element('button',title);b.setAttribute('aria-pressed',String(value===domain));b.onclick=()=>{domain=value;render();search()};return b}));if(globalThis.ONX_PANEL_EDITOR){globalThis.ONX_PANEL_EDITOR(slots,active,busy,()=>drawPreview(slots[active]),editTarget);renderPresetCard();saveStates()}preview();
}
function changed(){if(recovery){recovery=null;$('draft-notice').hidden=true}persistDraft();render();message('Changes not saved. Review and save when ready.')}
function selectMode(value,index=-1){
 if(busy||!initialized)return;mode=value;playlistTarget=value==='playlist'?index:-1;domain=isActionPicker()?'actions':'';
 $('search').value='';$('room').value='';render();search();
 if(!$('entity-picker').open)$('entity-picker').showModal();$('search').focus();
}
function choose(item){
 if(busy)return;
 if(mode==='page'){const pick=pagePick;pagePick=null;mode='entity';closePicker();search();pick?.done(item);return}const s=slots[active];
 if(isActionPicker()&&!/^(automation|script)\./.test(item.entity))return;
 const target=playlistTarget,chosenMode=mode;
 if(mode==='gesture')s.gestures[gestureTarget]=item.entity;
 else if(mode==='gestureFeedback')s.gestures.feedback=item.entity;
 else if(mode==='playlist'){
  if(target<0||target>s.playlists.length||target>=8)return;
  if(target===s.playlists.length)s.playlists.push({name:'',action:''});
  const entry=s.playlists[target],old=ha.items.find(i=>i.entity===entry.action);
  if(!entry.name.trim()||entry.name===shortPlaylistName(old?.name||entry.action))entry.name=shortPlaylistName(item.name||item.entity);
  entry.action=item.entity;slotTab='playlists';
 }else if(mode==='secondary'){s.secondary=item.entity;if(s.entity.startsWith('switch.'))s.sensorGraph='1';}
 else if(mode==='entity'){
  if(s.entity!==item.entity){s.action='';s.actionName='';s.secondary='';s.secondaryText='{value}';s.sensorGraph='0';s.timerHidden='0';s.gestures=emptyGestures();s.playlists=[];s.typeOption='';s.coverPresets=item.entity.startsWith('cover.')?'15,25,50,75':'';s.coverPresetsEnabled='1'}
  Object.assign(s,{entity:item.entity,name:item.name||item.entity,area:item.area||'',icon:item.icon||'',iconOverride:item.iconOverride||'0'});slotTab='display';
 }else{if(!s.entity||nativePress(s.entity))return;s.action=item.entity;s.actionName=item.name||actionLabel(item.entity)}
 closePicker();mode='entity';playlistTarget=-1;changed();search();
 if(chosenMode==='playlist')document.querySelector('[data-choose-playlist="'+target+'"]')?.focus();
 else if(chosenMode==='gesture'||chosenMode==='gestureFeedback')document.querySelector('[data-gesture-choose="'+(chosenMode==='gestureFeedback'?'feedback':gestureTarget)+'"]')?.focus();
 else $(chosenMode==='action'?'choose-action':chosenMode==='secondary'?'choose-secondary':'choose-entity').focus();
}
function isManualEntity(value){return /^[a-z0-9_]+\.[a-z0-9_]+$/.test(value)&&value.length<=128&&/^(sensor|binary_sensor|light|switch|climate|media_player|timer|weather|cover|automation|script|fan|number|input_number|select|input_select|scene|button|input_button|siren|event)\./.test(value)&&mode!=='playlist'&&(!isActionPicker()||/^(automation|script)\./.test(value))}
function search(append=false){const mine=++revision,offset=append?next:0,query=$('search').value.trim();if(!append)$('results').replaceChildren();$('manual').replaceChildren();$('more').hidden=true;
 const available=mode==='page'?ha.items.filter(i=>!pagePick||pagePick.accept(i.entity)):isActionPicker()?ha.items.filter(i=>/^(automation|script)\./.test(i.entity)):mode==='secondary'&&slots[active].entity.startsWith('switch.')?ha.items.filter(i=>i.entity.startsWith('sensor.')&&(i.unit===undefined||['W','kWh'].includes(i.unit))):ha.items;const items=$('room').value?available.filter(i=>i.area===$('room').value):available,data=OnxHA.search(items,query,domain,offset);if(mine!==revision)return;$('count').textContent=data.total+' matches';
 if(!data.items.length&&!append){$('results').append(element('p',ha.ready?'No matches. Try another name, room, or entity type.':mode==='playlist'?'Sign in on the Home Assistant page to choose a script or automation.':'Sign in on the Home Assistant page to browse entities, or enter a full entity ID.','empty'));if(!ha.ready){const b=element('button','Open Home Assistant setup');b.onclick=()=>{closePicker();page('connection')};$('results').append(b)}}
 for(const item of data.items){const row=element('button','','result'),text=element('span','');text.append(element('strong',item.name||item.entity),element('small',item.entity));row.append(text,element('em',item.area||item.entity.split('.')[0]));row.classList.toggle('selected',selectedPickerEntity()===item.entity);row.disabled=busy||(mode==='action'&&(!slots[active].entity||nativePress(slots[active].entity)));row.onclick=()=>choose(item);row.draggable=true;row.ondragstart=e=>{e.dataTransfer.setData('application/x-onx-entity',JSON.stringify(item));e.dataTransfer.effectAllowed='copy';document.body.classList.add('dragging-entity')};$('results').append(row)}
 next=data.next;$('more').hidden=next<0;if(isManualEntity(query)&&!ha.items.some(i=>i.entity===query)){const b=element('button','Use entity ID: '+query);b.disabled=busy;b.onclick=()=>choose({entity:query,name:query});$('manual').append(b)}
}

// Searchable select (shadcn-style combobox). The hidden <select> keeps the value.
function combo(select){
 const box=select.parentElement;if(box.dataset.ready){comboLabel(select);return}box.dataset.ready='1';
 const trigger=element('button','');trigger.type='button';trigger.className='combo-trigger';trigger.setAttribute('aria-haspopup','listbox');trigger.setAttribute('aria-labelledby',select.getAttribute('aria-labelledby'));trigger.append(element('span',''));
 const pop=element('div','','combo-pop');pop.hidden=true;const input=element('input','');input.className='combo-search';input.placeholder='Search sensors…';input.setAttribute('aria-label','Search');const list=element('ul','','combo-list');list.setAttribute('role','listbox');pop.append(input,list);box.append(trigger,pop);
 let shown=[],hl=0;
 const close=()=>{pop.hidden=true;trigger.setAttribute('aria-expanded','false')};
 const pick=o=>{select.value=o.value;comboLabel(select);close();trigger.focus();select.dispatchEvent(new Event('input',{bubbles:true}))};
 const draw=()=>{const q=input.value.trim().toLowerCase();shown=[...select.options].filter(o=>!q||(o.textContent+' '+o.value).toLowerCase().includes(q)).slice(0,200);hl=Math.min(hl,Math.max(0,shown.length-1));list.replaceChildren(...shown.map((o,i)=>{const [name,id]=o.textContent.split(' · ');const li=element('li',name);li.setAttribute('role','option');li.setAttribute('aria-selected',String(o.value===select.value));if(i===hl)li.classList.add('hl');if(id)li.append(element('small',id));li.onmousedown=e=>{e.preventDefault();pick(o)};return li}));if(!shown.length)list.append(element('li','No sensors found.','combo-empty'));list.querySelector('.hl')?.scrollIntoView({block:'nearest'})};
 trigger.onclick=()=>{if(!pop.hidden){close();return}document.querySelectorAll('.combo-pop').forEach(p=>p.hidden=true);pop.hidden=false;trigger.setAttribute('aria-expanded','true');input.value='';hl=Math.max(0,[...select.options].findIndex(o=>o.value===select.value));draw();input.focus()};
 input.oninput=()=>{hl=0;draw()};
 input.onkeydown=e=>{if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();hl=Math.max(0,Math.min(shown.length-1,hl+(e.key==='ArrowDown'?1:-1)));draw()}else if(e.key==='Enter'){e.preventDefault();if(shown[hl])pick(shown[hl])}else if(e.key==='Escape'){e.preventDefault();close();trigger.focus()}};
 input.onblur=()=>setTimeout(()=>{if(!box.contains(document.activeElement))close()},100);
 comboLabel(select);
}
function comboLabel(select){const t=select.parentElement.querySelector('.combo-trigger span');if(t){const o=select.selectedOptions[0];t.textContent=o?o.textContent.split(' · ')[0]:'Choose a sensor';t.title=o?.value||''}const b=select.parentElement.querySelector('.combo-trigger');if(b)b.disabled=select.disabled}
function weatherOptions(){const value=settings.weather||'',items=ha.items.filter(i=>i.entity.startsWith('weather.'));if(value&&!items.some(i=>i.entity===value))items.push({entity:value,name:value});$('standby-weather').replaceChildren(...[{entity:'',name:'None'},...items].map(item=>{const o=element('option',item.name||item.entity);o.value=item.entity;return o}));$('standby-weather').value=value;for(const [id,key,cls] of [['clock-temperature','clock_temperature','temperature'],['clock-humidity','clock_humidity','humidity']]){const current=settings[key]||'',sensors=ha.items.filter(i=>i.entity.startsWith('sensor.')).sort((a,b)=>(a.name||a.entity).localeCompare(b.name||b.entity));if(current&&!sensors.some(i=>i.entity===current))sensors.unshift({entity:current,name:current});$(id).replaceChildren(...[{entity:'',name:'Weather '+cls+' (default)'},...sensors].map(item=>{const o=element('option',item.name&&item.name!==item.entity?item.name+' · '+item.entity:item.entity||item.name);o.value=item.entity;return o}));$(id).value=current;combo($(id))}}
function colorPicker(){const bg=(settings.background_color||'#101418').toUpperCase();$('background-color').value=bg;$('bg-value').textContent=bg;let preset=false;document.querySelectorAll('[data-bg-color]').forEach(b=>{const on=b.dataset.bgColor===bg;preset||=on;b.setAttribute('aria-pressed',String(on))});document.querySelector('.bg-custom').toggleAttribute('data-active',!preset);const color=settings.media_color||'#62B0FF';$('media-color').value=color;$('color-value').textContent=color;$('ring-sample').style.setProperty('--ring-color',color);document.querySelectorAll('[data-ring-color]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.ringColor===color)))}
function sleepFields(){for(const key of ['sleep-start','sleep-end','sleep-timeout'])$(key).disabled=settings.sleep_enabled!=='1'}
function layoutParts(){let v=0;for(const c of document.querySelectorAll('#page-photos input[data-part]'))if(c.checked)v|=+c.dataset.part;return v}
function clockLayout(value){const v=value===undefined||value===''?15:+value;for(const c of document.querySelectorAll('#page-photos input[data-part]'))c.checked=!!(v&+c.dataset.part);for(const b of document.querySelectorAll('.layout-presets button'))b.setAttribute('aria-pressed',String(+b.dataset.layout===v));const shown=[1,2,4,8].filter(p=>v&p).length;for(const s of document.querySelectorAll('#clock-preview [data-part]'))s.hidden=!(v&+s.dataset.part);$('clock-preview').style.setProperty('--pv-clock',(46-shown*3)+'px')}
for(const b of document.querySelectorAll('.layout-presets button'))b.onclick=()=>{clockLayout(b.dataset.layout);$('settings-form').dispatchEvent(new Event('input'))};
function settingsForm(){colorPicker();$('hold-cue').checked=settings.hold_cue!=='0';$('assist-mute-media').checked=settings.assist_mute_media==='1';$('notifications-cover-timers').checked=settings.notifications_cover_timers!=='0';$('orientation').value=settings.orientation||'0';$('date-format').value=settings.date_format||'dd-mm';$('hour-format').value=settings.hour_format||'24';$('show-ampm').checked=settings.show_ampm!=='0';$('show-ampm').disabled=settings.hour_format!=='12';$('standby-scope').value=settings.standby_scope||'main';$('idle-view').value=settings.idle_view||'clock';clockLayout(settings.clock_layout);for(const key of ['brightness','standby','sensitivity'])$(key).value=settings[key];$('sleep-enabled').checked=settings.sleep_enabled==='1';$('sleep-start').value=settings.sleep_start;$('sleep-end').value=settings.sleep_end;$('sleep-timeout').value=settings.sleep_timeout;sleepFields();weatherOptions()}
function roomOptions(){const value=$('room').value;$('room').replaceChildren(...['',...new Set(ha.items.map(i=>i.area).filter(Boolean).sort())].map(room=>{const o=element('option',room||'All rooms');o.value=room;return o}));$('room').value=value;if(!$('room').value)$('room').value=''}
function validatePlaylists(entries,entity){
 if(!Array.isArray(entries)||entries.length>8||entries.length&&!entity)throw Error('Quick actions need an entity and allow at most eight entries.');
 return entries.map(entry=>{if(!entry||typeof entry.name!=='string'||!entry.name.trim()||/[\r\n]/.test(entry.name)||new TextEncoder().encode(entry.name).length>48||typeof entry.action!=='string'||entry.action.length>128||!/^(automation|script)\.[a-z0-9_]+$/.test(entry.action))throw Error('Each quick action needs a name (up to 48 UTF-8 bytes) and a script or automation entity ID.');return {name:entry.name,action:entry.action}});
}
function validateGestures(input){
 const value={...emptyGestures(),...(input||{})},result={};
 for(const key of ['cw','ccw','holdCw','holdCcw']){const action=value[key];if(typeof action!=='string'||action.length>128||action&&!/^(none|(?:automation|script)\.[a-z0-9_]+)$/.test(action))throw Error('Choose a script, automation, Default or No action for each gesture.');result[key]=action}
 if(typeof value.feedback!=='string'||value.feedback.length>128||value.feedback&&!/^[a-z_]+\.[a-z0-9_]+$/.test(value.feedback))throw Error('Choose a valid feedback entity.');result.feedback=value.feedback;
 for(const key of ['popup','responses','events','pressResponses']){if(!['0','1'].includes(value[key]))throw Error('Invalid gesture feedback option.');result[key]=value[key]}
 if(typeof value.step!=='string'||!/^\d{1,3}$/.test(value.step)||+value.step<1||+value.step>100)throw Error('Gesture step must be 1–100.');result.step=value.step;return result;
}
function renderGestures(){
 const gesture=slots[active].gestures;
 for(const key of ['cw','ccw','holdCw','holdCcw']){
  $('gesture-'+key).value=gesture[key];$('gesture-'+key).disabled=busy||!initialized||!slots[active].entity;
 }
 $('gesture-feedback').value=gesture.feedback;$('gesture-step').value=gesture.step;
 for(const key of ['popup','responses','events','pressResponses'])$('gesture-'+key).checked=gesture[key]==='1';
 for(const node of document.querySelectorAll('[data-gesture-choose],[data-gesture-reset],#gesture-feedback,#gesture-step,#gesture-popup,#gesture-responses,#gesture-pressResponses,#gesture-events'))node.disabled=busy||!initialized||!slots[active].entity;
}
for(const key of ['cw','ccw','holdCw','holdCcw']){
 $('gesture-'+key).onchange=()=>{slots[active].gestures[key]=$('gesture-'+key).value.trim();changed()};
}
for(const button of document.querySelectorAll('[data-gesture-choose]'))button.onclick=()=>{gestureTarget=button.dataset.gestureChoose;selectMode(gestureTarget==='feedback'?'gestureFeedback':'gesture')};
for(const button of document.querySelectorAll('[data-gesture-reset]'))button.onclick=()=>{slots[active].gestures[button.dataset.gestureReset]='';changed()};
$('gesture-feedback').onchange=()=>{slots[active].gestures.feedback=$('gesture-feedback').value.trim();changed()};
$('gesture-step').onchange=()=>{slots[active].gestures.step=$('gesture-step').value;changed()};
for(const key of ['popup','responses','events','pressResponses'])$('gesture-'+key).onchange=()=>{slots[active].gestures[key]=$('gesture-'+key).checked?'1':'0';changed()};
const retiredPanelGpio=value=>/^\[(?:Local\] HMI|Preset\] Local) IO(?:10|11|17|18)$/.test(value);
function validateCoverPresets(value){
 const text=String(value??'').trim();if(!text)return '';
 const parts=text.split(',').map(v=>v.trim());
 if(parts.length>4||parts.some(v=>!/^\d{1,3}$/.test(v)||Number(v)>100)||new Set(parts.map(Number)).size!==parts.length)
  throw Error('Use up to four different cover positions from 0 to 100, separated by commas.');
 return parts.map(Number).join(',');
}
function trimSlots(list){let n=list.length;while(n>1&&!list[n-1]?.entity)n--;return list.slice(0,n)}
function validateSlots(input){if(!Array.isArray(input)||input.length>MAX_SLOTS)throw Error('A configuration can contain up to '+MAX_SLOTS+' entries.');input=[...input,...Array.from({length:MAX_SLOTS-input.length},empty)];return input.map(s=>{if(!s||typeof s!=='object')throw Error('Invalid slot in configuration.');if(retiredPanelGpio(s.entity))return empty();const result=empty();for(const key of Object.keys(result)){if(key==='playlists'||key==='gestures')continue;const value=s[key]??(key==='coverPresetsEnabled'?'1':key==='coverPresets'&&s.entity?.startsWith('cover.')?'15,25,50,75':['iconOverride','sensorGraph','timerHidden'].includes(key)?'0':key==='secondaryText'?'{value}':'');if(typeof value!=='string'||value.length>(['entity','action','secondary'].includes(key)?128:key==='secondaryText'?160:key==='typeOption'?32:192)||new TextEncoder().encode(value).length>(['entity','action','secondary'].includes(key)?128:key==='secondaryText'?160:key==='typeOption'?32:192))throw Error('Invalid or oversized slot field: '+key);result[key]=value}if(!['0','1'].includes(result.timerHidden))throw Error('Invalid timer visibility.');result.coverPresets=validateCoverPresets(result.coverPresets);if(!['0','1'].includes(result.coverPresetsEnabled)||result.coverPresets&&!result.entity.startsWith('cover.'))throw Error('Invalid cover position presets.');result.gestures=validateGestures(s.gestures);if(result.entity.startsWith('switch.')&&result.secondary){const reading=ha.items.find(i=>i.entity===result.secondary);if(!result.secondary.startsWith('sensor.')||reading?.unit&&!['W','kWh'].includes(reading.unit))throw Error('Choose a power sensor in W or energy sensor in kWh.');result.sensorGraph='1';}result.playlists=validatePlaylists(s.playlists??[],result.entity);if(result.secondary&&!/^[a-z_]+\.[a-z0-9_]+$/.test(result.secondary)||!['0','1'].includes(result.sensorGraph))throw Error('Choose a valid secondary entity and sensor display.');if(result.icon&&!/^mdi:[a-z0-9-]{1,76}$/.test(result.icon)||!['0','1'].includes(result.iconOverride))throw Error('Invalid entity icon.');if(result.entity&&!isSupported(result.entity))throw Error('Unsupported entity: '+result.entity);if(result.action&&!(result.action==='open_playlists'&&result.entity)&&!/^(none|(?:automation|script)\.[a-z0-9_]+)$/.test(result.action))throw Error('Unsupported press action.');if(!result.entity&&result.action)throw Error('An empty slot cannot have a press action.');if(result.typeOption&&!choices(result).includes(result.typeOption))throw Error('Display type does not match its entity.');return result})}
function isSupported(value){return (globalThis.ONX_PANEL&&['[Local] Dashboard','[Local] Assist'].includes(value))||/^(sensor|binary_sensor|light|switch|climate|media_player|timer|weather|cover|automation|script|fan|number|input_number|select|input_select|scene|button|input_button|siren|event)\.[a-z0-9_]+$/.test(value)}
function validateSettings(value){if(!value||typeof value!=='object')throw Error('Device settings are missing.');value={clock_seconds_ring:'0',hold_cue:'1',assist_mute_media:'0',clock_temperature:'',clock_humidity:'',clock_layout:'15',notifications_cover_timers:'1',orientation:'0',idle_view:'clock',media_color:'#62B0FF',background_color:'#101418',standby_scope:'main',date_format:'dd-mm',hour_format:'24',show_ampm:'1',...value};const result={};for(const key of ['clock_seconds_ring','hold_cue','assist_mute_media','clock_temperature','clock_humidity','clock_layout','notifications_cover_timers','orientation','idle_view','show_ampm','date_format','hour_format','media_color','background_color','standby_scope','brightness','standby','sensitivity','sleep_enabled','sleep_start','sleep_end','sleep_timeout','weather']){if(typeof value[key]!=='string')throw Error('Invalid device setting: '+key);result[key]=value[key]}if(!['0','1'].includes(result.clock_seconds_ring))throw Error('Choose a valid clock seconds option.');if(!['0','1'].includes(result.assist_mute_media))throw Error('Choose a valid Assist media mute option.');if(!/^\d{1,2}$/.test(result.clock_layout)||+result.clock_layout>15)throw Error('Choose what the clock screen shows.');if(!['0','1'].includes(result.notifications_cover_timers))throw Error('Choose a valid notification priority.');if(!['0','90','180','270'].includes(result.orientation))throw Error('Choose a hardware display orientation.');if(!['clock','media'].includes(result.idle_view))throw Error('Choose a valid idle screen.');if(!/^#[0-9a-f]{6}$/i.test(result.background_color))throw Error('Choose a valid panel background color.');result.background_color=result.background_color.toUpperCase();if(!/^#[0-9a-f]{6}$/i.test(result.media_color)||!['main','all','never'].includes(result.standby_scope))throw Error('Choose a valid progress color and weather timeout behavior.');if(!['0','1'].includes(result.show_ampm)||!['dd-mm','mm-dd','yyyy-mm-dd','dd-mmm','mmm-dd','yyyy-mmm-dd'].includes(result.date_format)||!['12','24'].includes(result.hour_format))throw Error('Choose a valid date and clock format.');result.media_color=result.media_color.toUpperCase();for(const [key,min,max] of [['brightness',10,100],['standby',1,900],['sleep_timeout',1,3600]])if(!/^\d+$/.test(result[key])||+result[key]<min||+result[key]>max)throw Error('Invalid value for '+key);if(!['1','2','4'].includes(result.sensitivity)||!['0','1'].includes(result.sleep_enabled)||!/^([01]\d|2[0-3]):[0-5]\d$/.test(result.sleep_start)||!/^([01]\d|2[0-3]):[0-5]\d$/.test(result.sleep_end)||result.weather&&!/^weather\.[a-z0-9_]+$/.test(result.weather)||!['0','1'].includes(result.hold_cue)||[result.clock_temperature,result.clock_humidity].some(v=>v&&!/^sensor\.[a-z0-9_]+$/.test(v)))throw Error('Invalid sleep, sensitivity, or weather setting.');return result}
const singlePages=list=>list.filter(s=>s.entity&&s.timerHidden!=='1').map(s=>({name:'',icon:'',layout:'single',entities:[s.entity]}));
function validatePages(input,list){
 if(input===undefined||input===null)return singlePages(list);
 if(!Array.isArray(input)||input.length>MAX_SLOTS)throw Error('Invalid page list.');
 const have=new Set(list.map(s=>s.entity).filter(Boolean)),used=new Set(),leads=new Set(),sizes={single:1,two_controls:2,control_sensors:4};
 return input.filter(p=>!Array.isArray(p?.entities)||!p.entities.some(retiredPanelGpio)).map(p=>{
  const grid=p?.layout==='action_grid',rows=p?.rows,columns=p?.columns,stack=p?.layout==='stack'?p.stack:null,cells={full:1,split:2,compact:1,chips:3};
  if(grid&&![rows,columns].every(n=>Number.isInteger(n)&&n>=1&&n<=4))throw Error('Choose 1–4 rows and columns.');
  if(p?.layout==='stack'&&(!Array.isArray(stack)||stack.length<1||stack.length>6||!stack.every(r=>Object.hasOwn(cells,r))||stack.reduce((n,r)=>n+cells[r],0)>16))throw Error('A stack has 1–6 rows of full, split, compact or chips.');
  const n=grid?rows*columns:stack?stack.reduce((n,r)=>n+cells[r],0):(Object.hasOwn(sizes,p?.layout)?sizes[p.layout]:0);
  if(!n||!Array.isArray(p.entities)||p.entities.length!==n||typeof (p.name??'')!=='string'||typeof (p.icon??'')!=='string')throw Error('Invalid page in configuration.');
  const assigned=grid||stack?p.entities.filter(e=>e!==''):p.entities,first=assigned[0];
  if(!first||new Set(assigned).size!==assigned.length||assigned.some(e=>!have.has(e))||leads.has(first))throw Error('A page uses a missing entity, repeats one, or shares its leading entity.');
  leads.add(first);for(const e of assigned)used.add(e);
  const artwork=p.artwork,carousel=p.carousel_artwork,iconOnly=p.icon_only;
  if(iconOnly!==undefined&&(!grid||!Array.isArray(iconOnly)||iconOnly.length!==n||
   iconOnly.some((v,i)=>typeof v!=='boolean'||(v&&!p.entities[i]))))throw Error('Invalid icon-only key settings.');
  if(carousel!==undefined&&(typeof carousel!=='string'||!/^[0-9a-f]{14}$/.test(carousel)))throw Error('Invalid carousel image reference.');
  if(artwork!==undefined&&(!Array.isArray(artwork)||artwork.length!==n||
   artwork.some((id,i)=>typeof id!=='string'||(id&&(!/^[0-9a-f]{14}$/.test(id)||!p.entities[i])))))throw Error('Invalid page image references.');
  return {name:(p.name||'').slice(0,48),icon:p.icon||'',layout:p.layout,...(grid?{rows,columns}:{}),...(stack?{stack:[...stack]}:{}),entities:[...p.entities],...(artwork?.some(Boolean)?{artwork:[...artwork]}:{}),...(carousel?{carousel_artwork:carousel}:{}),...(iconOnly?.some(Boolean)?{icon_only:[...iconOnly]}:{})};
 }).concat(singlePages(list.filter(s=>s.entity&&!used.has(s.entity))));
}
function validateDocument(value){if(![1,2].includes(value?.schema))throw Error('Unsupported configuration version.');const slots=validateSlots(value.slots);return {slots,settings:validateSettings(value.settings),pages:validatePages(value.pages,slots)}}
async function saveSlots(indices){if(busy||!initialized)return;busy=true;work++;render();search();let completed=0;const pendingPreset=new Map();try{for(const i of indices){const snapshot=copy(slots[i]),item=ha.items.find(item=>item.entity===snapshot.entity);if(item&&Object.hasOwn(item,'icon')){snapshot.icon=item.icon||'';snapshot.iconOverride=item.iconOverride||'0';slots[i].icon=snapshot.icon;slots[i].iconOverride=snapshot.iconOverride;}validateSlots([snapshot,...Array.from({length:MAX_SLOTS-1},empty)]);if(nativePress(snapshot.entity)&&snapshot.action)throw Error('Choose Default for the press action of '+(snapshot.name||snapshot.entity)+'.');if(item?.types&&snapshot.typeOption&&!item.types.includes(snapshot.typeOption))throw Error((snapshot.name||snapshot.entity||'An entity')+' uses an unsupported control type. Choose a type detected by Home Assistant.');if(editTarget){pendingPreset.set(i,snapshot);continue}progress(completed,indices.length);message((indices.length>1?'Saving '+(completed+1)+' of '+indices.length+' · ':'Saving ')+(snapshot.entity?snapshot.name||snapshot.entity:'removed entry '+(i+1))+'…');await rpc(slotPayload(i,snapshot));saved[i]=snapshot;completed++;persistDraft()}if(editTarget){message('Saving preset “'+editTarget.name+'”…');await putPreset(editTarget.name,saved.map((s,i)=>pendingPreset.get(i)||s),editTarget.pages);for(const [i,snapshot] of pendingPreset)saved[i]=snapshot;completed=pendingPreset.size;const created=editTarget.isNew;editTarget.isNew=false;await loadPresets();message(created?'Preset “'+editTarget.name+'” created. The panel is unchanged.':'Preset “'+editTarget.name+'” saved. The panel is unchanged.');return true}message(completed+' entit'+(completed===1?'y':'ies')+' saved.');return true}catch(error){message(error.message+' '+completed+' saved; remaining changes are still marked unsaved.',true);return false}finally{busy=false;if(!--work)progress();persistDraft();render();search()}}
const dirtyIndices=()=>slots.map((_,i)=>i).filter(i=>dirtySlot(i)||editTarget?.isNew&&slots[i].entity);
$('save').onclick=()=>saveSlots([active]);$('save-all').onclick=()=>globalThis.ONX_PAGES?.save?globalThis.ONX_PAGES.save():saveSlots(dirtyIndices());
$('undo').onclick=undo;document.addEventListener('keydown',e=>{if((e.ctrlKey||e.metaKey)&&!e.shiftKey&&e.key.toLowerCase()==='z'&&!$('page-entities').hidden&&!e.target.closest('input,textarea,select,[contenteditable]')){e.preventDefault();undo()}});
$('discard').onclick=async()=>{if(!await ask({title:'Discard unsaved changes?',text:'Unsaved page and entity changes will be lost.',ok:'Discard changes',danger:true}))return;slots=copy(saved);globalThis.ONX_PAGES?.discard?.();changed();search();message('Unsaved changes discarded.')};
$('choose-secondary').onclick=()=>selectMode('secondary');
$('choose-entity').onclick=()=>selectMode('entity');$('choose-action').onclick=()=>selectMode('action');$('open-playlists').onclick=()=>{if(!slots[active].entity)return;slots[active].action='open_playlists';slots[active].actionName='';changed()};$('default').onclick=()=>{slots[active].action='';slots[active].actionName='';changed()};$('none').onclick=()=>{slots[active].action='none';slots[active].actionName='';changed()};$('clear').onclick=()=>{slots[active]=empty();changed();search()};
$('cover-presets').oninput=()=>{slots[active].coverPresets=$('cover-presets').value;try{validateCoverPresets(slots[active].coverPresets);$('cover-presets').setCustomValidity('')}catch(e){$('cover-presets').setCustomValidity(e.message)}changed()};
$('cover-presets-enabled').onchange=()=>{slots[active].coverPresetsEnabled=$('cover-presets-enabled').checked?'1':'0';changed()};
$('display-name').oninput=()=>{slots[active].name=$('display-name').value;persistDraft();saveStates();preview();message('Display name not saved.')};$('display-name').onchange=()=>render();
for(const field of ['secondary-entity','secondary-text','sensor-presentation'])$(field).oninput=()=>{slots[active].secondary=$('secondary-entity').value.trim();slots[active].secondaryText=$('secondary-text').value;slots[active].sensorGraph=slots[active].entity.startsWith('switch.')?(slots[active].secondary?'1':'0'):$('sensor-presentation').value;changed()};
function move(delta){if(busy)return;const target=active+delta;[slots[active],slots[target]]=[slots[target],slots[active]];active=target;changed();search()}
$('add-slot').onclick=()=>{if(busy||visibleSlots>=MAX_SLOTS)return;active=visibleSlots++;slotTab='display';render();search();refreshPreview()};
$('move-up').onclick=()=>move(-1);$('move-down').onclick=()=>move(1);$('duplicate').onclick=()=>{const target=slots.findIndex(s=>!s.entity);if(target<0||busy)return;slots[target]=copy(slots[active]);active=target;changed();search()};
$('search').oninput=()=>{clearTimeout(debounce);debounce=setTimeout(()=>search(),200)};$('room').onchange=()=>search();$('more').onclick=()=>search(true);
$('preview-screen').onchange=()=>drawPreview(slots[active]);
document.querySelectorAll('[data-bg-color]').forEach(button=>button.onclick=()=>{$('background-color').value=button.dataset.bgColor;$('settings-form').dispatchEvent(new Event('input',{bubbles:true}))});
document.querySelectorAll('[data-ring-color]').forEach(button=>button.onclick=()=>{$('media-color').value=button.dataset.ringColor;$('settings-form').dispatchEvent(new Event('input',{bubbles:true}))});
const settingsInput=()=>{if(!initialized)return;settings={clock_seconds_ring:'0',hold_cue:$('hold-cue').checked?'1':'0',assist_mute_media:$('assist-mute-media').checked?'1':'0',notifications_cover_timers:$('notifications-cover-timers').checked?'1':'0',orientation:$('orientation').value,idle_view:$('idle-view').value,show_ampm:$('show-ampm').checked?'1':'0',date_format:$('date-format').value,hour_format:$('hour-format').value,background_color:$('background-color').value.toUpperCase(),media_color:$('media-color').value.toUpperCase(),standby_scope:$('standby-scope').value,brightness:$('brightness').value,standby:$('standby').value,sensitivity:$('sensitivity').value,sleep_enabled:$('sleep-enabled').checked?'1':'0',sleep_start:$('sleep-start').value,sleep_end:$('sleep-end').value,sleep_timeout:$('sleep-timeout').value,weather:$('standby-weather').value,clock_temperature:$('clock-temperature').value,clock_humidity:$('clock-humidity').value,clock_layout:String(layoutParts())};clockLayout(settings.clock_layout);$('show-ampm').disabled=settings.hour_format!=='12';sleepFields();colorPicker();drawPreview(slots[active]);persistDraft();$('save-settings').disabled=busy||!dirtySettings();message('Device settings not saved.')};
$('settings-form').oninput=settingsInput;
$('discard-settings').onclick=()=>{settings=copy(savedSettings);settingsForm();persistDraft();render();message('Unsaved device settings discarded.')};
async function saveSettings(){if(busy||!initialized)return false;try{validateSettings(settings)}catch(error){message(error.message,true);return false}busy=true;render();const snapshot=copy(settings);try{message('Saving device settings…');await rpc({op:'settings',...snapshot,clock_temperature:snapshot.clock_temperature||'none',clock_humidity:snapshot.clock_humidity||'none'});savedSettings={...snapshot,weather:savedWeather};persistDraft();if(snapshot.weather!==savedWeather){const item=ha.items.find(i=>i.entity===snapshot.weather)||{};await rpc({op:'weather',entity:snapshot.weather,name:item.name||snapshot.weather,area:item.area||''});savedWeather=snapshot.weather}savedSettings=snapshot;message('Device settings saved.');return true}catch(error){message(error.message+' Review the settings and retry any unsaved changes.',true);return false}finally{busy=false;persistDraft();render()}}
$('settings-form').onsubmit=event=>{event.preventDefault();void saveSettings()};
async function syncIconMetadata(){
 let count=0;if(editTarget)return count;
 for(let i=0;i<saved.length;i++){
  const source=saved[i],item=ha.items.find(item=>item.entity===source.entity);
  if(!source.entity||!item||!Object.hasOwn(item,'icon'))continue;
  const icon=item.icon||'',iconOverride=item.iconOverride||'0';
  if(source.icon===icon&&source.iconOverride===iconOverride)continue;
  await rpc({op:'icon',slot:i,entity:source.entity,icon,icon_override:iconOverride});
  source.icon=icon;source.iconOverride=iconOverride;
  if(slots[i].entity===source.entity){slots[i].icon=icon;slots[i].iconOverride=iconOverride}
  count++;
 }
 return count;
}
async function refreshCatalogue(){
 if(busy)return;$('refresh').disabled=true;message('Loading entities from Home Assistant…');
 try{
  await ha.load();lastLoaded=new Date();
  const icons=await syncIconMetadata();persistDraft();weatherOptions();roomOptions();render();search();
  message('Entity list refreshed.'+(icons?' '+icons+' device icon'+(icons===1?'':'s')+' synced.':'')+' Unsaved slot edits are preserved.');
 }catch(error){message(error.message+' Check the address and sign in again if needed.',true)}
 finally{$('refresh').disabled=false;browserStatus()}
}
$('refresh').onclick=refreshCatalogue;$('ha-connect').onclick=async()=>{persistDraft();try{const origin=OnxHA.baseUrl($('ha-url').value);await rpc({op:'ha_host',q:origin});await ha.login(origin)}catch(error){message(error.message,true)}};$('ha-disconnect').onclick=()=>{ha.disconnect();lastLoaded=null;roomOptions();weatherOptions();browserStatus();render();search();message('Browser disconnected. The panel’s Home Assistant connection is unchanged.')};
async function refreshStatus(){if(work||statusBusy||busy||!initialized)return;statusBusy=true;try{device=await rpc({op:'state'});deviceStatus(true)}catch(error){deviceStatus(false);message(error.message,true)}finally{statusBusy=false}}
$('check-status').onclick=refreshStatus;
$('retry-states').onclick=async()=>{if(busy)return;$('retry-states').disabled=true;try{await rpc({op:'retry'});message('Entity state refresh queued. Waiting for Home Assistant…');await refreshStatus()}catch(error){message(error.message,true)}finally{$('retry-states').disabled=false}};
function download(name,value){const url=URL.createObjectURL(new Blob([JSON.stringify(value,null,2)],{type:'application/json'})),a=element('a','');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000)}
async function getPreset(name){let text='',offset=0,total=1;while(offset<total){const part=await rpc({op:'preset_get',name,offset});total=part.total;if(!part.data||!(part.next>offset))break;text+=part.data;offset=part.next}const preset=JSON.parse(text);return {name:preset.name||name,slots:preset.slots,pages:preset.pages}}
async function exportPresets(){const list=await rpc({op:'presets'}),out=[];for(const item of list.presets||[])out.push(await getPreset(item.name));return out}
$('export').onclick=async()=>{
 let presetList=[],deviceSlots=saved;
 try{
  if(editTarget)deviceSlots=validateSlots((await rpc({op:'state'})).slots);
  presetList=await exportPresets();
 }catch(error){if(!await ask({title:'Presets could not be read',text:error.message+' Download the configuration without presets?',ok:'Download without presets'}))return}
 // Settings saved outside the slot/device-settings forms: the idle screen,
 // the dashboard stream and dock, and the Home Assistant address.
 const extra={},missing=[];
 try{const s=await rpc({op:'immich_state'});if(s.error)throw Error(s.error);extra.idle_screen={url:s.url||'',idle:s.idle||'0',fit:s.fit||'0',split:s.split||'0',details:s.details||'',fade:s.fade||'0',interval:s.interval||'20',overlay:s.overlay||'0'}}catch{missing.push('idle screen')}
 try{const s=await rpc({op:'panel_state'});if(s.error)throw Error(s.error);extra.dashboard={server:s.server||'',url:s.url||'',scale:s.scale||1,dock_pin:s.dock_pin||'dashboard',toolbar_side:s.toolbar_side||'right'}}catch{missing.push('dashboard')}
 const haUrl=device.ha_url||$('ha-url').value.trim();if(haUrl)extra.ha_url=haUrl;
 if(missing.length&&!await ask({title:'Some settings could not be read',text:'The '+missing.join(' and ')+' settings could not be read from the panel. Download the configuration without them?',ok:'Download anyway'}))return;
 try{
  const pages=globalThis.ONX_PAGES?.saved()||singlePages(deviceSlots);
  const artwork=await globalThis.ONX_PAGE_ART?.export([...pages,...presetList.flatMap(p=>p.pages||[])]);
  download('room-companion-config.json',{schema:2,model:'Room Companion',exported_at:new Date().toISOString(),slots:trimSlots(deviceSlots),pages,settings:savedSettings,presets:presetList,...extra,...(artwork&&Object.keys(artwork).length?{artwork}:{})});
  message('Configuration downloaded'+(presetList.length?' with '+presetList.length+' preset'+(presetList.length===1?'':'s'):'')+'.');
 }catch(error){message('Export failed: '+error.message,true)}
};
function validatePresets(input){if(input===undefined)return [];if(!Array.isArray(input)||input.length>8)throw Error('A backup can contain up to 8 presets.');const names=new Set();return input.map(p=>{const name=typeof p?.name==='string'?p.name.trim():'';if(!name||name.length>32||/[\u0000-\u001f\u007f]/.test(name)||name.toLowerCase()==='custom'||names.has(name.toLowerCase()))throw Error('Invalid or duplicate preset name in backup.');names.add(name.toLowerCase());const slots=validateSlots(p.slots);return {name,slots,pages:validatePages(p.pages,slots)}})}
// Upload in chunks the panel accepts: offsets and totals count UTF-8 bytes, chunks
// never split a character, and each encoded request stays well under 8 KB.
async function putPreset(name,list,pages){
 let valid;try{valid=validatePages(pages,list)}catch{valid=singlePages(list)}
 const compact=trimSlots(list);
 const body=Array.from(JSON.stringify({slots:compact,pages:valid})),utf8=new TextEncoder(),total=utf8.encode(body.join('')).length;
 let index=0,offset=0;
 while(index<body.length){
  let end=Math.min(body.length,index+1500);
  while(end>index+1&&encodeURIComponent(body.slice(index,end).join('')).length>3000)end=index+Math.max(1,Math.floor((end-index)*0.75));
  const data=body.slice(index,end).join('');
  await rpc({op:'preset_put',name,offset,total,data});
  offset+=utf8.encode(data).length;index=end}}
async function importPresets(list){let done=0;for(const preset of list){message('Restoring preset '+(done+1)+' of '+list.length+' · “'+preset.name+'”…');await globalThis.ONX_PAGE_ART?.upload(preset.pages);await putPreset(preset.name,preset.slots,preset.pages);done++}await loadPresets();return done}
$('diagnostics').onclick=async()=>{try{const icons=await rpc({op:'icon_status'});download('room-companion-diagnostics.json',{model:'Room Companion',firmware:device.firmware||'unknown',uptime_ms:device.uptime_ms,reset_reason:device.reset_reason,reset_reason_text:device.reset_reason_text,internal_heap_free:device.internal_heap_free,internal_heap_minimum:device.internal_heap_minimum,internal_heap_largest_block:device.internal_heap_largest_block,psram_free:device.psram_free,artwork_status:device.artwork_status,artwork_error:device.artwork_error,artwork_stats:device.artwork_stats,wifi_rssi_dbm:device.wifi_rssi_dbm,wifi_disconnects_since_boot:device.wifi_disconnects_since_boot,last_wifi_disconnect_ms:device.last_wifi_disconnect_ms,last_event_before_boot:device.last_event_before_boot,wifi_connected:device.wifi_connected,ha_connected:device.ha_connected,browser_catalogue_ready:ha.ready,captured_at:new Date().toISOString(),icons:icons.slots||[]});}catch(error){message('Could not load icon diagnostics: '+error.message,true)}};
// Idle screen, dashboard and Home Assistant address are stored immediately (they
// are not part of the draft), so ask before replacing them.
function extraNames(value){const names=[];if(value.idle_screen&&typeof value.idle_screen==='object')names.push('idle screen');if(value.dashboard&&typeof value.dashboard==='object')names.push('dashboard and dock');if(typeof value.ha_url==='string'&&value.ha_url.trim())names.push('Home Assistant address');return names}
async function importExtras(value){
 const idle=value.idle_screen,dash=value.dashboard,haUrl=typeof value.ha_url==='string'?value.ha_url.trim():'',names=[];
 if(idle&&typeof idle==='object')names.push('idle screen');if(dash&&typeof dash==='object')names.push('dashboard and dock');if(haUrl)names.push('Home Assistant address');
 if(!names.length)return [];message('Restoring '+names.join(', ')+'…');
 const done=[],failed=[],str=v=>v===undefined||v===null?'':String(v);
 if(idle&&typeof idle==='object'){try{const r=await rpc({op:'immich',data:[str(idle.url),str(idle.idle||'0'),str(idle.fit||'0'),str(idle.split)==='1'?'1':'0',str(idle.details),str(idle.fade)==='1'?'1':'0',str(idle.interval||'20'),str(idle.overlay||'0')].join('\n')});if(r.error)throw Error(r.error);done.push('idle screen');loadIF()}catch(e){failed.push('idle screen: '+e.message)}}
 if(dash&&typeof dash==='object'){try{const r=await rpc({op:'panel_save',data:JSON.stringify({server:str(dash.server),url:str(dash.url),scale:+dash.scale||1,dock_pin:str(dash.dock_pin||'dashboard'),toolbar_side:dash.toolbar_side==='left'?'left':'right'})});if(r.error)throw Error(r.error);done.push('dashboard')}catch(e){failed.push('dashboard: '+e.message)}}
 if(haUrl){try{const origin=OnxHA.baseUrl(haUrl);const r=await rpc({op:'ha_host',q:origin});if(r?.error)throw Error(r.error);$('ha-url').value=origin;device.ha_url=origin;done.push('Home Assistant address')}catch(e){failed.push('Home Assistant address: '+e.message)}}
 return [...done,...failed.map(f=>'not restored — '+f)]}
$('import').onclick=()=>{if(editTarget){message('Choose “On the panel” in Display & actions before importing a configuration.',true);return}$('import-file').click()};// Import chooser: pick which pages and which other parts of the backup to use,
// then apply them now or load them as a draft. Resolves to null when cancelled.
function importChoice(draft,presetList,extras){
 const dialog=$('import-pick'),groups=$('import-groups'),error=$('import-error');groups.replaceChildren();error.hidden=true;
 const title=(p,i)=>p.name||(draft.slots.find(s=>s.entity===p.entities.find(Boolean))?.name)||p.entities.find(Boolean)||'Page '+(i+1);
 const box=(text,note='',checked=true)=>{const l=element('label','');const c=document.createElement('input');c.type='checkbox';c.checked=checked;l.append(c,element('span',text));if(note)l.append(element('span',note,'muted'));return [l,c]};
 const group=(name,items)=>{const f=document.createElement('fieldset'),lg=document.createElement('legend'),[all,ac]=box(name);lg.append(...all.childNodes);f.append(lg);const wrap=element('div','',items.length>3?'import-pages':'');for(const [l] of items)wrap.append(l);f.append(wrap);const sync=()=>{const n=items.filter(([,c])=>c.checked).length;ac.checked=n===items.length;ac.indeterminate=n>0&&n<items.length};ac.onchange=()=>{for(const [,c] of items)c.checked=ac.checked;sync()};for(const [,c] of items)c.addEventListener('change',sync);groups.append(f);return items.map(([,c])=>c)};
 const pages=group('Pages',draft.pages.map((p,i)=>box(title(p,i),p.entities.filter(Boolean).length+' entit'+(p.entities.filter(Boolean).length===1?'y':'ies'))));
 const other=[['settings','Device settings','Brightness, colours, clock, sleep']];
 if(presetList.length)other.push(['presets','Presets',presetList.length+' stored']);
 for(const n of extras)other.push(['extra:'+n,n[0].toUpperCase()+n.slice(1),'']);
 const otherBoxes=group('Other settings',other.map(([,t,n])=>box(t,n)));
 $('import-summary').textContent='Untick anything you do not want. Entities used only by unticked pages are left out. Apply saves it to the panel now; Load as draft lets you review first (presets and other stored settings are only restored by Apply).';
 return new Promise(resolve=>{
  const pick=apply=>{const keep=new Set(pages.flatMap((c,i)=>c.checked?[i]:[]));const parts=new Set(other.flatMap(([k],i)=>otherBoxes[i].checked?[k]:[]));if(!keep.size&&!parts.size){error.textContent='Choose at least one item.';error.hidden=false;return}finish({apply,keep,parts})};
  const finish=v=>{dialog.onclose=null;$('import-form').onsubmit=null;if(dialog.open)dialog.close();resolve(v)};
  $('import-cancel').onclick=()=>finish(null);$('import-draft').onclick=()=>pick(false);
  $('import-form').onsubmit=e=>{e.preventDefault();pick(true)};dialog.onclose=()=>finish(null);
  dialog.showModal();$('import-apply').focus()})}
$('import-file').onchange=async()=>{const file=$('import-file').files[0];try{if(!file)return;if(file.size>2097152)throw Error('Configuration file is too large (maximum 2 MB).');message('Reading '+file.name+'…');const value=JSON.parse(await file.text());if(typeof value.model!=='string')throw Error('This file is not a Room Companion configuration.');const draft=validateDocument(value),presetList=validatePresets(value.presets),extras=extraNames(value);
 message('Choose what to import from '+file.name+'.');
 const choice=await importChoice(draft,presetList,extras);if(!choice){message('Import cancelled. Nothing changed.');return}
 const {apply,keep,parts}=choice;
 if(!apply&&(hasChanges()||pagesDirty())&&!await ask({title:'Replace your unsaved draft?',text:'The imported configuration replaces the changes you have not saved yet.',ok:'Replace draft',danger:true}))return;
 // Unticked pages are dropped; entities only they used are cleared.
 const pages=draft.pages.filter((_,i)=>keep.has(i)),used=new Set(pages.flatMap(p=>p.entities).filter(Boolean));
 const importedSlots=draft.slots.map(s=>s.entity&&!used.has(s.entity)&&s.timerHidden!=='1'?empty():s);
 const keepPresets=parts.has('presets')?presetList:[];
 globalThis.ONX_PAGE_ART?.import(value.artwork,[...pages,...keepPresets.flatMap(p=>p.pages)]);
 if(keep.size||!parts.size){slots=importedSlots;globalThis.ONX_PAGES?.set(pages)}
 if(parts.has('settings'))settings=draft.settings;
 settingsForm();changed();search();page('entities');
 if(!apply){message('Configuration loaded as a draft. Review it — delete or reorder pages as needed — then choose Save changes'+(parts.has('settings')?' and Save device settings':'')+'.');return}
 const extraValue={...value};for(const n of ['idle screen','dashboard and dock','Home Assistant address'])if(!parts.has('extra:'+n)){if(n==='idle screen')delete extraValue.idle_screen;if(n==='dashboard and dock')delete extraValue.dashboard;if(n==='Home Assistant address')delete extraValue.ha_url}
 await hold(async()=>{progress(1,10);const extraRestored=await importExtras(extraValue);const restored=keepPresets.length?await importPresets(keepPresets):0;progress(2,10);
  const ok=await(globalThis.ONX_PAGES?.save?globalThis.ONX_PAGES.save():saveSlots(dirtyIndices()));const settingsOk=ok&&(!dirtySettings()||await saveSettings());
  if(ok&&settingsOk)message('Backup applied'+(keep.size?': '+keep.size+' page'+(keep.size===1?'':'s'):'')+(parts.has('settings')?(keep.size?',':':')+' device settings':'')+' saved'+(restored?', '+restored+' preset'+(restored===1?'':'s')+' restored':'')+(extraRestored.length?'; '+extraRestored.join(', ')+' restored':'')+'.');
  else message('The backup was only partly applied. Unsaved items stay marked; choose Save changes'+(dirtySettings()?' and Save device settings':'')+' to retry.',true)})
}catch(error){message('Import failed: '+error.message,true)}finally{$('import-file').value=''}};
let presets={presets:[],active:'',applying:'',capacity:8},presetBusy=false;
const dirtyAny=()=>slots.some((_,i)=>dirtySlot(i))||pagesDirty();
// The Preset card: what the slot editor below is editing, and what can be done with it.
function renderPresetCard(){
 const select=$('preset-target'),items=presets.presets||[],names=items.map(item=>item.name),locked=busy||presetBusy||!initialized||!!presets.applying;
 if(editTarget?.isNew&&!names.includes(editTarget.name))names.push(editTarget.name);
 const panel=element('option','On the panel (live)');panel.value='';
 const group=document.createElement('optgroup');group.label=names.length?'Saved presets':'No saved presets yet';
 group.append(...names.map(name=>{const o=element('option',name+(name===presets.active?' · active':'')+(editTarget?.isNew&&editTarget.name===name?' · new':''));o.value=name;return o}));
 select.replaceChildren(panel,group);select.value=editTarget?editTarget.name:'';select.disabled=locked;
 const full=names.length>=(presets.capacity||8);
 $('preset-new').disabled=locked||full;$('preset-new').title=full?'All '+(presets.capacity||8)+' presets are in use':'New preset';
 const badge=$('preset-badge'),mode=!editTarget?'live':editTarget.isNew?'new':editTarget.name===presets.active?'active':'preset';
 badge.dataset.mode=mode;badge.textContent={live:'Live',new:'New',active:'Active',preset:'Saved'}[mode];
 $('preset-summary').textContent=editTarget?editTarget.name:'On the panel';
 $('preset-mode').textContent=presets.applying?'Applying “'+presets.applying+'” to the panel… '+(presets.remaining||0)+' step'+(presets.remaining===1?'':'s')+' left.':
  !editTarget?'Pages below are on the panel now. Saving changes the panel right away.'+(presets.active?' They match the “'+presets.active+'” preset.':''):
  editTarget.isNew?'New preset. Add pages below, then save to create it. The panel does not change.':
  'Editing this preset. Saving stores the preset only; the panel changes when you apply it.';
 $('preset-save-as').hidden=!!editTarget;$('preset-apply').hidden=!editTarget;$('preset-rename').hidden=!editTarget||editTarget.isNew;$('preset-delete').hidden=!editTarget||editTarget.isNew;
 for(const id of ['preset-save-as','preset-rename','preset-delete'])$(id).disabled=locked;
 const applyBlocked=editTarget&&(editTarget.isNew||dirtyAny());
 $('preset-apply').disabled=locked||applyBlocked;$('preset-apply').title=applyBlocked?'Save the preset before applying it':'Replace the panel’s controls with this preset';
 $('preset-save-as').disabled=locked||full&&!presets.active}
async function loadPresets(){if(!initialized)return;try{presets=await rpc({op:'presets'});renderPresetCard()}catch(error){message('Could not load presets: '+error.message,true)}}
async function presetOp(values,done){if(presetBusy||busy||!initialized)return false;presetBusy=true;renderPresetCard();try{presets=await rpc(values);message(done);return true}catch(error){message(error.message,true);return false}finally{presetBusy=false;renderPresetCard()}}
function fullSlots(list){const out=(Array.isArray(list)?list:[]).slice(0,MAX_SLOTS);while(out.length<MAX_SLOTS)out.push(empty());return validateSlots(out)}
async function switchTarget(name,isNew=false,force=false){
 if(!force&&dirtyAny()&&!await ask({title:'Discard unsaved changes?',text:'Switching loses the page and entity changes you have not saved.',ok:'Discard changes',danger:true})){renderPresetCard();return}
 if(busy||!initialized){renderPresetCard();return}
 busy=true;render();
 try{
  if(!name){device=await rpc({op:'state'});slots=validateSlots(device.slots);editTarget=null}
  else if(isNew){slots=Array.from({length:MAX_SLOTS},empty);editTarget={name,isNew:true}}
  else{const preset=await getPreset(name);slots=fullSlots(preset.slots);editTarget={name:preset.name,isNew:false,pages:preset.pages}}
  saved=copy(slots);active=0;visibleSlots=1;slotTab='display';
  message(!editTarget?'Showing the controls on the panel.':editTarget.isNew?'New preset “'+name+'”. Add pages, then save to create it.':'Editing preset “'+editTarget.name+'”. The panel does not change until you apply it.');
 }catch(error){message(error.message,true)}
 finally{busy=false;render();search();refreshPreview()}}
$('preset-target').onchange=()=>switchTarget($('preset-target').value);
// Collapsed by default; the choice is remembered in this browser only.
function setPresetExpanded(open){$('preset-toggle').setAttribute('aria-expanded',String(open));$('preset-body').hidden=!open;try{localStorage.setItem('onx.presets.expanded',open?'1':'0')}catch{}}
$('preset-toggle').onclick=()=>setPresetExpanded($('preset-toggle').getAttribute('aria-expanded')!=='true');
try{setPresetExpanded(localStorage.getItem('onx.presets.expanded')==='1')}catch{setPresetExpanded(false)}
$('preset-new').onclick=async()=>{
 const name=await ask({title:'New preset',text:'Starts empty. Add pages, then save to create it. The panel keeps its current controls.',ok:'Create',input:{label:'Preset name',placeholder:'Evening',validate:value=>presetNameProblem(value)}});
 if(name)switchTarget(name,true)};
$('preset-save-as').onclick=async()=>{
 const name=await ask({title:'Save as preset',text:'Stores the controls on the panel under a name, so you can switch back to them later.'+(dirtyAny()?' Unsaved changes are not included.':''),ok:'Save preset',input:{label:'Preset name',value:presets.active||'',placeholder:'Evening',validate:value=>{const problem=presetNameProblem(value,value);return problem||(!(presets.presets||[]).some(item=>item.name.toLowerCase()===value.toLowerCase())&&(presets.presets||[]).length>=(presets.capacity||8)?'All '+(presets.capacity||8)+' presets are in use; delete one first.':'')}}});
 if(!name)return;
 if((presets.presets||[]).some(item=>item.name.toLowerCase()===name.toLowerCase())&&!await ask({title:'Replace “'+name+'”?',text:'The preset’s controls are replaced with the controls on the panel.',ok:'Replace preset',danger:true}))return;
 await presetOp({op:'preset_save',name},'Saved the panel’s controls as “'+name+'”.')};
$('preset-rename').onclick=async()=>{
 const name=editTarget?.name;if(!name)return;
 const to=await ask({title:'Rename preset',text:'Home Assistant shows the new name in the panel’s Preset select.',ok:'Rename',input:{label:'Preset name',value:name,validate:value=>presetNameProblem(value,name)}});
 if(!to||to===name)return;
 if(await presetOp({op:'preset_rename',name,q:to},'Renamed “'+name+'” to “'+to+'”.')){editTarget.name=to;render()}};
$('preset-delete').onclick=async()=>{
 const name=editTarget?.name;if(!name)return;
 if(!await ask({title:'Delete preset “'+name+'”?',text:'This cannot be undone. The panel keeps its current controls.',ok:'Delete preset',danger:true}))return;
 if(await presetOp({op:'preset_delete',name},'Deleted preset “'+name+'”.'))await switchTarget('',false,true)};
$('preset-apply').onclick=async()=>{
 const name=editTarget?.name;if(!name||editTarget.isNew||dirtyAny())return;
 if(!await ask({title:'Apply “'+name+'” to the panel?',text:'The panel’s pages and controls are replaced with this preset. Only entries that differ are rewritten.',ok:'Apply preset'}))return;
 if(!await presetOp({op:'preset_apply',name},'Applying “'+name+'”…'))return;
 presetBusy=true;renderPresetCard();
 try{for(let i=0;i<240&&presets.applying;i++){await new Promise(resolve=>setTimeout(resolve,500));presets=await rpc({op:'presets'});renderPresetCard()}}
 catch(error){message('Could not confirm the preset: '+error.message,true)}
 finally{presetBusy=false}
 await switchTarget('',false,true);
 message(presets.active===name?'Preset “'+name+'” applied. Showing the panel’s controls.':'Preset “'+name+'” was applied with errors; check the pages and the device log.',presets.active!==name)};
$('restore-draft').onclick=()=>{if(!recovery)return;if(editTarget){message('Choose “On the panel” in Display & actions before restoring the draft.',true);return}slots=recovery.slots;settings=recovery.settings;recovery=null;$('draft-notice').hidden=true;settingsForm();changed();search();message('Draft restored. Review and save to apply it to the panel.')};$('dismiss-draft').onclick=()=>{recovery=null;$('draft-notice').hidden=true;try{sessionStorage.removeItem(DRAFT)}catch{}};
window.addEventListener('beforeunload',event=>{if(hasChanges()||pagesDirty()||busy){event.preventDefault();event.returnValue=''}});
try{const theme=localStorage.getItem('onx.theme')||'system';$('theme').value=['light','dark','system'].includes(theme)?theme:'system';if($('theme').value!=='system')document.documentElement.dataset.theme=$('theme').value}catch{}
$('theme').onchange=()=>{const theme=$('theme').value;if(theme==='system')delete document.documentElement.dataset.theme;else document.documentElement.dataset.theme=theme;try{localStorage.setItem('onx.theme',theme)}catch{}};
// Bridge for panel_web.h (pages); defined before loading so pages work at once.
globalThis.ONX_APP={
 pick:(accept,done)=>{pagePick={accept,done};selectMode('page')},
 item:entity=>ha.items.find(i=>i.entity===entity),
 slots:()=>slots,select:i=>{active=i;slotTab='display';render();search();refreshPreview()},
 // A new entity becomes its own configuration entry (shortcut, gestures, quick actions), saved with Save changes.
 add:item=>{let i=slots.findIndex((s,j)=>!s.entity&&!saved[j].entity);if(i<0)i=slots.findIndex(s=>!s.entity);if(i<0)throw Error('All '+MAX_SLOTS+' entities are in use. Remove one from a page first.');slots[i]={...empty(),entity:item.entity,name:item.name||item.entity,area:item.area||'',icon:item.icon||'',iconOverride:item.iconOverride||'0',coverPresets:item.entity.startsWith('cover.')?'15,25,50,75':''};changed();return i},
 clear:i=>{slots[i]=empty();changed()},
 setTimer:(i,on)=>{slots[i].timerHidden=on?'1':'0';changed()},
 saveAll:()=>saveSlots(dirtyIndices()),hold,progress,
 refresh:()=>render(),history:(entity,seconds)=>ha.history(entity,seconds),dirtyAt:i=>dirtySlot(i),message,views:()=>liveViews,settings:()=>settings
};
(async()=>{try{
 render();search();
 device=await rpc({op:'state'});if(Number.isInteger(device.max_slots)&&device.max_slots>=40)MAX_SLOTS=device.max_slots;slots=validateSlots(device.slots);saved=copy(slots);savedWeather=device.weather||'';const data=await rpc({op:'settings_state'});settings=validateSettings({...data,weather:savedWeather});savedSettings=copy(settings);initialized=true;deviceStatus(true);$('ha-url').value=device.ha_url||'';settingsForm();loadPresets();
 const draft=safeRead(DRAFT);if(draft){try{recovery=validateDocument(draft);$('draft-notice').hidden=false}catch{message('An older recovery draft could not be read. Your saved configuration is unchanged.',true)}}
 await ha.finishLogin();const auth=ha.read('onx.ha.auth');if(auth)$('ha-url').value=auth.base;browserStatus();render();search();if(auth)await refreshCatalogue();else message('Panel settings loaded. Sign in to Home Assistant to browse entities.');await refreshPreview();if(!auth&&!$('page-connection').hidden)void discoverHA(true);
 }catch(error){if(!initialized)deviceStatus(false);message(error.message,true);render();browserStatus()}
 document.addEventListener('visibilitychange',()=>{if(!document.hidden){refreshStatus();refreshPreview()}});setInterval(()=>{if(slots[active].entity.startsWith('media_player.')||Date.now()-lastPreviewAt>=2000)refreshPreview()},500);setInterval(()=>{if(!document.hidden)refreshStatus()},30000);
})();
})();
)ONXJS";
}

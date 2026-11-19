const assert=require('node:assert/strict');
const fs=require('node:fs');
const {chromium}=require('playwright');
(async()=>{
 const browser=await chromium.launch({headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:950}});
  const html=fs.readFileSync('my_components/onx_config_web/config_page.h','utf8').split('R"ONXHTML(')[1].split(')ONXHTML"')[0];
  const script=fs.readFileSync('my_components/onx_config_web/panel_web.h','utf8').split('R"ONXPANEL(')[1].split(')ONXPANEL"')[0];
  const configs=[],payloads=[],dashPayloads=[],tickets=new Map(),artwork=new Map();let ticket=0;
  await page.route('http://panel.test/**',async route=>{
   const req=route.request(),url=new URL(req.url());
   if(url.pathname==='/'){await route.fulfill({contentType:'text/html',body:html});return}
   if(url.pathname==='/panel/panel.js'){await route.fulfill({contentType:'text/javascript',body:script});return}
   if(url.pathname==='/panel/app.js'||url.pathname==='/panel/ha.js'){await route.fulfill({contentType:'text/javascript',body:''});return}
   if(url.pathname==='/panel/api'){
    const q=new URLSearchParams(req.postData());let response={};
    if(q.get('op')==='panel_state')response={status:'ready',scale:1,toolbar_side:'right'};
    if(q.get('op')==='panel_save'){const p=JSON.parse(q.get('data'));dashPayloads.push(p);response={...p,status:'ready'}}
    if(q.get('op')==='panel_pages')response={pages:configs};
    if(q.get('op')==='panel_art_put'){
     const data=q.get('data'),id=q.get('q'),offset=Number(q.get('offset')),chunk=Buffer.from(data,'base64url');
     const pixels=offset?artwork.get(id):Buffer.alloc(id?18432:3200);
     chunk.copy(pixels,offset);
     if(id)artwork.set(id,pixels);
     if(offset+chunk.length<pixels.length)response={next:offset+chunk.length};
     else {
      let hash=14695981039346656037n;
      for(const byte of pixels)hash=BigInt.asUintN(64,(hash^BigInt(byte))*1099511628211n);
      const actual=((hash&0x00ffffffffffffffn)|1n).toString(16).padStart(14,'0');
      if(id)assert.equal(actual,id);
      artwork.set(actual,pixels);response={id:actual};
     }
    }
    if(q.get('op')==='panel_art_get'){
     const pixels=artwork.get(q.get('data')),offset=Number(q.get('offset')),next=Math.min(offset+4800,pixels.length);
     response={data:pixels.subarray(offset,next).toString('base64'),next,total:pixels.length};
    }
    if(q.get('op')==='panel_pages_save'){const p=JSON.parse(q.get('data'));payloads.push(p);configs.splice(0,configs.length,...p.pages);response={pages:configs}}
    tickets.set(++ticket,response);await route.fulfill({json:{ticket}});return;
   }
   if(url.pathname==='/panel/result'){await route.fulfill({json:tickets.get(Number(url.searchParams.get('ticket')))||{}});return}
   await route.fulfill({status:204});
  });
  // Icon search: a tiny stand-in for the Iconify MDI collection.
  await page.route('https://api.iconify.design/**',async route=>{
   const url=new URL(route.request().url());
   if(url.pathname==='/collection')await route.fulfill({json:{prefix:'mdi',uncategorized:['home','bed'],categories:{Furniture:['sofa','sofa-outline','sofa-single','desk']}}});
   else await route.fulfill({contentType:'image/svg+xml',body:'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><rect width="24" height="24"/></svg>'});
  });
  await page.goto('http://panel.test/');await page.waitForFunction(()=>!!globalThis.ONX_PANEL_EDITOR);
  await page.evaluate(()=>document.getElementById('page-settings').hidden=false);
  assert.equal(await page.locator('#dock-settings').evaluate(n=>n.open),false);
  await page.locator('#dock-settings summary').click();
  await page.locator('#dash-toolbar-side').waitFor();
  assert.equal(await page.locator('#dash-toolbar-side').inputValue(),'right');
  await page.selectOption('#dash-toolbar-side','left');
  await page.locator('#dock-settings-form button[type=submit]').click();
  await page.waitForFunction(()=>document.getElementById('dock-status').textContent==='Saved.');
  assert.deepEqual(dashPayloads.at(-1),{toolbar_side:'left',dock_pin:'dashboard'});
  await page.fill('#dash-url','http://homeassistant.local:8123/room/0');
  await page.locator('#panel-dashboard button[type=submit]').click();
  await page.waitForFunction(()=>document.getElementById('dash-status').textContent.startsWith('Saved.'));
  assert.equal(Object.hasOwn(dashPayloads.at(-1),'toolbar_side'),false);
  assert.equal(dashPayloads.at(-1).url,'http://homeassistant.local:8123/room/0');
  assert.equal(await page.locator('#dash-toolbar-side').inputValue(),'left');
  await page.evaluate(()=>document.getElementById('page-settings').hidden=true);
  await page.evaluate(()=>{
   window.testSlots=[{entity:'light.kitchen',name:'Kitchen lights'},{entity:'switch.fan',name:'Fan'},{entity:'sensor.temperature',name:'Temperature'},{entity:'sensor.humidity',name:'Humidity'},{entity:'binary_sensor.door',name:'Door'}];
   window.testViews=testSlots.map((s,i)=>({entity:s.entity,layout:'value',available:true,value_ring:500,icon:'mdi:lightbulb',lines:[{text:'Sensor'},{text:i===2?'21 °C':i===3?'54 %':i===4?'Closed':'On'},{text:''},{text:''},{text:s.name}],carousel:{layout:'carousel',lines:[{text:''},{text:i===2?'21 °C':i===3?'54 %':i===4?'Closed':'On'},{text:''},{text:''},{text:s.name}],navigator:[]}}));
   globalThis.ONX_PANEL_SIZE={width:320,height:480};document.getElementById('preview-screen').value='main';
   window.testDraw=()=>ONX_PANEL_PREVIEW(document.getElementById('device-preview'),testSlots[0],testViews[0],testViews,'main',{});
   window.added=[];window.nextPick=null;
   // Mirrors config_app.h: selecting or changing an entry re-renders the editor.
   window.act=0;window.target=null;const again=()=>{ONX_PANEL_EDITOR(testSlots,act,false,testDraw,target);testDraw()};
   globalThis.ONX_APP={pick:(accept,done)=>{window.lastAccept=accept;if(window.nextPick)done(window.nextPick)},item:()=>null,slots:()=>testSlots,select:i=>{window.selected=window.act=i;again()},
    add:item=>{testSlots.push({entity:item.entity,name:item.name});added.push(item.entity);return testSlots.length-1},clear:i=>{testSlots[i]={entity:'',name:''}},setTimer:(i,on)=>{testSlots[i].timerHidden=on?'1':'0'},saveAll:async()=>true,refresh:again,dirtyAt:()=>false,message:t=>{window.lastMessage=t}};
   ONX_PAGES.set([{name:'',icon:'',layout:'single',entities:['light.kitchen']}]);
   ONX_PANEL_EDITOR(testSlots,0,false,testDraw);testDraw();
   window.dropEntity=(pos,item)=>{const dt=new DataTransfer();dt.setData('application/x-onx-entity',JSON.stringify(item));document.querySelector(`[data-member="${pos}"]`).dispatchEvent(new DragEvent('drop',{dataTransfer:dt,bubbles:true,cancelable:true}))};
   window.dropCell=(from,to)=>{const dt=new DataTransfer();dt.setData('application/x-onx-cell',String(from));document.querySelector(`[data-member="${to}"]`).dispatchEvent(new DragEvent('drop',{dataTransfer:dt,bubbles:true,cancelable:true}))};
  });
  assert.equal(await page.locator('#device-preview').getAttribute('viewBox'),'0 0 320 480');
  assert.equal(await page.locator('#device-preview circle[r="120"]').count(),0,'No round-display mock');
  const frame=await page.locator('.screen-frame').boundingBox();assert(frame.height>frame.width*1.35,'Preview frame must be rectangular, not a square SVG letterbox');
  const status=()=>page.locator('#page-layout-status').textContent();
  const layout=kind=>page.locator('#page-kind label',{has:page.locator(`input[value="${kind}"]`)}).click();
  const save=()=>page.evaluate(()=>ONX_PAGES.save());
  // The rail lists pages; the editor shows the selected page.
  assert.equal(await page.locator('#slots-heading').textContent(),'Pages');
  assert.equal(await page.locator('#slots .page-item').count(),1);
  assert.equal(await page.locator('#page-kind input').count(),4,'Layouts are chosen from visual cards');
  // Two entities: drag one in from the entity list; page name and carousel icon.
  await layout('two_controls');assert.equal(await page.locator('#page-grid [data-member]').count(),2);
  await page.evaluate(()=>dropEntity(1,{entity:'switch.fan',name:'Fan'}));
  assert.equal(await page.locator('#member-id').textContent(),'switch.fan','Placing an entity selects it for editing');
  await page.fill('#page-name','Living');
  await page.click('#page-icon-button');await page.locator('#icon-picker').waitFor({state:'visible'});
  assert.ok(await page.locator('#icon-grid [data-icon]').count()>5,'Suggestions show before typing');
  await page.fill('#icon-search','sofa');await page.locator('#icon-caption',{hasText:'Results'}).waitFor();
  assert.equal(await page.locator('#icon-grid [data-icon]').first().getAttribute('data-icon'),'mdi:sofa','Exact matches rank first');
  assert.equal(await page.locator('#icon-grid [data-icon]').count(),3);
  assert.match(await page.locator('#icon-grid [data-icon="mdi:sofa"] .mdi').getAttribute('style'),/api\.iconify\.design\/mdi\/sofa\.svg/,'Each result previews its icon');
  await page.locator('#icon-picker').screenshot({path:'/config/tests/artifacts/panel-icon-picker.png'});
  await page.click('#icon-grid [data-icon="mdi:sofa"]');assert.equal(await page.locator('#icon-picker').isVisible(),false);
  assert.match(await page.locator('#slots .page-item').first().getAttribute('aria-label'),/Page 1: Living, unsaved/);
  assert.equal(await page.locator('#slots .page-item .dirty-dot').textContent(),'●');
  assert.equal(await page.locator('#device-preview [data-panel-item]').count(),2);
  assert.equal(await page.locator('#page-grid svg.page-live [data-panel-item]').count(),2,'The page editor shows the live page');
  assert.equal(await save(),true);
  assert.deepEqual(payloads.at(-1),{pages:[{name:'Living',icon:'mdi:sofa',layout:'two_controls',entities:['light.kitchen','switch.fan']}]});
  // Large + three compact: picker, a new (unconfigured) entity, compact-only rule, swap, duplicates.
  await layout('control_sensors');assert.equal(await page.locator('#page-grid [data-member]').count(),4);
  assert.match(await status(),/Fan was removed/,'Non-sensors leave compact positions');
  assert.equal(await page.evaluate(()=>added.length),0);
  await page.evaluate(()=>dropEntity(1,{entity:'light.other',name:'Other'}));assert.match(await status(),/Compact positions/);
  await page.evaluate(()=>{nextPick={entity:'sensor.temperature',name:'Temperature'}});await page.click('[data-member="1"] .cell-main');
  assert.equal(await page.evaluate(()=>lastAccept('light.other')),false);
  await page.evaluate(()=>dropEntity(2,{entity:'sensor.co2',name:'CO2'}));assert.deepEqual(await page.evaluate(()=>added),['sensor.co2'],'New entities get their own entry');
  await page.evaluate(()=>dropEntity(3,{entity:'sensor.temperature',name:'Temperature'}));assert.match(await status(),/already on this page/);
  await page.evaluate(()=>dropEntity(3,{entity:'binary_sensor.door',name:'Door'}));
  await page.evaluate(()=>dropCell(1,2));
  assert.equal(await page.locator('#device-preview [data-panel-item]').count(),4);
  assert.equal(await save(),true);
  assert.deepEqual(payloads.at(-1).pages[0].entities,['light.kitchen','sensor.co2','sensor.temperature','binary_sensor.door']);
  // Selecting a position edits that entity; Remove clears just that position.
  await page.click('[data-member="3"] .cell-main');assert.equal(await page.locator('#member-id').textContent(),'binary_sensor.door');
  assert.match(await page.locator('#member-role').textContent(),/Compact 3/);
  // Reuse: one entry (one subscription) on several pages, but one lead per carousel stop.
  await page.click('#add-page');assert.ok(await page.locator('#member-empty').isVisible());
  await page.evaluate(()=>dropEntity(0,{entity:'light.kitchen',name:'Kitchen lights'}));assert.match(await status(),/already leads/);
  await page.evaluate(()=>dropEntity(0,{entity:'sensor.co2',name:'CO2'}));
  assert.deepEqual(await page.evaluate(()=>added),['sensor.co2'],'A reused entity keeps its single entry');
  await page.click('#page-up');assert.match(await page.locator('#slots .page-item').first().getAttribute('aria-label'),/Page 1: CO2/);
  await page.click('#page-down');
  assert.equal(await save(),true);
  assert.deepEqual(payloads.at(-1).pages.map(p=>p.entities[0]),['light.kitchen','sensor.co2']);
  await page.click('#delete-page');
  // Timers can be registered without any page.
  await page.evaluate(()=>{nextPick={entity:'timer.tea',name:'Tea'}});await page.click('#add-timer');
  assert.equal(await page.evaluate(()=>lastAccept('light.x')),false);
  assert.equal(await page.locator('#timer-list li').count(),1);
  assert.equal(await page.evaluate(()=>testSlots.find(s=>s.entity==='timer.tea').timerHidden),'1');
  assert.equal(await page.locator('#member-role').textContent(),'Registered timer');
  assert(!(await page.evaluate(()=>ONX_PAGES.saved())).some(p=>p.entities.includes('timer.tea')));
  await page.click('[data-timer="timer.tea"]');assert.equal(await page.locator('#timer-list li').count(),0);
  // Incomplete pages are not saved.
  await page.click('#add-page');assert.equal(await save(),false);assert.match(await status(),/Fill every position/);assert.equal(payloads.length,3);
  await page.click('#delete-page');
  await page.evaluate(()=>{ONX_PANEL_EDITOR(testSlots,1,false,()=>{});ONX_PANEL_EDITOR(testSlots,0,false,testDraw);testDraw()});
  assert.equal(await page.locator('#page-kind input:checked').getAttribute('value'),'control_sensors');assert.deepEqual(await page.evaluate(()=>ONX_PAGES.saved()[0].icon),'mdi:sofa');
  await page.screenshot({path:'/config/tests/artifacts/panel-pages-35.png',fullPage:true});
  await page.evaluate(()=>{ONX_PANEL_SIZE={width:240,height:320};testDraw()});
  assert.equal(await page.locator('#device-preview').getAttribute('viewBox'),'0 0 240 320');
  const rects=await page.evaluate(()=>ONX_PANEL_GEOMETRY('control_sensors',240,320));assert(rects.every(r=>r.y+r.h<=268));
  await page.screenshot({path:'/config/tests/artifacts/panel-pages-28.png',fullPage:true});
  await page.evaluate(()=>{
   ONX_PANEL_SIZE={width:320,height:480};
   const media={entity:'media_player.room',name:'Room speaker'},view={layout:'media',playing:true,progress:500,lines:[{text:''},{text:'Volume: 50%'},{text:'Track title'},{text:'Artist'},{text:'Room speaker'}]};
   ONX_PANEL_PREVIEW(document.getElementById('device-preview'),media,view,[view],'detail',{});
  });
  assert.match(await page.locator('#device-preview').textContent(),/Track title/);assert.match(await page.locator('#device-preview').textContent(),/Artist/);
  assert.equal(await page.locator('#device-preview [data-media-button]').count(),3,'Round previous, play and next');
  // Media matches the device: three transport buttons, no close button, no header icons.
  assert.equal(await page.locator('[data-media-button]').count(),3);
  assert.doesNotMatch(await page.locator('#device-preview').innerHTML(),/close|monitor-dashboard|bell/);
  // Landscape: the selected orientation turns the preview and the layout.
  await page.evaluate(()=>{
   const media={entity:'media_player.room',name:'Room speaker'},view={layout:'media',playing:true,progress:500,lines:[{text:''},{text:'Volume: 50%'},{text:'Track title'},{text:'Artist'},{text:'Room speaker'}]};
   ONX_PANEL_PREVIEW(document.getElementById('device-preview'),media,view,[view],'detail',{orientation:'90',media_color:'#F97316'});
  });
  assert.equal(await page.locator('#device-preview').getAttribute('viewBox'),'0 0 480 320');
  const play=await page.locator('[data-media-button="play"]').evaluate(n=>({x:+(n.getAttribute('cx')??n.getAttribute('x')),fill:n.getAttribute('fill')}));
  assert(play.x>200,'Landscape transport sits beside the artwork');assert.notEqual(play.fill,'#1f6feb');
  const wide=await page.evaluate(()=>ONX_PANEL_GEOMETRY('two_controls',480,320));assert.equal(wide[0].y,wide[1].y);
  await page.evaluate(()=>{ONX_PANEL_PREVIEW(document.getElementById('device-preview'),testSlots[0],testViews[0],testViews,'main',{orientation:'270'})});
  assert.equal(await page.locator('#device-preview').getAttribute('viewBox'),'0 0 480 320');
  assert.match(await page.locator('#device-preview').innerHTML(),/bell-outline/,'Main view shows the bell');
  await page.screenshot({path:'/config/tests/artifacts/panel-pages-35-landscape.png',fullPage:true});
  // Domain previews follow rectangular controls instead of the old text-only view.
  for(const [width,height]of [[320,480],[240,320]])for(const orientation of ['0','90']){
   await page.evaluate(({width,height,orientation})=>{
    ONX_PANEL_SIZE={width,height};
    const slot={entity:'weather.madrid',name:'Madrid'},view={entity:slot.entity,icon:'mdi:weather-rainy',layout:'weather',lines:[{text:'Weather'},{text:'16°C'},{text:'Rainy · Feels 14°C'},{text:'Humidity 86% Turn for more'},{text:'Madrid'}],panel:{state:'rainy',temperature:'16',feels_like:'14',humidity:'86',wind:'12',wind_unit:'km/h',forecast:[{date:(d=>d.getFullYear()+'-'+String(d.getMonth()+1).padStart(2,'0')+'-'+String(d.getDate()).padStart(2,'0'))(new Date()),low:'10',high:'18',rain:'60'}]},carousel:{navigator:[]}};
    ONX_PANEL_PREVIEW(document.getElementById('device-preview'),slot,view,[view],'detail',{orientation});
   },{width,height,orientation});
   assert.match(await page.locator('#device-preview').textContent(),/Humidity/);
   assert.match(await page.locator('#device-preview').textContent(),/16°/);
   assert.match(await page.locator('#device-preview').textContent(),/Today/);
   assert.doesNotMatch(await page.locator('#device-preview').textContent(),/Turn for more/);
   assert.equal(await page.locator('[data-weather-forecast]').count(),1);
  }
  await page.locator('#device-preview').screenshot({path:'/config/tests/artifacts/panel-weather-preview.png'});
  await page.evaluate(()=>{
   ONX_PANEL_SIZE={width:320,height:480};
   const slot={entity:'media_player.room',name:'Room'},view={entity:slot.entity,layout:'media',playing:true,artwork:'https://example.test/cover.png',panel:{title:'Track title',artist:'Artist'},carousel:{navigator:[]}};
   ONX_PANEL_PREVIEW(document.getElementById('device-preview'),slot,view,[view],'main',{});
  });
  assert.equal(await page.locator('[data-panel-artwork]').count(),1);
  assert.match(await page.locator('[data-panel-artwork]').getAttribute('clip-path'),/url/);
  assert.equal(await page.locator('#device-preview [data-media-button]').count(),3,'Round transport on the media card');
  await page.evaluate(()=>{
   const slot={entity:'weather.home',name:'Home'},view={entity:slot.entity,panel:{temperature:'16',state:'cloudy'},carousel:{navigator:[]}};
   ONX_PANEL_PREVIEW(document.getElementById('device-preview'),slot,view,[view],'main',{background_color:'#FFFFFF'});
  });
  assert.equal(await page.locator('#device-preview > rect').first().getAttribute('fill'),'#FFFFFF');
  assert.notEqual(await page.locator('#device-preview text').first().getAttribute('fill'),'#ffffff','Light backgrounds need dark readable text');
  // Presets edit their own page list and never save the panel's.
  await page.evaluate(()=>{window.target={name:'Guests',isNew:false,pages:[{name:'Guest room',icon:'mdi:bed',layout:'single',entities:['switch.fan']}]};ONX_PANEL_EDITOR(testSlots,0,false,testDraw,target)});
  assert.equal(await page.locator('#slots .page-item').count(),1);assert.match(await page.locator('#slots').textContent(),/Guest room/);
  assert.match(await page.locator('#slot-title').textContent(),/Guest room · Guests/);
  const before=payloads.length;await page.fill('#page-name','Guests room');assert.equal(await save(),true);
  assert.equal(payloads.length,before,'Saving a preset must not save the panel pages');
  assert.equal(await page.evaluate(()=>target.pages[0].name),'Guests room');
  await page.evaluate(()=>{window.target=null;ONX_PANEL_EDITOR(testSlots,0,false,testDraw,null)});
  assert.match(await page.locator('#slots').textContent(),/Living/);
  await page.setViewportSize({width:390,height:844});
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>window.innerWidth),false,'Page editor fits on mobile');
  await page.screenshot({path:'/config/tests/artifacts/panel-pages-mobile.png',fullPage:true});
  await page.setViewportSize({width:1100,height:950});
  await page.evaluate(()=>ONX_PAGES.set([{name:'Keys',icon:'',layout:'single',entities:['light.kitchen']}]));
  await layout('action_grid');
  await page.selectOption('#grid-rows','4');
  await page.selectOption('#grid-columns','4');
  assert.equal(await page.locator('#page-grid [data-member]').count(),16);
  await page.evaluate(()=>dropCell(0,15));
  assert.match(await page.locator('#slots .slot-icon .mdi').getAttribute('style'),/view-grid-outline/);
  await page.click('#page-icon-button');
  await page.click('#icon-entity');
  assert.match(await page.locator('#slots .slot-icon .mdi').getAttribute('style'),/lightbulb/);
  await page.click('#page-icon-button');
  await page.click('#icon-default');
  await page.check('#member-icon-only-value');
  assert(await page.locator('[data-member="15"]').evaluate(n=>n.classList.contains('icon-only')));
  assert.equal(await page.locator('#member-animation').isVisible(),true,'Grid keys offer an icon animation');
  await page.check('#member-animation-value');
  assert.match(await page.locator('#member-animation-sample').getAttribute('class'),/fx-pulse/,'The sample previews the effect');
  assert.equal(await save(),true);
  assert.deepEqual(payloads.at(-1).pages,[{
   name:'Keys',icon:'',layout:'action_grid',rows:4,columns:4,
   entities:[...Array(15).fill(''),'light.kitchen'],
   icon_only:[...Array(15).fill(false),true],
   animation:{'light.kitchen':'pulse'}
  }]);
  await page.uncheck('#member-animation-value');
  assert.equal(await save(),true);
  assert.equal(payloads.at(-1).pages[0].animation,undefined,'No animation is omitted from the payload');
  const keys=await page.evaluate(()=>ONX_PANEL_GEOMETRY('action_grid',480,320,false,4,4));
  assert.equal(keys.length,16);assert(keys.every(r=>r.w>0&&r.h>0&&r.y>=46&&r.y+r.h<=264));
  assert.equal(keys.at(-1).y+keys.at(-1).h,264);
  // The same selector crops artwork for a non-grid position and for the dock.
  await page.evaluate(()=>ONX_PAGES.set([{name:'Artwork',icon:'',layout:'single',entities:['light.kitchen']}]));
  const png=await page.evaluate(()=>{
   const canvas=document.createElement('canvas');canvas.width=80;canvas.height=40;
   const ctx=canvas.getContext('2d');ctx.fillStyle='red';ctx.fillRect(0,0,40,40);ctx.fillStyle='blue';ctx.fillRect(40,0,40,40);
   return canvas.toDataURL().split(',')[1];
  });
  const crop=async value=>{
   await page.locator('#art-file').setInputFiles({name:'two-colors.png',mimeType:'image/png',buffer:Buffer.from(png,'base64')});
   await page.locator('#art-use').waitFor();
   await page.locator('#art-x').evaluate((input,x)=>{input.value=x;input.dispatchEvent(new Event('input'))},value);
   await page.click('#art-use');
  };
  await page.click('#member-icon-button');await crop('0');
  assert.equal(await page.locator('#page-grid .art-preview').count(),1);
  await page.click('#page-icon-button');await page.click('#upload-icon');await crop('100');
  assert.equal(await save(),true);
  const output=payloads.at(-1).pages[0];
  assert(output.artwork?.[0] && output.carousel_artwork && output.artwork[0]!==output.carousel_artwork);
  assert.equal(artwork.get(output.artwork[0]).length,96*96*2);
  // An enlarged crop can interpolate at the boundary. Check its centre.
  const centrePixel=(48*96+48)*2;
  assert.equal(artwork.get(output.artwork[0]).readUInt16LE(centrePixel),0xf800);
  assert.equal(artwork.get(output.carousel_artwork).readUInt16LE(centrePixel),0x001f);
  const backup=await page.evaluate(()=>ONX_PAGE_ART.export(ONX_PAGES.saved()));
  assert.deepEqual(Object.keys(backup).sort(),[output.artwork[0],output.carousel_artwork].sort());
  await page.reload();
  await page.waitForFunction(()=>!!globalThis.ONX_PAGE_ART);
  const restored=await page.evaluate(p=>ONX_PAGE_ART.export([p]),output);
  assert.deepEqual(restored,backup,'A fresh browser assembles every image chunk');
  console.log('Panel pages: drag-and-drop pages with icons, compact rules, new-entity entries, save payloads, duplicate rejection and both model geometries passed');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});

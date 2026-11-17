const assert=require('node:assert/strict');
const fs=require('node:fs');
const {chromium}=require('playwright');
(async()=>{
 const browser=await chromium.launch({headless:true,args:['--no-sandbox']});
 try{
  const page=await browser.newPage({viewport:{width:1100,height:950}});
  const html=fs.readFileSync('my_components/onx_config_web/config_page.h','utf8').split('R"ONXHTML(')[1].split(')ONXHTML"')[0];
  const script=fs.readFileSync('my_components/onx_config_web/panel_web.h','utf8').split('R"ONXPANEL(')[1].split(')ONXPANEL"')[0];
  const configs=[],payloads=[],tickets=new Map();let ticket=0;
  await page.route('http://panel.test/**',async route=>{
   const req=route.request(),url=new URL(req.url());
   if(url.pathname==='/'){await route.fulfill({contentType:'text/html',body:html});return}
   if(url.pathname==='/panel/panel.js'){await route.fulfill({contentType:'text/javascript',body:script});return}
   if(url.pathname==='/panel/app.js'||url.pathname==='/panel/ha.js'){await route.fulfill({contentType:'text/javascript',body:''});return}
   if(url.pathname==='/panel/api'){
    const q=new URLSearchParams(req.postData());let response={};
    if(q.get('op')==='panel_state')response={status:'ready',scale:1};
    if(q.get('op')==='panel_pages')response={pages:configs};
    if(q.get('op')==='panel_page_save'){const p=JSON.parse(q.get('data'));payloads.push(p);configs.splice(0,configs.length,p);response={pages:configs}}
    tickets.set(++ticket,response);await route.fulfill({json:{ticket}});return;
   }
   if(url.pathname==='/panel/result'){await route.fulfill({json:tickets.get(Number(url.searchParams.get('ticket')))||{}});return}
   await route.fulfill({status:204});
  });
  await page.goto('http://panel.test/');await page.waitForFunction(()=>!!globalThis.ONX_PANEL_EDITOR);
  await page.evaluate(()=>{
   window.testSlots=[{entity:'light.kitchen',name:'Kitchen lights'},{entity:'switch.fan',name:'Fan'},{entity:'sensor.temperature',name:'Temperature'},{entity:'sensor.humidity',name:'Humidity'},{entity:'binary_sensor.door',name:'Door'}];
   window.testViews=testSlots.map((s,i)=>({entity:s.entity,layout:'value',available:true,value_ring:500,icon:'mdi:lightbulb',lines:[{text:'Sensor'},{text:i===2?'21 °C':i===3?'54 %':i===4?'Closed':'On'},{text:''},{text:''},{text:s.name}],carousel:{layout:'carousel',lines:[{text:''},{text:i===2?'21 °C':i===3?'54 %':i===4?'Closed':'On'},{text:''},{text:''},{text:s.name}],navigator:[]}}));
   globalThis.ONX_PANEL_SIZE={width:320,height:480};document.getElementById('preview-screen').value='main';
   window.testDraw=()=>ONX_PANEL_PREVIEW(document.getElementById('device-preview'),testSlots[0],testViews[0],testViews,'main',{});
   ONX_PANEL_EDITOR(testSlots,0,false,testDraw);testDraw();
  });
  assert.equal(await page.locator('#device-preview').getAttribute('viewBox'),'0 0 320 480');
  assert.equal(await page.locator('#device-preview circle').count(),0);
  const frame=await page.locator('.screen-frame').boundingBox();assert(frame.height>frame.width*1.35,'Preview frame must be rectangular, not a square SVG letterbox');
  await page.selectOption('#page-kind','two_controls');await page.selectOption('[data-member="0"]','switch.fan');
  assert.equal(await page.locator('[data-panel-item]').count(),2);
  await page.click('#save-page-layout');await page.getByText('Page layout saved.',{exact:true}).waitFor();
  assert.deepEqual(payloads.at(-1),{entity:'light.kitchen',layout:'two_controls',members:['switch.fan']});
  await page.selectOption('#page-kind','control_sensors');
  assert.equal(await page.locator('[data-member="0"] option[value="switch.fan"]').count(),0);
  for(const [i,id]of ['sensor.temperature','sensor.humidity','binary_sensor.door'].entries())await page.selectOption(`[data-member="${i}"]`,id);
  assert.equal(await page.locator('[data-panel-item]').count(),4);
  await page.click('#save-page-layout');await page.getByText('Page layout saved.',{exact:true}).waitFor();assert.equal(payloads.at(-1).members.length,3);
  await page.selectOption('[data-member="2"]','sensor.humidity');await page.click('#save-page-layout');
  await page.getByText('Choose a different entity for every position.',{exact:true}).waitFor();assert.equal(payloads.length,2);
  await page.selectOption('[data-member="2"]','binary_sensor.door');
  await page.evaluate(()=>{ONX_PANEL_EDITOR(testSlots,1,false,()=>{});ONX_PANEL_EDITOR(testSlots,0,false,testDraw);testDraw()});
  assert.equal(await page.locator('#page-kind').inputValue(),'control_sensors');
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
  assert.equal(await page.locator('#device-preview circle').count(),0);
  // Media matches the device: three transport buttons, no close button, no header icons.
  assert.equal(await page.locator('[data-media-button]').count(),3);
  assert.doesNotMatch(await page.locator('#device-preview').innerHTML(),/close|monitor-dashboard|bell/);
  // Landscape: the selected orientation turns the preview and the layout.
  await page.evaluate(()=>{
   const media={entity:'media_player.room',name:'Room speaker'},view={layout:'media',playing:true,progress:500,lines:[{text:''},{text:'Volume: 50%'},{text:'Track title'},{text:'Artist'},{text:'Room speaker'}]};
   ONX_PANEL_PREVIEW(document.getElementById('device-preview'),media,view,[view],'detail',{orientation:'90',media_color:'#F97316'});
  });
  assert.equal(await page.locator('#device-preview').getAttribute('viewBox'),'0 0 480 320');
  const play=await page.locator('[data-media-button="play"]').evaluate(n=>({x:+n.getAttribute('x'),fill:n.getAttribute('fill')}));
  assert(play.x>200,'Landscape transport sits beside the artwork');assert.notEqual(play.fill,'#1f6feb');
  const wide=await page.evaluate(()=>ONX_PANEL_GEOMETRY('two_controls',480,320));assert.equal(wide[0].y,wide[1].y);
  await page.evaluate(()=>{ONX_PANEL_PREVIEW(document.getElementById('device-preview'),testSlots[0],testViews[0],testViews,'main',{orientation:'270'})});
  assert.equal(await page.locator('#device-preview').getAttribute('viewBox'),'0 0 480 320');
  assert.match(await page.locator('#device-preview').innerHTML(),/bell-outline/,'Main view shows the bell');
  await page.screenshot({path:'/config/tests/artifacts/panel-pages-35-landscape.png',fullPage:true});
  await page.evaluate(()=>{ONX_PANEL_EDITOR(testSlots,0,false,testDraw,true)});
  assert.equal(await page.locator('#panel-page-layout').isVisible(),false,'Editing a preset must not accidentally save a live-device page layout');
  console.log('Panel pages: full-height previews, grouped live values, editing, save payloads, duplicate rejection and both model geometries passed');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});

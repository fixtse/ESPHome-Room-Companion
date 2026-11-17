// Loads the configuration page with all three real scripts (ha.js, the panel
// extension and app.js) against a mocked panel. Regression: the panel script
// once replaced a label's text and removed a checkbox app.js binds to, so the
// page stayed at "Connecting to the panel…" forever.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {chromium}=require('playwright');
const raw=(file,tag)=>fs.readFileSync('my_components/onx_config_web/'+file,'utf8').split('R"'+tag+'(')[1].split(')'+tag+'"')[0];
(async()=>{
 const browser=await chromium.launch({headless:true,args:['--no-sandbox']});
 const html=raw('config_page.h','ONXHTML').replace('__ONX_TOKEN__','test'),panel=raw('panel_web.h','ONXPANEL'),
  app=raw('config_app.h','ONXJS'),ha=raw('ha_browser.h','ONXJS');
 const empty=()=>({entity:'',name:'',area:'',action:'',actionName:'',typeOption:'',icon:'',iconOverride:'0',secondary:'',secondaryText:'{value}',sensorGraph:'0',timerHidden:'0',playlists:[],gestures:{cw:'',ccw:'',holdCw:'',holdCcw:'',feedback:'',popup:'1',responses:'0',pressResponses:'0',events:'0',step:'1'}});
 const slots=Array.from({length:20},empty);
 Object.assign(slots[0],{entity:'weather.home',name:'Madrid',type:'weather'});
 Object.assign(slots[1],{entity:'media_player.bedroom',name:'Bedroom'});
 Object.assign(slots[2],{entity:'sensor.codex_remaining',name:'Codex 5 hour remaining',secondary:'sensor.codex_usage'});
 const responses={
  ha_discover:{status:"ready",instances:[{name:"Home",url:"http://192.168.10.2:8123"}]},
  state:{wifi_connected:true,ha_connected:true,count:3,capacity:41,firmware:'test',uptime_ms:60000,ha_url:'http://homeassistant.local:8123',weather:'weather.home',slots},
  settings_state:{brightness:'73',standby:'30',date_format:'dd-mm',show_ampm:'1',hour_format:'24',media_color:'#F97316',orientation:'0',media_view:'material',clock_seconds_ring:'0',hold_cue:'1',clock_temperature:'',clock_humidity:'',notifications_cover_timers:'1',idle_view:'clock',standby_scope:'main',sensitivity:'2',sleep_enabled:'0',sleep_start:'00:00',sleep_end:'00:00',sleep_timeout:'60'},
  presets:{presets:[],active:'',applying:'',capacity:8,remaining:8,max_name:32},
  preview:{panel_width:320,panel_height:480,slots:[]},
  panel_state:{status:'ready',scale:1},panel_pages:{pages:[]},dashboard:{url:'',server:''}};
 const run=async(viewport,scheme,name)=>{
  const page=await browser.newPage({viewport,colorScheme:scheme});
  const errors=[];page.on('pageerror',e=>errors.push(String(e)));
  const tickets=new Map();let ticket=0;
  await page.route('**/*',async route=>{
   const req=route.request(),url=new URL(req.url());
   if(url.host!=='panel.test'){await route.abort();return}  // Fonts and links stay offline.
   const js=body=>route.fulfill({contentType:'text/javascript',body});
   if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:html});
   if(url.pathname==='/panel/ha.js')return js(ha);
   if(url.pathname==='/panel/panel.js')return js(panel);
   if(url.pathname==='/panel/app.js')return js(app);
   if(url.pathname==='/panel/api'){const op=new URLSearchParams(req.postData()).get('op');tickets.set(++ticket,responses[op]||{});return route.fulfill({status:202,json:{ticket}})}
   if(url.pathname==='/panel/result')return route.fulfill({json:tickets.get(Number(url.searchParams.get('ticket')))||{}});
   return route.fulfill({status:204});
  });
  await page.goto('http://panel.test/');
  await page.getByText('Panel: reachable',{exact:true}).waitFor({timeout:8000});
  assert.deepEqual(errors,[],'The page must start without script errors');
  await page.waitForFunction(()=>document.getElementById('ha-url').value==='http://192.168.10.2:8123');
  await page.locator('[data-page="settings"]').click();
  await page.locator('#background-color').evaluate(input=>{input.value='#f4f4f4';input.dispatchEvent(new Event('input',{bubbles:true}))});
  assert.equal(await page.locator('#device-preview > rect').first().getAttribute('fill'),'#F4F4F4','Background changes update the preview before saving');
  await page.click('#discard-settings');
  await page.locator('[data-page="connection"]').click();
  await page.fill('#ha-url','https://custom.example.test');await page.click('#ha-discover');
  await page.getByText('Home Assistant found. Sign in to connect this browser.',{exact:true}).waitFor();
  assert.equal(await page.locator('#ha-url').inputValue(),'https://custom.example.test','Discovery must preserve a manually entered address');
  await page.click('#rail-toggle');assert.equal(await page.locator('#rail-toggle').getAttribute('aria-expanded'),'false');
  await page.reload();await page.waitForFunction(()=>document.getElementById('device-status').dataset.ok==='true');
  assert.equal(await page.locator('#rail-toggle').getAttribute('aria-expanded'),'false','Sidebar preference survives reload');
  await page.click('#rail-toggle');assert.equal(await page.locator('#rail-toggle').getAttribute('aria-expanded'),'true');

  assert.equal(await page.locator('#gesture-pressResponses').count(),1,'The shortcut result checkbox must survive the panel script');
  assert.match(await page.locator('#gesture-pressResponses').locator('xpath=..').innerText(),/after the shortcut runs/);
  assert.equal(await page.locator('#slots button').count()>=3,true,'Configured slots are listed');
  assert.equal(await page.evaluate(()=>getComputedStyle(document.documentElement).getPropertyValue('--accent').trim().toUpperCase()),'#F97316');
  await page.screenshot({path:`/config/tests/artifacts/config-${name}.png`,fullPage:true});
  await page.close();
 };
 try{
  await run({width:1440,height:960},'dark','desktop-dark');
  await run({width:1440,height:960},'light','desktop-light');
  await run({width:390,height:844},'dark','mobile-dark');
  console.log('Panel configuration startup tests passed');
 }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exit(1)});

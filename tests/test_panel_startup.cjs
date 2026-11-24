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
 const slots=Array.from({length:40},empty);
 Object.assign(slots[0],{entity:'weather.home',name:'Madrid',type:'weather'});
 Object.assign(slots[1],{entity:'media_player.bedroom',name:'Bedroom'});
 Object.assign(slots[2],{entity:'sensor.codex_remaining',name:'Codex 5 hour remaining',secondary:'sensor.codex_usage'});
 const responses={
  ha_discover:{status:"ready",instances:[{name:"Home",url:"http://192.168.10.2:8123"}]},
  state:{wifi_connected:true,ha_connected:true,count:3,capacity:41,firmware:'test',uptime_ms:60000,ha_url:'http://homeassistant.local:8123',weather:'weather.home',slots},
  settings_state:{brightness:'73',standby:'30',date_format:'dd-mm',show_ampm:'1',hour_format:'24',media_color:'#F97316',orientation:'0',clock_seconds_ring:'0',hold_cue:'1',clock_temperature:'',clock_humidity:'',notifications_cover_timers:'1',idle_view:'clock',standby_scope:'main',sensitivity:'2',sleep_enabled:'0',sleep_start:'00:00',sleep_end:'00:00',sleep_timeout:'60'},
  presets:{presets:[],active:'',applying:'',capacity:8,remaining:8,max_name:32},
  preview:{panel_width:320,panel_height:480,slots:[{entity:'weather.home',available:true,layout:'value',lines:Array.from({length:5},()=>({text:''})),panel:{state:'sunny',available:true,temperature:20,humidity:50,forecast:[]}}]},
  panel_state:{status:'ready',scale:1},panel_pages:{pages:[{name:'',icon:'',layout:'single',entities:['weather.home']},{name:'Bedroom',icon:'mdi:bed',layout:'control_sensors',entities:['media_player.bedroom','sensor.codex_remaining','','']}]},dashboard:{url:'',server:''}};
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
  await page.waitForFunction(()=>document.getElementById('ha-url').value==='http://homeassistant.local:8123');
  await page.waitForFunction(()=>document.querySelector('#page-grid.live svg.page-live'));
  assert.equal(await page.locator('#page-connection #panel-dashboard').count(),1,'Dashboard settings belong in Home Assistant');
  assert.equal(await page.locator('#page-settings #panel-dashboard').count(),0);
  // The idle clock layouts live on the Idle screen tab; colours stay in Settings.
  await page.locator('[data-page="photos"]').click();
  await page.locator('[data-layout="7"]').click();
  for(const accent of ['#F97316','#151515','#F8FAFC']){
   await page.locator('#color-value').evaluate((el,value)=>el.textContent=value,accent);
   await page.waitForFunction(value=>getComputedStyle(document.documentElement).getPropertyValue('--accent').trim().toUpperCase()===value,accent);
   const contrast=await page.locator('[data-layout="7"]').evaluate(el=>{
    const luminance=color=>{const c=color.match(/[\d.]+/g).slice(0,3).map(Number).map(v=>v/255).map(v=>v<=0.04045?v/12.92:((v+0.055)/1.055)**2.4);return c[0]*.2126+c[1]*.7152+c[2]*.0722};
    const style=getComputedStyle(el),a=luminance(style.color),b=luminance(style.backgroundColor);return (Math.max(a,b)+.05)/(Math.min(a,b)+.05);
   });
   assert(contrast>=4.5,`Selected preset needs readable text on ${accent}: ${contrast}`);
  }

  await page.locator('[data-page="settings"]').click();
  await page.locator('#background-color').evaluate(input=>{input.value='#f4f4f4';input.dispatchEvent(new Event('input',{bubbles:true}))});
  assert.equal(await page.locator('#device-preview > rect').first().getAttribute('fill'),'#F4F4F4','Background changes update the preview before saving');
  await page.click('#discard-settings');
  await page.locator('[data-page="connection"]').click();
  await page.fill('#ha-url','https://custom.example.test');await page.click('#ha-discover');
  await page.getByText('Found 1 Home Assistant. Select it to sign in.',{exact:true}).waitFor();
  assert.equal(await page.locator('#ha-url').inputValue(),'https://custom.example.test','Discovery must preserve a manually entered address');
  await page.click('#rail-toggle');assert.equal(await page.locator('#rail-toggle').getAttribute('aria-expanded'),'false');
  await page.reload();await page.waitForFunction(()=>document.getElementById('device-status').dataset.ok==='true');
  assert.equal(await page.locator('#rail-toggle').getAttribute('aria-expanded'),'false','Sidebar preference survives reload');
  await page.click('#rail-toggle');assert.equal(await page.locator('#rail-toggle').getAttribute('aria-expanded'),'true');

  assert.equal(await page.locator('#gesture-pressResponses').count(),1,'The shortcut result checkbox must survive the panel script');
  assert.match(await page.locator('#gesture-pressResponses').locator('xpath=..').innerText(),/after the shortcut runs/);
  await page.locator('#slots .page-item').nth(1).waitFor({timeout:12000});
  assert.equal(await page.locator('#slots .page-item').count(),2,'Pages are listed');
  await page.locator('[data-page="entities"]').click();
  assert.equal(await page.locator('#page-grid [data-member]').count(),1,'The first page shows its layout');
  await page.locator('#slots .page-item').nth(1).click();
  assert.equal(await page.locator('#page-grid [data-member]').count(),4);assert.equal(await page.locator('#page-grid .page-cell.empty').count(),2);
  assert.equal(await page.evaluate(()=>getComputedStyle(document.documentElement).getPropertyValue('--accent').trim().toUpperCase()),'#F97316');
  await page.screenshot({path:`/config/tests/artifacts/config-${name}.png`,fullPage:true});
  assert.deepEqual(errors,[],'Page editing and tab changes must not throw script errors');
  await page.close();
 };
 try{
  await run({width:1440,height:960},'dark','desktop-dark');
  await run({width:1440,height:960},'light','desktop-light');
  await run({width:390,height:844},'dark','mobile-dark');
  console.log('Panel configuration startup tests passed');
 }finally{await browser.close()}
})().catch(error=>{console.error(error);process.exit(1)});

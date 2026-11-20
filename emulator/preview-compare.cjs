// Draws the configuration page's panel preview (panel_web.h) for every screen
// the emulator rendered, from the same preview data (onx_preview.h), and writes
// side-by-side sheets: emulator (device code) | web preview.
//
//   node emulator/preview-compare.cjs emulator/out      (run by emulator/render.sh)
//
// Reads <out>/preview/*.json and <out>/*.png; writes <out>/web/*.png and
// <out>/compare/*.png, plus <out>/compare/<model>-all.png contact sheets.
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('playwright');

const out = process.argv[2] || 'emulator/out';
const read = file => fs.readFileSync(file, 'utf8');
const html = read('my_components/onx_config_web/config_page.h').split('R"ONXHTML(')[1].split(')ONXHTML"')[0];
const script = read('my_components/onx_config_web/panel_web.h').split('R"ONXPANEL(')[1].split(')ONXPANEL"')[0];
const font = (file, weight) =>
  `@font-face{font-family:MiSans;font-weight:${weight};src:url(data:font/ttf;base64,${fs.readFileSync(file).toString('base64')})}`;
const fonts = font('fonts/MiSans-Medium.ttf', 500) + font('fonts/MiSans-Bold.ttf', 700);

(async () => {
  const browser = await chromium.launch({headless: true, args: ['--no-sandbox']});
  // One context, so the icon route below also serves the per-screen pages.
  const context = await browser.newContext({viewport: {width: 1100, height: 950}});
  const page = await context.newPage();
  await page.route('http://panel.test/**', async route => {
    const url = new URL(route.request().url());
    if (url.pathname === '/') return route.fulfill({contentType: 'text/html', body: html});
    if (url.pathname === '/panel/panel.js') return route.fulfill({contentType: 'text/javascript', body: script});
    if (url.pathname === '/panel/api') return route.fulfill({json: {ticket: 1}});
    if (url.pathname === '/panel/result') return route.fulfill({json: {pages: []}});
    return route.fulfill({status: 204, body: ''});
  });
  // MDI icons (api.iconify.design): served offline from @mdi/svg (MDI_SVG_DIR,
  // installed by render.sh) or the repository's icons, else a neutral dot.
  const mdi = name => [process.env.MDI_SVG_DIR || '', 'assets/entity_icons', 'assets/panel']
    .filter(Boolean).map(dir => path.join(dir, name + '.svg')).find(f => fs.existsSync(f));
  const dot = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><circle cx="12" cy="12" r="6" fill="#fff"/></svg>';
  // Anything else outside the page (artwork on homeassistant.local, ...) fails at
  // once instead of holding the page open.
  await context.route(url => !/^http:\/\/panel\.test\//.test(url.href) && !/^https:\/\/api\.iconify\.design\//.test(url.href),
    route => route.abort());
  await context.route(/^https:\/\/api\.iconify\.design\//, route => {
    const url = new URL(route.request().url());
    const name = url.pathname.split('/').pop().replace(/\.svg$/, '');
    const file = mdi(name), color = url.searchParams.get('color') || '#ffffff';
    const body = (file ? read(file) : dot).replace(/fill="[^"]*"/g, '').replace('<svg ', `<svg fill="${color}" `);
    return route.fulfill({contentType: 'image/svg+xml', body});
  });
  await page.goto('http://panel.test/');
  await page.waitForFunction(() => !!globalThis.ONX_PANEL_PREVIEW);
  await page.addStyleTag({content: fonts});
  await page.evaluate(async () => { await document.fonts.load('500 16px MiSans'); await document.fonts.load('700 16px MiSans'); });

  const files = fs.readdirSync(path.join(out, 'preview')).filter(f => f.endsWith('.json')).sort();
  fs.mkdirSync(path.join(out, 'web'), {recursive: true});
  for (const file of files) {
    const shot = JSON.parse(read(path.join(out, 'preview', file)));
    const svg = await page.evaluate(shot => {
      globalThis.ONX_PANEL_SIZE = {width: shot.width, height: shot.height};
      const slots = shot.entities.map(e => ({entity: e.entity_id, name: e.name || e.entity_id}));
      const views = shot.data.slots || [];
      globalThis.ONX_APP = {slots: () => slots, views: () => views, settings: () => ({}), select: () => {}, refresh: () => {},
        pick: () => {}, item: () => null, add: () => -1, clear: () => {}, setTimer: () => {}, dirtyAt: () => false, message: () => {}};
      ONX_PAGES.set(shot.pages);
      const active = Math.max(0, slots.findIndex(s => s.entity === shot.entity));
      ONX_PANEL_EDITOR(slots, active, false, () => {});
      const svg = document.getElementById('device-preview');
      ONX_PANEL_PREVIEW(svg, slots[active], views.find(v => v.entity === shot.entity), views, shot.screen, {});
      const copy = svg.cloneNode(true);
      copy.setAttribute('width', shot.width); copy.setAttribute('height', shot.height);
      copy.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
      return copy.outerHTML;
    }, shot);
    const shotPage = await context.newPage();
    await shotPage.setViewportSize({width: shot.width, height: shot.height});
    await shotPage.setContent(`<style>${fonts}html,body{margin:0;background:#000}svg{display:block}</style>${svg}`);
    await shotPage.evaluate(async () => {
      await document.fonts.ready;
      await Promise.all([...document.images, ...document.querySelectorAll('image')].map(i => i.decode?.().catch(() => {})));
    });
    await shotPage.waitForLoadState('networkidle', {timeout: 5000}).catch(() => {});
    await shotPage.screenshot({path: path.join(out, 'web', file.replace(/\.json$/, '.png'))});
    await shotPage.close();
  }

  // Side-by-side sheets.
  fs.mkdirSync(path.join(out, 'compare'), {recursive: true});
  const data = file => 'data:image/png;base64,' + fs.readFileSync(file).toString('base64');
  const byModel = {};
  for (const file of files) {
    const name = file.replace(/\.json$/, '');
    const device = path.join(out, name + '.png'), web = path.join(out, 'web', name + '.png');
    if (!fs.existsSync(device) || !fs.existsSync(web)) continue;
    (byModel[name.split('-')[0]] ||= []).push({name, device: data(device), web: data(web)});
  }
  const cell = s => `<figure><figcaption>${s.name}</figcaption><div><img src="${s.device}"><img src="${s.web}"></div>` +
    `<p><span>emulator (device code)</span><span>web preview</span></p></figure>`;
  const style = `body{margin:0;padding:12px;background:#222;font:12px sans-serif;color:#ddd;display:flex;flex-wrap:wrap;gap:16px}
    figure{margin:0}figcaption{margin-bottom:4px}div{display:flex;gap:6px}img{display:block;outline:1px solid #555}
    p{display:flex;justify-content:space-around;margin:4px 0 0;color:#999}`;
  const sheet = await browser.newPage({viewport: {width: 900, height: 600}});
  for (const [model, shots] of Object.entries(byModel)) {
    for (const s of shots) {
      await sheet.setContent(`<style>${style}</style>${cell(s)}`);
      await sheet.locator('figure').screenshot({path: path.join(out, 'compare', s.name + '.png')});
    }
    await sheet.setViewportSize({width: model === '35' ? 2200 : 1700, height: 600});
    await sheet.setContent(`<style>${style}</style>${shots.map(cell).join('')}`);
    await sheet.screenshot({path: path.join(out, 'compare', model + '-all.png'), fullPage: true});
    console.log(`compare: ${shots.length} ${model} screens -> ${path.join(out, 'compare')}`);
  }
  await browser.close();
})().catch(error => { console.error(error); process.exit(1); });

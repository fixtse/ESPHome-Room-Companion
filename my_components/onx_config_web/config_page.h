#pragma once
namespace esphome::onx_config_web {
static const char CONFIG_PAGE[] = R"ONXHTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark"><title>OpenNextion Room Companion by fixtSE</title>
<link rel="icon" href="/panel/favicon.svg" type="image/svg+xml">
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin><link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:opsz,wght@12..96,500..700&amp;family=Geist:wght@400..700&amp;family=Geist+Mono:wght@400..500&amp;display=swap">
<style>
:root{--sans:"Geist","Inter",ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif;--display:"Bricolage Grotesque","Geist",ui-sans-serif,system-ui,sans-serif;--mono:"Geist Mono",ui-monospace,"SFMono-Regular",Menlo,monospace;--accent:#f97316;--bg:#f3f2ee;--rail:#ebe9e3;--paper:#fff;--raised:#f8f7f4;--line:#dedad2;--line-strong:#c9c4b8;--ink:#16181d;--muted:#676b74;--error:#b42318;--ok:#16a34a;--accent-text:color-mix(in oklab,var(--accent) 72%,#000);--wash:color-mix(in oklab,var(--accent) 12%,var(--paper));--shadow:0 1px 2px #1a17110d,0 8px 24px -12px #1a171126;font-family:var(--sans);font-size:15px;color:var(--ink);background:var(--bg);color-scheme:light}
:root[data-theme=dark]{--bg:#0b0d11;--rail:#0f1217;--paper:#14181e;--raised:#191e26;--line:#232933;--line-strong:#333b47;--ink:#eef1f5;--muted:#8b93a1;--error:#ff8a80;--ok:#4ade80;--accent-text:color-mix(in oklab,var(--accent) 82%,#fff);--wash:color-mix(in oklab,var(--accent) 14%,var(--paper));--shadow:0 1px 0 #ffffff08 inset,0 12px 32px -16px #000c;color-scheme:dark}
@media(prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#0b0d11;--rail:#0f1217;--paper:#14181e;--raised:#191e26;--line:#232933;--line-strong:#333b47;--ink:#eef1f5;--muted:#8b93a1;--error:#ff8a80;--ok:#4ade80;--accent-text:color-mix(in oklab,var(--accent) 82%,#fff);--wash:color-mix(in oklab,var(--accent) 14%,var(--paper));--shadow:0 1px 0 #ffffff08 inset,0 12px 32px -16px #000c;color-scheme:dark}}
*{box-sizing:border-box}
html,body{background:var(--bg)}
body{margin:0;min-height:100dvh;display:grid;grid-template-columns:252px minmax(0,1fr);line-height:1.5;-webkit-font-smoothing:antialiased}
[hidden]{display:none!important}
.sr{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap}
a{color:var(--accent-text)}
a:focus-visible{outline:2px solid var(--accent);outline-offset:3px;border-radius:4px}
h1,h2,h3{font-family:var(--display);letter-spacing:-.015em;margin:0;font-weight:650}
h2{font-size:22px}
h3{font-size:15px;margin:0 0 12px}
p{margin:0;max-width:72ch}
.hint,small{font-size:13.5px;color:var(--muted)}
.hint{margin:10px 0;line-height:1.55}
button,input,select{font:inherit;color:var(--ink)}
button,input,select{border:1px solid var(--line);border-radius:10px;padding:9px 13px;background:var(--paper)}
input,select{background:var(--raised)}
input::placeholder{color:color-mix(in oklab,var(--muted) 80%,transparent)}
button{cursor:pointer;min-height:40px;font-weight:550;transition:background .12s,border-color .12s,color .12s,transform .12s}
button:hover{border-color:var(--line-strong);background:var(--raised)}
button:active{transform:translateY(1px)}
button:focus-visible,input:focus-visible,select:focus-visible,summary:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
input:focus,select:focus{border-color:var(--accent)}
button:disabled{opacity:.45;cursor:default;transform:none}
.primary{background:var(--accent);border-color:var(--accent);color:#111;font-weight:650}
.primary:hover{background:color-mix(in oklab,var(--accent) 88%,#fff);border-color:transparent}
.quiet{border-color:transparent;background:transparent;color:var(--muted)}
.quiet:hover{background:var(--raised);color:var(--ink);border-color:transparent}
button.danger{color:var(--error)}
input[type=checkbox]{width:18px;height:18px;padding:0;accent-color:var(--accent)}
/* ------------------------------------------------ rail */
.rail{position:sticky;top:0;height:100dvh;display:flex;flex-direction:column;gap:22px;padding:22px 16px 18px;background:var(--rail);border-right:1px solid var(--line);overflow:auto}
.rail header{display:flex;flex-direction:column;gap:18px;padding:0;margin:0}
.brand-lockup{display:flex;align-items:center;gap:12px;min-width:0}
.brand-logo{display:grid;place-items:center;flex:none;width:44px;height:44px;border-radius:12px;background:#0f1217;border:1px solid #2a313c;padding:7px}
.brand-logo img{width:100%;height:100%;object-fit:contain;display:block}
.rail h1{font-size:17px;line-height:1.15;letter-spacing:-.01em}
.brand-by{display:block;font-family:var(--sans);font-size:12px;font-weight:500;color:var(--muted);letter-spacing:.02em;margin-top:3px}
.rail header .hint{display:none}
.rail header label{order:9}
.tabs{display:flex;flex-direction:column;gap:2px}
.tabs button{display:flex;align-items:center;gap:11px;width:100%;text-align:left;border:0;background:transparent;color:var(--muted);padding:10px 12px;border-radius:10px;font-weight:550}
.tabs button svg{width:18px;height:18px;flex:none;fill:none;stroke:currentColor;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
.tabs button:hover{background:color-mix(in oklab,var(--ink) 5%,transparent);color:var(--ink)}
.tabs button[aria-current=page]{background:var(--paper);color:var(--ink);box-shadow:var(--shadow),inset 3px 0 0 var(--accent)}
.tabs button[aria-current=page] svg{color:var(--accent)}
.rail-foot{margin-top:auto;display:flex;flex-direction:column;gap:14px}
.connections{display:flex;flex-direction:column;gap:2px;padding:12px;border:1px solid var(--line);border-radius:12px;background:var(--paper)}
.connections:before{content:"Connections";font-size:11px;font-weight:650;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:6px}
.connection{display:flex;align-items:flex-start;gap:9px;font-size:12.5px;line-height:1.35;color:var(--muted);padding:3px 0}
.connection:before{content:"";flex:none;width:8px;height:8px;border-radius:50%;margin-top:4px;background:var(--line-strong)}
.connection[data-ok=true]{color:var(--ink)}
.connection[data-ok=true]:before{background:var(--ok);box-shadow:0 0 0 3px color-mix(in oklab,var(--ok) 22%,transparent)}
.connection[data-ok=false]:before{background:var(--error)}
#theme{width:100%;font-size:13px;padding:8px 10px}
/* ------------------------------------------------ stage */
.stage{min-width:0;display:flex;flex-direction:column}
main{width:100%;max-width:1320px;margin:0 auto;padding:26px 32px 40px;flex:1}
.status{max-width:1256px;width:calc(100% - 64px);margin:22px auto 0;padding:11px 14px 11px 40px;border:1px solid var(--line);border-radius:12px;background:var(--paper);font-size:13.5px;position:relative;box-shadow:var(--shadow)}
.status:before{content:"";position:absolute;left:16px;top:50%;width:10px;height:10px;margin-top:-5px;border-radius:50%;background:var(--accent);box-shadow:0 0 0 4px var(--wash)}
.status.error{border-color:color-mix(in oklab,var(--error) 45%,var(--line));color:var(--error)}
.status.error:before{background:var(--error);box-shadow:0 0 0 4px color-mix(in oklab,var(--error) 18%,transparent)}
.status:empty{display:none}
#draft-notice{max-width:1256px;width:calc(100% - 64px);margin:16px auto 0;padding:14px 16px;background:var(--wash);border:1px solid color-mix(in oklab,var(--accent) 40%,var(--line));border-radius:12px;display:flex;gap:12px;align-items:center;justify-content:space-between;flex-wrap:wrap}
footer{font-size:12.5px;color:var(--muted);padding:18px 32px 26px;text-align:center}
footer a{color:var(--muted);text-decoration:none;font-weight:550}
footer a:hover{color:var(--ink)}
.toolbar,.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.toolbar{justify-content:space-between;margin-bottom:14px}
.counter{font-size:12.5px;color:var(--muted);font-family:var(--mono)}
.unsaved{display:inline-block;margin-left:10px;font-size:12px;font-weight:600;color:var(--accent-text);background:var(--wash);border-radius:999px;padding:2px 9px;vertical-align:middle}
.unsaved:empty{display:none}
.help-links{display:flex;flex-wrap:wrap;gap:6px 18px;margin:0 0 16px;font-size:13px}
.help-links a{color:var(--accent-text);text-decoration:none;padding:3px 0;border-bottom:1px solid color-mix(in oklab,var(--accent) 35%,transparent)}
.help-links a:hover{border-bottom-color:var(--accent)}
.help-links a:after{content:" ↗";font-size:11px}
/* ------------------------------------------------ workspace */
.workspace{display:grid;grid-template-columns:236px minmax(0,1fr) 300px;gap:28px;align-items:start}
.workspace>section{min-width:0;background:var(--paper);border:1px solid var(--line);border-radius:18px;padding:22px 24px;box-shadow:var(--shadow)}
.slot-column{display:flex;flex-direction:column;gap:14px;min-width:0;position:sticky;top:20px}
.column-heading{display:flex;align-items:center;justify-content:space-between}
.column-heading h2,.toggle-label{font-family:var(--sans);font-size:11px;font-weight:650;letter-spacing:.09em;text-transform:uppercase;color:var(--muted)}
.slots-heading{padding:0 4px}
.slot-list{display:flex;flex-direction:column;gap:4px;counter-reset:slot}
.slot-list button{display:flex;gap:10px;align-items:center;min-width:0;text-align:left;padding:9px 10px;border:1px solid transparent;background:transparent;border-radius:12px;font-weight:500}
.slot-list button:hover{background:var(--paper);border-color:var(--line)}
.slot-list button[aria-current=true]{background:var(--paper);border-color:var(--line);box-shadow:var(--shadow),inset 3px 0 0 var(--accent)}
.slot-list b{font-family:var(--mono);font-size:11.5px;font-weight:500;color:var(--muted);min-width:20px}
.slot-list span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:13.5px}
.slot-list .slot-icon{display:inline-grid;place-items:center;width:28px;height:28px;flex:0 0 28px;border-radius:8px;background:var(--raised);border:1px solid var(--line);color:var(--muted)}
.slot-list button[aria-current=true] .slot-icon{color:var(--accent);border-color:color-mix(in oklab,var(--accent) 40%,var(--line));background:var(--wash)}
.slot-icon svg,.slot-icon img{width:17px;height:17px}
.slot-icon svg{fill:currentColor}
.slot-list .dirty-dot{color:var(--accent);margin-left:auto;flex-shrink:0;font-size:10px}
.preset-card{display:flex;flex-direction:column;gap:10px;padding:12px 14px;border:1px solid var(--line);border-radius:14px;background:var(--paper);box-shadow:var(--shadow)}
.preset-heading{margin:0}
.preset-heading button{all:unset;box-sizing:border-box;display:grid;grid-template-columns:1fr auto auto;align-items:center;gap:4px 8px;width:100%;min-height:30px;cursor:pointer;border-radius:8px}
.preset-heading button:focus-visible{outline:2px solid var(--accent);outline-offset:3px}
.preset-summary{grid-column:1/-1;grid-row:2;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:14.5px;font-weight:650;color:var(--ink);font-family:var(--display)}
.preset-summary:empty{display:none}
.preset-heading [aria-expanded=true] .preset-summary{display:none}
.preset-heading .chevron{width:16px;height:16px;fill:none;stroke:var(--muted);stroke-width:2;stroke-linecap:round;stroke-linejoin:round;transition:transform .15s}
.preset-heading [aria-expanded=true] .chevron{transform:rotate(180deg)}
.preset-body{display:flex;flex-direction:column;gap:10px}
.preset-bar,.preset-actions{display:flex;gap:6px}
.preset-bar select{flex:1;min-width:0;font-weight:600}
.preset-actions>button:not(.icon-button){flex:1 1 auto;min-width:0;white-space:nowrap}
.preset-mode{font-size:12.5px;line-height:1.45;color:var(--muted)}
#preset-apply svg{fill:currentColor;stroke:none}
.mode-badge{font-size:10.5px;font-weight:650;letter-spacing:.05em;text-transform:uppercase;padding:2px 8px;border-radius:999px;border:1px solid var(--line);color:var(--muted);white-space:nowrap}
.mode-badge:empty{display:none}
.mode-badge[data-mode=live]{border-color:color-mix(in oklab,var(--ok) 50%,transparent);color:var(--ok)}
.mode-badge[data-mode=active]{border-color:var(--accent);color:var(--accent-text)}
.mode-badge[data-mode=new]{border-style:dashed}
.icon-button{width:40px;min-width:40px;height:40px;padding:0;display:inline-grid;place-items:center;flex:none}
.icon-button svg{width:18px;height:18px;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}
/* editor */
.workspace>section>.toolbar{margin-bottom:12px}
#slot-title{font-size:26px;display:inline}
.slot-tools{margin:0 0 18px;padding-bottom:18px;border-bottom:1px solid var(--line)}
.slot-tools .hint{margin:0 0 0 6px;font-family:var(--mono);font-size:12px}
.editor-tabs{display:inline-flex;gap:2px;padding:4px;border-radius:12px;background:var(--raised);border:1px solid var(--line);max-width:100%;overflow-x:auto}
.editor-tabs button{flex-shrink:0;white-space:nowrap;border:0;border-radius:9px;background:transparent;padding:7px 16px;min-height:34px;font-weight:600;color:var(--muted)}
.editor-tabs button:hover{color:var(--ink);background:transparent}
.editor-tabs button[aria-selected=true]{color:var(--ink);background:var(--paper);box-shadow:0 1px 2px #0000001f,0 0 0 1px var(--line)}
.editor-pane{padding:22px 0 4px}
.binding{position:relative;background:var(--raised);padding:18px 18px 16px;border:1px solid var(--line);border-radius:14px;min-width:0}
.binding.active{border-color:color-mix(in oklab,var(--accent) 55%,var(--line));background:linear-gradient(var(--wash),var(--raised) 70%)}
.binding h3{font-family:var(--sans);font-size:11px;font-weight:650;letter-spacing:.09em;text-transform:uppercase;color:var(--muted);margin-bottom:8px}
.binding strong{display:block;font-family:var(--display);font-size:20px;font-weight:650;overflow-wrap:anywhere;line-height:1.25}
.binding small{display:block;font-family:var(--mono);font-size:12px;overflow-wrap:anywhere;margin:6px 0 14px}
.binding small:empty{margin:0 0 14px}
.binding button{font-size:13px;min-height:36px;padding:6px 12px}
.editor-pane>.binding{margin-bottom:20px}
.field{display:grid;align-content:start;min-width:0;gap:7px;margin:16px 0;font-size:13.5px;font-weight:550}
.field input,.field select{width:100%;min-width:0;font-weight:400;min-height:42px}
.field>span:has(>input[type=checkbox]),.checkbox-field>span{display:flex;align-items:center;gap:10px;font-weight:500}
.field input[type=checkbox]{width:18px;min-height:0;flex:none;margin:0}
.field-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;align-items:start}
.field-grid>.field{margin:0}
.field-grid>.field-note,.field-grid>.checkbox-field{grid-column:1/-1}
.field-note{margin-top:-6px}
#sensor-options{border-top:1px solid var(--line);padding-top:20px;margin-top:24px}
#sensor-options h3{font-size:16px}
/* preview */
.preview{position:sticky;top:20px;display:flex;flex-direction:column;gap:12px;padding:18px;border:1px solid var(--line);border-radius:18px;background:var(--paper);box-shadow:var(--shadow)}
.preview>h3{font-size:16px;margin:0}
.preview>.field{margin:0}
.preview .screen-frame{margin:6px auto 0;width:calc(100% - 58px);max-width:220px;aspect-ratio:2/3;height:auto;padding:9px;box-sizing:content-box;border-radius:26px;background:linear-gradient(160deg,#2b2f36,#0d0f12 60%);box-shadow:0 0 0 1px #00000080,0 1px 0 1px #ffffff14 inset,0 22px 40px -18px #000c;overflow:hidden}
.preview .screen-frame svg{display:block;width:100%;height:100%;border-radius:17px;background:#000;font-family:Arial,sans-serif}
.preview-label{font-size:12px;color:var(--muted);text-align:center}
.preview-ring{transition:stroke-dasharray 2s linear}
.preview-ring[data-playing=false]{transition:none}
.controls{margin:0;display:grid;grid-template-columns:auto 1fr;gap:0;font-size:12.5px;border:1px solid var(--line);border-radius:12px;overflow:hidden}
.controls dt,.controls dd{padding:8px 10px;border-top:1px solid var(--line);margin:0}
.controls dt:first-of-type,.controls dd:first-of-type{border-top:0}
.controls dt{color:var(--muted);background:var(--raised);max-width:120px}
.controls dd{line-height:1.4}
.preview .hint{margin:0;font-size:12.5px}
.preview .help-links{margin:0}
/* ------------------------------------------------ pages */
.panel{max-width:880px;background:var(--paper);border:1px solid var(--line);padding:26px 28px;border-radius:18px;margin:0 0 20px;box-shadow:var(--shadow)}
.panel>h2{font-size:24px;margin-bottom:10px}
.panel>p{color:var(--muted)}
.panel details{margin:18px 0 0;padding:12px 14px;border:1px solid var(--line);border-radius:12px;background:var(--raised)}
.panel summary{cursor:pointer;font-weight:600}
.panel details p{margin-top:8px;color:var(--muted);font-size:13.5px}
.panel>.row{margin-top:16px}
dl.info{display:grid;grid-template-columns:180px 1fr;margin:14px 0 18px;font-size:13.5px;border:1px solid var(--line);border-radius:12px;overflow:hidden}
.info dt,.info dd{padding:9px 12px;border-top:1px solid var(--line);margin:0}
.info dt:first-of-type,.info dd:first-of-type{border-top:0}
.info dt{color:var(--muted);background:var(--raised)}
.info dd{overflow-wrap:anywhere;font-family:var(--mono);font-size:12.5px}
#settings-form{max-width:920px}
.settings-section{border:1px solid var(--line);border-radius:14px;padding:18px 20px 20px;margin:18px 0 0;min-width:0;background:var(--raised)}
.settings-section>legend{font-family:var(--display);font-size:17px;font-weight:650;padding:0 8px;margin-left:-8px}
.settings-section>.hint:first-of-type{margin-top:0}
#settings-form .field select,#settings-form .field input:not([type=checkbox]):not([type=color]){height:44px;background:var(--paper)}
.settings-actions{position:sticky;bottom:0;display:flex;gap:10px;flex-wrap:wrap;margin:22px -28px -26px;padding:16px 28px;border-top:1px solid var(--line);background:color-mix(in oklab,var(--paper) 88%,transparent);backdrop-filter:blur(8px);border-radius:0 0 18px 18px}
.ring-picker{border:1px solid var(--line);border-radius:12px;padding:16px;margin:16px 0 0;background:var(--paper)}
.ring-picker legend{font-weight:650;padding:0 6px}
.color-swatches{display:flex;gap:10px;flex-wrap:wrap;margin-top:14px}
.color-swatches button{width:34px;height:34px;min-height:34px;padding:0;border:3px solid var(--paper);border-radius:50%;background:var(--swatch);box-shadow:0 0 0 1px var(--line)}
.color-swatches button[aria-pressed=true]{box-shadow:0 0 0 2px var(--ink)}
.ring-sample{width:56px;height:56px;border-radius:50%;background:conic-gradient(var(--ring-color,#62b0ff) 72%,var(--line) 0);position:relative;flex:none}
.ring-sample:after{content:"";position:absolute;inset:6px;border-radius:50%;background:var(--paper)}
.color-custom{display:flex;align-items:center;gap:10px;font-size:13.5px}
.color-custom input{width:46px;height:36px;padding:3px;border-radius:8px}
#color-value{display:block;margin-top:4px;font-family:var(--mono)}
.panel-page-layout{margin:18px 0;padding:16px;border:1px solid var(--line);border-radius:12px;background:var(--raised)}
.panel-page-layout select{width:100%}
.page-list{list-style:none;margin:8px 0;padding:0;display:flex;flex-direction:column;gap:6px}
.page-list li{cursor:grab}.page-list button{width:100%;text-align:left}.page-list button[aria-current=true]{outline:2px solid var(--accent)}
.page-grid{display:grid;gap:8px;margin:12px 0;aspect-ratio:2/3;max-width:260px}
.page-grid.single{grid-template-rows:1fr}.page-grid.two_controls{grid-template-rows:1fr 1fr}
.page-grid.control_sensors{grid-template-rows:55fr 15fr 15fr 15fr}
.page-cell{display:flex;flex-direction:column;gap:2px;padding:8px;border:1px solid var(--line);border-radius:10px;background:var(--paper);overflow:hidden;cursor:pointer;min-height:0}
.page-cell.empty{border-style:dashed;cursor:default}.page-cell.compact{flex-direction:row;align-items:center;flex-wrap:wrap}
.page-cell.drop{outline:2px dashed var(--accent)}.page-cell[aria-current=true]{outline:2px solid var(--accent)}
.page-cell small{opacity:.7;font-size:11px}.page-cell button{padding:2px 8px;font-size:12px}
/* combo */
.combo{position:relative;min-width:0;margin-top:2px}
.combo>select{display:none}
.combo-trigger{width:100%;min-width:0;min-height:44px;display:flex;align-items:center;justify-content:space-between;gap:8px;border:1px solid var(--line);border-radius:10px;padding:9px 13px;background:var(--paper);font-weight:500;text-align:left}
.combo-trigger span{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.combo-trigger:after{content:"";flex:none;width:7px;height:7px;border-right:2px solid var(--muted);border-bottom:2px solid var(--muted);transform:rotate(45deg) translateY(-2px)}
.combo-pop{position:absolute;z-index:30;left:0;right:0;top:calc(100% + 6px);background:var(--paper);border:1px solid var(--line);border-radius:12px;box-shadow:0 18px 40px -12px #0006;overflow:hidden}
.combo .combo-search,.combo .combo-search:focus{display:block;width:100%;margin:0;border:0;border-bottom:1px solid var(--line);border-radius:0;padding:12px 14px;background:transparent;outline:none}
.combo-list{max-height:260px;overflow-y:auto;padding:4px;margin:0;list-style:none}
.combo-list li{padding:8px 10px;border-radius:8px;cursor:pointer;line-height:1.3;font-size:14px}
.combo-list li small{display:block;font-family:var(--mono);font-size:11.5px;overflow-wrap:anywhere}
.combo-list li[aria-selected=true]{font-weight:650;color:var(--accent-text)}
.combo-list li.hl,.combo-list li:hover{background:var(--wash)}
.combo-empty{padding:14px;color:var(--muted);text-align:center;font-size:13.5px}
/* playlists */
.playlist-row{padding:20px 0;border-bottom:1px solid var(--line)}
.playlist-row:first-child{padding-top:10px}
.playlist-row-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px}
.playlist-order{font-family:var(--mono);font-weight:500;color:var(--muted)}
.playlist-row-tools{display:flex;gap:6px}
.playlist-fields{display:grid;grid-template-columns:minmax(130px,1fr) minmax(180px,1.35fr);gap:16px}
.playlist-fields .field{margin:0}
.field-caption{display:block;font-size:13.5px;color:var(--muted);margin-bottom:8px}
.playlist-action{min-width:0}
.playlist-action strong{display:block;overflow-wrap:anywhere}
.playlist-action small{display:block;font-family:var(--mono);font-size:12px;overflow-wrap:anywhere;margin:4px 0 10px}
#playlist-options>.hint{margin:0 0 16px}
#playlist-options .toolbar h3{margin:0}
#add-playlist{width:100%;margin-top:16px;border-style:dashed}
.empty{padding:22px 0;color:var(--muted);text-align:center}
/* dialogs */
dialog{color:var(--ink)}
.ask{width:min(480px,calc(100% - 32px));border:1px solid var(--line);border-radius:18px;padding:24px;background:var(--paper);box-shadow:0 30px 80px -20px #000a}
.ask[open]{animation:pop .16s ease-out}
.ask::backdrop,#entity-picker::backdrop{background:#05070acc;backdrop-filter:blur(3px);animation:fade .16s ease-out}
@keyframes pop{from{opacity:0;transform:translateY(6px) scale(.98)}to{opacity:1;transform:none}}
@keyframes fade{from{opacity:0}}
.ask h2{font-size:19px;margin:0 0 8px}
.ask p{color:var(--muted);font-size:14px}
.ask .ask-field{margin:16px 0 0}
.ask-error{color:var(--error)!important;margin-top:8px!important}
.ask-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:22px}
.ask .danger-action{background:var(--error);border-color:var(--error);color:#fff}
#entity-picker{width:min(720px,calc(100% - 32px));max-height:88dvh;border:1px solid var(--line);border-radius:20px;padding:0;background:var(--paper);box-shadow:0 30px 90px -20px #000b}
#entity-picker[open]{animation:pop .16s ease-out}
.picker-heading{display:flex;justify-content:space-between;align-items:flex-start;gap:18px;padding:22px 24px 16px;border-bottom:1px solid var(--line)}
.picker-heading h2{font-size:21px}
.picker-heading .hint{margin:4px 0 0}
.picker-body{padding:18px 24px 22px;max-height:66dvh;overflow:auto;overscroll-behavior:contain}
.picker-body .toolbar{margin:0}
.picker-body .toolbar h3{font-family:var(--sans);font-size:11px;font-weight:650;letter-spacing:.09em;text-transform:uppercase;color:var(--muted);margin:0}
.search{width:100%;margin:12px 0 4px;min-height:46px;font-size:15px;padding-left:40px;background:var(--raised) url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%238b93a1' stroke-width='2' stroke-linecap='round'%3E%3Ccircle cx='11' cy='11' r='7'/%3E%3Cpath d='m20 20-3.5-3.5'/%3E%3C/svg%3E") no-repeat 13px 50%/17px}
.filters{display:flex;gap:6px;flex-wrap:wrap;margin:14px 0}
.filters button{font-size:12.5px;min-height:32px;padding:5px 11px;border-radius:999px}
.filters button[aria-pressed=true]{background:var(--ink);border-color:var(--ink);color:var(--paper)}
#manual{margin:10px 0}
.results{border-top:1px solid var(--line)}
.result{display:flex;align-items:center;justify-content:space-between;gap:12px;width:100%;text-align:left;border:0;border-bottom:1px solid var(--line);border-radius:0;background:transparent;padding:13px 8px;font-weight:500}
.result:hover{background:var(--raised)}
.result span{min-width:0}
.result strong{display:block;font-weight:600}
.result small{display:block;font-family:var(--mono);font-size:12px;overflow-wrap:anywhere;margin-top:3px}
.result em{flex:none;font-size:11.5px;color:var(--muted);font-style:normal;max-width:110px;text-align:right}
.result.selected{background:var(--wash);box-shadow:inset 3px 0 0 var(--accent)}
.more{width:100%;margin-top:12px}
/* ------------------------------------------------ responsive */
@media(max-width:1180px){.workspace{grid-template-columns:210px minmax(0,1fr)}
.preview{position:static;grid-column:2;display:grid;grid-template-columns:220px 1fr;align-items:start;gap:14px 20px}
.preview>h3,.preview>.field,.preview>p{grid-column:1/-1}
.preview .screen-frame{grid-row:span 2;width:100%;margin:0}}
@media(max-width:860px){body{display:block}
.rail{position:sticky;z-index:20;height:auto;flex-direction:row;flex-wrap:wrap;align-items:center;gap:10px 14px;padding:12px 16px;border-right:0;border-bottom:1px solid var(--line);background:color-mix(in oklab,var(--rail) 92%,transparent);backdrop-filter:blur(10px)}
.rail header{flex-direction:row;align-items:center;flex:1;gap:12px}
.rail header label{order:0;margin-left:auto}
#theme{width:auto}
.brand-logo{width:36px;height:36px;padding:5px;border-radius:10px}
.tabs{order:3;flex-basis:100%;flex-direction:row;overflow-x:auto;scrollbar-width:none}
.tabs button{width:auto;white-space:nowrap;padding:8px 12px}
.tabs button[aria-current=page]{box-shadow:var(--shadow),inset 0 -2px 0 var(--accent)}
.rail-foot{order:2;margin:0}
.connections{flex-direction:row;gap:12px;padding:0;border:0;background:transparent}
.connections:before{display:none}
.connection{font-size:0;padding:0}
.connection:before{margin:0}
main{padding:18px 16px 32px}
.status,#draft-notice{width:calc(100% - 32px);margin-top:14px}
.workspace{display:block}
.slot-column{position:static;margin-bottom:16px}
.slot-list{flex-direction:row;overflow-x:auto;padding:2px 2px 8px;scrollbar-width:thin}
.slot-list button{flex-shrink:0;max-width:200px;background:var(--paper);border-color:var(--line)}
.workspace>section{padding:18px 16px;border-radius:16px}
.preview{display:flex;margin-top:16px}
.field-grid{grid-template-columns:1fr}
.panel{padding:20px 18px}
.settings-actions{margin:20px -18px -20px;padding:14px 18px}
dl.info{grid-template-columns:120px 1fr}
.playlist-fields{grid-template-columns:1fr}}
@media(max-width:520px){.toolbar>.row{width:100%}
.toolbar>.row button{flex:1;white-space:nowrap;padding:8px 9px;font-size:13.5px}
#slot-title{font-size:22px}
.ask-actions{flex-direction:column-reverse}
.ask-actions button{width:100%}
.picker-heading{padding:18px}
.picker-body{padding:14px 18px}
#entity-picker{width:calc(100% - 20px)}}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}

.rail-toggle{width:40px;height:40px;flex:none;padding:0;font-size:22px;align-self:flex-end}
body.rail-collapsed{grid-template-columns:76px minmax(0,1fr)}
.rail-collapsed .rail{padding-left:12px;padding-right:12px}
.rail-collapsed .rail-toggle{align-self:center}
.rail-collapsed .brand-lockup{display:none}
.rail-collapsed .tabs button{font-size:0;justify-content:center;padding:12px}
.rail-collapsed .connections{padding:10px;border:0;background:transparent}
.rail-collapsed .connections:before,.rail-collapsed .connection{font-size:0}
.rail-collapsed .rail-foot>label{display:none}
.discovery{border:1px solid var(--line);border-radius:12px;background:var(--raised);padding:14px 16px;margin:12px 0 16px}.discovery-head{display:flex;align-items:center;justify-content:space-between;gap:12px}.discovery-head h3{margin:0;font-size:15px}.icon-btn{display:inline-grid;place-items:center;width:34px;height:34px;padding:0;border-radius:50%}.ha-scanner{width:132px;height:132px;margin:6px auto 2px}.ha-scanner svg{width:100%;height:100%;display:block}.ha-search-rotor,.ha-pulse{transform-box:view-box;transform-origin:50% 50%}.ha-pulse{opacity:0}.ha-scanner.loading .ha-search-rotor{animation:spin 1.5s linear infinite}.ha-scanner.loading .ha-pulse{animation:ha-pulse 1.8s ease-out infinite}@keyframes spin{to{transform:rotate(360deg)}}@keyframes ha-pulse{from{transform:scale(.35);opacity:.9}to{transform:scale(1.05);opacity:0}}@media(prefers-reduced-motion:reduce){.ha-scanner.loading .ha-search-rotor,.ha-scanner.loading .ha-pulse{animation:none}}#ha-discovery-status{text-align:center;margin:4px 0 8px}.ha-results{display:grid;gap:8px}.ha-result{display:flex;align-items:center;gap:12px;width:100%;text-align:left;padding:10px 12px;border-radius:10px;animation:ha-in .25s ease-out both}.ha-result svg{flex:none}.ha-result span{display:grid;min-width:0;flex:1}.ha-result b{font-weight:650}.ha-result small{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.ha-result .chev{flex:none;color:var(--muted);font-size:20px;line-height:1}@keyframes ha-in{from{opacity:0;transform:translateY(4px)}}@media(prefers-reduced-motion:reduce){.ha-result{animation:none}}
@media(max-width:860px){.rail-toggle{order:-1;align-self:center}.rail-collapsed .rail header{display:none}.rail-collapsed .tabs,.rail-collapsed .rail-foot{display:none}.rail-collapsed .rail{padding:8px 12px}.rail-collapsed .rail-toggle{margin:0}.rail:not(.rail-collapsed){gap:10px}}
</style></head><body>
<aside class="rail">
<button id="rail-toggle" class="rail-toggle" type="button" aria-expanded="true" aria-controls="rail-navigation" aria-label="Collapse sidebar" title="Collapse sidebar">☰</button>
<header><div class="brand-lockup"><a class="brand-logo" href="https://fixtse.com/" target="_blank" rel="noopener noreferrer" aria-label="Visit fixtSE"><img src="/panel/fixtse-logo.png" width="56" height="52" alt="fixtSE logo"></a><div><h1>OpenNextion Room Companion <span class="brand-by">by fixtSE</span></h1><p class="hint">Your rooms, controls, and everyday shortcuts.</p></div></div></header>
<nav id="rail-navigation" class="tabs" aria-label="Settings pages"><button data-page="entities" aria-current="page"><svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="3" width="7" height="7" rx="2"/><rect x="14" y="3" width="7" height="7" rx="2"/><rect x="3" y="14" width="7" height="7" rx="2"/><rect x="14" y="14" width="7" height="7" rx="2"/></svg>Display &amp; actions</button><button data-page="connection"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 11.5 12 4l9 7.5"/><path d="M5.5 10v10h13V10"/><path d="M10 20v-5h4v5"/></svg>Home Assistant</button><button data-page="settings"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/></svg>Device settings</button><button data-page="maintenance"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3v12m-5-5 5 5 5-5"/><path d="M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/></svg>Backup &amp; diagnostics</button></nav>
<div class="rail-foot"><div class="connections" aria-label="Connection status"><span class="connection" id="device-status" role="status">Panel: connecting…</span><span class="connection" id="ha-status" role="status">Panel → Home Assistant: checking…</span><span class="connection" id="browser-status" role="status">Browser → Home Assistant: disconnected</span></div><label><span class="sr">Color theme</span><select id="theme"><option value="system">System theme</option><option value="light">Light</option><option value="dark">Dark</option></select></label></div>
</aside>
<div class="stage">
<div id="draft-notice" hidden><p>A draft from your last visit is available. Restore it to review your unsaved changes.</p><div class="row"><button id="restore-draft">Restore draft</button><button id="dismiss-draft">Dismiss</button></div></div>
<p id="status" class="status" role="status" aria-live="polite">Connecting to the panel…</p>
<main>
<section id="page-entities" class="workspace" aria-label="Display and actions"><div class="slot-column"><section class="preset-card" aria-labelledby="preset-heading"><h2 class="preset-heading" id="preset-heading"><button id="preset-toggle" type="button" aria-expanded="false" aria-controls="preset-body"><span class="toggle-label">Preset</span><span id="preset-summary" class="preset-summary"></span><span id="preset-badge" class="mode-badge"></span><svg class="chevron" viewBox="0 0 24 24" aria-hidden="true"><path d="m6 9 6 6 6-6"/></svg></button></h2><div id="preset-body" class="preset-body" hidden><div class="preset-bar"><label class="sr" for="preset-target">Choose what to edit: the panel or a saved preset</label><select id="preset-target"></select><button id="preset-new" type="button" class="icon-button" aria-label="New preset" title="New preset"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5v14M5 12h14"/></svg></button></div><p id="preset-mode" class="preset-mode"></p><div class="preset-actions"><button id="preset-save-as" type="button">Save as preset</button><button id="preset-apply" type="button" class="icon-button primary" aria-label="Apply preset to panel" title="Apply preset to panel"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 5v14l11-7z"/></svg></button><button id="preset-rename" type="button" class="icon-button" aria-label="Rename preset" title="Rename preset"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 20h4L19 9l-4-4L4 16v4zM13.5 6.5l4 4"/></svg></button><button id="preset-delete" type="button" class="icon-button danger" aria-label="Delete preset" title="Delete preset"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/></svg></button></div></div></section><div class="column-heading slots-heading"><h2 id="slots-heading">Slots</h2></div><nav id="slots" class="slot-list" aria-labelledby="slots-heading"></nav></div><section aria-label="Slot configuration">
<div class="toolbar"><div><h2 id="slot-title">Slot 1</h2><span class="unsaved" id="unsaved-count"></span></div><div class="row"><button id="discard">Discard changes</button><button id="save-all">Save all</button><button id="save" class="primary">Save slot</button></div></div>
<div class="slot-tools"><div class="row"><button id="move-up" class="icon-button" aria-label="Move slot earlier" title="Move slot earlier"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 19V5m-6 6 6-6 6 6"/></svg></button><button id="move-down" class="icon-button" aria-label="Move slot later" title="Move slot later"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5v14m-6-6 6 6 6-6"/></svg></button><button id="duplicate" class="quiet">Duplicate into empty slot</button><button id="add-slot" type="button">Add slot</button><span id="slot-capacity" class="hint"></span></div></div>
<div class="editor-tabs" role="tablist" aria-label="Slot settings"><button id="slot-tab-display" type="button" role="tab" data-slot-tab="display" aria-controls="slot-panel-display" aria-selected="true">Display</button><button id="slot-tab-action" type="button" role="tab" data-slot-tab="action" aria-controls="slot-panel-action" aria-selected="false" tabindex="-1">Press action</button><button id="slot-tab-gestures" type="button" role="tab" data-slot-tab="gestures" aria-controls="slot-panel-gestures" aria-selected="false" tabindex="-1">Gestures</button><button id="slot-tab-playlists" type="button" role="tab" data-slot-tab="playlists" aria-controls="slot-panel-playlists" aria-selected="false" tabindex="-1" hidden>Quick actions</button></div>
<section id="slot-panel-display" class="editor-pane" role="tabpanel" aria-labelledby="slot-tab-display"><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/web-configuration.md#add-and-arrange-controls" target="_blank" rel="noopener noreferrer">How to add and arrange controls<span class="sr"> (opens in a new tab)</span></a></p>
<article class="binding active" id="display-binding"><h3>Show on the display</h3><strong id="entity-name">Empty slot</strong><small id="entity-id"></small><button id="choose-entity">Choose entity</button><button id="clear" class="quiet">Clear</button></article>
<label class="field">Display name<input id="display-name" maxlength="96" placeholder="Use the Home Assistant name"></label>

<div id="sensor-options" hidden><h3 id="secondary-heading">Secondary reading</h3><label class="field">Sensor view<select id="sensor-presentation"><option value="0">Value</option><option value="1">12-hour graph when opened</option></select></label><label class="field">Secondary entity (optional)<input id="secondary-entity" maxlength="128" placeholder="sensor.openai_codex_5_hour_reset"></label><button type="button" id="choose-secondary">Choose secondary entity</button><label class="field">Secondary text<input id="secondary-text" maxlength="160" placeholder="Resets {relative} ({time})"></label><p class="hint" id="secondary-help">Replaces the sensor name below the value. Use {value}, {unit}, {relative}, {time}, {date}, or {datetime}. Example: Resets {relative} ({time}). Graphs use recorded statistics where available, otherwise readings collected since boot.</p><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/web-configuration.md#sensors-and-graphs" target="_blank" rel="noopener noreferrer">Sensor graphs and secondary text<span class="sr"> (opens in a new tab)</span></a></p></div>
</section>
<section id="slot-panel-action" class="editor-pane" role="tabpanel" aria-labelledby="slot-tab-action" hidden><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/web-configuration.md#press-actions" target="_blank" rel="noopener noreferrer">Choose what a press does<span class="sr"> (opens in a new tab)</span></a></p>
<article class="binding" id="action-binding"><h3>When pressed</h3><strong id="action-name">Device default</strong><small id="action-id"></small><button id="choose-action">Choose action</button><button id="default" class="quiet">Default</button><button id="none" class="quiet">No action</button><button id="open-playlists" class="quiet" hidden>Open quick actions</button></article>
<label class="field"><span><input id="gesture-pressResponses" type="checkbox"> Show the returned value after pressing</span></label><p class="hint">Scripts can return value and optional unit. Automations can send the result using the panel’s show_control_feedback action. <a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/gesture-controls.md#press-action-results" target="_blank" rel="noopener noreferrer">See response examples</a>.</p>
<p class="hint" id="action-limit" hidden>This entity uses its own press controls. Custom press actions are not supported.</p>
</section>
<section id="slot-panel-gestures" class="editor-pane" role="tabpanel" aria-labelledby="slot-tab-gestures" hidden><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/gesture-controls.md" target="_blank" rel="noopener noreferrer">Set up swipe actions and feedback<span class="sr"> (opens in a new tab)</span></a></p><p class="hint">Actions run only while this entity is opened. Empty means its normal control; enter none to ignore a gesture. A hold without turning still returns to the device list.</p><div class="field-grid"><article class="binding"><h3>Swipe up</h3><label class="field"><span class="sr">Swipe up action</span><input id="gesture-cw" maxlength="128" placeholder="Default, none, script.name or automation.name"></label><div class="row"><button type="button" data-gesture-choose="cw">Choose action</button><button type="button" class="quiet" data-gesture-reset="cw">Default</button></div></article><article class="binding"><h3>Swipe down</h3><label class="field"><span class="sr">Swipe down action</span><input id="gesture-ccw" maxlength="128" placeholder="Default, none, script.name or automation.name"></label><div class="row"><button type="button" data-gesture-choose="ccw">Choose action</button><button type="button" class="quiet" data-gesture-reset="ccw">Default</button></div></article><article class="binding"><h3>Hold + swipe up</h3><label class="field"><span class="sr">Hold + swipe up action</span><input id="gesture-holdCw" maxlength="128" placeholder="Default, none, script.name or automation.name"></label><div class="row"><button type="button" data-gesture-choose="holdCw">Choose action</button><button type="button" class="quiet" data-gesture-reset="holdCw">Default</button></div></article><article class="binding"><h3>Hold + swipe down</h3><label class="field"><span class="sr">Hold + swipe down action</span><input id="gesture-holdCcw" maxlength="128" placeholder="Default, none, script.name or automation.name"></label><div class="row"><button type="button" data-gesture-choose="holdCcw">Choose action</button><button type="button" class="quiet" data-gesture-reset="holdCcw">Default</button></div></article></div><label class="field">Movement step<input id="gesture-step" type="number" min="1" max="100" step="1" value="1"></label><label class="field">Feedback entity (optional)<input id="gesture-feedback" maxlength="128" placeholder="Use the displayed entity"></label><div class="row"><button type="button" data-gesture-choose="feedback">Choose feedback entity</button><button type="button" class="quiet" data-gesture-reset="feedback">Use displayed entity</button></div><label class="field"><span><input id="gesture-popup" type="checkbox"> Show a brief popup when feedback changes after a swipe</span></label><label class="field"><span><input id="gesture-responses" type="checkbox"> Read script responses (value and optional unit)</span></label><label class="field"><span><input id="gesture-events" type="checkbox"> Publish swipe events to Home Assistant</span></label><p class="hint">For a desktop control with a live value, choose a sensor as the display entity. Script response and event options need a matching Home Assistant setup; see the guide above for examples.</p></section>
<section id="slot-panel-playlists" class="editor-pane" role="tabpanel" aria-labelledby="slot-tab-playlists" hidden><section id="playlist-options" hidden><div class="toolbar"><h3>Quick actions</h3><span id="playlist-count" class="counter"></span></div><p class="hint">A list of scripts or automations for this control, such as playlists, radio stations or scenes. On the panel, hold until the cue appears and release to open the list; swipe to choose and tap to run.</p><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/web-configuration.md#playlists-and-stations" target="_blank" rel="noopener noreferrer">How quick actions work<span class="sr"> (opens in a new tab)</span></a></p><div id="playlist-entries"></div><button type="button" id="add-playlist">Add quick action</button></section></section>
</section><aside class="preview" aria-label="Configuration preview"><h3>On your panel</h3><label class="field"><span class="sr">Preview screen</span><select id="preview-screen"><option value="detail">Opened entity</option><option value="main">Main screen</option></select></label><div class="screen-frame"><svg id="device-preview" viewBox="0 0 240 240" role="img" aria-label="Device display preview"></svg></div><p id="preview-status" class="preview-label">Loading device view…</p><dl class="controls"><dt>Tap its card</dt><dd>Open its screen</dd><dt>On its screen</dt><dd id="press-hint">—</dd><dt>Swipe up / down</dt><dd id="rotate-hint">—</dd><dt>Swipe from the left edge</dt><dd>Back to the main view</dd><dt>Pull down from the top</dt><dd>Notifications</dd></dl><p id="icon-hint" class="hint"></p><p class="hint">Live values and the same layout used on your panel. Unsaved display names appear here before saving.</p><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/controls.md" target="_blank" rel="noopener noreferrer">Learn the panel controls<span class="sr"> (opens in a new tab)</span></a> <a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/icons-and-media.md" target="_blank" rel="noopener noreferrer">Icons and album art<span class="sr"> (opens in a new tab)</span></a></p></aside></section>
<section id="page-connection" hidden><div class="panel"><h2>Home Assistant</h2><p>Sign in to browse entities and sync their Home Assistant icons to saved slots. Refresh the entity list after changing an icon in Home Assistant.</p><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/setup.md#add-it-to-home-assistant" target="_blank" rel="noopener noreferrer">Connect the panel to Home Assistant<span class="sr"> (opens in a new tab)</span></a> <a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/setup.md#connect-to-wi-fi" target="_blank" rel="noopener noreferrer">Wi-Fi and Bluetooth setup<span class="sr"> (opens in a new tab)</span></a></p><section class="discovery" aria-labelledby="discovery-title"><div class="discovery-head"><h3 id="discovery-title">Find Home Assistant on your network</h3><button id="ha-discover" class="icon-btn" type="button" aria-label="Search again" title="Search again"><svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M17.65 6.35A7.96 7.96 0 0 0 12 4a8 8 0 1 0 7.73 10h-2.08A6 6 0 1 1 12 6c1.66 0 3.14.69 4.22 1.78L13 11h7V4z"/></svg></button></div><div id="ha-scanner" class="ha-scanner"><svg viewBox="0 0 100 100" fill="none" aria-hidden="true" focusable="false"><circle class="ha-pulse" cx="50" cy="50" r="46" fill="#0177FD" fill-opacity=".14"/><g class="ha-search-rotor"><circle cx="50" cy="50" r="38" stroke="#547FF6" stroke-opacity=".8" stroke-width=".33"/><circle cx="50" cy="50" r="45.3" stroke="#547FF6" stroke-opacity=".8" stroke-width=".33" stroke-dasharray="2.33 2.33"/><circle cx="50" cy="50" r="30.6" stroke="#0177FD" stroke-width=".67"/><path d="M23.42 65.42a30.6 30.6 0 0 0 11.21 11.15l-.66 1.16a31.9 31.9 0 0 1-11.7-11.64ZM66.01 22.27a31.9 31.9 0 0 1 11.72 11.78l-1.16.66a30.6 30.6 0 0 0-11.23-11.29Z" fill="#0177FD"/><circle cx="50" cy="50" r="26.4" fill="#3866E7" fill-opacity=".05"/><circle cx="50" cy="50" r="20.5" fill="#3866E7" fill-opacity=".1"/><path d="M50 19a31 31 0 0 1 31 31H50Z" fill="url(#ha-sweep)"/></g><circle cx="50.2" cy="50.2" r="16.1" fill="#0177FD" stroke="#2e2b82" stroke-opacity=".25" stroke-width=".67"/><g class="ha-search-rotor"><circle cx="8.6" cy="50.4" r="1.33" fill="#0177FD"/><circle cx="79.3" cy="73.7" r="1.33" fill="#8DABFF"/><circle cx="91.7" cy="66.1" r="1.05" fill="#22D6FF"/><circle cx="39.3" cy="86.5" r="1.05" fill="#7E67FF"/><circle cx="16.3" cy="28.8" r="1.05" fill="#00CB84"/><circle cx="17.2" cy="8" r="1.26" fill="#498FFF"/><circle cx="87.4" cy="32.9" r="1.5" fill="#7E67FF"/><circle cx="86.1" cy="7.9" r="1.5" fill="#FFC067"/></g><path d="M49.23 43.2a1.07 1.07 0 0 1 1.5 0l5.9 5.77c.5.49.79 1.16.79 1.87v5.41c0 .47-.39.85-.87.85h-5.99v-1.87l2.78-2.72a1.25 1.25 0 1 0-.77-.76l-2 1.96v-5.06a1.23 1.23 0 1 0-1.16 0v7.14l-2-1.96a1.25 1.25 0 1 0-.77.76l2.55 2.5h-5.77a.86.86 0 0 1-.87-.85v-5.41c0-.7.29-1.38.8-1.88Z" fill="#fff"/><defs><linearGradient id="ha-sweep" x1="49" y1="38.9" x2="77.3" y2="74.7" gradientUnits="userSpaceOnUse"><stop stop-color="#3866E7" stop-opacity="0"/><stop offset="1" stop-color="#3866E7"/></linearGradient></defs></svg></div><p id="ha-discovery-status" class="hint" role="status" aria-live="polite"></p><div id="ha-discovery-results" class="ha-results"></div></section><label class="field">Home Assistant address<input id="ha-url" type="url" placeholder="http://homeassistant.local:8123"></label><div class="row"><button id="ha-connect" class="primary">Sign in to Home Assistant</button><button id="ha-disconnect">Disconnect browser</button><button id="refresh">Refresh entity list</button><button id="retry-states">Retry panel entity states</button></div><p id="catalogue-time" class="hint">No entity list loaded in this browser.</p><p class="hint">Your browser remembers this sign-in on this panel address and renews it automatically. Disconnect browser removes it. This does not change the panel’s ESPHome credentials.</p><details><summary>The panel is offline in Home Assistant</summary><p>In Home Assistant, add the panel through the ESPHome integration. Open its integration options and enable “Allow the device to perform Home Assistant actions”. Keep both devices on a network where they can reach each other.</p></details></div></section>
<section id="page-settings" hidden><form id="settings-form" class="panel"><h2>Device settings</h2><p class="hint">Review each group, then save your changes once at the bottom.</p><fieldset class="settings-section"><legend>Screen &amp; controls</legend><p class="hint">Set the brightness, orientation and swipe sensitivity.</p><div class="field-grid"><label class="field">Screen brightness (%)<input id="brightness" name="brightness" type="number" min="10" max="100" step="1" required></label><label class="field">Display orientation<select id="orientation" name="orientation"><option value="0">Portrait</option><option value="90">Landscape</option><option value="180">Portrait · upside down</option><option value="270">Landscape · flipped</option></select></label><label class="field">Swipe sensitivity<select id="sensitivity" name="sensitivity"><option value="1">Fast</option><option value="2">Balanced</option><option value="4">Fine</option></select></label></div></fieldset><fieldset class="settings-section"><legend>Clock &amp; idle screen</legend><p class="hint">Choose what appears when you stop using the panel.</p><div class="field-grid"><label class="field">Idle screen<select id="idle-view" aria-describedby="idle-view-help" name="idle_view"><option value="clock">Clock</option><option value="media">Playing media · clock when stopped</option></select></label><label class="field">Standby after (seconds)<input id="standby" name="standby" type="number" min="1" max="900" step="1" required></label><label class="field">Automatic idle screen<select id="standby-scope"><option value="main">From the main screen only</option><option value="all">From any screen</option><option value="never">Never automatically</option></select></label><label class="field">Standby weather<select id="standby-weather" name="weather"></select></label><div class="field"><span id="clock-temperature-label">Clock first reading</span><div class="combo" data-combo="clock-temperature"><select id="clock-temperature" aria-labelledby="clock-temperature-label"></select></div></div><div class="field"><span id="clock-humidity-label">Clock second reading</span><div class="combo" data-combo="clock-humidity"><select id="clock-humidity" aria-labelledby="clock-humidity-label"></select></div></div></div><p class="hint">The two readings next to the clock show the weather’s temperature and humidity by default. Choose any sensor for either one; it shows with its own unit, and the icons stay the same.</p><p id="idle-view-help" class="hint">Playing media can replace the idle clock while a configured player is active.</p><div class="field-grid"><label class="field">Date format<select id="date-format" name="date_format"><option value="dd-mm">10-11 · Day first</option><option value="mm-dd">11-10 · Month first</option><option value="yyyy-mm-dd">2026-11-10 · Year first</option><option value="dd-mmm">10 Nov · Day first, named month</option><option value="mmm-dd">Nov 10 · Month first, named month</option><option value="yyyy-mmm-dd">2026-Nov-10 · Year first, named month</option></select></label><label class="field">Clock format<select id="hour-format" name="hour_format"><option value="24">24 hour · 18:30</option><option value="12">12 hour · 06:30 PM</option></select></label><label class="field checkbox-field"><span><input id="show-ampm" type="checkbox" checked> Show AM/PM with 12-hour time</span></label></div><label class="field checkbox-field"><span><input id="clock-seconds-ring" type="checkbox"> Show seconds around the clock edge</span></label><p class="hint field-note">The ring fills once per minute. Off by default.</p><label class="field checkbox-field"><span><input id="hold-cue" type="checkbox" checked> Show hold progress</span></label><p class="hint field-note">While you hold the button on a control, a bar shows how long until the hold closes the page or opens quick actions.</p></fieldset><fieldset class="settings-section"><legend>Appearance</legend><p class="hint">Choose the background, accent color and player layout.</p><label class="field">Media player view<select id="media-view" name="media_view"><option value="classic">Classic</option><option value="material">Material · tonal card</option></select></label><label class="field">Panel background<input id="background-color" type="color" value="#101418" aria-label="Panel background color"></label><p class="hint">Used by Settings, entity pages and the idle clock. Text and cards adjust for contrast.</p><fieldset class="ring-picker"><legend>Accent color</legend><div class="row"><span id="ring-sample" class="ring-sample" aria-hidden="true"></span><div><label class="color-custom">Custom color <input id="media-color" type="color" value="#62b0ff" aria-label="Accent color"></label><span id="color-value" class="hint">#62B0FF</span></div></div><div class="color-swatches" aria-label="Accent color presets"><button type="button" data-ring-color="#62B0FF" style="--swatch:#62b0ff" aria-label="Ocean blue"></button><button type="button" data-ring-color="#F97316" style="--swatch:#f97316" aria-label="Warm orange"></button><button type="button" data-ring-color="#10B981" style="--swatch:#10b981" aria-label="Emerald green"></button><button type="button" data-ring-color="#A78BFA" style="--swatch:#a78bfa" aria-label="Soft violet"></button><button type="button" data-ring-color="#F472B6" style="--swatch:#f472b6" aria-label="Rose pink"></button><button type="button" data-ring-color="#F8FAFC" style="--swatch:#f8fafc" aria-label="Ice white"></button></div><p class="hint">Used by playback, numeric values, timed notifications and the optional clock seconds ring.</p></fieldset></fieldset><fieldset class="settings-section"><legend>Notifications</legend><p class="hint">Timed messages show their remaining time as a shrinking edge ring.</p><label class="field checkbox-field"><span><input id="notifications-cover-timers" type="checkbox" checked> Allow notifications to cover timers</span></label><p class="hint">On by default: notifications cover timer popups while timers keep counting underneath. Turn off to let timer popups take priority.</p><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/display-notifications.md" target="_blank" rel="noopener noreferrer">Set up reminders and notification images</a></p></fieldset><fieldset class="settings-section"><legend>Sleep &amp; wake</legend><p class="hint">Keep the panel quiet overnight while Home Assistant stays connected.</p><label class="field"><span><input id="sleep-enabled" name="sleep_enabled" type="checkbox"> Enable scheduled sleep</span></label><div class="field-grid"><label class="field">Sleep starts<input id="sleep-start" name="sleep_start" type="time" step="60" required></label><label class="field">Sleep ends<input id="sleep-end" name="sleep_end" type="time" step="60" required></label><label class="field">During sleep hours, turn screen off after (seconds)<input id="sleep-timeout" name="sleep_timeout" type="number" min="1" max="3600" step="1" required></label></div><p class="hint">The schedule turns off the screen after inactivity during these hours. Equal start and end times mean all day.</p><p class="hint">For additional power saving, use the <strong>Low Power Sleep</strong> switch in Home Assistant. It sleeps the panel, pauses display rendering and enables Wi-Fi modem power saving. Turn the switch off or touch the screen to wake. Home Assistant can automate this switch for your night routine.</p></fieldset><div class="settings-actions"><button class="primary" id="save-settings" type="submit">Save device settings</button><button id="discard-settings" type="button">Discard settings changes</button></div></form></section>
<section id="page-maintenance" hidden><div class="panel"><h2>Backup & restore</h2><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/web-configuration.md#backups-and-diagnostics" target="_blank" rel="noopener noreferrer">Back up and restore your settings<span class="sr"> (opens in a new tab)</span></a></p><p>Back up your controls, tap and swipe actions, playlists, presets, and device settings. Credentials are excluded.</p><div class="row" style="margin-top:15px"><button id="export">Download saved configuration</button><button id="import">Import configuration</button><input id="import-file" type="file" accept="application/json,.json" hidden></div><p class="hint">Imports open as an unsaved draft. Review the slots and device settings, then save each section to apply. Presets in a backup can be restored to the panel right away.</p></div><div class="panel"><h2>Device information</h2><dl id="device-info" class="info"></dl><div class="row"><button id="diagnostics">Download diagnostics</button><button id="check-status">Refresh status</button></div><p class="hint">Diagnostics include firmware, uptime, and connection status. Entity names, addresses, and credentials are excluded.</p><p class="hint">Firmware updates are available through the panel’s “Firmware Update” entity in Home Assistant.</p><p class="help-links"><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/setup.md#keep-it-updated" target="_blank" rel="noopener noreferrer">Install wireless updates<span class="sr"> (opens in a new tab)</span></a> <a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/docs/controls.md#reset-wi-fi-and-device-configuration" target="_blank" rel="noopener noreferrer">Reset instructions<span class="sr"> (opens in a new tab)</span></a></p></div></section>
</main><dialog id="ask" class="ask" aria-labelledby="ask-title" aria-describedby="ask-text"><form id="ask-form" method="dialog" novalidate><h2 id="ask-title"></h2><p id="ask-text"></p><label id="ask-field" class="field ask-field" hidden><span id="ask-label">Name</span><input id="ask-input" maxlength="32" autocomplete="off"></label><p id="ask-error" class="ask-error" role="alert" hidden></p><div class="ask-actions"><button id="ask-cancel" type="button">Cancel</button><button id="ask-ok" type="submit" class="primary">Continue</button></div></form></dialog><footer><a href="https://github.com/fixtse/OpenNextion-Room-Companion/blob/main/README.md" target="_blank" rel="noopener noreferrer">User guide</a> · <a href="https://fixtse.com/" target="_blank" rel="noopener noreferrer">OpenNextion Room Companion by fixtSE</a></footer>
</div>
<dialog id="entity-picker" aria-labelledby="picker-title"><div class="picker-heading"><div><h2 id="picker-title">Choose an entity</h2><p id="picker-context" class="hint"></p></div><button type="button" id="close-picker" class="icon-button" aria-label="Close entity picker" title="Close"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m6 6 12 12M18 6 6 18"/></svg></button></div><div class="picker-body"><div class="toolbar"><h3 id="search-title">Find an entity</h3><span id="count" class="counter"></span></div><label class="sr" for="search">Search by name, entity ID, or room</label><input id="search" class="search" placeholder="Search by name, entity ID, or room" autocomplete="off" maxlength="128"><label class="field">Room<select id="room"><option value="">All rooms</option></select></label><div id="filters" class="filters" aria-label="Filter by entity type"></div><div id="manual"></div><div id="results" class="results" aria-live="polite"></div><button id="more" class="more" hidden>Show more</button><p id="action-hint" class="hint" hidden>Choose an automation or script. Automation conditions are checked before its actions run.</p></div></dialog>

<script src="/panel/ha.js"></script><script>window.ONX_TOKEN='__ONX_TOKEN__';</script><script src="/panel/panel.js"></script><script src="/panel/app.js"></script>
</body></html>)ONXHTML";
}

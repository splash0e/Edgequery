#include <WiFi.h>

#include <WebServer.h>

#include <ctype.h>

#include <stdlib.h>

#include <string.h>

#include <math.h>



// ============================================================

// EDGEQUERY v0.7 PRO

// Native ESP32 CSV Data Analyzer

//

// Board: ESP32 DevKit V1

// Mode: ESP32 creates its own Wi-Fi access point

// CSV limit: 3 MB

// Dataset is streamed; it is NOT permanently stored.

//

// Laptop/phone = browser + file picker only

// ESP32 = upload receiver + CSV parser + analyzer + web UI

// ============================================================



// ------------------------- WIFI ------------------------------



const char* AP_SSID     = "EDGEQUERY";

const char* AP_PASSWORD = "edgequery123";



WebServer server(80);



// ------------------------- LIMITS ----------------------------



#define MAX_FILE_SIZE       (3UL * 1024UL * 1024UL)

#define MAX_COLUMNS         64

#define MAX_COLUMN_NAME     32

#define MAX_FIELD_LENGTH    128



#define PREVIEW_ROWS        8

#define PREVIEW_FIELD_LEN   32



// We store compact 32-bit hashes instead of complete strings.

#define UNIQUE_LIMIT        32



// ------------------------- TYPES -----------------------------



enum ColumnType : uint8_t {

  TYPE_UNKNOWN = 0,

  TYPE_INTEGER,

  TYPE_FLOAT,

  TYPE_BOOLEAN,

  TYPE_DATE,

  TYPE_TEXT

};



// ------------------------- COLUMN INFO -----------------------



struct ColumnInfo {

  char name[MAX_COLUMN_NAME];



  ColumnType type;



  uint32_t missing;

  uint32_t nonEmpty;



  uint16_t uniqueCount;

  bool uniqueOverflow;



  uint32_t uniqueHashes[UNIQUE_LIMIT];



  float sum;

  float minimum;

  float maximum;

  bool numericInitialized;

};



ColumnInfo columns[MAX_COLUMNS];



// ------------------------- ANALYZER STATE --------------------



uint16_t columnCount = 0;

uint32_t rowCount = 0;

uint32_t totalMissing = 0;

uint32_t processedBytes = 0;



bool headerProcessed = false;

bool analyzing = false;

bool analysisComplete = false;

bool analysisError = false;



String analysisErrorMessage;



unsigned long analysisStart = 0;

unsigned long analysisTime = 0;

// Live resource telemetry.
// CPU utilization is a lightweight software workload estimate based on
// the time spent servicing the web server in the main loop.
float cpuUsagePercent = 0.0f;
unsigned long cpuWindowStart = 0;
uint64_t cpuBusyMicros = 0;
unsigned long cpuLastUpdate = 0;

// Adaptive analysis workspace. It deliberately uses additional heap during
// heavy analysis, but always leaves a safety reserve so the HTTP server and
// upload pipeline remain stable. This is safer than blindly consuming 80-95%
// of RAM, which can cause heap exhaustion and failed uploads.
uint8_t* analysisWorkspace = nullptr;
size_t analysisWorkspaceSize = 0;
uint32_t peakFreeHeap = 0;
float peakRamUsagePercent = 0.0f;
const size_t RAM_SAFETY_FLOOR = 96UL * 1024UL;
const float RAM_TARGET = 0.85f;



// ------------------------- CSV STATE -------------------------



char fieldBuffer[MAX_FIELD_LENGTH];

uint16_t fieldLength = 0;

uint16_t currentColumn = 0;



bool insideQuotes = false;

bool quotePending = false;
bool rowTouched = false;



// ------------------------- PREVIEW ---------------------------



char previewData[PREVIEW_ROWS][MAX_COLUMNS][PREVIEW_FIELD_LEN];

uint16_t previewRowCount = 0;



// ============================================================

// HTML UI

// ============================================================



const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>EDGEQUERY ? Local Data Intelligence</title>
<style>
*{box-sizing:border-box}
:root{--bg:#f5f5f7;--surface:rgba(255,255,255,.82);--surface2:rgba(247,247,249,.9);--surface3:#f0f1f4;--text:#17181b;--muted:#73757b;--soft:#4d5056;--border:rgba(0,0,0,.08);--accent:#0071e3;--green:#34c759;--orange:#ff9f0a;--red:#ff453a;--track:#e4e5e8;--shadow:0 18px 55px rgba(0,0,0,.07)}
body.dark{--bg:#08090b;--surface:rgba(24,25,28,.86);--surface2:#111214;--surface3:#191a1e;--text:#f5f5f7;--muted:#9a9ca3;--soft:#c7c8cc;--border:rgba(255,255,255,.09);--track:#2a2c31;--shadow:0 22px 70px rgba(0,0,0,.32)}
html{scroll-behavior:smooth}body{margin:0;background:radial-gradient(circle at 12% 0%,rgba(0,113,227,.08),transparent 28%),radial-gradient(circle at 88% 12%,rgba(52,199,89,.06),transparent 28%),var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","SF Pro Text","Segoe UI",Arial,sans-serif;transition:background .45s,color .35s;min-height:100vh}
body:before{content:"";position:fixed;inset:0;pointer-events:none;background:linear-gradient(120deg,transparent 0 45%,rgba(255,255,255,.035) 50%,transparent 55%);opacity:.45;animation:sheen 12s linear infinite}
@keyframes sheen{from{transform:translateX(-20%)}to{transform:translateX(20%)}}
.container{width:min(94%,1280px);margin:auto;padding:28px 0 80px}.header{display:flex;justify-content:space-between;align-items:center;gap:18px;margin-bottom:54px}.brand{display:flex;align-items:center;gap:12px}.brandMark{width:38px;height:38px;border-radius:12px;background:linear-gradient(145deg,#0a84ff,#0071e3);box-shadow:0 8px 24px rgba(0,113,227,.25);display:grid;place-items:center;color:white;font-weight:900}.logo{font-size:21px;font-weight:800;letter-spacing:2.8px}.headerActions{display:flex;gap:8px;align-items:center}.pill{font-size:12px;font-weight:700;padding:8px 12px;border-radius:999px;background:var(--surface);border:1px solid var(--border);backdrop-filter:blur(18px)}.online{color:var(--green)}.btn{border:1px solid var(--border);background:var(--surface);color:var(--text);padding:10px 15px;border-radius:12px;font-weight:700;cursor:pointer;transition:transform .2s,box-shadow .2s,background .2s}.btn:hover{transform:translateY(-2px);box-shadow:0 9px 25px rgba(0,0,0,.10)}.btn.primary{background:#0071e3;color:white;border-color:#0071e3}.btn.ghost{background:var(--surface2)}
.hero{max-width:860px;margin-bottom:38px}.eyebrow{color:var(--accent);font-size:11px;font-weight:800;letter-spacing:1.8px;text-transform:uppercase;margin-bottom:12px}.hero h1{font-size:clamp(40px,7vw,72px);line-height:.98;letter-spacing:-3px;margin:0 0 18px}.hero p{font-size:18px;line-height:1.55;color:var(--muted);max-width:760px}.heroGlow{display:inline-block;background:linear-gradient(90deg,var(--text),#69717c);-webkit-background-clip:text;background-clip:text;color:transparent}
.card{background:var(--surface);border:1px solid var(--border);border-radius:25px;padding:25px;margin-bottom:20px;box-shadow:var(--shadow);backdrop-filter:blur(22px);animation:rise .65s ease both}.card:nth-child(2){animation-delay:.06s}.card:nth-child(3){animation-delay:.12s}.card:nth-child(4){animation-delay:.18s}@keyframes rise{from{opacity:0;transform:translateY(12px)}to{opacity:1;transform:none}}.card h2{font-size:25px;letter-spacing:-.5px;margin:0 0 18px}.small{font-size:13px;line-height:1.65;color:var(--muted)}
.monitorGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:14px}.metric{background:var(--surface2);border:1px solid var(--border);border-radius:18px;padding:19px;position:relative;overflow:hidden}.metric:after{content:"";position:absolute;inset:auto -30px -45px auto;width:110px;height:110px;border-radius:50%;background:radial-gradient(circle,rgba(0,113,227,.10),transparent 65%)}.metricTop{display:flex;justify-content:space-between;align-items:center;gap:12px}.metricName{font-size:11px;letter-spacing:1px;text-transform:uppercase;font-weight:800;color:var(--muted)}.metricValue{font-size:27px;font-weight:800;letter-spacing:-1px}.bar{height:9px;background:var(--track);border-radius:20px;overflow:hidden;margin-top:14px}.fill{height:100%;width:0;border-radius:20px;background:linear-gradient(90deg,#0071e3,#34c759);transition:width .5s cubic-bezier(.2,.8,.2,1)}.meta{font-size:11px;color:var(--muted);margin-top:9px}.pulse{animation:pulse 1.8s ease-in-out infinite}@keyframes pulse{0%,100%{opacity:1}50%{opacity:.55}}
.uploadZone{border:1.5px dashed var(--border);border-radius:20px;background:var(--surface2);padding:38px 20px;text-align:center;transition:.25s}.uploadZone.drag{border-color:var(--accent);background:rgba(0,113,227,.08);transform:scale(1.005)}.uploadIcon{width:56px;height:56px;border-radius:18px;margin:0 auto 13px;background:rgba(0,113,227,.1);color:var(--accent);display:grid;place-items:center;font-size:25px}.fileInput{display:none}.fileActions{display:flex;justify-content:center;gap:9px;flex-wrap:wrap;margin:17px 0}.fileName{font-size:13px;color:var(--soft);min-height:20px}.meter{margin-top:17px;padding:14px;border:1px solid var(--border);border-radius:16px;background:var(--surface3)}.meterTop{display:flex;justify-content:space-between;font-size:12px;color:var(--muted);margin-bottom:8px}.progress{height:9px;border-radius:20px;background:var(--track);overflow:hidden}.progressFill{height:100%;width:0;background:linear-gradient(90deg,#0071e3,#5ac8fa,#34c759);border-radius:20px;transition:width .25s ease}.status{margin-top:15px;min-height:43px;padding:12px 14px;border-radius:13px;background:var(--surface3);border:1px solid var(--border);font-size:13px;color:var(--soft)}
.metrics{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:12px}.metricSmall{background:var(--surface2);border:1px solid var(--border);border-radius:16px;padding:17px}.metricSmall .v{font-size:26px;font-weight:800}.metricSmall .l{font-size:12px;color:var(--muted);margin-top:5px}
.chartGrid{display:grid;grid-template-columns:1.1fr .9fr;gap:16px}.chartPanel{background:var(--surface2);border:1px solid var(--border);border-radius:18px;padding:18px}.chartTitle{font-size:13px;font-weight:800;margin-bottom:15px}.barRow{display:grid;grid-template-columns:110px 1fr 55px;gap:10px;align-items:center;margin:11px 0}.barLabel{font-size:11px;color:var(--soft);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.chartTrack{height:10px;background:var(--track);border-radius:20px;overflow:hidden}.chartBar{height:100%;background:linear-gradient(90deg,#0071e3,#34c759);border-radius:20px;transform-origin:left;animation:grow .9s cubic-bezier(.2,.8,.2,1) both}@keyframes grow{from{transform:scaleX(0)}to{transform:scaleX(1)}}.barValue{text-align:right;font-size:11px;color:var(--muted)}.donutWrap{display:flex;align-items:center;gap:20px}.donut{width:145px;height:145px;border-radius:50%;position:relative;background:conic-gradient(#0071e3 0 25%,#34c759 25% 50%,#ff9f0a 50% 75%,#af52de 75% 100%);flex:none}.donut:after{content:"";position:absolute;inset:31px;border-radius:50%;background:var(--surface2)}.legend{display:grid;gap:8px;font-size:12px;color:var(--soft)}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:7px;background:#0071e3}.dot.g{background:#34c759}.dot.o{background:#ff9f0a}.dot.p{background:#af52de}.numericGrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:11px}.stat{border:1px solid var(--border);background:var(--surface3);border-radius:15px;padding:15px}.statName{font-weight:800;font-size:13px;margin-bottom:8px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.statLine{font-size:12px;color:var(--muted);line-height:1.8}.statLine b{color:var(--text)}
.queryBox{display:grid;grid-template-columns:1fr auto;gap:9px}.queryInput{width:100%;border:1px solid var(--border);border-radius:13px;padding:13px 14px;background:var(--surface2);color:var(--text);outline:none;font:inherit}.queryInput:focus{border-color:var(--accent);box-shadow:0 0 0 4px rgba(0,113,227,.09)}.select{border:1px solid var(--border);border-radius:13px;background:var(--surface2);color:var(--text);padding:12px}.queryResult{margin-top:14px;padding:16px;border-radius:15px;background:var(--surface2);border:1px solid var(--border)}.answer{font-size:17px;font-weight:800;margin-bottom:10px}.sql{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;color:var(--accent);font-size:12px;white-space:pre-wrap}
.tableWrap{overflow:auto;border:1px solid var(--border);border-radius:15px}table{border-collapse:collapse;width:100%;min-width:850px}th,td{padding:11px 12px;border-bottom:1px solid var(--border);text-align:left;white-space:nowrap;font-size:12px}th{color:var(--muted);font-size:10px;text-transform:uppercase;letter-spacing:.7px;background:var(--surface2)}tr:last-child td{border-bottom:0}.good{color:var(--green)}.error{color:var(--red)}.hidden{display:none!important}
.funToast{position:fixed;left:50%;top:50%;z-index:20;transform:translate(-50%,-50%) scale(.75);opacity:0;pointer-events:none;padding:20px 28px;border-radius:24px;background:rgba(20,20,22,.88);color:white;box-shadow:0 25px 80px rgba(0,0,0,.35);backdrop-filter:blur(24px);font-size:25px;font-weight:900;letter-spacing:-.7px}.funToast.show{animation:hello 5s cubic-bezier(.2,.8,.2,1) both}@keyframes hello{0%{opacity:0;transform:translate(-50%,-50%) scale(.75)}10%,82%{opacity:1;transform:translate(-50%,-50%) scale(1)}100%{opacity:0;transform:translate(-50%,-55%) scale(.96)}}
@media(max-width:900px){.monitorGrid,.chartGrid{grid-template-columns:1fr}.header{margin-bottom:35px}.hero h1{letter-spacing:-2px}.queryBox{grid-template-columns:1fr}.donutWrap{justify-content:center}}
@media(prefers-reduced-motion:reduce){*,*:before,*:after{animation:none!important;transition:none!important}}
</style>
</head>
<body class="dark">
<div class="container">
<header class="header">
  <div class="brand"><div class="brandMark">E</div><div class="logo">EDGEQUERY</div></div>
  <div class="headerActions"><div class="pill online">? LOCAL / ONLINE</div><button class="btn ghost" type="button" onclick="toggleTheme()">? <span id="themeLabel">Dark</span></button><button class="btn ghost" type="button" onclick="sayHi()">*</button></div>
</header>
<section class="hero"><div class="eyebrow">Private edge intelligence</div><h1><span class="heroGlow">Your data.</span><br>Analyzed locally.</h1><p>EDGEQUERY turns the ESP32 into a compact offline data intelligence engine. Upload a dataset, inspect its structure, visualize its profile and ask questions ? without cloud processing.</p></section>

<section class="card"><div class="eyebrow">Live Device Monitor</div><h2>ESP32 Resources</h2><div class="monitorGrid">
<div class="metric"><div class="metricTop"><div class="metricName">CPU activity</div><div class="metricValue" id="cpuValue">--%</div></div><div class="bar"><div class="fill" id="cpuBar"></div></div><div class="meta" id="cpuMeta">Waiting for telemetry...</div></div>
<div class="metric"><div class="metricTop"><div class="metricName">RAM usage</div><div class="metricValue" id="ramValue">--%</div></div><div class="bar"><div class="fill" id="ramBar"></div></div><div class="meta" id="ramMeta">Waiting for telemetry...</div></div>
<div class="metric"><div class="metricTop"><div class="metricName">Dataset capacity</div><div class="metricValue" id="datasetValue">0%</div></div><div class="bar"><div class="fill" id="datasetBar"></div></div><div class="meta" id="datasetMeta">0 B / 3.00 MB</div></div>
</div></section>

<section class="card"><div class="eyebrow">Ingest</div><h2>Analyze Dataset</h2><p class="small">CSV and compatible plain-text tables up to 3 MB. The dataset is streamed through the ESP32 parser and is not permanently stored.</p>
<div class="uploadZone" id="dropZone"><div class="uploadIcon">^</div><div style="font-weight:800;font-size:17px">Drop your dataset here</div><div class="small" style="margin-top:5px">or choose a file from this device</div><div class="fileActions"><label class="btn primary" for="fileInput">Choose File</label><button class="btn ghost" type="button" onclick="clearFile()">Clear</button><input id="fileInput" class="fileInput" type="file" accept=".csv,.txt,text/csv,text/plain"></div><div class="fileName" id="selectedName">No file selected</div></div>
<div class="meter"><div class="meterTop"><span>Selected file</span><span id="selectedFileSize">0 B / 3.00 MB</span></div><div class="progress"><div class="progressFill" id="selectedFileBar"></div></div></div>
<div style="text-align:center;margin-top:15px"><button class="btn primary" id="analyzeButton" type="button" onclick="analyzeFile()">Analyze Dataset</button></div><div class="status" id="status">Waiting for dataset...</div><div class="meter"><div class="meterTop"><span>Upload / analysis progress</span><span id="uploadPercent">0%</span></div><div class="progress"><div class="progressFill" id="progressBar"></div></div></div></section>

<section class="card hidden" id="overview"><div class="eyebrow">Result</div><h2>Dataset Overview</h2><div class="metrics"><div class="metricSmall"><div class="v" id="fileSize">--</div><div class="l">File size</div></div><div class="metricSmall"><div class="v" id="rows">--</div><div class="l">Rows</div></div><div class="metricSmall"><div class="v" id="columns">--</div><div class="l">Columns</div></div><div class="metricSmall"><div class="v" id="missing">--</div><div class="l">Missing values</div></div><div class="metricSmall"><div class="v" id="processing">--</div><div class="l">Processing time</div></div></div></section>

<section class="card hidden" id="chartsCard"><div class="eyebrow">Visual intelligence</div><h2>Data Profile</h2><div class="chartGrid"><div class="chartPanel"><div class="chartTitle">Missing-value profile</div><div id="missingChart"></div></div><div class="chartPanel"><div class="chartTitle">Column type composition</div><div class="donutWrap"><div class="donut" id="typeDonut"></div><div class="legend" id="typeLegend"></div></div></div></div><div style="height:14px"></div><div class="chartPanel"><div class="chartTitle">Numeric range & average</div><div class="numericGrid" id="numericCards"></div></div></section>

<section class="card hidden" id="queryCard"><div class="eyebrow">Offline language interface</div><h2>Ask Your Dataset</h2><p class="small">Choose English ? SQL-style insight for natural questions, or SQL ? English to explain supported SQL queries without an Internet connection.</p><div class="queryBox"><select class="select" id="queryMode"><option value="english">English ? Insight</option><option value="sql">SQL ? English</option></select><input class="queryInput" id="queryInput" placeholder="e.g. What is the average salary?" autocomplete="off"><button class="btn primary" type="button" onclick="askDataset()">Ask</button></div><div class="queryResult hidden" id="queryResult"></div></section>

<section class="card hidden" id="schemaCard"><div class="eyebrow">Structure</div><h2>Schema Profile</h2><div class="tableWrap"><table><thead><tr><th>#</th><th>Column</th><th>Data type</th><th>Missing</th><th>Non-empty</th><th>Unique</th><th>Min</th><th>Max</th><th>Average</th></tr></thead><tbody id="schemaBody"></tbody></table></div></section>
<section class="card hidden" id="previewCard"><div class="eyebrow">Sample</div><h2>Dataset Preview</h2><div class="tableWrap"><table><tbody id="previewBody"></tbody></table></div></section>
<section class="card"><div class="eyebrow">Edge runtime</div><h2>Built for the edge</h2><div class="small">Processing occurs locally on the ESP32. No cloud service or external API is required for the core analyzer and embedded language interface. Maximum dataset size: <strong>3 MB</strong> ? Maximum columns: <strong>64</strong>.</div></section>
</div>
<div class="funToast" id="funToast">HI USER HI</div>
<script>
const MAX_DATASET_BYTES=3*1024*1024;let selectedFile=null;
function $(id){return document.getElementById(id)}
function esc(v){return String(v??'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#039;')}
function fmt(b){b=Number(b)||0;if(b<1024)return b+' B';if(b<1048576)return (b/1024).toFixed(1)+' KB';return (b/1048576).toFixed(2)+' MB'}
function setStatus(t,e=false){$('status').innerHTML=e?"<span class='error'>"+esc(t)+"</span>":t}
function updateSelectedFile(f){selectedFile=f||null;const n=f?f.size:0;const p=Math.min(100,n/MAX_DATASET_BYTES*100);$('selectedName').textContent=f?f.name+' ? '+fmt(n):'No file selected';$('selectedFileSize').textContent=fmt(n)+' / 3.00 MB';$('selectedFileBar').style.width=p+'%'}
function chooseFile(f){if(!f)return;if(f.size>MAX_DATASET_BYTES){setStatus('File exceeds the 3 MB limit.',true);clearFile();return}updateSelectedFile(f);setStatus('Ready ? '+esc(f.name))}
function clearFile(){$('fileInput').value='';updateSelectedFile(null);$('progressBar').style.width='0%';$('uploadPercent').textContent='0%';setStatus('Waiting for dataset...')}
function updateTelemetry(d){let c=Math.max(0,Math.min(100,Number(d.cpu_usage)||0)),r=Math.max(0,Math.min(100,Number(d.ram_usage)||0)),p=Math.max(0,Math.min(100,Number(d.dataset_percent)||0));$('cpuValue').textContent=c.toFixed(0)+'%';$('cpuBar').style.width=c+'%';$('cpuMeta').textContent=(d.cpu_mode||'Live activity')+' ? '+d.cpu_mhz+' MHz';$('ramValue').textContent=r.toFixed(0)+'%';$('ramBar').style.width=r+'%';$('ramMeta').textContent=fmt(d.ram_used)+' used ? '+fmt(d.free_heap)+' free'+(d.workspace_bytes?' ? workspace '+fmt(d.workspace_bytes):'');$('datasetValue').textContent=p.toFixed(0)+'%';$('datasetBar').style.width=p+'%';$('datasetMeta').textContent=fmt(d.dataset_bytes)+' / 3.00 MB'}
function poll(){const x=new XMLHttpRequest();x.open('GET','/status',true);x.onload=()=>{if(x.status===200){try{updateTelemetry(JSON.parse(x.responseText))}catch(e){}}};x.send()}
function toggleTheme(){document.body.classList.toggle('dark');const d=document.body.classList.contains('dark');$('themeLabel').textContent=d?'Dark':'Light';try{localStorage.setItem('edgequery_theme',d?'dark':'light')}catch(e){}}
function loadTheme(){let t='dark';try{t=localStorage.getItem('edgequery_theme')||'dark'}catch(e){}document.body.classList.toggle('dark',t==='dark');$('themeLabel').textContent=t==='dark'?'Dark':'Light'}
function sayHi(){const t=$('funToast');t.classList.remove('show');void t.offsetWidth;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),5050)}
function analyzeFile(){if(!selectedFile){setStatus('Choose a dataset first.',true);return}if(selectedFile.size>MAX_DATASET_BYTES){setStatus('File exceeds the 3 MB limit.',true);return}const b=$('analyzeButton');b.disabled=true;$('progressBar').style.width='1%';$('uploadPercent').textContent='1%';['overview','chartsCard','queryCard','schemaCard','previewCard'].forEach(id=>$(id).classList.add('hidden'));setStatus('Streaming '+esc(selectedFile.name)+' through EDGEQUERY...');const x=new XMLHttpRequest();x.open('POST','/analyze',true);x.upload.onprogress=e=>{if(e.lengthComputable){let p=Math.round(e.loaded/e.total*100);$('progressBar').style.width=p+'%';$('uploadPercent').textContent=p+'%';setStatus('Streaming locally ? '+p+'%')}};x.onload=()=>{b.disabled=false;$('progressBar').style.width='100%';$('uploadPercent').textContent='100%';try{let r=JSON.parse(x.responseText);if(!r.success)throw new Error(r.error||'Analysis failed.');display(r);setStatus("<span class='good'>Analysis complete ? "+r.rows.toLocaleString()+" rows ? "+r.columns+" columns.</span>")}catch(e){setStatus(e.message,true)}};x.onerror=()=>{b.disabled=false;setStatus('Connection failed. Check the EDGEQUERY Wi-Fi connection.',true)};let f=new FormData();f.append('file',selectedFile,selectedFile.name);x.send(f)}
function display(d){['overview','chartsCard','queryCard','schemaCard','previewCard'].forEach(id=>$(id).classList.remove('hidden'));$('fileSize').textContent=fmt(d.file_size);$('rows').textContent=Number(d.rows).toLocaleString();$('columns').textContent=d.columns;$('missing').textContent=Number(d.missing).toLocaleString();$('processing').textContent=d.processing_ms+' ms';let h='';d.schema.forEach((c,i)=>h+='<tr><td>'+(i+1)+'</td><td>'+esc(c.name)+'</td><td>'+esc(c.type)+'</td><td>'+Number(c.missing).toLocaleString()+'</td><td>'+Number(c.non_empty).toLocaleString()+'</td><td>'+esc(c.unique)+'</td><td>'+esc(c.min)+'</td><td>'+esc(c.max)+'</td><td>'+esc(c.average)+'</td></tr>');$('schemaBody').innerHTML=h;let p='<tr>';d.schema.forEach(c=>p+='<th>'+esc(c.name)+'</th>');p+='</tr>';d.preview.forEach(row=>{p+='<tr>';for(let i=0;i<d.schema.length;i++)p+='<td>'+esc(row[i]??'')+'</td>';p+='</tr>'});$('previewBody').innerHTML=p;renderCharts(d.schema)}
function renderCharts(s){let max=0;s.forEach(c=>max=Math.max(max,Number(c.missing)||0));let h='';s.slice(0,24).forEach(c=>{let m=Number(c.missing)||0,total=m+(Number(c.non_empty)||0),rate=total?m/total*100:0,p=max?m/max*100:0;h+='<div class="barRow"><div class="barLabel" title="'+esc(c.name)+'">'+esc(c.name)+'</div><div class="chartTrack"><div class="chartBar" style="width:'+p+'%"></div></div><div class="barValue">'+rate.toFixed(1)+'%</div></div>'});$('missingChart').innerHTML=h||'<div class="small">No profile data.</div>';let n={Integer:0,Float:0,Boolean:0,Date:0,Text:0,Unknown:0};s.forEach(c=>n[c.type]=(n[c.type]||0)+1);let t=s.length||1,a=n.Integer/t*100,b=(n.Integer+n.Float)/t*100,c=(n.Integer+n.Float+n.Boolean)/t*100,d=(n.Integer+n.Float+n.Boolean+n.Date)/t*100;$('typeDonut').style.background='conic-gradient(#0071e3 0 '+a+'%,#34c759 '+a+'% '+b+'%,#ff9f0a '+b+'% '+c+'%,#af52de '+c+'% '+d+'%,#64748b '+d+'% 100%)';$('typeLegend').innerHTML='<div><i class="dot"></i>Integer: '+n.Integer+'</div><div><i class="dot g"></i>Float: '+n.Float+'</div><div><i class="dot o"></i>Text: '+n.Text+'</div><div><i class="dot p"></i>Boolean/Date: '+(n.Boolean+n.Date+n.Unknown)+'</div>';let nums=s.filter(c=>c.min!=='-'&&c.max!=='');$('numericCards').innerHTML=nums.length?nums.map(c=>'<div class="stat"><div class="statName">'+esc(c.name)+'</div><div class="statLine">Min <b>'+esc(c.min)+'</b><br>Average <b>'+esc(c.average)+'</b><br>Max <b>'+esc(c.max)+'</b></div></div>').join(''):'<div class="small">No numeric columns detected.</div>'}
function askDataset(){let q=$('queryInput').value.trim();if(!q)return;let mode=$('queryMode').value;let x=new XMLHttpRequest();x.open('POST',mode==='sql'?'/sql-explain':'/query',true);x.setRequestHeader('Content-Type','application/x-www-form-urlencoded;charset=UTF-8');x.onload=()=>{try{let r=JSON.parse(x.responseText);$('queryResult').classList.remove('hidden');$('queryResult').innerHTML='<div class="answer">'+esc(r.answer||'No answer.')+'</div><div class="small">'+(mode==='sql'?'English explanation':'SQL-style translation')+'</div><div class="sql">'+esc(r.sql||q)+'</div>'}catch(e){$('queryResult').classList.remove('hidden');$('queryResult').innerHTML='<span class="error">Could not read response.</span>'}};x.onerror=()=>{$('queryResult').classList.remove('hidden');$('queryResult').innerHTML='<span class="error">Query request failed.</span>'};x.send('q='+encodeURIComponent(q))}
$('fileInput').addEventListener('change',e=>chooseFile(e.target.files[0]));const dz=$('dropZone');dz.addEventListener('dragover',e=>{e.preventDefault();dz.classList.add('drag')});dz.addEventListener('dragleave',()=>dz.classList.remove('drag'));dz.addEventListener('drop',e=>{e.preventDefault();dz.classList.remove('drag');chooseFile(e.dataTransfer.files[0])});$('queryInput').addEventListener('keydown',e=>{if(e.key==='Enter')askDataset()});loadTheme();poll();setInterval(poll,1000);
</script>
</body>
</html>
)rawliteral";



// ============================================================

// TYPE HELPERS

// ============================================================



bool isIntegerValue(const char* s) {

  if(!s || !*s) return false;



  char* endPtr;

  strtol(s,&endPtr,10);



  return (*endPtr=='\0');

}



bool isFloatValue(const char* s) {

  if(!s || !*s) return false;



  char* endPtr;

  strtof(s,&endPtr);



  return (*endPtr=='\0');

}



bool isBooleanValue(const char* s) {

  if(!s || !*s) return false;



  String v=s;

  v.trim();

  v.toLowerCase();



  return v=="true" || v=="false" ||

         v=="yes"  || v=="no"    ||

         v=="1"    || v=="0";

}



bool isDateValue(const char* s) {

  if(!s || !*s) return false;



  // YYYY-MM-DD

  if(strlen(s)!=10 || s[4]!='-' || s[7]!='-')

    return false;



  for(int i=0;i<10;i++){

    if(i==4 || i==7) continue;

    if(!isdigit((unsigned char)s[i]))

      return false;

  }



  return true;

}



ColumnType detectType(const char* value) {

  if(isBooleanValue(value))

    return TYPE_BOOLEAN;



  if(isIntegerValue(value))

    return TYPE_INTEGER;



  if(isFloatValue(value))

    return TYPE_FLOAT;



  if(isDateValue(value))

    return TYPE_DATE;



  return TYPE_TEXT;

}



ColumnType mergeTypes(ColumnType oldType, ColumnType newType) {

  if(oldType==TYPE_UNKNOWN)

    return newType;



  if(oldType==newType)

    return oldType;



  if((oldType==TYPE_INTEGER && newType==TYPE_FLOAT) ||

     (oldType==TYPE_FLOAT && newType==TYPE_INTEGER))

    return TYPE_FLOAT;



  return TYPE_TEXT;

}



const char* typeToString(ColumnType type) {

  switch(type){

    case TYPE_INTEGER: return "Integer";

    case TYPE_FLOAT:   return "Float";

    case TYPE_BOOLEAN: return "Boolean";

    case TYPE_DATE:    return "Date";

    case TYPE_TEXT:    return "Text";

    default:           return "Unknown";

  }

}



// ============================================================

// HASH / UNIQUE TRACKER

// ============================================================



uint32_t hashString(const char* text) {

  uint32_t hash=2166136261UL;



  while(*text){

    hash^=(uint8_t)*text;

    hash*=16777619UL;

    text++;

  }



  return hash;

}



void addUniqueValue(ColumnInfo& col,const char* value) {

  if(col.uniqueOverflow)

    return;



  uint32_t hash=hashString(value);



  for(uint16_t i=0;i<col.uniqueCount;i++){

    if(col.uniqueHashes[i]==hash)

      return;

  }



  if(col.uniqueCount>=UNIQUE_LIMIT){

    col.uniqueOverflow=true;

    return;

  }



  col.uniqueHashes[col.uniqueCount]=hash;

  col.uniqueCount++;

}



// ============================================================

// ANALYZER RESET

// ============================================================



// ============================================================
// ADAPTIVE RAM WORKSPACE / TELEMETRY
// ============================================================

void updatePeakRAM() {
  const uint32_t total = ESP.getHeapSize();
  const uint32_t freeNow = ESP.getFreeHeap();

  if (freeNow > peakFreeHeap) peakFreeHeap = freeNow;

  if (total > 0) {
    const uint32_t used = total - freeNow;
    peakRamUsagePercent = ((float)used * 100.0f) / (float)total;
  }
}

void releaseAnalysisWorkspace() {
  if (analysisWorkspace != nullptr) {
    free(analysisWorkspace);
    analysisWorkspace = nullptr;
  }
  analysisWorkspaceSize = 0;
}

void allocateAnalysisWorkspace() {
  releaseAnalysisWorkspace();

  const uint32_t total = ESP.getHeapSize();
  const uint32_t freeNow = ESP.getFreeHeap();

  if (total == 0 || freeNow <= RAM_SAFETY_FLOOR) {
    updatePeakRAM();
    return;
  }

  size_t targetUsed = (size_t)((float)total * RAM_TARGET);
  size_t currentUsed = (size_t)(total - freeNow);
  size_t desired = (targetUsed > currentUsed) ? (targetUsed - currentUsed) : 0;
  size_t safeMaximum = (size_t)freeNow - RAM_SAFETY_FLOOR;

  if (desired > safeMaximum) desired = safeMaximum;
  if (desired < 4096) {
    updatePeakRAM();
    return;
  }

  size_t attempt = desired;
  while (attempt >= 4096) {
    analysisWorkspace = (uint8_t*)malloc(attempt);
    if (analysisWorkspace != nullptr) {
      analysisWorkspaceSize = attempt;
      memset(analysisWorkspace, 0, analysisWorkspaceSize);
      break;
    }
    attempt = (attempt * 3UL) / 4UL;
  }

  updatePeakRAM();
}

void resetAnalyzer() {

  releaseAnalysisWorkspace();
  memset(columns,0,sizeof(columns));

  memset(previewData,0,sizeof(previewData));



  columnCount=0;

  rowCount=0;

  totalMissing=0;

  processedBytes=0;



  headerProcessed=false;

  analyzing=false;

  analysisComplete=false;

  analysisError=false;



  analysisErrorMessage="";



  analysisStart=0;

  analysisTime=0;



  fieldLength=0;

  currentColumn=0;



  insideQuotes=false;

  quotePending=false;

  rowTouched=false;

  previewRowCount=0;

}



// ============================================================

// ERROR

// ============================================================



void setAnalysisError(const char* message) {

  analysisError=true;

  analysisErrorMessage=message;

}



// ============================================================

// FIELD PROCESSING

// ============================================================



void processField() {

  if(currentColumn>=MAX_COLUMNS){

    setAnalysisError("Too many columns. Maximum is 64.");

    return;

  }



  fieldBuffer[fieldLength]='\0';



  // Remove UTF-8 BOM if it appears at the beginning

  // of the first header field.

  if(!headerProcessed &&

     currentColumn==0 &&

     fieldLength>=3 &&

     (uint8_t)fieldBuffer[0]==0xEF &&

     (uint8_t)fieldBuffer[1]==0xBB &&

     (uint8_t)fieldBuffer[2]==0xBF){



    memmove(

      fieldBuffer,

      fieldBuffer+3,

      fieldLength-2

    );



    fieldLength-=3;

    fieldBuffer[fieldLength]='\0';

  }



  // ---------------- HEADER ----------------



  if(!headerProcessed){

    if(fieldLength==0){
      String autoName="Column "+String(currentColumn+1);
      strncpy(
        columns[currentColumn].name,
        autoName.c_str(),
        MAX_COLUMN_NAME-1
      );
    }
    else{
      strncpy(
        columns[currentColumn].name,
        fieldBuffer,
        MAX_COLUMN_NAME-1
      );
    }

    columns[currentColumn]
      .name[MAX_COLUMN_NAME-1]='\0';

    return;

  }



  ColumnInfo& col=columns[currentColumn];



  // ---------------- MISSING ----------------



  if(fieldLength==0){

    col.missing++;

    totalMissing++;

  }

  else{

    col.nonEmpty++;



    // ---------------- TYPE ----------------



    ColumnType detected=detectType(fieldBuffer);



    col.type=mergeTypes(

      col.type,

      detected

    );



    // ---------------- UNIQUE ----------------



    addUniqueValue(

      col,

      fieldBuffer

    );



    // ---------------- NUMERIC ----------------



    if(detected==TYPE_INTEGER ||

       detected==TYPE_FLOAT){



      float value=strtof(

        fieldBuffer,

        nullptr

      );



      col.sum+=value;



      if(!col.numericInitialized){

        col.minimum=value;

        col.maximum=value;

        col.numericInitialized=true;

      }

      else{

        if(value<col.minimum)

          col.minimum=value;



        if(value>col.maximum)

          col.maximum=value;

      }

    }

  }



  // ---------------- PREVIEW ----------------



  if(previewRowCount<PREVIEW_ROWS &&

     currentColumn<MAX_COLUMNS){



    strncpy(

      previewData[previewRowCount][currentColumn],

      fieldBuffer,

      PREVIEW_FIELD_LEN-1

    );



    previewData[previewRowCount][currentColumn]

      [PREVIEW_FIELD_LEN-1]='\0';

  }

}



// ============================================================

// ROW PROCESSING

// ============================================================



void finishRow() {

  if(!rowTouched){
    currentColumn=0;
    fieldLength=0;
    return;
  }

  uint16_t fieldsInRow=currentColumn+1;

  if(!headerProcessed){

    if(fieldsInRow==0 || fieldsInRow>MAX_COLUMNS){
      setAnalysisError(
        "CSV header exceeds the 64-column limit."
      );
      return;
    }

    columnCount=fieldsInRow;
    headerProcessed=true;

  }
  else{

    if(fieldsInRow>MAX_COLUMNS){
      setAnalysisError(
        "CSV row exceeds the 64-column limit."
      );
      return;
    }

    // Some real-world CSV exports contain rows with fewer or more
    // fields than the header. Keep the analysis usable instead of
    // rejecting the complete dataset.
    if(fieldsInRow>columnCount){

      for(uint16_t i=columnCount;i<fieldsInRow;i++){
        String autoName="Column "+String(i+1);
        strncpy(
          columns[i].name,
          autoName.c_str(),
          MAX_COLUMN_NAME-1
        );
        columns[i].name[MAX_COLUMN_NAME-1]='\0';
      }

      columnCount=fieldsInRow;
    }
    else if(fieldsInRow<columnCount){

      while(currentColumn+1<columnCount){
        currentColumn++;
        fieldLength=0;
        processField();

        if(analysisError)
          return;
      }
    }

    rowCount++;

    if(previewRowCount<PREVIEW_ROWS)
      previewRowCount++;
  }

  currentColumn=0;
  fieldLength=0;
  rowTouched=false;
}



// ============================================================

// CSV STREAM PARSER

//

// Handles:

// - commas

// - CRLF / LF

// - quoted fields

// - commas inside quoted fields

// - escaped quotes ("")

// - quoted newlines

//

// Field storage is intentionally capped at 127 characters.

// Longer fields are truncated for analysis/preview, while the

// CSV stream continues to be consumed.

// ============================================================



void processCSVByte(char c) {

  if(analysisError)

    return;

  if(c!='\\r' && (c!='\\n' || insideQuotes))
    rowTouched=true;



  // A quote was seen inside a quoted field on the previous byte.

  // Decide whether it was an escaped quote or the closing quote.

  if(quotePending){



    if(c=='"'){

      // Escaped quote: ""

      if(fieldLength<MAX_FIELD_LENGTH-1)

        fieldBuffer[fieldLength++]='"';



      quotePending=false;

      return;

    }



    // Previous quote was the closing quote.

    insideQuotes=false;

    quotePending=false;



    // Continue processing this same byte outside quotes.

  }



  if(insideQuotes){



    if(c=='"'){

      quotePending=true;

      return;

    }



    if(fieldLength<MAX_FIELD_LENGTH-1)

      fieldBuffer[fieldLength++]=c;



    return;

  }



  // ---------------- OUTSIDE QUOTES ----------------



  if(c=='"'){

    // A quote starts a quoted field only when the

    // field is currently empty.

    if(fieldLength==0){

      insideQuotes=true;

      return;

    }



    // Otherwise preserve it as ordinary text.

    if(fieldLength<MAX_FIELD_LENGTH-1)

      fieldBuffer[fieldLength++]=c;



    return;

  }



  if(c==','){

    processField();



    if(analysisError)

      return;



    fieldLength=0;

    currentColumn++;

    return;

  }



  if(c=='\n'){

    processField();



    if(analysisError)

      return;



    fieldLength=0;



    finishRow();



    return;

  }



  // Ignore CR. This handles Windows CRLF without

  // introducing an extra row.

  if(c=='\r')

    return;



  if(fieldLength<MAX_FIELD_LENGTH-1)

    fieldBuffer[fieldLength++]=c;

}



// ============================================================

// FINALIZE CSV

// ============================================================



void finalizeCSV() {

  if(quotePending){

    // A final quote closes the quoted field.

    insideQuotes=false;

    quotePending=false;

  }



  if(insideQuotes){

    setAnalysisError(

      "CSV ended inside a quoted field."

    );

    return;

  }



  // Process final row if there is remaining data.

  if(rowTouched || fieldLength>0 || currentColumn>0){



    processField();



    if(analysisError)

      return;



    fieldLength=0;



    finishRow();

  }



  if(!headerProcessed){

    setAnalysisError(

      "CSV has no valid header row."

    );

  }

}



// ============================================================

// JSON ESCAPE

// ============================================================



String jsonEscape(const char* text) {

  String out;



  for(size_t i=0;text[i]!='\0';i++){

    char c=text[i];



    if(c=='"' || c=='\\'){

      out+='\\';

      out+=c;

    }

    else if(c=='\n'){

      out+="\\n";

    }

    else if(c=='\r'){

      out+="\\r";

    }

    else if((uint8_t)c<32){

      // Skip other control characters.

    }

    else{

      out+=c;

    }

  }



  return out;

}



// ============================================================

// NUMBER FORMAT

// ============================================================



String numberString(float value) {

  if(isnan(value))

    return "-";



  return String(value,2);

}



// ============================================================

// RESULT JSON

// ============================================================



String createResultJSON() {

  String json;



  // The response is relatively small compared with the dataset.

  json.reserve(50000);



  json+="{";

  json+="\"success\":true,";

  json+="\"file_size\":"+String(processedBytes)+",";

  json+="\"rows\":"+String(rowCount)+",";

  json+="\"columns\":"+String(columnCount)+",";

  json+="\"missing\":"+String(totalMissing)+",";

  json+="\"processing_ms\":"+String(analysisTime)+",";



  // ---------------- SCHEMA ----------------



  json+="\"schema\":[";



  for(uint16_t i=0;i<columnCount;i++){



    if(i>0)

      json+=",";



    ColumnInfo& col=columns[i];



    json+="{";



    json+="\"name\":\"";

    json+=jsonEscape(col.name);

    json+="\",";



    json+="\"type\":\"";

    json+=typeToString(col.type);

    json+="\",";



    json+="\"missing\":"+String(col.missing)+",";



    json+="\"non_empty\":"+String(col.nonEmpty)+",";



    if(col.uniqueOverflow){

      json+="\"unique\":\"32+\",";

    }

    else{

      json+="\"unique\":"+String(col.uniqueCount)+",";

    }



    if(col.numericInitialized){



      json+="\"min\":\"";

      json+=numberString(col.minimum);

      json+="\",";



      json+="\"max\":\"";

      json+=numberString(col.maximum);

      json+="\",";



      float average=

        col.nonEmpty>0

        ? col.sum/(float)col.nonEmpty

        : 0.0f;



      json+="\"average\":\"";

      json+=numberString(average);

      json+="\"";



    }

    else{



      json+="\"min\":\"-\",";



      json+="\"max\":\"-\",";



      json+="\"average\":\"-\"";

    }



    json+="}";

  }



  json+="],";



  // ---------------- PREVIEW ----------------



  json+="\"preview\":[";



  for(uint16_t r=0;r<previewRowCount;r++){



    if(r>0)

      json+=",";



    json+="[";



    for(uint16_t c=0;c<columnCount;c++){



      if(c>0)

        json+=",";



      json+="\"";

      json+=jsonEscape(

        previewData[r][c]

      );

      json+="\"";

    }



    json+="]";

  }



  json+="]";



  json+="}";



  return json;

}




// ============================================================
// ENGLISH QUERY / SQL-STYLE INSIGHT
// ============================================================

int findColumnForQuery(String queryLower) {

  for(uint16_t i=0;i<columnCount;i++){

    String name=columns[i].name;
    name.toLowerCase();
    name.trim();

    if(name.length()>0 && queryLower.indexOf(name)>=0)
      return (int)i;
  }

  return -1;
}

String columnAverageText(ColumnInfo& col) {

  if(!col.numericInitialized || col.nonEmpty==0)
    return "-";

  return numberString(
    col.sum/(float)col.nonEmpty
  );
}

void sendQueryResponse(
  const String& answer,
  const String& sql
){

  String json="{";

  json+="\"success\":true,";
  json+="\"answer\":\"";
  json+=jsonEscape(answer.c_str());
  json+="\",";
  json+="\"sql\":\"";
  json+=jsonEscape(sql.c_str());
  json+="\"}";

  server.send(
    200,
    "application/json",
    json
  );
}

void handleQuery() {

  String query=server.arg("q");
  query.trim();

  if(query.length()==0){
    sendQueryResponse(
      "Please enter a question.",
      ""
    );
    return;
  }

  String q=query;
  q.toLowerCase();

  // Dataset-level questions.
  if(
    (q.indexOf("row")>=0 || q.indexOf("record")>=0) &&
    (
      q.indexOf("how many")>=0 ||
      q.indexOf("count")>=0 ||
      q.indexOf("number")>=0
    )
  ){

    sendQueryResponse(
      "The dataset contains "+String(rowCount)+" rows.",
      "SELECT COUNT(*) FROM dataset;"
    );
    return;
  }

  if(
    q.indexOf("column")>=0 &&
    (
      q.indexOf("how many")>=0 ||
      q.indexOf("count")>=0 ||
      q.indexOf("number")>=0
    )
  ){

    sendQueryResponse(
      "The dataset contains "+String(columnCount)+" columns.",
      "SELECT COUNT(*) AS columns FROM schema;"
    );
    return;
  }

  if(
    q.indexOf("missing")>=0 &&
    (
      q.indexOf("total")>=0 ||
      q.indexOf("all")>=0 ||
      findColumnForQuery(q)<0
    )
  ){

    int idx=findColumnForQuery(q);

    if(idx<0){

      sendQueryResponse(
        "The dataset contains "+
        String(totalMissing)+
        " missing values in total.",
        "SELECT COUNT(*) FROM dataset WHERE value IS NULL;"
      );

    }
    else{

      ColumnInfo& col=columns[idx];

      String answer=
        String(col.name)+
        " contains "+
        String(col.missing)+
        " missing values.";

      String sql=
        "SELECT COUNT(*) FROM dataset WHERE `"+
        String(col.name)+
        "` IS NULL;";

      sendQueryResponse(answer,sql);
    }

    return;
  }

  int idx=findColumnForQuery(q);

  if(
    idx>=0 &&
    (
      q.indexOf("average")>=0 ||
      q.indexOf("mean")>=0 ||
      q.indexOf("avg")>=0
    )
  ){

    ColumnInfo& col=columns[idx];

    if(!col.numericInitialized){
      sendQueryResponse(
        String(col.name)+
        " is not recognized as a numeric column.",
        "SELECT AVG(`"+String(col.name)+"`) FROM dataset;"
      );
    }
    else{
      sendQueryResponse(
        "The average of "+String(col.name)+
        " is "+columnAverageText(col)+".",
        "SELECT AVG(`"+String(col.name)+"`) FROM dataset;"
      );
    }

    return;
  }

  if(idx>=0 && (q.indexOf("minimum")>=0 || q.indexOf("minimum")>=0 || q.indexOf("min ")>=0)){

    ColumnInfo& col=columns[idx];

    sendQueryResponse(
      "The minimum of "+String(col.name)+
      " is "+(col.numericInitialized ? numberString(col.minimum) : "-")+".",
      "SELECT MIN(`"+String(col.name)+"`) FROM dataset;"
    );

    return;
  }

  if(idx>=0 && (q.indexOf("maximum")>=0 || q.indexOf("max ")>=0)){

    ColumnInfo& col=columns[idx];

    sendQueryResponse(
      "The maximum of "+String(col.name)+
      " is "+(col.numericInitialized ? numberString(col.maximum) : "-")+".",
      "SELECT MAX(`"+String(col.name)+"`) FROM dataset;"
    );

    return;
  }

  if(idx>=0 && (q.indexOf("unique")>=0 || q.indexOf("distinct")>=0)){

    ColumnInfo& col=columns[idx];

    String uniqueText=
      col.uniqueOverflow
      ? "32+ tracked distinct values"
      : String(col.uniqueCount)+" distinct values";

    sendQueryResponse(
      String(col.name)+" has "+uniqueText+".",
      "SELECT COUNT(DISTINCT `"+String(col.name)+"`) FROM dataset;"
    );

    return;
  }

  if(idx>=0 &&
     (
       q.indexOf("type")>=0 ||
       q.indexOf("data type")>=0
     )
  ){

    ColumnInfo& col=columns[idx];

    sendQueryResponse(
      String(col.name)+
      " is detected as "+String(typeToString(col.type))+".",
      "SELECT data_type FROM schema WHERE column_name = `"+
      String(col.name)+"`;"
    );

    return;
  }

  if(
    q.indexOf("schema")>=0 ||
    q.indexOf("columns")>=0 ||
    q.indexOf("fields")>=0
  ){

    String answer="Schema: ";

    for(uint16_t i=0;i<columnCount;i++){

      if(i>0)
        answer+=", ";

      answer+=String(columns[i].name);
      answer+=" (";
      answer+=String(typeToString(columns[i].type));
      answer+=")";
    }

    sendQueryResponse(
      answer,
      "SELECT column_name, data_type FROM schema;"
    );

    return;
  }

  sendQueryResponse(
    "I can answer profile questions such as row count, column count, missing values, unique values, data type, average, minimum and maximum. Example: \"What is the average salary?\"",
    "-- Supported embedded profile query --"
  );
}



// ============================================================

// ROOT

// ============================================================



// ============================================================
// SQL -> ENGLISH EXPLAINER
// ============================================================

String sqlIdentifier(const String& sqlLower) {
  int b=sqlLower.indexOf('`');
  if(b>=0){
    int e=sqlLower.indexOf('`',b+1);
    if(e>b) return sqlLower.substring(b+1,e);
  }
  int open=sqlLower.indexOf('(');
  if(open>=0){
    int close=sqlLower.indexOf(')',open+1);
    if(close>open){
      String x=sqlLower.substring(open+1,close);
      x.trim();
      return x;
    }
  }
  return "";
}

int findColumnLoose(String name) {
  name.trim();
  name.replace("`","");
  for(uint16_t i=0;i<columnCount;i++){
    String c=columns[i].name;
    c.toLowerCase();
    if(c==name) return (int)i;
  }
  return -1;
}

void handleSQLExplain() {
  String q=server.arg("q");
  q.trim();
  if(q.length()==0){
    sendQueryResponse("Please enter a SQL query.","");
    return;
  }

  String s=q;
  s.toLowerCase();

  if(s.indexOf("count(*)")>=0 && s.indexOf("select")>=0){
    sendQueryResponse("This query counts every row in the dataset. The current result is "+String(rowCount)+" rows.",q);
    return;
  }

  if(s.indexOf("count(distinct")>=0){
    String name=sqlIdentifier(s);
    int idx=findColumnLoose(name);
    if(idx>=0){
      ColumnInfo& col=columns[idx];
      sendQueryResponse("This query counts distinct values in "+String(col.name)+". EDGEQUERY tracked "+(col.uniqueOverflow?String("32+"):String(col.uniqueCount))+" distinct values during streaming analysis.",q);
      return;
    }
  }

  if(s.indexOf("avg(")>=0 || s.indexOf("average(")>=0 || s.indexOf("min(")>=0 || s.indexOf("max(")>=0){
    String name=sqlIdentifier(s);
    int idx=findColumnLoose(name);
    if(idx>=0){
      ColumnInfo& col=columns[idx];
      String verb,value;
      if(s.indexOf("avg(")>=0 || s.indexOf("average(")>=0){verb="average";value=columnAverageText(col);}
      else if(s.indexOf("min(")>=0){verb="minimum";value=col.numericInitialized?numberString(col.minimum):"-";}
      else {verb="maximum";value=col.numericInitialized?numberString(col.maximum):"-";}
      sendQueryResponse("This query asks for the "+verb+" of "+String(col.name)+". The local profile reports "+value+".",q);
      return;
    }
  }

  if(s.indexOf("select")>=0 && s.indexOf("from schema")>=0){
    sendQueryResponse("This query requests schema information. EDGEQUERY currently has "+String(columnCount)+" analyzed columns.",q);
    return;
  }

  sendQueryResponse("I can explain supported COUNT(*), COUNT(DISTINCT column), AVG(column), MIN(column), MAX(column), and schema queries. The raw dataset is not re-read for SQL execution.",q);
}

// ============================================================
// ROOT
// ============================================================

void handleRoot() {

  server.send(

    200,

    "text/html",

    INDEX_HTML

  );

}



// ============================================================

// STATUS

// ============================================================



void handleStatus() {

  const uint32_t heapTotal = ESP.getHeapSize();
  const uint32_t heapFree = ESP.getFreeHeap();
  const uint32_t heapUsed =
      heapTotal > heapFree ? heapTotal - heapFree : 0;

  float ramUsage =
      heapTotal > 0
      ? ((float)heapUsed * 100.0f) / (float)heapTotal
      : 0.0f;

  if(ramUsage < 0.0f) ramUsage = 0.0f;
  if(ramUsage > 100.0f) ramUsage = 100.0f;

  float datasetPercent =
      ((float)processedBytes * 100.0f) /
      (float)MAX_FILE_SIZE;

  if(datasetPercent < 0.0f) datasetPercent = 0.0f;
  if(datasetPercent > 100.0f) datasetPercent = 100.0f;

  String json="{";

  json+="\"project\":\"EDGEQUERY\",";
  json+="\"version\":\"0.6\",";
  json+="\"device\":\"ESP32 DevKit V1\",";
  json+="\"status\":\"online\",";
  json+="\"clients\":"+
        String(WiFi.softAPgetStationNum())+",";

  json+="\"cpu_usage\":"+
        String(cpuUsagePercent,1)+",";

  json+="\"cpu_mhz\":"+
        String(ESP.getCpuFreqMHz())+",";

  json+="\"cpu_mode\":\""+
        String(analyzing ? "CSV parser activity" : "Idle / ready")+
        "\",";

  json+="\"free_heap\":"+
        String(heapFree)+",";

  json+="\"heap_total\":"+
        String(heapTotal)+",";

  json+="\"ram_used\":"+
        String(heapUsed)+",";

  json+="\"ram_usage\":"+
        String(ramUsage,1)+",";

  json+="\"dataset_bytes\":"+
        String(processedBytes)+",";

  json+="\"dataset_percent\":"+
        String(datasetPercent,1)+",";

  json+="\"workspace_bytes\":"+
        String((uint32_t)analysisWorkspaceSize)+",";

  json+="\"peak_ram_usage\":"+
        String(peakRamUsagePercent,1)+",";

  json+="\"max_file_size\":"+
        String(MAX_FILE_SIZE);

  json+="}";

  server.send(
    200,
    "application/json",
    json
  );
}



// ============================================================

// UPLOAD CALLBACK

// ============================================================



void handleUpload() {

  HTTPUpload& upload=server.upload();



  if(upload.status==UPLOAD_FILE_START){



    resetAnalyzer();



    analyzing=true;



    analysisStart=millis();

    allocateAnalysisWorkspace();

    cpuWindowStart=micros();
    cpuBusyMicros=0;
    cpuUsagePercent=0.0f;



    Serial.println();

    Serial.println(

      "================================"

    );

    Serial.println(

      "EDGEQUERY DATASET UPLOAD"

    );

    Serial.print(

      "Filename: "

    );

    Serial.println(

      upload.filename

    );

    Serial.println(

      "================================"

    );

  }



  else if(upload.status==UPLOAD_FILE_WRITE){



    if(analysisError)

      return;



    // Check before processing the new chunk.

    if(

      processedBytes >

      MAX_FILE_SIZE-upload.currentSize

    ){



      processedBytes+=upload.currentSize;



      setAnalysisError(

        "Dataset exceeds the 3 MB limit."

      );



      return;

    }



    processedBytes+=upload.currentSize;



    const unsigned long parserStart=micros();

    for(

      size_t i=0;

      i<upload.currentSize;

      i++

    ){

      processCSVByte(
        (char)upload.buf[i]
      );

      if(analysisError)
        break;
    }

    cpuBusyMicros += (uint64_t)(micros()-parserStart);
    updatePeakRAM();

  }



  else if(upload.status==UPLOAD_FILE_END){



    if(!analysisError){



      finalizeCSV();



      if(!analysisError)

        analysisComplete=true;

    }



    analyzing=false;



    analysisTime=

      millis()-analysisStart;

    // Release the temporary high-utilization workspace before creating the
    // final JSON response so the web server has maximum headroom.
    releaseAnalysisWorkspace();
    updatePeakRAM();


    Serial.println();

    Serial.println(

      "Analysis finished."

    );



    Serial.print(

      "Bytes: "

    );

    Serial.println(

      processedBytes

    );



    Serial.print(

      "Rows: "

    );

    Serial.println(

      rowCount

    );



    Serial.print(

      "Columns: "

    );

    Serial.println(

      columnCount

    );



    Serial.print(

      "Missing: "

    );

    Serial.println(

      totalMissing

    );



    Serial.print(

      "Time: "

    );

    Serial.print(

      analysisTime

    );

    Serial.println(

      " ms"

    );



    if(analysisError){



      Serial.print(

        "ERROR: "

      );

      Serial.println(

        analysisErrorMessage

      );

    }



    Serial.println(

      "================================"

    );

  }

}



// ============================================================

// ANALYZE ROUTE

// ============================================================



void handleAnalyze() {



  if(analysisError){



    String json="{";

    json+="\"success\":false,";

    json+="\"error\":\"";

    json+=jsonEscape(

      analysisErrorMessage.c_str()

    );

    json+="\"}";



    server.send(

      400,

      "application/json",

      json

    );



    return;

  }



  if(!analysisComplete){



    server.send(

      400,

      "application/json",

      "{\"success\":false,\"error\":\"Analysis did not complete.\"}"

    );



    return;

  }



  String result=createResultJSON();



  server.send(

    200,

    "application/json",

    result

  );

}



// ============================================================

// NOT FOUND

// ============================================================



void handleNotFound() {

  server.send(

    404,

    "text/plain",

    "EDGEQUERY: Endpoint not found"

  );

}



// ============================================================

// SETUP

// ============================================================



void setup() {



  Serial.begin(115200);



  delay(1000);



  Serial.println();

  Serial.println(

    "======================================"

  );

  Serial.println(

    "          EDGEQUERY v0.7"

  );

  Serial.println(

    "     NATIVE ESP32 CSV ANALYZER"

  );

  Serial.println(

    "======================================"

  );



  resetAnalyzer();

  cpuWindowStart = micros();
  cpuBusyMicros = 0;
  cpuUsagePercent = 0.0f;



  // ---------------- WIFI AP ----------------



  WiFi.mode(WIFI_AP);



  bool started=

    WiFi.softAP(

      AP_SSID,

      AP_PASSWORD

    );



  if(!started){



    Serial.println(

      "ERROR: Wi-Fi AP failed."

    );



    return;

  }



  Serial.print(

    "SSID: "

  );

  Serial.println(

    AP_SSID

  );



  Serial.print(

    "Password: "

  );

  Serial.println(

    AP_PASSWORD

  );



  Serial.print(

    "IP: "

  );

  Serial.println(

    WiFi.softAPIP()

  );



  // ---------------- ROUTES ----------------



  server.on(

    "/",

    HTTP_GET,

    handleRoot

  );



  server.on(

    "/status",

    HTTP_GET,

    handleStatus

  );



  server.on(

    "/query",

    HTTP_POST,

    handleQuery

  );



  server.on(

    "/sql-explain",

    HTTP_POST,

    handleSQLExplain

  );



  server.on(

    "/analyze",

    HTTP_POST,

    handleAnalyze,

    handleUpload

  );



  server.onNotFound(

    handleNotFound

  );



  server.begin();



  Serial.println();

  Serial.println(

    "EDGEQUERY analyzer online."

  );

  Serial.println(

    "Maximum CSV size: 3 MB"

  );

  Serial.println(

    "Processing: STREAMING"

  );

  Serial.println(

    "Permanent dataset storage: NONE"

  );

  Serial.print(

    "Free heap at startup: "

  );

  Serial.println(

    ESP.getFreeHeap()

  );

  Serial.println(

    "======================================"

  );

}



// ============================================================

// LOOP

// ============================================================



void loop() {

  const unsigned long workStart = micros();

  server.handleClient();

  const unsigned long workTime =
      micros() - workStart;

  cpuBusyMicros += workTime;

  const unsigned long now = micros();

  if(now - cpuWindowStart >= 1000000UL){

    const unsigned long windowTime =
        now - cpuWindowStart;

    if(windowTime > 0){

      cpuUsagePercent =
        ((float)cpuBusyMicros * 100.0f) /
        (float)windowTime;

      if(cpuUsagePercent < 0.0f)
        cpuUsagePercent = 0.0f;

      if(cpuUsagePercent > 100.0f)
        cpuUsagePercent = 100.0f;
    }

    cpuBusyMicros = 0;
    cpuWindowStart = now;
  }
}

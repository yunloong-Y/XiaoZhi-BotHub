'use strict';

const $ = (id) => document.getElementById(id);
let lastSeq = 0;
let polling = false;
let firmwareFiles = [];

function fmtSize(n){
  if(!n) return '0 B';
  if(n<1024) return n+' B';
  if(n<1024*1024) return (n/1024).toFixed(1)+' KB';
  return (n/1024/1024).toFixed(2)+' MB';
}

async function api(path, opts){
  const r = await fetch(path, opts);
  return r.json();
}

function setConn(state, text){
  const el = $('connBadge');
  el.className = 'conn ' + state;
  el.innerHTML = '<span class="dot"></span> ' + text;
}

async function loadPorts(selectDefault){
  try{
    const data = await api('/api/ports');
    const sel = $('port');
    const prev = sel.value;
    sel.innerHTML = '';
    data.ports.forEach(p=>{
      const o = document.createElement('option');
      o.value = p; o.textContent = p; sel.appendChild(o);
    });
    if(data.ports.length===0){
      const o=document.createElement('option'); o.value=''; o.textContent='未检测到串口';
      sel.appendChild(o);
      $('portHint').textContent='未发现设备，请检查 USB 连接与驱动';
    } else {
      sel.value = (selectDefault && data.current) ? data.current : (prev || data.current);
      $('portHint').textContent = `检测到 ${data.ports.length} 个串口`;
    }
    setConn('on','后端服务已连接');
  }catch(e){
    setConn('off','后端服务未连接');
    $('portHint').textContent='无法连接后端，请确认 start.command 正在运行';
  }
}

async function loadFirmware(){
  try{
    const data = await api('/api/firmware');
    firmwareFiles = data.files.map(f=>({...f, checked:true}));
    const s = data.settings || {};
    if(s.flash_mode) $('flash_mode').value = s.flash_mode;
    if(s.flash_size) $('flash_size').value = s.flash_size;
    if(s.flash_freq) $('flash_freq').value = s.flash_freq;
    renderFiles();
  }catch(e){
    $('filelist').innerHTML='<div class="empty">加载固件失败</div>';
  }
}

function renderFiles(){
  const box = $('filelist');
  if(!firmwareFiles.length){ box.innerHTML='<div class="empty">build/ 下没有固件文件，请先编译</div>'; return; }
  box.innerHTML='';
  firmwareFiles.forEach((f,i)=>{
    const row=document.createElement('div'); row.className='file-item';
    row.innerHTML=`
      <input type="checkbox" data-i="${i}" ${f.checked?'checked':''}>
      <span class="file-name" title="${f.path}">${f.label}</span>
      <span class="file-size">${fmtSize(f.size)}</span>
      <input class="offset" data-i="${i}" value="${f.offset||''}" placeholder="地址">`;
    box.appendChild(row);
  });
  box.querySelectorAll('input[type=checkbox]').forEach(cb=>{
    cb.onchange=()=>{ firmwareFiles[+cb.dataset.i].checked=cb.checked; };
  });
  box.querySelectorAll('.offset').forEach(inp=>{
    inp.oninput=()=>{ firmwareFiles[+inp.dataset.i].offset=inp.value.trim(); };
  });
}

function appendLog(m){
  const c=$('console');
  if(m.seq===1){ c.innerHTML=''; }
  const div=document.createElement('div');
  div.className='line '+m.level;
  div.textContent=m.text;
  c.appendChild(div);
  c.scrollTop=c.scrollHeight;
}

async function pollStatus(){
  if(polling) return;
  polling=true;
  try{
    const s=await api('/api/status?since='+lastSeq);
    (s.logs||[]).forEach(m=>{ appendLog(m); lastSeq=m.seq; });
    $('progressBar').style.width=(s.progress||0)+'%';
    const stageMap={idle:'就绪',starting:'启动中',flash:'烧录中',erase:'擦除中'};
    let txt=(s.result? (s.result==='success'?'完成':s.result) : (stageMap[s.stage]||s.stage||'就绪'));
    if(s.running && s.progress) txt+=` ${s.progress}%`;
    $('progressText').textContent=txt;
    const busy=s.running;
    $('flashBtn').disabled=busy;
    $('eraseBtn').disabled=busy;
    $('stopBtn').disabled=!busy;
    document.querySelectorAll('.file-item input').forEach(el=>el.disabled=busy);
  }catch(e){}
  polling=false;
}

async function startFlash(){
  const port=$('port').value;
  if(!port){ alert('请先选择串口'); return; }
  const files=firmwareFiles.filter(f=>f.checked).map(f=>({path:f.path,offset:f.offset}));
  const missing=files.filter(f=>!f.offset);
  if(missing.length){ alert('以下固件未填写烧录地址: '+missing.map(f=>f.label).join(', ')); return; }
  if(!files.length){ alert('请至少选择一个固件文件'); return; }
  if(!confirm(`确认烧录到 ${port}？\n\n共 ${files.length} 个文件，将覆盖设备 Flash。`)) return;
  lastSeq=0; $('console').innerHTML='';
  try{
    await api('/api/flash',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({port,baud:+$('baud').value,chip:$('chip').value,
        flash_mode:$('flash_mode').value,flash_size:$('flash_size').value,
        flash_freq:$('flash_freq').value,files})});
    pollStatus();
  }catch(e){ alert('启动烧录失败: '+e); }
}

async function eraseFlash(){
  const port=$('port').value;
  if(!port){ alert('请先选择串口'); return; }
  if(!confirm(`确认擦除 ${port} 的整个 Flash？此操作不可恢复。`)) return;
  lastSeq=0; $('console').innerHTML='';
  try{
    await api('/api/erase',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({port,baud:+$('baud').value,chip:$('chip').value})});
    pollStatus();
  }catch(e){ alert('启动擦除失败: '+e); }
}

async function stopFlash(){
  try{ await api('/api/stop',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'}); }
  catch(e){}
}

$('flashBtn').onclick=startFlash;
$('eraseBtn').onclick=eraseFlash;
$('stopBtn').onclick=stopFlash;
$('refreshPorts').onclick=()=>loadPorts(true);
$('clearLog').onclick=()=>{ $('console').innerHTML=''; };

(async function init(){
  setInterval(pollStatus, 400);
  await loadPorts(true);
  await loadFirmware();
  try{ const d=await api('/api/esptool'); $('esptoolPath').textContent='esptool: '+d.path; }catch(e){}
})();

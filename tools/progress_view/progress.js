'use strict';
(() => {
  const reports = window.XITA_PROGRESS || [];
  const $ = id => document.getElementById(id);
  const labels = {translated:'Translated function', native:'Native helper present', unsupported:'Unsupported handler present', boundary:'HLE / kernel boundary'};
  const colors = {translated:'#247d9a', native:'#74b332', unsupported:'#ce8844', boundary:'#485965'};
  const format = n => Number(n).toLocaleString('en-US');
  let report, filtered = [], rectangles = [], selected = null, page = 0, hovered = null;
  const canvas = $('map'), ctx = canvas.getContext('2d');
  if (!reports.length) { $('visible').textContent = 'No profile reports are available yet.'; return; }
  for (const r of reports) { const option = document.createElement('option'); option.value=r.profile_id; option.textContent=r.name; $('profile').append(option); }
  function text(tag, content) { const node=document.createElement(tag); node.textContent=content; return node; }
  function detail(item) {
    selected=item;
    const box=$('detail'); box.replaceChildren(text('p',labels[item.status]),text('h2',item.name)); box.firstChild.className='eyebrow';
    const dl=document.createElement('dl');
    const values=[['Entry',item.address||item.kind.toUpperCase()],['Instruction markers',format(item.instructions)],['Source',item.source||'Referenced API'],['Exact match','Not recorded'],['Behavior validation','Not recorded']];
    if(item.helpers?.length) values.push(['Native entry helpers',item.helpers.join(', ')]);
    if(item.unsupported_sites) values.push(['Unsupported sites',format(item.unsupported_sites)],['Mnemonics',item.unsupported.join(', ')]);
    for(const [key,value] of values) dl.append(text('dt',key),text('dd',value));
    box.append(dl);
    if(item.source) { const explore=text('button','Explore source chunk');explore.type='button';explore.addEventListener('click',()=>{$('search').value=item.source;$('filter').value='all';filter();});box.append(explore); }
    draw();
  }
  function emptyDetail() { selected=null; $('detail').replaceChildren(text('p','Inspect an entry'),text('h2','Select a block'),text('p','Click the map or choose an entry below to see the evidence behind its color.')); $('detail').firstChild.className='eyebrow'; }
  function weight(item) { return $('weight').value==='count'?1:Math.max(1,item.instructions); }
  // Weighted binary partition keeps thousands of entries inexpensive to lay out.
  // Grouping is by source chunk, then by descending weight; it does not infer an engine subsystem.
  function partition(entries,x,y,w,h) {
    if(!entries.length) return;
    if(entries.length===1) { rectangles.push({item:entries[0],x,y,w,h}); return; }
    const total=entries.reduce((n,i)=>n+weight(i),0); let left=0,cut=1;
    for(let i=0;i<entries.length-1;i++) { left+=weight(entries[i]);cut=i+1;if(left>=total/2)break; }
    const ratio=left/total;
    if(w>=h) { partition(entries.slice(0,cut),x,y,w*ratio,h);partition(entries.slice(cut),x+w*ratio,y,w*(1-ratio),h); }
    else { partition(entries.slice(0,cut),x,y,w,h*ratio);partition(entries.slice(cut),x,y+h*ratio,w,h*(1-ratio)); }
  }
  function draw() {
    const width=canvas.clientWidth,height=canvas.clientHeight,dpr=window.devicePixelRatio||1;
    canvas.width=Math.round(width*dpr);canvas.height=Math.round(height*dpr);ctx.setTransform(dpr,0,0,dpr,0,0);
    ctx.fillStyle='#111e1c';ctx.fillRect(0,0,width,height);rectangles=[];
    partition(filtered,0,0,width,height);
    for(const r of rectangles) {
      ctx.fillStyle=colors[r.item.status];ctx.fillRect(r.x+.5,r.y+.5,Math.max(.1,r.w-1),Math.max(.1,r.h-1));
      if(r.w>85&&r.h>28) { ctx.save();ctx.beginPath();ctx.rect(r.x+4,r.y+3,r.w-8,r.h-6);ctx.clip();ctx.fillStyle='#f5f8f5';ctx.font='10px ui-monospace, monospace';ctx.fillText(r.item.name,r.x+7,r.y+17);ctx.restore(); }
      if(r.item===selected||r.item===hovered) { ctx.strokeStyle=r.item===selected?'#e0fa8d':'#fff';ctx.lineWidth=2;ctx.strokeRect(r.x+1,r.y+1,Math.max(0,r.w-2),Math.max(0,r.h-2)); }
    }
  }
  function list() {
    const pages=Math.max(1,Math.ceil(filtered.length/30)); page=Math.min(page,pages-1);$('rows').replaceChildren();
    for(const item of filtered.slice(page*30,page*30+30)) { const button=text('button',item.name);button.type='button';button.append(text('small',labels[item.status]));button.addEventListener('click',()=>detail(item));$('rows').append(button); }
    $('page-number').textContent=`Page ${page+1} of ${pages}`;$('previous').disabled=page===0;$('next').disabled=page>=pages-1;
  }
  function filter() {
    const query=$('search').value.trim().toLowerCase(),status=$('filter').value;
    filtered=report.items.filter(i=>(status==='all'||i.status===status)&&[i.name,i.address,i.source,...(i.helpers||[])].filter(Boolean).join(' ').toLowerCase().includes(query));
    filtered.sort((a,b)=>(a.source||a.kind).localeCompare(b.source||b.kind)||weight(b)-weight(a)||a.id.localeCompare(b.id));
    page=0;hovered=null;emptyDetail();$('empty').hidden=filtered.length!==0;
    const sites=filtered.reduce((n,i)=>n+i.instructions,0);
    $('visible').textContent=`${format(filtered.length)} / ${format(report.items.length)} entries shown · ${format(sites)} instruction markers · exact matching unrecorded`;
    list();draw();
  }
  function load() {
    report=reports.find(r=>r.profile_id===$('profile').value)||reports[0];
    $('revision').textContent=`Snapshot ${report.source_revision} · ${report.profile_id}`;
    $('stats').replaceChildren();
    for(const [n,label] of [[report.summary.functions,'Generated functions'],[report.summary.statuses.native||0,'Native entry helpers'],[report.summary.statuses.unsupported||0,'Functions to review'],[report.summary.hle+report.summary.kernel,'Referenced API boundaries']]) { const box=document.createElement('div');box.className='stat';box.append(text('strong',format(n)),text('span',label));$('stats').append(box); }
    filter();
  }
  function hit(event) { const bounds=canvas.getBoundingClientRect(),x=event.clientX-bounds.left,y=event.clientY-bounds.top;return rectangles.find(r=>x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)?.item; }
  canvas.addEventListener('click',event=>{const item=hit(event);if(item)detail(item);});
  canvas.addEventListener('mousemove',event=>{const item=hit(event)||null;if(item!==hovered){hovered=item;canvas.title=item?`${item.name} · ${labels[item.status]} · ${format(item.instructions)} markers`:'';draw();}});
  canvas.addEventListener('mouseleave',()=>{hovered=null;draw();});
  $('profile').addEventListener('change',load);$('search').addEventListener('input',filter);$('filter').addEventListener('change',filter);$('weight').addEventListener('change',filter);
  $('reset').addEventListener('click',()=>{$('search').value='';$('filter').value='all';$('weight').value='instructions';filter();});
  $('previous').addEventListener('click',()=>{page--;list();});$('next').addEventListener('click',()=>{page++;list();});
  new ResizeObserver(draw).observe(canvas.parentElement);
  load();
})();

const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+8000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,25));}throw Error('condition timed out');}
test('scratchpad grid rules and floating window grid commands preserve tiling and native ownership',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-grid-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','layers','scratchpads','floating-grid','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications','mouse','geometry-forget'],'geometry-window-limit':20,
 outputs:[{id:'screen-a',name:'DP-1',scale:1,geometry:{x:-2560,y:0,w:2560,h:1440},'work-area':{x:-2560,y:0,w:2560,h:1440},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[1,2,3].map(n=>({uuid:`aaaaaaaa-aaaa-4aaa-8aaa-${String(n).padStart(12,'0')}`,app:n===3?'elecwhat':'kitty',title:'test',output:'DP-1',desktops:[],activities:[],layer:'normal',frame:{x:n*100-2560,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:n===1}))};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:8000});const json=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const windows=()=>json('bridge','windows').windows,w=id=>windows().find(w=>w.id===String(id));
 const ok=(...args)=>{const r=call('-m',...args);assert.equal(r.status,0,r.stderr);assert.equal(r.stdout,'');};
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);write();await until(()=>json('doctor').bridge.sequence>=s.sequence);};
 const policyReady=()=>{const q=json('doctor')['layer-policy'];assert.deepEqual(q.errors,{});return !q.pending;};
 const ready=()=>{const q=json('context','status','DP-1');assert.notEqual(q.management.state,'stopped',q.management.error);return q.management.state==='active'&&q.active?.['context-generation']===q['stored-generation'];};


 const close=(a,b)=>Math.abs(a-b)<=1;
 const matches=(w,f)=>Object.keys(f).every(k=>close(w.frame[k],f[k]));
 const rule=['rule','--add','label=whatsapp','app=elecwhat','title=.*','scratchpad=whatsapp','grid=11:11:1:1:9:9'];
 const gridReady=()=>{const s=json('doctor')['grid-policy'];assert.deepEqual(s.errors,{});return !s.pending&&!json('doctor')['geometry-busy'];};
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);
  ok('config','layout','bsp');await until(ready);
  const tiled=call('-m','window','1','--grid','11:11:1:1:9:9');assert.notEqual(tiled.status,0);assert.match(tiled.stderr,/floating window/);
  ok(...rule);await until(()=>ready()&&w(3).scratchpad==='whatsapp'&&gridReady()&&matches(w(3),{x:-2560+2560/11,y:1440/11,w:2560*9/11,h:1440*9/11}));
  await until(()=>w(3).layer==='above');assert.equal(json('-m','query','--windows','id,is-floating','--window','3')['is-floating'],true);
  assert.equal(json('context','show','DP-1').members.length,2);assert.equal(json('-m','rule','--list').find(r=>r.label==='whatsapp').grid,'11:11:1:1:9:9');
  ok('window','--toggle','whatsapp');ok('window','--toggle','whatsapp');assert.equal(w(3).focused,true,'grid did not leave scratchpad geometry owned');
  ok('window','3','--grid','2:2:1:0:1:1');assert.ok(matches(w(3),{x:-1280,y:0,w:1280,h:720}));await new Promise(r=>setTimeout(r,200));assert.ok(matches(w(3),{x:-1280,y:0,w:1280,h:720}),'unchanged rule must not override a manual grid');assert.equal(ready(),true);
  // Padding, gaps, non-square grids, negative origins and clamped coordinates.
  ok('config','left_padding','10');ok('config','right_padding','10');ok('config','bottom_padding','10');ok('config','window_gap','10');await until(ready);
  ok('window','3','--grid','2:4:1:0:2:1');assert.ok(matches(w(3),{x:-1910,y:0,w:1260,h:710}));
  ok('window','3','--grid','2:2:99:99:0:0');assert.ok(matches(w(3),{x:-1275,y:720,w:1265,h:710}));
  // A plain floating rule can use a grid independently of scratchpad assignment.
  ok('rule','--add','label=terminal-float','app=^kitty$','manage=off');ok('config','layout','bsp');await until(()=>json('context','show','DP-1').members.length===0&&!json('doctor')['geometry-busy']);
  ok('rule','--add','label=terminal-grid','app=^kitty$','grid=1:1:0:0:1:1');await until(()=>gridReady()&&matches(w(1),{x:-2550,y:0,w:2540,h:1430})&&matches(w(2),{x:-2550,y:0,w:2540,h:1430}));
  const saved=fs.readFileSync(stateFile,'utf8');for(const spec of ['0:11:1:1:9:9','11:11:-1:1:9:9','1:1:0:0:1','1:1:0:0:1:1:1','1:1:0:0:nan:1']){assert.notEqual(call('-m','window','3','--grid',spec).status,0);assert.notEqual(call('-m',...rule.slice(0,-1),'grid='+spec).status,0);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);}
  // Rules persist and can be loaded by a new daemon without leaving windows owned.
  await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);await until(gridReady);assert.equal(json('-m','rule','--list').find(r=>r.label==='whatsapp').grid,'11:11:1:1:9:9');
  ok('window','--toggle','whatsapp');ok('window','--toggle','whatsapp');
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+8000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,25));}throw Error('condition timed out');}
test('Yabai layer aliases, independent rule properties, automatic tiling layers and KDE rule refusal',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-layers-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','layers','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications','mouse','geometry-forget'],'geometry-window-limit':20,
 outputs:[{id:'screen-a',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:0,w:2560,h:1440},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[1,2,3].map(n=>({uuid:`aaaaaaaa-aaaa-4aaa-8aaa-${String(n).padStart(12,'0')}`,app:n===3?'systemsettings':'kitty',title:'test',output:'DP-1',desktops:[],activities:[],layer:'normal',frame:{x:n*100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:n===1}))};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:8000});const json=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const windows=()=>json('bridge','windows').windows,w=id=>windows().find(w=>w.id===String(id));
 const ok=(...args)=>{const r=call('-m',...args);assert.equal(r.status,0,r.stderr);assert.equal(r.stdout,'');};
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);write();await until(()=>json('doctor').bridge.sequence>=s.sequence);};
 const policyReady=()=>{const q=json('doctor')['layer-policy'];assert.deepEqual(q.errors,{});return !q.pending;};
 const ready=()=>{const q=json('context','status','DP-1');assert.notEqual(q.management.state,'stopped',q.management.error);return q.management.state==='active'&&q.active?.['context-generation']===q['stored-generation'];};
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);await until(()=>policyReady()&&windows().every(w=>w['layer-mode']==='auto'));
  ok('rule','--add','label=settings','app=^systemsettings$','manage=off','layer=above');ok('config','layout','bsp');await until(()=>ready()&&w(1).layer==='below'&&w(2).layer==='below'&&w(3).layer==='above');
  const before=windows().map(w=>({id:w.id,frame:w.frame}));
  for(const spelling of ['--layer','--sub-layer'])for(const layer of ['above','below','normal','auto']){ok('window',spelling,layer);assert.equal(w(1).layer,layer==='auto'?'below':layer);assert.deepEqual(windows().map(w=>({id:w.id,frame:w.frame})),before);}
  ok('window','2','--layer','above');ok('window','2','--toggle','float');assert.equal(w(2).layer,'above','explicit override survives unmanagement');ok('window','2','--layer','auto');assert.equal(w(2).layer,'normal');ok('window','2','--toggle','float');await until(()=>ready()&&w(2).layer==='below');
  await until(policyReady);ok('rule','--add','label=term-layer','app=^kitty$','sub-layer=above');await until(()=>w(1).layer==='above'&&w(2).layer==='above'&&policyReady());assert.equal(json('context','show','DP-1').members.length,2,'layer-only rule does not float tiles');
  const rules=json('-m','rule','--list');assert.equal(rules.find(r=>r.label==='settings').layer,'above');assert.equal(rules.find(r=>r.label==='term-layer')['sub-layer'],'above');assert.equal(rules.find(r=>r.label==='term-layer').manage,undefined);
  ok('window','--sub-layer','normal');assert.equal(w(1).layer,'normal');await new Promise(r=>setTimeout(r,250));assert.equal(w(1).layer,'normal','manual override survives an unchanged rule match');
  // Layer rules are per-property: this title-only layer must retain manage=off.
  ok('rule','--add','label=settings-layer','app=^systemsettings$','layer=below');await until(()=>w(3).layer==='below'&&policyReady());assert.equal(json('rule','test',w(3).id).action,'float');
  const saved=fs.readFileSync(stateFile,'utf8');for(const args of [['window','--layer','sideways'],['rule','--add','app=.*','layer=sideways'],['rule','--add','app=.*','layer=above','sub-layer=below']]){assert.notEqual(call('-m',...args).status,0);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);}
  await mutate(s=>{s.windows[0]['test-force-layer']='normal';});const denied=call('-m','window','--layer','above');assert.notEqual(denied.status,0);assert.match(denied.stderr,/KDE.*window rules/);assert.equal(w(1).layer,'normal');assert.equal(ready(),true);await mutate(s=>delete s.windows[0]['test-force-layer']);
  // External user layer changes supersede automatic ownership and are not reset on stop.
  ok('window','2','--layer','auto');await mutate(s=>s.windows[1].layer='above');await until(()=>w(2)['layer-mode']==='external');assert.equal(w(2).layer,'above');
  ok('window','--layer','auto');assert.equal(w(1).layer,'below');const kept=windows().map(w=>({uuid:w.uuid,frame:w.frame}));await stop(d);await until(()=>JSON.parse(fs.readFileSync(file)).windows[0].layer==='normal');assert.equal(JSON.parse(fs.readFileSync(file)).windows[1].layer,'above');assert.equal(JSON.parse(fs.readFileSync(file)).windows[2].layer,'below','explicit rule layer survives stop');assert.deepEqual(JSON.parse(fs.readFileSync(file)).windows.map(w=>({uuid:w.uuid,frame:w.frame})),kept);
  // Persisted layer rules apply when a new daemon connects.
  d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);await until(()=>w(1).layer==='above'&&w(3).layer==='below'&&policyReady());
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

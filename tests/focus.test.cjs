const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('directional focus changes active UUID without changing geometry or saved intent',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-focus-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};




 s.capabilities.push('geometry-retention','geometry-lifecycle','notifications');s['geometry-window-limit']=20;s['test-geometry-fast']=true;s.windows.push({...s.windows[0],uuid:'44444444-4444-4444-8444-444444444444',focused:false});
 const write=()=>{s.sequence=String(BigInt(s.sequence)+1n);fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const status=()=>json(socket,'context','status','DP-1'),show=()=>json(socket,'context','show','DP-1');
 const ready=()=>status().management.state==='active';
 const refresh=async()=>{write();await until(()=>json(socket,'doctor').bridge.sequence===s.sequence);};
 const sync=()=>Object.assign(s,JSON.parse(fs.readFileSync(file,'utf8')));
 const result=id=>json(socket,'bridge','focus-status',id);
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);
  json(socket,'context','add','DP-1','1');json(socket,'context','add','DP-1','2');assert.equal(call(socket,'context','focus','DP-1','east').status,3);await stop(d);
  d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);assert.equal(call(socket,'context','focus','DP-1','east').status,2,'unapplied intent must not choose directional focus');
  // Give the simulated compositor the planned frames before enabling geometry control.
  for(const placement of json(socket,'context','plan','DP-1').placements)s.windows.find(w=>w.uuid===placement.uuid).frame=placement.frame;await refresh();
  json(socket,'context','manage','DP-1','on');await until(ready);assert.equal(call(socket,'context','focus','DP-1','east').status,2,'older bridges must report missing focus support');assert.equal(call(socket,'context','focus-window','DP-1','focused').status,2,'no-op must still require focus capability');s.capabilities.push('focus');await refresh();
  const saved=fs.readFileSync(stateFile,'utf8'),geometry=status().active.id,frames=s.windows.map(w=>w.frame);assert.equal(call(socket,'context','focus','focused','west').status,2,'no neighbor must be an explicit error');
  const east=json(socket,'context','focus','focused','east');assert.equal(east.success,false);assert.equal(east.completed,false);assert.equal(call(socket,'context','focus-window','DP-1','focused').status,2,'no-op focus cannot bypass a pending activation');await until(()=>result(east.id).completed);assert.equal(result(east.id).state,'focused');assert.equal(result(east.id).acknowledged,true);
  await until(()=>json(socket,'bridge','windows').windows.find(w=>w.uuid===s.windows[1].uuid).focused);sync();assert.equal(s.windows[1].focused,true);
  const west=json(socket,'context','focus','focused','west');await until(()=>result(west.id).completed);assert.equal(result(west.id).success,true);await until(()=>json(socket,'bridge','windows').windows[0].focused);sync();assert.deepEqual(s.windows.map(w=>w.frame),frames);assert.equal(status().active.id,geometry);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);assert.equal(status().management.enabled,true);
  const unchanged=json(socket,'context','focus-window','DP-1','1');assert.equal(unchanged.unchanged,true);assert.equal(unchanged.completed,true);assert.equal(unchanged.success,true);assert.equal(unchanged['confirmed-focus-id'],'1');
  // Eligibility checks are shared with directional focus; rejected requests make no edits.
  s.windows[1].minimized=true;await refresh();assert.equal(call(socket,'context','focus-window','DP-1','2').status,2);s.windows[1].minimized=false;await refresh();assert.equal(fs.readFileSync(stateFile,'utf8'),saved);assert.equal(status().active.id,geometry);
  s['test-focus-refuse']=true;await refresh();const denied=json(socket,'context','focus','DP-1','east');await until(()=>result(denied.id).completed);assert.equal(result(denied.id).state,'failed');assert.equal(result(denied.id).success,false);delete s['test-focus-refuse'];s['test-focus-ignore']=true;await refresh();const timeout=json(socket,'context','focus','DP-1','east');await until(()=>result(timeout.id).completed);assert.equal(result(timeout.id).state,'timed_out');assert.equal(result(timeout.id).acknowledged,false);delete s['test-focus-ignore'];await refresh();
  // Manually changed geometry must not choose a focus destination from stale slots.
  json(socket,'context','manage','DP-1','off');s.windows[0].frame.x+=20;await refresh();assert.equal(call(socket,'context','focus','DP-1','east').status,2);assert.equal(call(socket,'context','focus-window','DP-1','2').status,2);s.windows[0].frame.x-=20;await refresh();
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);assert.equal(status()['active-count'],0);assert.equal(call(socket,'bridge','focus-status','missing').status,2);
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('opt-in lifecycle reconciles membership, empty contexts, persistence failures and restore',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-lifecycle-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};


 s.capabilities.push('geometry-retention');s['geometry-window-limit']=20;s['test-geometry-fast']=true;
 const write=()=>{s.sequence=String(BigInt(s.sequence)+1n);fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const status=()=>json(socket,'context','status','DP-1'),show=()=>json(socket,'context','show','DP-1');
 const ready=()=>status().management.state==='active'&&status().active?.['context-generation']===show().generation;
 const refresh=async()=>{write();await until(()=>json(socket,'doctor').bridge.sequence===s.sequence);};
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);
  json(socket,'context','add','DP-1','1');assert.equal(call(socket,'context','manage','DP-1','on','--lifecycle').status,2,'old effects must reject lifecycle before admission');assert.equal(status().management.enabled,false);
  s.capabilities.push('geometry-lifecycle');s.capabilities=s.capabilities.filter(c=>c!=='events');await refresh();
  const beforeBaseline=fs.readFileSync(stateFile,'utf8');assert.equal(call(socket,'context','manage','DP-1','on','--lifecycle').status,2);assert.equal(fs.readFileSync(stateFile,'utf8'),beforeBaseline,'missing event baseline must reject before saving');
  s.capabilities.push('events');await refresh();await until(()=>json(socket,'doctor')['event-stream'].ready);
  json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);assert.equal(status().management['automatic-membership'],true);const first=status().active.id;
  assert.equal(call(socket,'context','manage','DP-1','on').status,2,'mode changes require off first');
  const second={...s.windows[0],focused:false,uuid:'44444444-4444-4444-8444-444444444444'};s.windows.push(second);await refresh();await until(()=>show().members.length===2&&ready());assert.notEqual(status().active.id,first);assert.equal(status().history.undo,0);
  assert.equal(JSON.parse(fs.readFileSync(stateFile)).contexts[0].members.length,2);
  s.windows.shift();await refresh();await until(()=>show().members.length===1&&ready());
  s.windows=[];await refresh();await until(()=>show().members.length===0&&ready());assert.equal(status()['active-count'],1,'empty replacement retires the previous transaction');
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);assert.equal(status()['active-count'],0);assert.equal(status().management.enabled,false);
  json(socket,'context','manage','DP-1','on','--lifecycle');await until(()=>status().management.state==='idle');s.windows=[second];await refresh();await until(()=>show().members.length===1&&ready());
  // Saving changed intent must succeed before the next geometry submission.
  const saved=fs.readFileSync(stateFile,'utf8'),job=status().active.id;
  fs.chmodSync(dir,0o500);s.windows.push({...second,uuid:'55555555-5555-4555-8555-555555555555'});
  // Write through the already-existing snapshot file while directory creation is forbidden.
  s.sequence=String(BigInt(s.sequence)+1n);fs.writeFileSync(file,JSON.stringify(s));
  await until(()=>status().management.state==='stopped');fs.chmodSync(dir,0o700);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);assert.equal(status().active.id,job);assert.equal(show().members.length,1);
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);
  json(socket,'context','manage','DP-1','on','--lifecycle');await until(()=>show().members.length===2&&ready());
  const feasible=fs.readFileSync(stateFile,'utf8'),applied=status().active.id;
  s.windows.push({...second,uuid:'77777777-7777-4777-8777-777777777777',minimum:{w:10000,h:10000}});await refresh();await until(()=>status().management.state==='stopped');assert.equal(fs.readFileSync(stateFile,'utf8'),feasible);assert.equal(status().active.id,applied,'infeasible admission must not submit geometry');
  s.windows.pop();await refresh();json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);
  // This legacy receipt-only fixture does not write geometry. Align observed
  // peers with its confirmed targets before testing the pause safety check.
  for(const p of status().active['target-placements'])s.windows.find(w=>w.uuid===p.uuid).frame={...p.frame};
  s.windows[0].minimized=true;s.windows[0].candidate=false;await refresh();await until(()=>status().management.state==='suspended-window');assert.equal(show().members.length,2,'minimized members retain their saved slots');assert.equal(status().management.enabled,true);assert.equal(status().management['paused-windows'][0].reason,'minimized');assert.equal(status().management.error,'');
  json(socket,'context','manage','DP-1','off');
  const blockedState=fs.readFileSync(stateFile,'utf8');const blocked=call(socket,'context','manage','DP-1','on','--lifecycle');assert.equal(blocked.status,2);assert.match(blocked.stderr,/minimized/);assert.match(blocked.stderr,/taskbar/);assert.equal(fs.readFileSync(stateFile,'utf8'),blockedState,'blocked startup must not change saved membership');s.windows[0].minimized=false;s.windows[0].candidate=true;await refresh();
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);await stop(d);
  d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);assert.equal(status().management.enabled,false);assert.equal(status().management['automatic-membership'],false);assert.equal(show().members.length,2);
  // An idle context must stop on effect replacement too, despite having no transaction.
  s.windows=[];await refresh();json(socket,'context','reconcile','DP-1','--apply');json(socket,'context','manage','DP-1','on','--lifecycle');await until(()=>status().management.state==='idle');
  s.instance='66666666-6666-4666-8666-666666666666';await refresh();await until(()=>status().management.state==='stopped');assert.match(status().management.error,/instance changed/);
 }finally{fs.chmodSync(dir,0o700);await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

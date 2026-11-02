const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('floating release, retile, persistence and failure recovery preserve management',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-floating-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};




 s.capabilities.push('geometry-retention','geometry-lifecycle','notifications');s['geometry-window-limit']=20;s['test-geometry-fast']=true;s.windows.push({...s.windows[0],uuid:'44444444-4444-4444-8444-444444444444',focused:false});
 const a=s.windows[0].uuid,b=s.windows[1].uuid;
 const write=()=>{s.sequence=String(BigInt(s.sequence)+1n);fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const status=()=>json(socket,'context','status','DP-1'),show=()=>json(socket,'context','show','DP-1');
 const ready=()=>status().management.state==='active'&&status().active?.['context-generation']===show().generation;
 const refresh=async()=>{write();await until(()=>json(socket,'doctor').bridge.sequence===s.sequence);};
 const checkRelease=(receipt,uuid)=>assert.equal(json(socket,'bridge','transaction',receipt.id)['release-request'][0].uuid,uuid);
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);json(socket,'context','reconcile','DP-1','--apply');assert.equal(call(socket,'context','float','DP-1','1','on').status,3);await stop(d);
  d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);assert.equal(call(socket,'context','float','DP-1','1','on').status,2,'older effects must reject release support');s.capabilities.push('geometry-release');await refresh();
  const floated=json(socket,'context','float','focused','focused','on');await until(ready);checkRelease(floated,a);assert.equal(status().management.enabled,true);assert.equal(show().members.length,1);assert.deepEqual(show().floating,[a]);assert.equal(status().history.undo,0);assert.equal(json(socket,'context','reconcile','DP-1').excluded[0].reason,'explicit-float');
  assert.equal(json(socket,'context','float','DP-1',a,'on').unchanged,true);assert.equal(status().active.id,floated.id);assert.equal(call(socket,'context','add','DP-1','1').status,2);
  // A floated window can be interactively ineligible without blocking the tiled members.
  s.windows[0].candidate=false;s.windows[0].frame.x+=40;await refresh();assert.equal(status().management.enabled,true);assert.equal(status().active.id,floated.id);s.windows[0].candidate=true;await refresh();
  // Float the final member: the active transaction now owns no window geometry.
  const empty=json(socket,'context','float','DP-1',b,'on');await until(ready);checkRelease(empty,b);assert.equal(show().members.length,0);assert.equal(status()['active-count'],1);assert.deepEqual(status().active['managed-windows'],[]);
  json(socket,'context','float','DP-1',a,'off');await until(ready);assert.equal(show().members.length,1);assert.deepEqual(show().floating,[b]);assert.equal(json(socket,'context','float','DP-1',a,'off').unchanged,true);
  json(socket,'context','float','DP-1',b,'off');await until(ready);assert.equal(show().members.length,2);assert.deepEqual(show().floating,[]);
  const saved=fs.readFileSync(stateFile,'utf8'),job=status().active.id;fs.chmodSync(dir,0o500);assert.equal(call(socket,'context','float','DP-1',a,'on').status,1);fs.chmodSync(dir,0o700);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);assert.equal(status().active.id,job);assert.equal(status().management.enabled,true);
  // Rejection retains the preceding active owner. Explicit resume re-derives its release.
  s['test-geometry-reject']=true;await refresh();json(socket,'context','float','DP-1',a,'on');await until(()=>status().management.state==='stopped');assert.equal(status().active.id,job);assert.deepEqual(show().floating,[a]);s['test-geometry-reject']=false;await refresh();json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);checkRelease(status().active,a);assert.equal(show().members.length,1);
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);assert.equal(status()['active-count'],0);await stop(d);
  d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);assert.deepEqual(show().floating,[a]);assert.equal(status().management.enabled,false);assert.equal(JSON.parse(fs.readFileSync(stateFile)).version,8);
  json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);assert.equal(show().members.length,1);json(socket,'context','float','DP-1',a,'off');await until(ready);assert.equal(show().members.length,2);
  // Fixed-membership management can explicitly float and retile too.
  json(socket,'context','manage','DP-1','off');json(socket,'context','manage','DP-1','on');await until(ready);json(socket,'context','float','DP-1',a,'on');await until(ready);assert.equal(status().management['automatic-membership'],false);assert.equal(show().members.length,1);json(socket,'context','float','DP-1',a,'off');await until(ready);
  // Retiling a single window overrides its float admission rule.
  json(socket,'rule','add','app-float','float','--app','kitty');json(socket,'context','float','DP-1',a,'on');await until(ready);json(socket,'context','float','DP-1',a,'off');await until(ready);assert.equal(show().members.length,2);json(socket,'rule','remove','app-float');
  json(socket,'context','float','DP-1',a,'on');await until(ready);json(socket,'context','manage','DP-1','off');json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);s.windows.shift();await refresh();await until(()=>show().floating.length===0&&ready());assert.equal(show().members.length,1,'closing a floated window forgets its marker');
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);assert.equal(status()['active-count'],0);
 }finally{fs.chmodSync(dir,0o700);await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

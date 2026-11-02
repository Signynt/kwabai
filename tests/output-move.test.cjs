const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('explicit output movement releases ownership and permits destination retiling',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-output-move-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const desktop='22222222-2222-4222-8222-222222222222';
 const out=(id,name,x,w,h)=>({id,name,scale:1,geometry:{x,y:0,w,h},'work-area':{x,y:0,w,h},'current-desktop':desktop,'current-activity':'work'});
 const win=(uuid,output)=>({uuid,app:'kitty',title:'test',output,desktops:[desktop],activities:['work'],frame:{x:output==='DP-1'?100:-900,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:output==='DP-1'});
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications'],'geometry-window-limit':20,'test-geometry-write':true,'test-geometry-exclusive':true,
 outputs:[out('screen-a','DP-1',0,2560,1440),out('screen-b','DP-2',-1080,1080,1920)],windows:[win('33333333-3333-4333-8333-333333333333','DP-1'),win('44444444-4444-4444-8444-444444444444','DP-2')]};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});
 const json=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const status=out=>json('context','status',out),window=()=>json('bridge','windows').windows.find(w=>w.id==='1');
 const ready=out=>{const r=status(out);assert.notEqual(r.management.state,'stopped',r.management.error);return ['active','idle'].includes(r.management.state)&&(!r.active||r.active['context-generation']===r['stored-generation']);};
 const waitJob=async receipt=>{let result;await until(()=>{result=json('bridge','transaction',receipt.id);return result.completed;});assert.equal(result.success,true,JSON.stringify(result));return result;};
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);const seq=s.sequence;write();await until(()=>BigInt(json('doctor').bridge.sequence)>=BigInt(seq));};
 try {
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);
  json('context','reconcile','DP-1','--apply');json('context','reconcile','DP-2','--apply');json('context','manage','DP-1','on','--lifecycle');json('context','manage','DP-2','on','--lifecycle');await until(()=>ready('DP-1')&&ready('DP-2'));
  const saved=fs.readFileSync(stateFile,'utf8');assert.equal(call('context','send-output','1','DP-2').status,2,'older effect must reject movement before edits');assert.equal(fs.readFileSync(stateFile,'utf8'),saved);
  await mutate(s=>s.capabilities.push('output-move'));
  assert.equal(call('context','send-output','1','DP-2').status,2,'tiling ownership must be released explicitly');
  const heldFrame=window().frame;await waitJob(json('context','float','DP-1','1','on'));await until(()=>ready('DP-1'));assert.deepEqual(window().frame,heldFrame);
  assert.equal(call('context','send-output','1','missing').status,2);assert.equal(call('context','send-output','1','DP-1').status,2);
  const before=fs.readFileSync(stateFile,'utf8');await mutate(s=>s.windows[0].minimum.w=1500);assert.equal(call('context','send-output','1','DP-2').status,2);assert.equal(fs.readFileSync(stateFile,'utf8'),before);await mutate(s=>s.windows[0].minimum.w=100);
  const receipt=json('context','send-output','1','screen-b');const moved=await waitJob(receipt);assert.equal(moved.state,'moved');assert.equal(moved.acknowledged,true);assert.deepEqual(moved['managed-windows'],[]);assert.equal(moved['active-transaction'],'');
  await until(()=>window().output==='DP-2');assert.ok(window().frame.x<0);assert.equal(json('context','show','DP-2').members.length,1,'move keeps explicit floating policy');
  assert.equal(call('bridge','restore',receipt.id).status,2);assert.equal(json('bridge','transaction',receipt.id).state,'moved');
  assert.equal(ready('DP-1'),true);assert.equal(ready('DP-2'),true);
  let compatible=call('-m','window','1','--display','east');assert.equal(compatible.status,0,compatible.stderr);assert.equal(compatible.stdout,'');assert.equal(window().output,'DP-1');
  compatible=call('-m','window','--display','west');assert.equal(compatible.status,0,compatible.stderr);assert.equal(compatible.stdout,'');assert.equal(window().output,'DP-2');
  const unmoved=window().frame;assert.equal(call('-m','window','1','--display','west').status,1);assert.deepEqual(window().frame,unmoved);
  assert.equal(call('-m','window','1','--display','2').status,1);
  // A failed save cannot lose the source marker or partially admit at the destination.
  const stateBefore=fs.readFileSync(stateFile,'utf8');fs.chmodSync(dir,0o500);assert.equal(call('context','float','DP-2','1','off').status,1);fs.chmodSync(dir,0o700);assert.equal(fs.readFileSync(stateFile,'utf8'),stateBefore);
  const destinationFloatingFrame=window().frame;await waitJob(json('context','float','DP-2','1','off'));await until(()=>ready('DP-1')&&ready('DP-2'));
  assert.equal(json('context','show','DP-1').members.length,0);assert.equal(json('context','show','DP-2').members.length,2);
  const stored=JSON.parse(fs.readFileSync(stateFile));assert.ok(stored.contexts.every(c=>!c.floating.includes(window().uuid)));
  // Destination restoration uses the arrival frame; it cannot send the window back to its former output.
  const kept=window().frame;json('context','reset','DP-2');assert.equal(window().output,'DP-2');assert.deepEqual(window().frame,kept);
  assert.equal(status('DP-1').management.enabled,true);
 }finally{fs.chmodSync(dir,0o700);await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

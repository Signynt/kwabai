const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('desktop moves confirm inactive membership and allow destination retiling',async()=>{
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
 const other='99999999-9999-4999-8999-999999999999';
 const desktopJob=id=>json('bridge','desktop-transaction',id);
 const waitDesktop=async receipt=>{let r;await until(()=>{r=desktopJob(receipt.id);return r.completed;});assert.equal(r.success,true,JSON.stringify(r));return r;};
 try {
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);
  json('context','reconcile','DP-1','--apply');json('context','manage','DP-1','on','--lifecycle');await until(()=>ready('DP-1'));
  const before=fs.readFileSync(stateFile,'utf8');assert.equal(call('bridge','desktops').status,2);assert.equal(call('context','send-desktop','1','2').status,2);assert.equal(fs.readFileSync(stateFile,'utf8'),before);
  await mutate(s=>{s.capabilities.push('desktop-move');s.desktops=[{id:other,index:2,name:'Second'},{id:desktop,index:1,name:'First'}];});assert.equal(json('bridge','desktops').desktops[0].index,2);
  assert.equal(call('context','send-desktop','1','2').status,2,'low-level movement requires an explicit release');assert.equal(fs.readFileSync(stateFile,'utf8'),before);
  const heldFrame=window().frame;await waitJob(json('context','float','DP-1','1','on'));await until(()=>ready('DP-1'));assert.deepEqual(window().frame,heldFrame);
  assert.equal(call('context','send-desktop','1','77').status,2);assert.equal(call('-m','window','1','--space','next').status,1);
  const saved=fs.readFileSync(stateFile,'utf8'),originalFrame=window().frame;
  const receipt=json('context','send-desktop','1',other);assert.equal(receipt.kind,'desktop-move');assert.equal(receipt.completed,false);
  assert.equal(call('context','send-desktop','1','1').status,2,'pending membership change serializes with other writes');
  const result=await waitDesktop(receipt);assert.equal(result.state,'moved');assert.equal(result.acknowledged,true);await until(()=>window().desktops.length===1&&window().desktops[0]===other);
  assert.equal(json('bridge','outputs').outputs[0]['current-desktop'],desktop,'sending a window must not switch desktops');assert.deepEqual(window().frame,originalFrame);assert.equal(window().output,'DP-1');assert.equal(fs.readFileSync(stateFile,'utf8'),saved,'native membership move leaves saved floating policy intact');assert.equal(status('DP-1').management.enabled,true);
  // Make the destination visible through KDE, then explicitly re-admit into its stored tree.
  await mutate(s=>{s.outputs[0]['current-desktop']=other;s.windows[0].candidate=true;s.windows[0].focused=true;});
  await until(()=>json('context','managers').contexts[0].state==='suspended');json('context','reconcile','DP-1','--apply');
  await waitJob(json('context','float','DP-1','1','off'));json('context','manage','DP-1','on','--lifecycle');await until(()=>ready('DP-1'));
  assert.equal(json('context','show','DP-1').members.length,1);assert.ok(JSON.parse(fs.readFileSync(stateFile)).contexts.every(c=>!c.floating.includes(window().uuid)));
  // Compatible movement waits for both native confirmation and its membership snapshot.
  await waitJob(json('context','float','DP-1','1','on'));await until(()=>ready('DP-1'));
  const compatible=call('-m','window','1','--space','1');assert.equal(compatible.status,0,compatible.stderr);assert.equal(compatible.stdout,'');assert.deepEqual(window().desktops,[desktop]);assert.equal(json('bridge','outputs').outputs[0]['current-desktop'],other);
  // A refusal or ignored write must never report a successful desktop movement.
  await mutate(s=>{s.outputs[0]['current-desktop']=desktop;s.windows[0].candidate=true;s.windows[0].focused=true;s['test-desktop-ignore']=true;});
  await until(()=>ready('DP-1'));
  const noop=call('-m','window','1','--space','1');assert.equal(noop.status,0,noop.stderr);assert.equal(noop.stdout,'');
  const ignored=json('context','send-desktop','1','2');await until(()=>desktopJob(ignored.id).completed);assert.equal(desktopJob(ignored.id).state,'rolled_back');assert.equal(desktopJob(ignored.id).success,false);assert.deepEqual(window().desktops,[desktop]);
 }finally{fs.chmodSync(dir,0o700);await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

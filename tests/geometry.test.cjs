const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('explicit control gate, asynchronous receipts, acknowledgement and removed-restore rejection',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-geometry-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock');let p,d;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin']);await until(()=>json(socket,'doctor')['event-stream'].ready);
  assert.equal(json(socket,'doctor')['desktop-control'],false);assert.equal(call(socket,'bridge','apply','DP-1','1').status,3);await stop(d);
  d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',path.join(dir,'contexts.json')]);await until(()=>json(socket,'doctor')['event-stream'].ready);assert.equal(json(socket,'doctor')['desktop-control'],true);
  s['test-legacy-restore']=true;s.sequence='2';write();await until(()=>!json(socket,'doctor').bridge.capabilities.includes('geometry-no-restore'));
  const rejectedLegacy=call(socket,'bridge','apply','DP-1','1');assert.equal(rejectedLegacy.status,2);assert.match(rejectedLegacy.stderr,/incompatible restore behavior/);
  assert.equal(call(socket,'context','manage','DP-1','on','--lifecycle').status,2);assert.equal(fs.existsSync(path.join(dir,'contexts.json')),false,'legacy effect must be rejected before saving lifecycle reconciliation');
  delete s['test-legacy-restore'];s.sequence='3';write();await until(()=>json(socket,'doctor').bridge.capabilities.includes('geometry-no-restore'));
  const receipt=json(socket,'bridge','apply','DP-1','1');assert.equal(receipt.completed,false);assert.equal(receipt.state,'submitting');
  await until(()=>json(socket,'bridge','transaction',receipt.id).completed);const result=json(socket,'bridge','transaction',receipt.id);assert.equal(result.state,'applied');assert.equal(result.acknowledged,true);assert.equal(result.success,true);
  assert.equal(call(socket,'bridge','restore',receipt.id).status,2);assert.equal(json(socket,'bridge','transaction',receipt.id).state,'applied');
  s['test-geometry-reject']=true;write();const rejected=json(socket,'bridge','apply','DP-1','1');await until(()=>json(socket,'bridge','transaction',rejected.id).completed);assert.equal(json(socket,'bridge','transaction',rejected.id).success,false);
  s['test-geometry-reject']=false;write();
  const admitted=json(socket,'context','add','DP-1','1');assert.equal(admitted.generation,'1');
  json(socket,'context','gap','DP-1','12');const stored=fs.readFileSync(path.join(dir,'contexts.json'),'utf8');
  assert.equal(call(socket,'context','add','DP-1','1').status,2);assert.equal(fs.readFileSync(path.join(dir,'contexts.json'),'utf8'),stored);
  const planned=json(socket,'context','plan','DP-1');assert.equal(planned['context-generation'],'2');
  const committed=json(socket,'context','commit','DP-1');assert.equal(committed['context-generation'],'2');
  await until(()=>json(socket,'bridge','transaction',committed.id).completed);assert.equal(json(socket,'bridge','transaction',committed.id)['context-generation'],'2');
  assert.equal(call(socket,'context','update','DP-1',committed.id).status,2,'same context generation must not replace itself');
  json(socket,'context','gap','DP-1','14');const updated=json(socket,'context','update','DP-1',committed.id);assert.equal(updated['context-generation'],'3');
  await until(()=>json(socket,'bridge','transaction',updated.id).state==='applied');await until(()=>json(socket,'bridge','transaction',committed.id).state==='superseded');
  assert.equal(json(socket,'bridge','transaction',committed.id)['active-transaction'],updated.id);
  assert.equal(call(socket,'bridge','restore',committed.id).status,2);assert.equal(call(socket,'bridge','restore',updated.id).status,2);
  assert.equal(json(socket,'bridge','transaction',committed.id)['active-transaction'],updated.id);
  await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',path.join(dir,'contexts.json')]);await until(()=>json(socket,'doctor')['event-stream'].ready);
  assert.equal(json(socket,'context','show','DP-1').generation,'3');assert.equal(json(socket,'context','plan','DP-1').gap,14);
  assert.equal(fs.statSync(path.join(dir,'contexts.json')).mode&0o777,0o600);
  fs.chmodSync(dir,0o500);assert.equal(call(socket,'context','gap','DP-1','13').status,1);assert.equal(json(socket,'context','show','DP-1').generation,'3');fs.chmodSync(dir,0o700);

  const beforeReconcile=fs.readFileSync(path.join(dir,'contexts.json'),'utf8');
  const review=json(socket,'context','reconcile','DP-1');assert.equal(review['dry-run'],true);assert.equal(review.add.length,0);assert.equal(fs.readFileSync(path.join(dir,'contexts.json'),'utf8'),beforeReconcile);
  assert.equal(call(socket,'context','reconcile','DP-1','--invalid').status,2);
  const next={...s.windows[0],uuid:'44444444-4444-4444-8444-444444444444'};s.windows.push(next);s.sequence='4';write();
  await until(()=>json(socket,'doctor').bridge['window-count']===2);
  assert.equal(json(socket,'context','reconcile','DP-1').add.length,1);
  fs.chmodSync(dir,0o500);assert.equal(call(socket,'context','reconcile','DP-1','--apply').status,1);assert.equal(json(socket,'context','show','DP-1').generation,'3');fs.chmodSync(dir,0o700);
  const batch=json(socket,'context','reconcile','DP-1','--apply');assert.equal(batch.applied,true);assert.equal(batch['desktop-control'],false);assert.equal(batch.context.generation,'4');assert.equal(batch.context.members.length,2);
  json(socket,'context','reconcile','DP-1','--apply');assert.equal(json(socket,'context','show','DP-1').generation,'4');
  await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--context-state',path.join(dir,'contexts.json')]);await until(()=>json(socket,'doctor')['event-stream'].ready);assert.equal(json(socket,'context','show','DP-1').members.length,2);assert.equal(json(socket,'context','show','DP-1').generation,'4');
  assert.equal(json(socket,'context','status','DP-1')['active-count'],0);
  assert.equal(call(socket,'context','apply','DP-1').status,3);assert.equal(call(socket,'context','restore','DP-1').status,2);
  await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',path.join(dir,'contexts.json')]);await until(()=>json(socket,'doctor')['event-stream'].ready);
  const automatic=json(socket,'context','apply','DP-1');assert.equal(automatic.completed,false);
  await until(()=>json(socket,'context','status','DP-1').active?.state==='applied');
  const again=json(socket,'context','apply','DP-1');assert.equal(again.unchanged,true);assert.equal(again.id,automatic.id);
  json(socket,'context','gap','DP-1','15');const replacement=json(socket,'context','apply','DP-1');assert.equal(replacement['previous-id'],automatic.id);
  await until(()=>json(socket,'context','status','DP-1').active?.id===replacement.id&&json(socket,'context','status','DP-1').active?.state==='applied');
  assert.equal(json(socket,'context','status','DP-1')['stored-generation'],'5');
  // A rejected replacement keeps the old active ownership discoverable, even when latest is failed.
  s['test-geometry-reject']=true;write();json(socket,'context','gap','DP-1','16');const failure=json(socket,'context','apply','DP-1');
  await until(()=>json(socket,'context','status','DP-1').latest?.state==='rejected'&&json(socket,'context','status','DP-1').active?.state==='applied');
  assert.equal(json(socket,'context','status','DP-1').active.id,replacement.id);
  s['test-geometry-reject']=false;write();
  json(socket,'context','reset','DP-1');await until(()=>json(socket,'context','status','DP-1')['active-count']===0);assert.equal(call(socket,'context','restore','DP-1').status,2);assert.equal(json(socket,'context','status','DP-1')['active-count'],0);
  const fresh=json(socket,'context','apply','DP-1');assert.notEqual(fresh.id,failure.id);await until(()=>json(socket,'context','status','DP-1').active?.state==='applied');json(socket,'context','reset','DP-1');await until(()=>json(socket,'context','status','DP-1')['active-count']===0);
  assert.equal(call(socket,'bridge','apply','DP-1','999').status,2);assert.equal(call(socket,'bridge','transaction','missing').status,2);assert.equal(json(socket,'doctor').generation,'0');
 }finally{fs.chmodSync(dir,0o700);await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

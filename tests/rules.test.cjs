const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('persisted admission rules exclude windows, support exceptions and preserve active membership',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-rules-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};




 s.capabilities.push('geometry-retention','geometry-lifecycle','notifications');s['geometry-window-limit']=20;s['test-geometry-fast']=true;
 const write=()=>{s.sequence=String(BigInt(s.sequence)+1n);fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const status=()=>json(socket,'context','status','DP-1'),show=()=>json(socket,'context','show','DP-1');
 const ready=()=>status().management.state==='active'&&status().active?.['context-generation']===show().generation;
 const refresh=async()=>{write();await until(()=>json(socket,'doctor').bridge.sequence===s.sequence);};
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);
  assert.equal(json(socket,'rule','list').rules.length,0);json(socket,'rule','add','tools','float','--app','float.*');assert.equal(JSON.parse(fs.readFileSync(stateFile)).version,8);
  assert.equal(json(socket,'rule','test','focused').action,'tile');json(socket,'context','add','DP-1','1');
  const second={...s.windows[0],focused:false,app:'float.tool',title:'Dialog',uuid:'44444444-4444-4444-8444-444444444444'};s.windows.push(second);await refresh();
  let preview=json(socket,'context','reconcile','DP-1');assert.equal(preview.add.length,0);assert.equal(preview.excluded[0].reason,'rule-float');assert.equal(preview.excluded[0].rule,'tools');assert.equal(json(socket,'rule','test',second.uuid).action,'float');const beforeAdd=fs.readFileSync(stateFile,'utf8');assert.equal(call(socket,'context','add','DP-1','2').status,2);assert.equal(fs.readFileSync(stateFile,'utf8'),beforeAdd);
  const beforeInvalid=fs.readFileSync(stateFile,'utf8');for(const args of [['add','tools','tile','--app','*'],['remove','missing'],['add','bad','float','--app','*','--app','*'],['test','unknown']])assert.equal(call(socket,'rule',...args).status,2);assert.equal(fs.readFileSync(stateFile,'utf8'),beforeInvalid);
  await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);assert.equal(show().members.length,1);
  // Last matching rule permits only matching titles to enter the tree.
  json(socket,'rule','add','documents','tile','--app','float.*','--title','*Document*');assert.equal(json(socket,'rule','test',second.uuid).action,'float');s.windows[1].title='Document / notes';await refresh();await until(()=>show().members.length===2&&ready());assert.equal(json(socket,'rule','test',second.uuid).matched,'documents');
  // Rules never eject an existing live member or throw away its restore baseline.
  const applied=status().active.id,generation=show().generation;json(socket,'rule','remove','documents');assert.equal(json(socket,'rule','test',second.uuid)['requires-manual-removal'],true);await new Promise(r=>setTimeout(r,250));assert.equal(status().active.id,applied);assert.equal(show().generation,generation);assert.equal(show().members.length,2);
  const saved=fs.readFileSync(stateFile,'utf8');fs.chmodSync(dir,0o500);assert.equal(call(socket,'rule','add','unsaved','float','--app','*').status,1);fs.chmodSync(dir,0o700);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);assert.equal(json(socket,'rule','list').rules.length,1);assert.equal(status().management.enabled,true);
  // New excluded arrivals do not stop management or change the active transaction.
  const third={...second,uuid:'55555555-5555-4555-8555-555555555555'};s.windows.push(third);await refresh();assert.equal(show().members.length,2);assert.equal(status().active.id,applied);assert.equal(status().management.enabled,true);
  json(socket,'rule','remove','tools');await until(()=>show().members.length===3&&ready());
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);assert.equal(status()['active-count'],0);await stop(d);
  // Corrupt rules fail startup rather than silently becoming a permissive policy.
  const valid=fs.readFileSync(stateFile,'utf8'),corrupt=JSON.parse(valid);corrupt.rules=[{label:'broken',action:'shell',app:'*',title:''}];fs.writeFileSync(stateFile,JSON.stringify(corrupt));
  const failed=spawnSync(daemon,['--socket',socket,'--observe-kwin','--context-state',stateFile],{encoding:'utf8',timeout:5000});assert.equal(failed.status,2);assert.match(failed.stderr,/rule/);fs.writeFileSync(stateFile,valid);
  d=await start(daemon,['--socket',socket,'--observe-kwin','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);assert.equal(json(socket,'rule','list').rules.length,0);assert.equal(status().management.enabled,false);assert.equal(show().members.length,3);
 }finally{fs.chmodSync(dir,0o700);await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

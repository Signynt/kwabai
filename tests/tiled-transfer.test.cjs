const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('tiled transfers serialize stages, preserve baselines, preflight and expose recovery',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-output-move-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const desktop='22222222-2222-4222-8222-222222222222';
 const out=(id,name,x,w,h)=>({id,name,scale:1,geometry:{x,y:0,w,h},'work-area':{x,y:0,w,h},'current-desktop':desktop,'current-activity':'work'});
 const win=(uuid,output)=>({uuid,app:'kitty',title:'test',output,desktops:[desktop],activities:['work'],frame:{x:output==='DP-1'?100:-900,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:output==='DP-1'});
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications','output-move','desktop-move'],'geometry-window-limit':20,'test-geometry-write':true,'test-geometry-exclusive':true,
 desktops:[{id:desktop,index:1,name:'First'},{id:'99999999-9999-4999-8999-999999999999',index:2,name:'Second'}],outputs:[out('screen-a','DP-1',0,2560,1440),out('screen-b','DP-2',-1080,1080,1920)],windows:[win('33333333-3333-4333-8333-333333333333','DP-1'),win('44444444-4444-4444-8444-444444444444','DP-2')]};
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
  const job=id=>json('context','transfer-status',id);
  await mutate(s=>s.windows[0].minimum.w=1500);const before=fs.readFileSync(stateFile,'utf8');assert.equal(call('context','transfer-output','1','DP-2').status,2);assert.equal(fs.readFileSync(stateFile,'utf8'),before,'infeasible transfer must fail before release or save');await mutate(s=>s.windows[0].minimum.w=100);
  const receipt=json('context','transfer-output','1','DP-2');assert.equal(receipt.kind,'output-transfer');assert.equal(receipt.completed,false);
  const held=fs.readFileSync(stateFile,'utf8');assert.equal(call('context','gap','DP-2','50').status,2);assert.equal(fs.readFileSync(stateFile,'utf8'),held);assert.equal(call('context','transfer-output','2','DP-1').status,2);
  assert.equal(status('DP-1').management['paused-for-transfer'],true);
  await until(()=>window().output==='DP-2');const arrival=window().frame;
  await until(()=>job(receipt.id).completed);assert.equal(job(receipt.id).state,'transferred',JSON.stringify(job(receipt.id)));assert.equal(job(receipt.id).acknowledged,true);assert.equal(job(receipt.id).transactions.length,4);
  await until(()=>ready('DP-1')&&ready('DP-2'));assert.equal(status('DP-1').management['paused-for-transfer'],false);assert.equal(status('DP-2').management['paused-for-transfer'],false);
  assert.equal(json('context','show','DP-1').members.length,0);assert.equal(json('context','show','DP-2').members.length,2);
  const kept=json('bridge','windows').windows;json('context','reset','DP-2');assert.deepEqual(json('bridge','windows').windows,kept);assert.equal(window().output,'DP-2');assert.equal(status('DP-1').management.enabled,true);
  json('context','manage','DP-2','on','--lifecycle');await until(()=>ready('DP-1')&&ready('DP-2'));
  let result=call('-m','window','1','--display','east');assert.equal(result.status,0,result.stderr);assert.equal(result.stdout,'');assert.equal(window().output,'DP-1');assert.equal(json('context','show','DP-1').members.length,1);assert.equal(json('context','show','DP-2').members.length,1);await until(()=>ready('DP-1')&&ready('DP-2'));
  // Failure after a confirmed release must retain explicit floating intent and report its phase.
  const failed=json('context','transfer-output','1','DP-2');await until(()=>job(failed.id).phase==='release-snapshot'||job(failed.id).phase==='moving');
  await mutate(s=>s['test-geometry-reject']=true);await until(()=>job(failed.id).completed);assert.equal(job(failed.id).success,false);assert.equal(job(failed.id).state,'failed');assert.ok(job(failed.id).recovery);assert.equal(status('DP-1').management.enabled,false);assert.equal(status('DP-2').management.enabled,false);
  assert.equal(status('DP-1').management['paused-for-transfer'],false);const failedState=fs.readFileSync(stateFile,'utf8');await new Promise(r=>setTimeout(r,250));assert.equal(fs.readFileSync(stateFile,'utf8'),failedState,'failure must not retry or rewrite state');
  // Recover explicitly, then interrupt a new transfer with a desktop switch.
  await mutate(s=>s['test-geometry-reject']=false);
  for(const out of ['DP-1','DP-2'])if(status(out)['active-count'])json('context','reset',out);
  const current=window().output;const floated=json('context','float',current,'1','on');if(Object.hasOwn(floated,'completed'))await waitJob(floated);
  await waitJob(json('context','float',current,'1','off'));
  json('context','manage','DP-1','on','--lifecycle');json('context','manage','DP-2','on','--lifecycle');await until(()=>ready('DP-1')&&ready('DP-2'));
  const interrupted=json('context','transfer-output','1',current==='DP-1'?'DP-2':'DP-1');
  await mutate(s=>s.outputs.find(o=>o.name===current)['current-desktop']='99999999-9999-4999-8999-999999999999');
  await until(()=>job(interrupted.id).completed);assert.equal(job(interrupted.id).success,false);assert.match(job(interrupted.id).error,/desktop|Activity|output/);assert.equal(window().output,current);assert.equal(json('doctor')['output-transfer'].id,interrupted.id);
 }finally{fs.chmodSync(dir,0o700);await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

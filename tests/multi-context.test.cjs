const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('independent contexts serialize, suspend, resume, reflow and isolate failures',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-multi-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock');let p,d;
 const desktop='22222222-2222-4222-8222-222222222222',other='99999999-9999-4999-8999-999999999999';
 const output=(id,name,x,w,h)=>({id,name,scale:1,geometry:{x,y:0,w,h},'work-area':{x,y:0,w,h},'current-desktop':desktop,'current-activity':'work'});
 const window=(uuid,out,ds=[desktop],activities=['work'])=>({uuid,app:'kitty',title:'test',output:out,desktops:ds,activities,frame:{x:out==='DP-1'?100:-900,y:100,w:500,h:500},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:out==='DP-1'});
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','notifications'],'geometry-window-limit':20,'test-geometry-write':true,'test-geometry-exclusive':true,
 outputs:[output('screen-a','DP-1',0,2560,1440),output('screen-b','DP-2',-1080,1080,1920)],windows:[window('33333333-3333-4333-8333-333333333333','DP-1'),window('44444444-4444-4444-8444-444444444444','DP-2')]};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const json=(...args)=>{const r=spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const managers=()=>json('context','managers');const status=out=>json('context','status',out);
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);const seq=s.sequence;write();await until(()=>BigInt(json('doctor').bridge.sequence)>=BigInt(seq));};
 try {
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',path.join(dir,'contexts.json')]);await until(()=>json('doctor')['event-stream'].ready);
  json('context','reconcile','DP-1','--apply');json('context','reconcile','DP-2','--apply');
  const a=json('context','manage','DP-1','on','--lifecycle')['context-id'];
  const b=json('context','manage','DP-2','on','--lifecycle')['context-id'];
  await until(()=>status('DP-1').management.state==='active'&&status('DP-2').management.state==='active');
  assert.equal(managers()['enabled-count'],2);assert.notEqual(a,b);
  json('context','gap','DP-1','30');json('context','gap','DP-2','40');
  await until(()=>['DP-1','DP-2'].every(out=>{const r=status(out);return r.management.state==='active'&&r.active['context-generation']===r['stored-generation'];}));
  json('context','reset','DP-2');await until(()=>status('DP-2')['active-count']===0);assert.equal(status('DP-1').management.state,'active');
  json('context','manage','DP-2','on','--lifecycle');await until(()=>status('DP-2').management.state==='active');
  // Switching away pauses just that context; a second context can run on the same output.
  await mutate(s=>{s.outputs[0]['current-desktop']=other;s.windows.push(window('55555555-5555-4555-8555-555555555555','DP-1',[other]));});
  await until(()=>managers().contexts.find(c=>c['context-id']===a).state==='suspended');assert.equal(status('DP-2').management.state,'active');
  json('context','reconcile','DP-1','--apply');const c=json('context','manage','DP-1','on','--lifecycle')['context-id'];
  await until(()=>status('DP-1').management.state==='active');assert.equal(managers()['enabled-count'],3);
  await mutate(s=>s.outputs[0]['current-desktop']=desktop);
  await until(()=>status('DP-1').management.state==='active'&&managers().contexts.find(e=>e['context-id']===c).state==='suspended');
  assert.equal(status('DP-1').management['context-id'],a);
  // Activities use the same pinned identity and safe suspension path.
  await mutate(s=>{s.outputs[0]['current-activity']='personal';s.windows.push({...window('77777777-7777-4777-8777-777777777777','DP-1',[desktop],['personal']),focused:false});});await until(()=>managers().contexts.find(e=>e['context-id']===a).state==='suspended');
  json('context','reconcile','DP-1','--apply');const activityContext=json('context','manage','DP-1','on','--lifecycle')['context-id'];await until(()=>status('DP-1').management.state==='active');
  await mutate(s=>s.outputs[0]['current-activity']='work');await until(()=>status('DP-1').management.state==='active'&&managers().contexts.find(e=>e['context-id']===activityContext).state==='suspended');
  // Stable output identity reconnects even after its connector name changes.
  let detached;await mutate(s=>{detached=s.outputs.pop();s.windows.find(w=>w.output==='DP-2').output='DP-1';});await until(()=>managers().contexts.find(e=>e['context-id']===b).state==='suspended');
  await mutate(s=>{detached.name='DP-9';s.outputs.push(detached);s.windows.find(w=>w.uuid==='44444444-4444-4444-8444-444444444444').output='DP-9';});await until(()=>status('DP-9').management.state==='active');
  // Work area changes get a fresh confirmed generation without adding undo history.
  const before=status('DP-9'),history=before.history;
  await mutate(s=>{s.outputs[1]['work-area'].y=40;s.outputs[1]['work-area'].h-=40;});
  await until(()=>{const r=status('DP-9');return r.management.state==='active'&&BigInt(r['stored-generation'])>BigInt(before['stored-generation'])&&r.active['context-generation']===r['stored-generation'];});
  assert.deepEqual(status('DP-9').history,history);assert.equal(json('bridge','windows').windows.find(w=>w.output==='DP-9').frame.y,50);
  // Lifecycle updates stay local while another context remains suspended.
  await mutate(s=>s.windows.push({...window('66666666-6666-4666-8666-666666666666','DP-1'),focused:false}));
  await until(()=>json('context','show','DP-1').members.length===2&&status('DP-1').management.state==='active');assert.equal(status('DP-9').management.state,'active');
  // A migrated/moved window cannot be silently reclaimed on reconnect.
  await mutate(s=>{detached=s.outputs.pop();s.windows.find(w=>w.output==='DP-9').output='DP-1';});await until(()=>managers().contexts.find(e=>e['context-id']===b).state==='suspended');
  await mutate(s=>{s.outputs.push(detached);const w=s.windows.find(w=>w.uuid==='44444444-4444-4444-8444-444444444444');w.frame.x=200;});
  await until(()=>status('DP-9').management.state==='stopped');assert.equal(status('DP-9').management.enabled,false);assert.equal(status('DP-1').management.enabled,true);
  assert.match(status('DP-9').management.error,/moved|context|ineligible/);
  json('context','manage-id',c,'off');assert.equal(managers().contexts.find(e=>e['context-id']===c).enabled,false);
  json('context','manage','all','off');assert.equal(managers()['enabled-count'],0);
  await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',path.join(dir,'contexts.json')]);await until(()=>json('doctor')['event-stream'].ready);assert.equal(managers()['enabled-count'],0);
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

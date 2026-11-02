const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
const first='22222222-2222-4222-8222-222222222222',second='99999999-9999-4999-8999-999999999999';
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
async function fixture(run){
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-desktop-arrival-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const win=(uuid,desktop,x)=>({uuid,app:'kitty',title:'test',output:'DP-1',desktops:[desktop],activities:['work'],frame:{x,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:desktop===first,minimized:false,fullscreen:false,focused:desktop===first});
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications','desktop-move'],'geometry-window-limit':20,'test-geometry-write':true,'test-geometry-exclusive':true,
 desktops:[{id:first,index:1,name:'First'},{id:second,index:2,name:'Second'}],outputs:[{id:'screen-a',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:0,w:2560,h:1440},'current-desktop':first,'current-activity':'work'}],windows:[win('33333333-3333-4333-8333-333333333333',first,100),win('44444444-4444-4444-8444-444444444444',second,1000)]};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:22000});
 const json=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const state=()=>JSON.parse(fs.readFileSync(stateFile));
 const status=()=>json('context','status','DP-1');
 const window=id=>json('bridge','windows').windows.find(w=>w.id===String(id));
 const ready=()=>{const r=status();assert.notEqual(r.management.state,'stopped',r.management.error);return ['active','idle'].includes(r.management.state)&&(!r.active||r.active['context-generation']===r['stored-generation']);};
 const waitJob=async receipt=>{let result;await until(()=>{result=json('bridge','transaction',receipt.id);return result.completed;});assert.equal(result.success,true,JSON.stringify(result));return result;};
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);const seq=s.sequence;write();await until(()=>BigInt(json('doctor').bridge.sequence)>=BigInt(seq));};
 const switchTo=async desktop=>mutate(s=>{s.outputs[0]['current-desktop']=desktop;for(const w of s.windows){w.candidate=w.desktops.includes(desktop);w.focused=false;}});
 const enable=async lifecycle=>{json('context','reconcile','DP-1','--apply');json('context','manage','DP-1','on',...(lifecycle?['--lifecycle']:[]));await until(ready);};
 const restart=async()=>{await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);};
 try {p=await start(provider,[file]);await restart();await run({call,json,state,status,window,ready,waitJob,mutate,switchTo,enable,restart,stateFile});}
 finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
}
test('tiled desktop transfers defer geometry and perform a one-time arrival without enabling lifecycle',()=>fixture(async f=>{
 const {json,call,enable,state,status,window,ready,mutate,switchTo,waitJob}=f;
 await enable(true);const source=json('context','show','DP-1').id;
 assert.equal(call('context','queue-desktop','screen-a',window(1).uuid,second).status,2,'internal admission is not a public mutation');
 await mutate(s=>s.windows[0].minimum.w=3000);const before=fs.readFileSync(f.stateFile,'utf8');assert.equal(call('context','transfer-desktop','1','2').status,2);assert.equal(fs.readFileSync(f.stateFile,'utf8'),before,'minimum preflight must not release or save');await mutate(s=>s.windows[0].minimum.w=100);
 const beforeTransfer=window(1).frame;const receipt=json('context','transfer-desktop','1','2');assert.equal(receipt.kind,'desktop-transfer');assert.equal(call('context','gap','DP-1','25').status,2);
 let result;await until(()=>{result=json('context','transfer-status',receipt.id);return result.completed;});assert.equal(result.state,'queued-for-desktop',JSON.stringify(result));assert.equal(result.success,true);assert.equal(result.transactions.length,3);assert.deepEqual(result['stage-transactions'].map(s=>s.kind),['geometry','desktop-move','geometry']);
 await until(ready);assert.deepEqual(window(1).desktops,[second]);assert.deepEqual(window(1).frame,beforeTransfer);assert.equal(json('bridge','outputs').outputs[0]['current-desktop'],first);
 const target=state().contexts.find(c=>c.desktop===second);assert.ok(target.members.some(m=>m.uuid===window(1).uuid));assert.ok(state().contexts.every(c=>!c.floating.includes(window(1).uuid)));assert.equal(status().management.enabled,true);
 const controller=()=>json('context','managers').contexts.find(c=>c['context-id']===target.id);
 assert.equal(controller().state,'suspended');assert.equal(controller()['arrival-only'],true);assert.deepEqual(controller().arrivals,[window(1).uuid]);assert.equal(controller()['automatic-membership'],false);
 await switchTo(second);await until(()=>{const s=status();assert.notEqual(s.management.state,'stopped',s.management.error);return s.active?.state==='applied'&&s.active['context-generation']===s['stored-generation']&&!s.management.enabled;});
 assert.deepEqual(controller().arrivals,[]);assert.equal(json('context','show','DP-1').members.length,1,'one-time arrival does not auto-admit unrelated destination windows');assert.deepEqual(window(1).frame,{x:10,y:10,w:2540,h:1420});assert.deepEqual(window(2).frame,{x:1000,y:100,w:800,h:600});
 assert.equal(json('context','managers').contexts.find(c=>c['context-id']===source).state,'suspended');
 const kept=window(1).frame;json('context','reset','DP-1');assert.deepEqual(window(1).frame,kept);assert.deepEqual(window(1).desktops,[second],'restore does not move membership back');
}));
test('arrival extends a suspended fixed-membership context and preserves its management mode and baselines',()=>fixture(async f=>{
 const {json,call,enable,ready,state,status,window,switchTo,waitJob,mutate}=f;
 await switchTo(second);await enable(false);const destination=json('context','show','DP-1').id;
 await switchTo(first);await enable(true);
 await mutate(s=>{for(const w of s.windows)w.minimum.w=1600;});const before=fs.readFileSync(f.stateFile,'utf8');assert.equal(call('context','transfer-desktop','1','2').status,2,'combined inactive destination minimums must reject before release');assert.equal(fs.readFileSync(f.stateFile,'utf8'),before);await mutate(s=>{for(const w of s.windows)w.minimum.w=100;});
 const heldFrame=window(1).frame;const result=call('-m','window','1','--space','2');assert.equal(result.status,0,result.stderr);assert.equal(result.stdout,'');assert.deepEqual(window(1).desktops,[second]);await until(ready);
 assert.equal(state().contexts.find(c=>c.id===destination).members.length,2);
 await switchTo(second);await until(ready);assert.equal(status().management.enabled,true);assert.equal(status().management['automatic-membership'],false);assert.deepEqual(status().management.arrivals,[]);assert.equal(json('context','show','DP-1').members.length,2);
 const kept=json('bridge','windows').windows;json('context','reset','DP-1');assert.deepEqual(json('bridge','windows').windows,kept);
}));
test('queued membership survives daemon restart with explicit management recovery',()=>fixture(async f=>{
 const {json,call,enable,state,status,window,switchTo,restart,ready}=f;
 await enable(true);const heldFrame=window(1).frame;const result=call('-m','window','1','--space','2');assert.equal(result.status,0,result.stderr);const target=state().contexts.find(c=>c.desktop===second);
 await restart();assert.equal(json('context','managers')['enabled-count'],0);assert.ok(state().contexts.find(c=>c.id===target.id).members.some(m=>m.uuid===window(1).uuid));assert.deepEqual(window(1).desktops,[second]);
 await switchTo(second);assert.equal(status().management.enabled,false);json('context','manage','DP-1','on');await until(ready);assert.deepEqual(window(1).frame,{x:10,y:10,w:2540,h:1420});
 // A same-desktop command is a confirmed no-op, without creating a second arrival.
 const before=fs.readFileSync(f.stateFile,'utf8');const noop=call('-m','window','1','--space','2');assert.equal(noop.status,0,noop.stderr);assert.equal(fs.readFileSync(f.stateFile,'utf8'),before);
}));
test('an unobserved desktop write fails without saving admission or retrying',()=>fixture(async f=>{
 const {json,enable,state,status,window,mutate}=f;await enable(true);await mutate(s=>s['test-desktop-ignore']=true);
 const receipt=json('context','transfer-desktop','1','2');let result;await until(()=>{result=json('context','transfer-status',receipt.id);return result.completed;});
 assert.equal(result.success,false);assert.equal(result.phase,'moving');assert.ok(result.recovery);assert.deepEqual(window(1).desktops,[first]);assert.equal(state().contexts.length,1);assert.equal(state().contexts[0].members.length,0);assert.ok(state().contexts[0].floating.includes(window(1).uuid));assert.equal(status().management.enabled,false);
 const before=fs.readFileSync(f.stateFile,'utf8');await new Promise(r=>setTimeout(r,250));assert.equal(fs.readFileSync(f.stateFile,'utf8'),before);
}));
test('stopping and explicitly re-enabling a pending fixed-context arrival retains its admission permission',()=>fixture(async f=>{
 const {json,call,enable,status,window,switchTo,ready}=f;await switchTo(second);await enable(false);const destination=json('context','show','DP-1').id;await switchTo(first);await enable(true);
 const heldFrame=window(1).frame;const result=call('-m','window','1','--space','2');assert.equal(result.status,0,result.stderr);json('context','manage-id',destination,'off');await switchTo(second);assert.equal(status().management.enabled,false);assert.deepEqual(window(1).frame,heldFrame);
 json('context','manage','DP-1','on');await until(ready);assert.equal(status().management['automatic-membership'],false);assert.deepEqual(status().management.arrivals,[]);assert.equal(json('context','show','DP-1').members.length,2);
}));
test('a closed queued arrival stops on activation without silently rewriting the saved intent',()=>fixture(async f=>{
 const {json,call,enable,state,status,window,mutate,switchTo}=f;await enable(true);const uuid=window(1).uuid;const heldFrame=window(1).frame;const result=call('-m','window','1','--space','2');assert.equal(result.status,0,result.stderr);
 const target=state().contexts.find(c=>c.desktop===second);await mutate(s=>s.windows=s.windows.filter(w=>w.uuid!==uuid));await switchTo(second);
 await until(()=>status().management.state==='stopped');assert.equal(status().management.enabled,false);assert.match(status().management.error,/arrival|disappeared|missing|unavailable/);assert.ok(state().contexts.find(c=>c.id===target.id).members.some(m=>m.uuid===uuid));assert.equal(status()['active-count'],0);
}));

const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
const first='22222222-2222-4222-8222-222222222222',second='99999999-9999-4999-8999-999999999999';
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
const desktopId='77777777-7777-4777-8777-777777777777';
async function fixture(run){
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-desktop-arrival-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const win=(uuid,desktop,x)=>({uuid,app:'kitty',title:'test',output:'DP-1',desktops:[desktopId],activities:[desktop],frame:{x,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:desktop===first,minimized:false,fullscreen:false,focused:desktop===first});
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications','activity-move'],'geometry-window-limit':20,'test-geometry-write':true,'test-geometry-exclusive':true,
 desktops:[{id:desktopId,index:1,name:'Desktop'}],activities:[{id:first},{id:second}],outputs:[{id:'screen-a',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:0,w:2560,h:1440},'current-desktop':desktopId,'current-activity':first}],windows:[win('33333333-3333-4333-8333-333333333333',first,100),win('44444444-4444-4444-8444-444444444444',second,1000)]};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:22000});
 const json=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const state=()=>JSON.parse(fs.readFileSync(stateFile));
 const status=()=>json('context','status','DP-1');
 const window=id=>json('bridge','windows').windows.find(w=>w.id===String(id));
 const ready=()=>{const r=status();assert.notEqual(r.management.state,'stopped',r.management.error);return ['active','idle'].includes(r.management.state)&&(!r.active||r.active['context-generation']===r['stored-generation']);};
 const waitJob=async receipt=>{let result;await until(()=>{result=json('bridge',receipt.kind==='activity-move'?'activity-transaction':'transaction',receipt.id);return result.completed;});assert.equal(result.success,true,JSON.stringify(result));return result;};
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);const seq=s.sequence;write();await until(()=>BigInt(json('doctor').bridge.sequence)>=BigInt(seq));};
 const switchTo=async desktop=>mutate(s=>{s.outputs[0]['current-activity']=desktop;for(const w of s.windows){w.candidate=w.activities.length===0||w.activities.includes(desktop);w.focused=false;}});
 const enable=async lifecycle=>{json('context','reconcile','DP-1','--apply');json('context','manage','DP-1','on',...(lifecycle?['--lifecycle']:[]));await until(ready);};
 const restart=async()=>{await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);};
 try {p=await start(provider,[file]);await restart();await run({call,json,state,status,window,ready,waitJob,mutate,switchTo,enable,restart,stateFile});}
 finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
}
test('Activity transfer reflows source, preserves desktop/output and defers destination until activation',()=>fixture(async f=>{
 const {json,call,enable,state,status,window,ready,mutate,switchTo}=f;
 await switchTo(second);await enable(false);const destination=json('context','show','DP-1').id;await switchTo(first);await enable(true);
 const before=fs.readFileSync(f.stateFile,'utf8');assert.equal(call('context','transfer-activity','1','missing').status,2);assert.equal(fs.readFileSync(f.stateFile,'utf8'),before);
 assert.equal(call('context','queue-activity','screen-a',window(1).uuid,second).status,2,'admission is an internal stage');
 const held=window(1).frame;const receipt=json('context','transfer-activity','1',second);assert.equal(receipt.kind,'activity-transfer');let result;
 await until(()=>{result=json('context','transfer-status',receipt.id);return result.completed;});assert.equal(result.success,true,JSON.stringify(result));assert.equal(result.state,'queued-for-activity');assert.equal(result['confirmed-activity'],second);assert.equal(json('doctor')['activity-move'].kind,'activity-move');assert.deepEqual(json('doctor')['desktop-move'],{});assert.deepEqual(result['stage-transactions'].map(s=>s.kind),['geometry','activity-move','geometry']);
 assert.deepEqual(window(1).activities,[second]);assert.deepEqual(window(1).desktops,[desktopId]);assert.equal(window(1).output,'DP-1');assert.deepEqual(window(1).frame,held);assert.equal(json('bridge','outputs').outputs[0]['current-activity'],first);
 assert.ok(state().contexts.find(c=>c.id===destination).members.some(m=>m.uuid===window(1).uuid));assert.ok(state().contexts.every(c=>!c.floating.includes(window(1).uuid)));await until(ready);
 await switchTo(second);await until(()=>ready()&&json('context','show','DP-1').members.length===2);assert.equal(status().management['automatic-membership'],false);assert.equal(window(1).frame.w,1265);assert.equal(window(2).frame.w,1265);assert.notEqual(window(1).frame.x,window(2).frame.x);
}));
test('explicit floating Activity movement keeps geometry and floating policy',()=>fixture(async f=>{
 const {json,call,enable,window,ready,waitJob}=f;await enable(true);const held=window(1).frame;await waitJob(json('context','float','DP-1','1','on'));await until(ready);assert.deepEqual(window(1).frame,held);
 assert.equal(call('context','send-activity','1','2').status,2,'Activity indexes are not invented');const result=await waitJob(json('context','send-activity','1',second));assert.equal(result.activity,second);assert.deepEqual(window(1).activities,[second]);assert.deepEqual(window(1).desktops,[desktopId]);assert.deepEqual(window(1).frame,held);assert.ok(json('context','show','DP-1').floating.includes(window(1).uuid));
}));
test('unobserved Activity write fails without saving destination admission or retrying',()=>fixture(async f=>{
 const {json,enable,state,status,window,mutate}=f;await enable(true);await mutate(s=>s['test-activity-ignore']=true);const receipt=json('context','transfer-activity','1',second);let result;await until(()=>{result=json('context','transfer-status',receipt.id);return result.completed;});assert.equal(result.success,false);assert.match(result.error,/rolled_back|timed/);assert.deepEqual(window(1).activities,[first]);assert.ok(!state().contexts.some(c=>c.activity===second));assert.equal(status().management.enabled,false);const saved=fs.readFileSync(f.stateFile,'utf8');await new Promise(r=>setTimeout(r,300));assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);
}));
test('all-Activities tiled window can be assigned exclusively to the destination Activity',()=>fixture(async f=>{
 const {json,enable,window,mutate,switchTo,ready}=f;await mutate(s=>s.windows[0].activities=[]);await enable(true);const receipt=json('context','transfer-activity','1',second);let result;await until(()=>{result=json('context','transfer-status',receipt.id);return result.completed;});assert.equal(result.success,true,JSON.stringify(result));assert.deepEqual(window(1).activities,[second]);await switchTo(second);await until(()=>json('context','status','DP-1').active?.state==='applied');assert.deepEqual(window(1).desktops,[desktopId]);
}));

test('queued Activity admission survives restart and waits for explicit management recovery',()=>fixture(async f=>{
 const {json,enable,restart,switchTo,status,window}=f;await enable(true);const receipt=json('context','transfer-activity','1',second);let result;await until(()=>{result=json('context','transfer-status',receipt.id);return result.completed;});assert.equal(result.success,true,JSON.stringify(result));await restart();await switchTo(second);assert.equal(status().management.enabled,false);json('context','manage','DP-1','on');await until(()=>status().active?.state==='applied'&&status().management.state==='active');assert.deepEqual(window(1).activities,[second]);assert.deepEqual(window(1).desktops,[desktopId]);
}));

const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider,qdbus]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,25));}throw Error('condition timed out');}
async function fixture(run,previews=false,outputDrops=false,hiddenLifecycle=false){
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-mouse-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 const desktop='22222222-2222-4222-8222-222222222222';
 const win=(uuid,x)=>({uuid,app:'kitty',title:'test',output:'DP-1',desktops:[desktop],activities:['work'],frame:{x,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:x===100});
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications','mouse','geometry-forget'],'geometry-window-limit':20,
 outputs:[{id:'screen-a',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:0,w:2560,h:1440},'current-desktop':desktop,'current-activity':'work'}],windows:[win('33333333-3333-4333-8333-333333333333',100),win('44444444-4444-4444-8444-444444444444',1000)]};
 if(hiddenLifecycle)s.capabilities.push("geometry-hidden-release");
 if(outputDrops)s.capabilities.push("mouse-output-drop","output-move","mouse-output-swap");
 if(previews&&outputDrops)s.capabilities.push("mouse-output-preview");
 if(previews){s.capabilities.push("drop-preview");if(!outputDrops){s.windows[0].minimum.w=1500;s.windows[0].frame.w=1600;}}
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:10000});
 const json=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const state=()=>JSON.parse(fs.readFileSync(stateFile));const status=()=>json('context','status','DP-1');const window=id=>json('bridge','windows').windows.find(w=>w.id===String(id));
 const ready=()=>{const r=status();assert.notEqual(r.management.state,'stopped',r.management.error);return r.management.state==='active'&&r.active?.state==='applied'&&r.active['context-generation']===r['stored-generation'];};
 const mutate=async fn=>{const before=json('doctor').bridge.sequence;s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);write();await until(()=>BigInt(json('doctor').bridge.sequence)>BigInt(before));};
 const gesture=(id,kind,phase,x=0,y=0)=>{const r=spawnSync(qdbus,['org.kde.KWin','/org/kwabai/Bridge','org.kwabai.Bridge1.TestInteraction',window(id).uuid,kind,phase,String(x),String(y)],{encoding:'utf8',timeout:3000});assert.equal(r.status,0,r.stderr);};
 const begin=async(id,kind)=>{const uuid=window(id).uuid;gesture(id,kind,'start');await mutate(s=>s.windows.find(w=>w.uuid===uuid).candidate=false);await until(()=>status().management.state==='interacting');};
 const finish=async(id,kind,x=0,y=0)=>{const uuid=window(id).uuid;await mutate(s=>s.windows.find(w=>w.uuid===uuid).candidate=true);gesture(id,kind,'finish',x,y);await until(ready);};
 try{p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);json('context','reconcile','DP-1','--apply');json('context','manage','DP-1','on','--lifecycle');await until(ready);await run({json,call,state,status,window,ready,mutate,gesture,begin,finish,stateFile,preview:()=>{const r=spawnSync(qdbus,["org.kde.KWin","/org/kwabai/Bridge","org.kwabai.Bridge1.TestDropPreview"],{encoding:"utf8"});assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}});}
 finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
}
test('mouse resize commits once after release; pointer drop swaps; cancel and undo preserve current layout ownership',()=>fixture(async f=>{
 const {json,call,status,window,ready,mutate,begin,finish,stateFile}=f;const original=window(1).frame;const before=fs.readFileSync(stateFile,'utf8');const firstGeneration=status()['stored-generation'];
 await begin(1,'resize');await mutate(s=>s.windows[0].frame.w+=200);assert.equal(fs.readFileSync(stateFile,'utf8'),before,'no saves or tree edits during drag');assert.equal(call('context','gap','DP-1','25').status,2,'competing CLI edit is rejected before saving');assert.equal(window(2).frame.w,1265);assert.equal(json('context','geometry','DP-1')['frames-match'],false);
 await finish(1,'resize',1475,500);await until(()=>window(1).frame.w>1400&&window(2).frame.w<1100);assert.equal(status()['stored-generation'],String(BigInt(firstGeneration)+1n));assert.equal(status().management.enabled,true);assert.equal(json('context','show','DP-1').history.undo,1);const resized=window(1).frame;const other=window(2).frame;
 await begin(1,'move');await mutate(s=>{s.windows[0].frame.x=1500;s.windows[0].frame.y=200;});await finish(1,'move',other.x+other.w/2,other.y+other.h/2);await until(()=>window(1).frame.x===other.x);assert.deepEqual(window(1).frame,other);assert.deepEqual(window(2).frame,resized);
 json('context','undo','DP-1');await until(ready);await until(()=>window(1).frame.x===resized.x);assert.deepEqual(window(1).frame,resized);
 // An unchanged endpoint (Escape/cancel) consumes the token without a layout history entry.
 const undoBefore=json('context','show','DP-1').history.undo;await begin(1,'move');await finish(1,'move',other.x+other.w/2,500);assert.deepEqual(window(1).frame,resized);assert.equal(json('context','show','DP-1').history.undo,undoBefore);
 // Outer edges cannot adjust a split and should return to the existing tile.
 await begin(1,'resize');await mutate(s=>{s.windows[0].frame.x-=50;s.windows[0].frame.w+=50;});await finish(1,'resize',-40,100);assert.deepEqual(window(1).frame,resized);
 const kept=json('bridge','windows').windows;json('context','reset','DP-1');assert.deepEqual(json('bridge','windows').windows,kept);
}));
test('concurrent peer movement stops without saving or overwriting the user gesture',()=>fixture(async f=>{
 const {begin,mutate,gesture,status,window,stateFile}=f;const before=fs.readFileSync(stateFile,'utf8');await begin(1,'move');await mutate(s=>{s.windows[0].frame.x+=100;s.windows[1].frame.y+=50;});gesture(1,'move','finish',2000,500);await until(()=>status().management.state==='stopped');assert.equal(status().management.enabled,false);assert.match(status().management.error,/another layout member/);assert.equal(fs.readFileSync(stateFile,'utf8'),before);assert.equal(window(1).frame.x,110);assert.equal(window(2).frame.y,60);
}));
test('a mouse move to another output stops and leaves the native endpoint for explicit recovery',()=>fixture(async f=>{
 const {begin,mutate,gesture,status,window,stateFile}=f;const before=fs.readFileSync(stateFile,'utf8');await begin(1,'move');await mutate(s=>{s.outputs.push({...s.outputs[0],id:'screen-b',name:'DP-2',geometry:{x:-1080,y:0,w:1080,h:1920},'work-area':{x:-1080,y:0,w:1080,h:1920}});s.windows[0].output='DP-2';s.windows[0].frame.x=-1000;});gesture(1,'move','finish',-600,400);await until(()=>status().management.state==='stopped');assert.match(status().management.error,/output|work area/);assert.equal(fs.readFileSync(stateFile,'utf8'),before);assert.equal(window(1).output,'DP-2');assert.equal(window(1).frame.x,-1000);
}));
test('edge drops insert on each side, while center drops still swap and history restores topology',()=>fixture(async f=>{
 const {json,status,window,ready,mutate,begin,finish}=f;const initialTree=json('context','show','DP-1').tree;
 for(const direction of ['north','south','west','east']) {
  const target=window(2).frame;const x=target.x+target.w*(direction==='west'?0.1:direction==='east'?0.9:0.5),y=target.y+target.h*(direction==='north'?0.1:direction==='south'?0.9:0.5);
  await begin(1,'move');await mutate(s=>{s.windows[0].frame.x+=100;s.windows[0].frame.y+=100;});await finish(1,'move',x,y);
  const a=window(1).frame,b=window(2).frame;
  if(direction==='north')assert.ok(a.y+a.h<=b.y);if(direction==='south')assert.ok(b.y+b.h<=a.y);if(direction==='west')assert.ok(a.x+a.w<=b.x);if(direction==='east')assert.ok(b.x+b.w<=a.x);
  assert.equal(status().management.enabled,true);json('context','undo','DP-1');await until(ready);
  // Undo restores topology and ratios but keeps allocation counters monotonic.
  const restored=json('context','show','DP-1').tree;assert.deepEqual(restored.nodes,initialTree.nodes);assert.equal(restored.root,initialTree.root);
 }
 // Programmatic swaps after a nonintegral ratio keep the same integer slots.
 json('context','ratio','DP-1','1','x','0.613');await until(ready);const originalA=window(1).frame,originalB=window(2).frame;
 for(let i=0;i<4;i++){json('context','swap','DP-1','1','2');await until(ready);assert.deepEqual(window(1).frame,i%2===0?originalB:originalA);assert.deepEqual(window(2).frame,i%2===0?originalA:originalB);}
}));
test('geometry diagnostics and tracing distinguish an acknowledged target from later horizontal client drift',()=>fixture(async f=>{
 const {json,mutate,stateFile}=f;
 json('context','manage','DP-1','off');const initial=json('context','geometry','DP-1');assert.equal(initial['exact-frames'],true);assert.equal(initial.confirmed,true);assert.equal(initial['frames-match'],true);
 const first=initial.windows.find(w=>w.id==='1'),target=first['target-frame'],saved=fs.readFileSync(stateFile,'utf8');
 const recorder=spawn('python3',[path.resolve(__dirname,'../scripts/trace-geometry.py'),'DP-1','1','--socket',path.join(path.dirname(stateFile),'control.sock'),'--seconds','1.2','--interval','0.05']);
 let output='',errors='';recorder.stdout.on('data',b=>output+=b);recorder.stderr.on('data',b=>errors+=b);const exited=once(recorder,'exit');
 try {
  await until(()=>output.includes('target-frame'));
  await mutate(s=>{const w=s.windows[0];w.app='firefox';w['requested-frame']={...target};w.frame.w+=6;w['client-frame']={...w.frame};w['buffer-frame']={...w.frame,x:w.frame.x-10,w:w.frame.w+20};});
  const [code]=await exited;assert.equal(code,0,errors);const samples=output.trim().split('\n').map(JSON.parse);assert.ok(samples.length>=2);assert.equal(samples[0].windows.length,1);assert.equal(samples.at(-1).windows[0].delta.w,6);assert.equal(samples.at(-1).windows[0].delta.h,0);assert.equal(samples.at(-1).windows[0]['right-edge-error'],6);assert.equal(samples.at(-1).windows[0]['target-frame'].w,target.w);assert.equal(samples.at(-1).windows[0]['requested-frame'].w,target.w);assert.equal(samples.at(-1).windows[0]['buffer-frame'].w,target.w+26);assert.equal(samples.at(-1).confirmed,true);
  const report=json('context','geometry','DP-1');assert.equal(report['frames-match'],false);assert.equal(report['exact-frames'],false);assert.equal(fs.readFileSync(stateFile,'utf8'),saved,'diagnostics must not rewrite layout intent');
  const appTrace=spawnSync('python3',[path.resolve(__dirname,'../scripts/trace-geometry.py'),'DP-1','app:firefox','--socket',path.join(path.dirname(stateFile),'control.sock'),'--seconds','0.1'],{encoding:'utf8',timeout:3000});assert.equal(appTrace.status,0,appTrace.stderr);assert.equal(JSON.parse(appTrace.stdout.trim()).windows[0].app,'firefox');
  await mutate(s=>s.windows[0].minimized=true);const blocked=json('context','geometry','DP-1');assert.match(blocked['plan-error'],/ineligible/);assert.equal(blocked.windows.find(w=>w.id==='1')['target-frame'].w,target.w,'blocked planning must not hide the actual submitted target');
 }finally{await stop(recorder);}
}));
test('hover preview matches the final constrained drop without saving or moving peers, and clears on stop/release',()=>fixture(async f=>{
 const {json,status,window,ready,mutate,gesture,begin,finish,stateFile,preview}=f;
 const saved=fs.readFileSync(stateFile,'utf8'),generation=status()['stored-generation'],target=window(2).frame;
 await begin(1,'move');await mutate(s=>{s.windows[0].frame.x+=100;s.windows[0].frame.y+=100;});gesture(1,'move','step',target.x+target.w/2,target.y+target.h/2);
 await until(()=>preview().visible);assert.equal(preview().color,'0xffd75f5f');assert.equal(preview().operation,'swap');assert.equal(preview().frame.w,1500,'preview accounts for the incoming client minimum');assert.notEqual(preview().frame.w,target.w);assert.equal(status()['stored-generation'],generation);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);assert.deepEqual(window(2).frame,target);
 const config=f.call('-m','config','insert_window_border_color','0x80AABBCC');assert.equal(config.status,0,config.stderr);assert.equal(config.stdout,'');await until(()=>preview().color==='0x80aabbcc');assert.equal(status()['stored-generation'],generation);assert.equal(json('context','show','DP-1').history.undo,0);assert.equal(f.call('-m','config','window_border_radius','16').status,0);await until(()=>preview().radius===16);const configured=fs.readFileSync(stateFile,'utf8');
 // Insertion removes the old source slot first: the preview is the actual
 // resulting tile, not simply half the old target rectangle.
 gesture(1,'move','step',target.x+target.w/2,target.y+10);await until(()=>preview().visible&&preview().direction==='north');const insertion=preview();assert.equal(insertion.operation,'insert');assert.equal(insertion.frame.w,2540);assert.equal(insertion.color,'0x80aabbcc');assert.equal(fs.readFileSync(stateFile,'utf8'),configured);
 gesture(1,'move','step',-100,-100);await until(()=>!preview().visible);gesture(1,'move','step',target.x+target.w/2,target.y+10);await until(()=>preview().visible);
 await finish(1,'move',target.x+target.w/2,target.y+10);await until(()=>!preview().visible);assert.deepEqual(window(1).frame,insertion.frame);assert.equal(status().management.enabled,true);
 json('context','undo','DP-1');await until(ready);
 const origin=window(1).frame,cancelTarget=window(2).frame,undoBefore=json('context','show','DP-1').history.undo;
 await begin(1,'move');await mutate(s=>s.windows[0].frame.x+=100);gesture(1,'move','step',cancelTarget.x+cancelTarget.w/2,cancelTarget.y+cancelTarget.h/2);await until(()=>preview().visible);
 await mutate(s=>s.windows[0].frame={...origin});await finish(1,'move',cancelTarget.x+cancelTarget.w/2,cancelTarget.y+cancelTarget.h/2);await until(()=>!preview().visible);assert.deepEqual(window(1).frame,origin);assert.equal(json('context','show','DP-1').history.undo,undoBefore);
 const other=window(2).frame;await begin(1,'move');await mutate(s=>s.windows[0].frame.x+=100);gesture(1,'move','step',other.x+other.w/2,other.y+other.h/2);await until(()=>preview().visible);json('context','manage','DP-1','off');await until(()=>!preview().visible);assert.equal(status().management.enabled,false);
},true));

async function secondOutput(f,withWindow=true,manage=true){
 await f.mutate(s=>{s.outputs.push({...s.outputs[0],id:'screen-b',name:'DP-2',geometry:{x:-1080,y:0,w:1080,h:1920},'work-area':{x:-1080,y:0,w:1080,h:1920}});if(withWindow)s.windows.push({...s.windows[0],uuid:'55555555-5555-4555-8555-555555555555',output:'DP-2',frame:{x:-1000,y:100,w:800,h:600},focused:false});});
 f.json('context','reconcile','DP-2','--apply');if(manage)f.json('context','manage','DP-2','on','--lifecycle');
 if(withWindow&&manage)await until(()=>{const s=f.json('context','status','DP-2');return s.management.state==='active'&&s.active?.state==='applied';});
}
test('output hover/cancel preserves both trees; release inserts beside the destination and closes the source slot',t=>fixture(async f=>{
 await secondOutput(f);const original=f.window(1).frame,saved=fs.readFileSync(f.stateFile,'utf8');
 await f.begin(1,'move');await f.mutate(s=>{s.windows[0].output='DP-2';s.windows[0].frame.x=-950;});await until(()=>f.status().management.state==='interacting-off-output');assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);assert.equal(f.json('context','show','DP-2').members.length,1);
 // Returning and cancelling consumes only the original same-context gesture.
 await f.mutate(s=>{s.windows[0].output='DP-1';s.windows[0].frame={...original};});await f.finish(1,'move',500,500);assert.equal(f.json('context','show','DP-1').history.undo,0);
 const target=f.window(3).frame;await f.begin(1,'move');await f.mutate(s=>{s.windows[0].output='DP-2';s.windows[0].frame.x=-950;});await f.finish(1,'move',target.x+target.w/2,target.y+10);
 await until(()=>{const s=f.json('context','status','DP-2');assert.notEqual(s.management.state,'stopped',s.management.error);return s.management.state==='active'&&s.active['context-generation']===s['stored-generation'];});
 assert.equal(f.json('context','show','DP-1').members.length,1);assert.equal(f.json('context','show','DP-2').members.length,2);assert.equal(f.window(1).output,'DP-2');assert.ok(f.window(1).frame.y+f.window(1).frame.h<=f.window(3).frame.y);assert.equal(f.window(2).frame.w,2540);assert.equal(f.json('doctor')['mouse-output-drop'],false);const timing=f.json('doctor')['mouse-output-drop-status'];t.diagnostic('output insert timing: '+JSON.stringify({elapsed:timing['elapsed-ms'],phases:timing['phase-ms']}));
 // Destination restore uses the user's dropped position as the incoming
 // window's baseline; it does not return it to the old output.
 const kept=f.window(1).frame;f.json('context','reset','DP-2');assert.deepEqual(f.window(1).frame,kept);assert.equal(f.window(1).output,'DP-2');
},false,true));
test('drop into an empty enabled output supports a source becoming empty',()=>fixture(async f=>{
 await secondOutput(f,false);for(const id of [1,2]){
  await f.begin(id,'move');await f.mutate(s=>{s.windows[id-1].output='DP-2';s.windows[id-1].frame.x=-950;});const occupant=f.json('bridge','windows').windows.find(w=>w.id!==String(id)&&w.output==='DP-2');await f.finish(id,'move',occupant?occupant.frame.x+occupant.frame.w/2:-500,occupant?occupant.frame.y+10:500);
  await until(()=>f.json('context','status','DP-2').management.state==='active');
 }
 assert.equal(f.json('context','show','DP-1').members.length,0);assert.equal(f.json('context','show','DP-2').members.length,2);assert.equal(f.status().management.enabled,true);
},false,true));
test('unmanaged destination rejects an output drop without changing saved layouts or native endpoints',()=>fixture(async f=>{
 await secondOutput(f,true,false);const saved=fs.readFileSync(f.stateFile,'utf8');await f.begin(1,'move');await f.mutate(s=>{s.windows[0].output='DP-2';s.windows[0].frame.x=-950;});await f.mutate(s=>s.windows[0].candidate=true);f.gesture(1,'move','finish',-500,500);await until(()=>f.status().management.state==='stopped');assert.match(f.status().management.error,/enable the destination context/);assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);assert.equal(f.window(1).frame.x,-950);
},false,true));
test('infeasible output destination leaves both layouts unchanged',()=>fixture(async f=>{
 await secondOutput(f);const saved=fs.readFileSync(f.stateFile,'utf8');await f.begin(1,'move');await f.mutate(s=>{s.windows[0].output='DP-2';s.windows[0].frame.x=-950;s.windows[0].minimum.w=1200;});await f.mutate(s=>s.windows[0].candidate=true);f.gesture(1,'move','finish',-500,500);await until(()=>f.status().management.state==='stopped');assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);assert.equal(f.window(3).frame.w,1060);
},false,true));
test('native source-release rejection stops both controllers without admitting or moving destination peers',()=>fixture(async f=>{
 await secondOutput(f);const peer=f.window(3).frame;await f.begin(1,'move');await f.mutate(s=>{s.windows[0].output='DP-2';s.windows[0].frame.x=-950;s['test-geometry-reject']=true;s.windows[0].candidate=true;});f.gesture(1,'move','finish',-500,20);
 await until(()=>f.status().management.state==='stopped');assert.equal(f.json('context','status','DP-2').management.state,'stopped');assert.equal(f.json('context','show','DP-2').members.length,1);assert.deepEqual(f.window(3).frame,peer);assert.equal(f.window(1).frame.x,-950);assert.equal(f.json('doctor')['mouse-output-drop'],false);
},false,true));

test('release pointer transfers a wide window whose center is still on the source display',()=>fixture(async f=>{
 await secondOutput(f);await f.begin(1,'move');await f.mutate(s=>{s.windows[0].frame.x=-120;});f.gesture(1,'move','step',-50,500);await until(()=>f.status().management.state==='interacting-off-output');assert.equal(f.window(1).output,'DP-1');
 await f.finish(1,'move',-50,500);await until(()=>f.json('context','status','DP-2').management.state==='active');assert.equal(f.window(1).output,'DP-2');assert.equal(f.json('context','show','DP-2').members.length,2);assert.equal(f.json('context','show','DP-1').members.length,1);
},false,true));
test('reset all rebuilds visible contexts at current window locations without moving them',()=>fixture(async f=>{
 await secondOutput(f);f.json('context','gap','DP-1','22');await until(f.ready);await f.begin(1,'move');await f.mutate(s=>{s.windows[0].output='DP-2';s.windows[0].frame.x=-950;s.windows[0].candidate=true;});f.gesture(1,'move','finish',2500,500);await until(()=>f.status().management.state==='stopped');
 const before=f.json('bridge','windows').windows.map(w=>({uuid:w.uuid,frame:w.frame,output:w.output}));const result=f.json('context','reset','all');assert.equal(result.state,'reset');assert.equal(result['windows-moved'],false);assert.deepEqual(f.json('bridge','windows').windows.map(w=>({uuid:w.uuid,frame:w.frame,output:w.output})),before);assert.equal(f.json('context','show','DP-1').gap,22);assert.equal(f.json('context','show','DP-1').members.length,1);assert.equal(f.json('context','show','DP-2').members.length,2);assert.equal(f.status().management.enabled,false);
 f.json('context','manage','DP-1','on','--lifecycle');f.json('context','manage','DP-2','on','--lifecycle');await until(f.ready);await until(()=>f.json('context','status','DP-2').management.state==='active');
},false,true));

test('cross-output hover previews the incoming tile while center assignment is still on the source',()=>fixture(async f=>{
 await secondOutput(f);const saved=fs.readFileSync(f.stateFile,'utf8');await f.begin(1,'move');await f.mutate(s=>{s.windows[0].frame.x=-120;});const target=f.window(3).frame;f.gesture(1,'move','step',target.x+target.w/2,target.y+10);await until(()=>f.preview().visible);const preview=f.preview();assert.equal(preview['preview-output'].name,'DP-2');assert.equal(preview.operation,'insert');assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);assert.deepEqual(f.window(3).frame,target);
 await f.finish(1,'move',target.x+target.w/2,target.y+10);await until(()=>f.json('context','status','DP-2').management.state==='active');assert.deepEqual(f.window(1).frame,preview.frame);await until(()=>!f.preview().visible);
},true,true));

test('projected output admission uses UUIDs when activation reorders public window numbers',()=>fixture(async f=>{
 await secondOutput(f);const uuid=f.window(1).uuid,target=f.window(3).frame;await f.begin(1,'move');await f.mutate(s=>{s.windows=s.windows.slice(1).concat(s.windows[0]);const moving=s.windows.find(w=>w.uuid===uuid);moving.output='DP-2';moving.frame.x=-950;moving.app='com.microsoft.VSCode';});f.gesture(1,'move','step',target.x+target.w/2,target.y+10);await until(()=>f.preview().visible);assert.equal(f.preview().window,uuid);const plan=f.preview().frame;
 await f.finish(1,'move',target.x+target.w/2,target.y+10);await until(()=>f.json('context','status','DP-2').management.state==='active');assert.equal(f.window(1).uuid,uuid);assert.deepEqual(f.window(1).frame,plan);assert.equal(f.json('context','show','DP-2').members.length,2);
},true,true));
test('cross-output center swaps exchange the existing slots and preserve unrelated windows',t=>fixture(async f=>{
 await secondOutput(f);await f.mutate(s=>s.windows.push({...s.windows[2],uuid:'66666666-6666-4666-8666-666666666666',frame:{x:-1000,y:1000,w:800,h:600}}));await until(()=>{const st=f.json('context','status','DP-2');return f.json('context','show','DP-2').members.length===2&&st.management.state==='active'&&st.active['context-generation']===st['stored-generation'];});const otherTarget=f.window(4).frame;const a=f.window(1),b=f.window(3),other=f.window(2),source=f.json('context','show','DP-1'),destination=f.json('context','show','DP-2');const before=fs.readFileSync(f.stateFile,'utf8');
 await f.begin(1,'move');await f.mutate(s=>{const w=s.windows.find(w=>w.uuid===a.uuid);w.output='DP-2';w.frame.x=-950;s.windows=s.windows.slice(1).concat(s.windows[0]);});f.gesture(1,'move','step',b.frame.x+b.frame.w/2,b.frame.y+b.frame.h/2);await until(()=>f.preview().visible&&f.preview().operation==='swap');assert.deepEqual(f.preview().frame,b.frame);assert.equal(fs.readFileSync(f.stateFile,'utf8'),before);
 await f.finish(1,'move',b.frame.x+b.frame.w/2,b.frame.y+b.frame.h/2);await until(()=>f.json('context','status','DP-2').management.state==='active');assert.deepEqual(f.window(1).frame,b.frame);assert.deepEqual(f.window(3).frame,a.frame);assert.deepEqual(f.window(2).frame,other.frame);assert.deepEqual(f.window(4).frame,otherTarget);const shape=t=>t.nodes.map(({windows,active,...node})=>node);assert.deepEqual(shape(f.json('context','show','DP-1').tree),shape(source.tree));assert.deepEqual(shape(f.json('context','show','DP-2').tree),shape(destination.tree));assert.equal(f.window(1).output,'DP-2');assert.equal(f.window(3).output,'DP-1');assert.equal(f.json('context','show','DP-1').members.length,source.members.length);assert.equal(f.json('context','show','DP-2').members.length,destination.members.length);assert.equal(f.json('doctor')['mouse-output-drop-status'].mode,'swap');assert.equal(f.json('doctor')['mouse-output-drop-status'].success,true);const timing=f.json('doctor')['mouse-output-drop-status'];assert.ok(timing['elapsed-ms']>=0);t.diagnostic('output swap timing: '+JSON.stringify({elapsed:timing['elapsed-ms'],phases:timing['phase-ms']}));for(const phase of ['release-source','release-target','move-peer','apply-source','apply-target'])assert.ok(timing['phase-ms'][phase]>=0,phase+' timing is retained');
 // The incoming peer can immediately be dragged back, with stable UUIDs/IDs.
 f.gesture(3,'move','start');await f.mutate(s=>s.windows.find(w=>w.uuid===b.uuid).candidate=false);await until(()=>f.status().management.state==='interacting');await f.mutate(s=>{const w=s.windows.find(w=>w.uuid===b.uuid);w.output='DP-2';w.frame.x=-950;w.candidate=true;});f.gesture(3,'move','finish',f.window(1).frame.x+f.window(1).frame.w/2,f.window(1).frame.y+f.window(1).frame.h/2);await until(f.ready);await until(()=>f.json('context','status','DP-2').management.state==='active');assert.deepEqual(f.window(1).frame,a.frame);assert.deepEqual(f.window(3).frame,b.frame);
},true,true));

test('minimizing pauses one output, preserves ownership, and reconciles queued lifecycle changes on restore',()=>fixture(async f=>{
 await secondOutput(f);const a=f.window(1),peer=f.window(2),saved=fs.readFileSync(f.stateFile,'utf8'),job=f.status().active.id;
 await f.mutate(s=>{s.windows[0].minimized=true;s.windows[0].candidate=false;});await until(()=>f.status().management.state==='suspended-window');assert.equal(f.status().management.enabled,true);assert.equal(f.status().management['paused-windows'][0].reason,'minimized');assert.equal(f.status().active.id,job);assert.deepEqual(f.window(2).frame,peer.frame);assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);assert.equal(f.call('context','gap','DP-1','22').status,2);assert.equal(f.call('context','restore','DP-1').status,2);assert.equal(f.call('context','float','DP-1','1','on').status,2);assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);assert.equal(f.status().management.enabled,true);
 // Another output can admit windows while this one is paused.
 await f.mutate(s=>s.windows.push({...s.windows[2],uuid:'66666666-6666-4666-8666-666666666666',frame:{x:-900,y:1000,w:800,h:600}}));await until(()=>{const st=f.json('context','status','DP-2');return f.json('context','show','DP-2').members.length===2&&st.management.state==='active'&&st.active['context-generation']===st['stored-generation'];});assert.equal(f.status().management.state,'suspended-window');
 await f.mutate(s=>{s.windows=s.windows.filter(w=>w.uuid!==peer.uuid);s.windows.push({...s.windows[0],uuid:'77777777-7777-4777-8777-777777777777',minimized:false,candidate:true,frame:{x:500,y:500,w:800,h:600}});});assert.equal(f.json('context','show','DP-1').members.length,2);
 await f.mutate(s=>{const w=s.windows.find(w=>w.uuid===a.uuid);w.minimized=false;w.candidate=true;});await until(f.ready);assert.equal(f.json('context','show','DP-1').members.length,2);assert.equal(f.status().management['paused-windows'].length,0);assert.equal(f.json('context','show','DP-1').history.undo,0);
 const kept=f.json('bridge','windows').windows;f.json('context','reset','DP-1');assert.deepEqual(f.json('bridge','windows').windows,kept);
},false,true));

test('fullscreen restores asynchronously without losing the tree or baseline; multiple hidden members wait together',()=>fixture(async f=>{
 const a=f.window(1),b=f.window(2),saved=fs.readFileSync(f.stateFile,'utf8'),job=f.status().active.id;
 await f.mutate(s=>{s.windows[0].fullscreen=true;s.windows[0].candidate=false;s.windows[0].frame={x:0,y:0,w:2560,h:1440};});await until(()=>f.status().management.state==='suspended-window');assert.equal(f.status().management['paused-windows'][0].reason,'fullscreen');assert.deepEqual(f.window(2).frame,b.frame);assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);
 await f.mutate(s=>{s.windows[0].fullscreen=false;});await until(()=>f.status().management.state==='resuming-window');assert.equal(f.window(1).frame.w,2560,'do not overwrite the pending client restore');await f.mutate(s=>s.windows[0].frame={...a.frame});assert.equal(f.status().management.state,'resuming-window');await f.mutate(s=>s.windows[0].candidate=true);await until(f.ready);assert.equal(f.status().active.id,job);assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);
 await f.mutate(s=>{for(const w of s.windows){w.minimized=true;w.candidate=false;}});await until(()=>f.status().management['paused-windows'].length===2);
 await f.mutate(s=>{s.windows[0].minimized=false;s.windows[0].candidate=true;});await until(()=>f.status().management['paused-windows'].length===1);assert.equal(f.status().management.state,'suspended-window');await f.mutate(s=>{s.windows[1].minimized=false;s.windows[1].candidate=true;});await until(f.ready);
 f.json('context','gap','DP-1','22');await until(f.ready);assert.equal(f.status().management.enabled,true);assert.equal(f.json('context','show','DP-1').gap,22);
}));

test('an unconfirmed fullscreen return stops without overwriting the desktop',()=>fixture(async f=>{
 const saved=fs.readFileSync(f.stateFile,'utf8');await f.mutate(s=>{s.windows[0].fullscreen=true;s.windows[0].candidate=false;s.windows[0].frame={x:0,y:0,w:2560,h:1440};});await until(()=>f.status().management.state==='suspended-window');
 await f.mutate(s=>{s.windows[0].fullscreen=false;s.windows[0].candidate=true;});await until(()=>f.status().management.state==='stopped');assert.match(f.status().management.error,/moved/);assert.equal(f.window(1).frame.w,2560);assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);
}));

test('closing a minimized member resumes lifecycle tiling and preserves surviving baselines',()=>fixture(async f=>{
 const a=f.window(1);await f.mutate(s=>{s.windows[0].minimized=true;s.windows[0].candidate=false;});await until(()=>f.status().management.state==='suspended-window');
 await f.mutate(s=>s.windows=s.windows.filter(w=>w.uuid!==a.uuid));await until(f.ready);assert.equal(f.json('context','show','DP-1').members.length,1);assert.equal(f.window(2).frame.w,2540);const kept=f.window(2).frame;f.json('context','reset','DP-1');assert.deepEqual(f.window(2).frame,kept);
}));

test('peer movement during a minimized pause stops without changing saved intent or overwriting movement',()=>fixture(async f=>{
 const saved=fs.readFileSync(f.stateFile,'utf8'),b=f.window(2);await f.mutate(s=>{s.windows[0].minimized=true;s.windows[0].candidate=false;});await until(()=>f.status().management.state==='suspended-window');await f.mutate(s=>s.windows[1].frame.x+=30);await until(()=>f.status().management.state==='stopped');assert.match(f.status().management.error,/moved/);assert.equal(f.window(2).frame.x,b.frame.x+30);assert.equal(fs.readFileSync(f.stateFile,'utf8'),saved);
}));

test('yabai lifecycle removes minimized members, reflows peers and readmits without reserving the old slot',()=>fixture(async f=>{
 f.json('context','ratio','DP-1','1','x','0.7');await until(f.ready);const a=f.window(1),old=f.json('context','show','DP-1').members.find(w=>w.uuid===a.uuid)['layout-window-id'];
 await f.mutate(s=>{s.windows[0].minimized=true;s.windows[0].candidate=false;});await until(()=>f.json('context','show','DP-1').members.length===1&&f.ready());assert.equal(f.window(2).frame.w,2540);assert.deepEqual(f.window(1).frame,a.frame);assert.equal(f.json('-m','query','--windows','id,is-floating').find(w=>w.id===1)['is-floating'],true);
 await f.mutate(s=>s.windows.push({...s.windows[1],uuid:'77777777-7777-4777-8777-777777777777',focused:false,frame:{x:300,y:300,w:800,h:600}}));await until(()=>f.json('context','show','DP-1').members.length===2&&f.ready());assert.equal(f.status().management.state,'active');
 await f.mutate(s=>{s.windows[0].minimized=false;s.windows[0].candidate=true;});await until(()=>f.json('context','show','DP-1').members.length===3&&f.ready());assert.notEqual(f.json('context','show','DP-1').members.find(w=>w.uuid===a.uuid)['layout-window-id'],old);assert.notDeepEqual(f.window(1).frame,a.frame);assert.equal(f.json('context','show','DP-1').history.undo,0);
 const kept=f.json('bridge','windows').windows;f.json('context','reset','DP-1');assert.deepEqual(f.json('bridge','windows').windows,kept);
},false,false,true));

test('all minimized members release the tree; arrivals and partial restoration continue tiling',()=>fixture(async f=>{
 await f.mutate(s=>{for(const w of s.windows){w.minimized=true;w.candidate=false;}});await until(()=>f.json('context','show','DP-1').members.length===0&&f.ready());assert.equal(f.status().active['managed-windows'].length,0);
 await f.mutate(s=>s.windows.push({...s.windows[0],uuid:'77777777-7777-4777-8777-777777777777',minimized:false,candidate:true,focused:true,frame:{x:300,y:300,w:800,h:600}}));await until(()=>f.json('context','show','DP-1').members.length===1&&f.ready());assert.equal(f.window(3).frame.w,2540);
 await f.mutate(s=>{s.windows[0].minimized=false;s.windows[0].candidate=true;});await until(()=>f.json('context','show','DP-1').members.length===2&&f.ready());const b=f.window(2).uuid;await f.mutate(s=>s.windows=s.windows.filter(w=>w.uuid!==b));await until(f.ready);assert.equal(f.status().management.enabled,true);assert.equal(f.json('context','show','DP-1').members.length,2);
},false,false,true));

test('native fullscreen untiles and reflows; return waits for eligibility then inserts normally',()=>fixture(async f=>{
 const a=f.window(1);await f.mutate(s=>{s.windows[0].fullscreen=true;s.windows[0].candidate=false;s.windows[0].frame={x:0,y:0,w:2560,h:1440};});await until(()=>f.json('context','show','DP-1').members.length===1&&f.ready());assert.equal(f.window(2).frame.w,2540);assert.equal(f.window(1).frame.w,2560);
 await f.mutate(s=>{s.windows[0].fullscreen=false;s.windows[0].frame={x:300,y:300,w:800,h:600};});assert.equal(f.status().management.state,'active');assert.equal(f.json('context','show','DP-1').members.length,1);
 await f.mutate(s=>s.windows[0].candidate=true);await until(()=>f.json('context','show','DP-1').members.length===2&&f.ready());assert.equal(f.status().management['paused-windows'].length,0);assert.equal(f.window(1).uuid,a.uuid);
 const kept=f.json('bridge','windows').windows;f.json('context','reset','DP-1');assert.deepEqual(f.json('bridge','windows').windows,kept);
},false,false,true));

test('returning windows pass through current admission rules after leaving the tree',()=>fixture(async f=>{
 await f.mutate(s=>{s.windows[0].minimized=true;s.windows[0].candidate=false;s.windows[0].title='Hidden test';});await until(()=>f.json('context','show','DP-1').members.length===1&&f.ready());f.json('rule','add','hidden-test','float','--title','Hidden test');
 await f.mutate(s=>{s.windows[0].minimized=false;s.windows[0].candidate=true;});assert.equal(f.json('context','show','DP-1').members.length,1);assert.equal(f.json('context','reconcile','DP-1').excluded.find(w=>w.uuid===f.window(1).uuid).reason,'rule-float');assert.equal(f.status().management.state,'active');
 f.json('rule','remove','hidden-test');await until(()=>f.json('context','show','DP-1').members.length===2&&f.ready());
},false,false,true));

for(const sameSize of [false,true])test(`a newly admitted Qt Quick client that centers on its first configure settles without stopping tiling (same size: ${sameSize})`,()=>fixture(async f=>{
 const {json,status,window,ready,mutate}=f;const uuid='99999999-9999-4999-8999-999999999999';
 await mutate(s=>{s['test-center-first-resize']=[uuid];s.windows.push({...s.windows[0],uuid,app:'org.kde.plasma-systemmonitor',focused:false,frame:{x:900,y:450,w:sameSize?1265:900,h:sameSize?705:700},minimum:{w:612,h:486}});});
 await until(()=>ready()&&json('context','show','DP-1').members.length===3);
 assert.equal(status().active['position-corrections'],1);assert.equal(status().active['position-candidates'],1);assert.ok(status().active['position-size-matches']>=3);const monitor=json('bridge','windows').windows.find(w=>w.uuid===uuid);const target=status().active['target-placements'].find(w=>w.uuid===uuid).frame;assert.deepEqual(monitor.frame,target);if(sameSize){assert.equal(target.w,1265);assert.equal(target.h,705);}assert.equal(status().management.enabled,true);
 await mutate(s=>s.windows=s.windows.filter(w=>w.uuid!==uuid));await until(()=>ready()&&json('context','show','DP-1').members.length===2);assert.equal(status().management.enabled,true);
}));

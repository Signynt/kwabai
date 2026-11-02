const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,25));}throw Error('condition timed out');}
test('zoom-fullscreen preserves BSP, peers, padding and independent zoom states through native confirmation',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-zoom-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','geometry-hidden-release','geometry-zoom','scratchpads','layers','notifications','mouse','geometry-forget'],'geometry-window-limit':20,
 outputs:[{id:'screen-a',name:'DP-1',scale:1,geometry:{x:-100,y:50,w:1200,h:800},'work-area':{x:-100,y:80,w:1200,h:770},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[1,2].map(n=>({uuid:`aaaaaaaa-aaaa-4aaa-8aaa-${String(n).padStart(12,'0')}`,app:'kitty',title:'test',output:'DP-1',desktops:['22222222-2222-4222-8222-222222222222'],activities:['work'],frame:{x:n*100,y:100,w:400,h:300},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:n===1}))};
 for(const [n,app] of [[8,'scratch-test'],[9,'systemsettings']])s.windows.push({...s.windows[1],uuid:`aaaaaaaa-aaaa-4aaa-8aaa-${String(n).padStart(12,'0')}`,app,focused:false});
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:8000});
 const json=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const window=id=>json('bridge','windows').windows.find(w=>w.id===String(id)),show=()=>json('context','show','DP-1');
 const ready=()=>{const r=json('context','status','DP-1');assert.notEqual(r.management.state,'stopped',r.management.error);return r.management.state==='active'&&r.active?.state==='applied'&&r.active['context-generation']===r['stored-generation'];};
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);const seq=s.sequence;write();await until(()=>BigInt(json('doctor').bridge.sequence)>=BigInt(seq));};
 const ok=(...args)=>{const r=call('-m',...args);assert.equal(r.status,0,r.stderr);assert.equal(r.stdout,'');};
 const toggle=id=>ok('window',...(id?[String(id)]:[]),'--toggle','zoom-fullscreen');
 const query=id=>json('-m','query','--windows','id,has-fullscreen-zoom,is-native-fullscreen','--window',String(id));
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);
  ok('rule','--add','app=^(scratch-test|systemsettings)$','manage=off');ok('window','3','--scratchpad','test');
  json('context','reconcile','DP-1','--apply');json('context','padding','DP-1','5','10','15','20');json('context','manage','DP-1','on','--lifecycle');await until(ready);
  await until(()=>window(1).layer==='below'&&window(2).layer==='below');
  const tree=show().tree,a=window(1).frame,b=window(2).frame,history=show().history;
  toggle();await until(ready);assert.deepEqual(window(1).frame,{x:-80,y:85,w:1170,h:750});assert.deepEqual(window(2).frame,b);assert.equal(json('context','neighbor','DP-1','1','east').id,'2');assert.deepEqual(show().tree,tree);assert.deepEqual(show().history,history);assert.deepEqual(query(1),{id:1,'has-fullscreen-zoom':true,'is-native-fullscreen':false});
  await until(()=>window(1).layer==='above'&&window(3).layer==='above');assert.equal(window(2).layer,'below');assert.equal(window(4).layer,'normal');assert.equal(window(1)['test-raise-count'],1);
  const aboveZoom=()=>{const order=JSON.parse(fs.readFileSync(file))['test-stacking-order'];return order.indexOf(window(3).uuid)>order.indexOf(window(1).uuid);};await until(aboveZoom);
  const restacks=JSON.parse(fs.readFileSync(file))['test-restacks'];await new Promise(r=>setTimeout(r,100));assert.equal(JSON.parse(fs.readFileSync(file))['test-restacks'],restacks,'correct order is not rewritten each tick');
  toggle();await until(ready);await until(()=>window(1).layer==='below');assert.deepEqual(window(1).frame,a);assert.deepEqual(window(2).frame,b);assert.equal(query(1)['has-fullscreen-zoom'],false);
  // Numeric zoom of an unfocused peer must rise above the tiles without a
  // click or activation. Unzoom restores ordinary below-layer tiling.
  const beforeRaises=window(2)['test-raise-count']||0;toggle(2);await until(()=>ready()&&window(2)['test-raise-count']===beforeRaises+1);await until(()=>window(2).layer==='above');assert.equal(window(1).layer,'below');assert.equal(window(4).layer,'normal');assert.equal(window(1).focused,true);assert.equal(window(2).focused,false);
  toggle(2);await until(()=>ready()&&window(2).layer==='below');assert.equal(window(2)['test-raise-count'],beforeRaises+1);
  toggle(1);toggle(2);await until(ready);assert.equal(query(1)['has-fullscreen-zoom'],true);assert.equal(query(2)['has-fullscreen-zoom'],true);toggle(1);assert.deepEqual(window(1).frame,a);assert.equal(query(2)['has-fullscreen-zoom'],true);toggle(2);
  toggle(1);const raisesBeforeReflow=window(1)['test-raise-count'];ok('config','window_gap','24');await until(ready);assert.equal(query(1)['has-fullscreen-zoom'],true);assert.equal(window(1)['test-raise-count'],raisesBeforeReflow);assert.deepEqual(window(1).frame,{x:-80,y:85,w:1170,h:750});toggle(1);
  // A new window reflows the tree behind the zoomed member; closing it also preserves zoom.
  toggle(1);await mutate(s=>s.windows.push({...s.windows[1],uuid:'aaaaaaaa-aaaa-4aaa-8aaa-000000000003',focused:false,frame:{x:200,y:200,w:300,h:200}}));await until(()=>ready()&&show().members.length===3);assert.equal(query(1)['has-fullscreen-zoom'],true);
  await mutate(s=>s.windows=s.windows.filter(w=>!w.uuid.endsWith('000000000003')));await until(()=>ready()&&show().members.length===2);assert.equal(query(1)['has-fullscreen-zoom'],true);toggle(1);
  // Swap clears the affected leaf zoom, as upstream does.
  toggle(1);ok('window','--swap','2');await until(ready);assert.equal(query(1)['has-fullscreen-zoom'],false);assert.equal(window(1).frame.w<1170,true);
  toggle(1);ok('window','--toggle','float');await until(ready);assert.equal(query(1)['has-fullscreen-zoom'],false);assert.notEqual(call('-m','window','--toggle','zoom-fullscreen').status,0);ok('window','--toggle','float');await until(ready);
  // Native fullscreen/minimization releases zoom membership; returning windows arrive normally.
  toggle(1);await mutate(s=>s.windows[0].minimized=true);await until(()=>ready()&&show().members.length===1);assert.equal(query(1)['has-fullscreen-zoom'],false);
  const rootSaved=fs.readFileSync(stateFile,'utf8'),rootJob=json('context','status','DP-1').active.id;toggle(2);assert.equal(fs.readFileSync(stateFile,'utf8'),rootSaved);assert.equal(json('context','status','DP-1').active.id,rootJob);
  await mutate(s=>s.windows[0].minimized=false);await until(()=>ready()&&show().members.length===2);assert.equal(query(1)['has-fullscreen-zoom'],false);
  // Old effects fail before changing the saved intent.
  await mutate(s=>s.capabilities=s.capabilities.filter(c=>c!=='geometry-zoom'));const saved=fs.readFileSync(stateFile,'utf8');const unsupported=call('-m','window','--toggle','zoom-fullscreen');assert.notEqual(unsupported.status,0);assert.match(unsupported.stderr,/reload-effect/);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);await mutate(s=>s.capabilities.push('geometry-zoom'));
  toggle(1);const kept=window(1).frame;await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);assert.deepEqual(window(1).frame,kept,'restart does not restore positions');assert.equal(query(1)['has-fullscreen-zoom'],false,'zoom is session state');
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

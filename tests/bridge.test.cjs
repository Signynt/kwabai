const {test}=require('node:test');const assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
function win(uuid){return {uuid,app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:-1080,y:0,w:1080,h:1920},minimum:{w:100,h:100},minimized:false,fullscreen:false,focused:true,candidate:true};}
async function launch(binary,args){const p=spawn(binary,args);p.errors='';p.stderr.on('data',b=>p.errors+=b);await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{p.kill('SIGKILL');reject(Error('timeout '+p.errors));},5000);p.stdout.once('data',()=>{clearTimeout(timer);resolve();});p.once('exit',code=>{clearTimeout(timer);reject(Error('startup failed '+code+' '+p.errors));});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const limit=Date.now()+6000;while(Date.now()<limit){if(f())return;await new Promise(r=>setTimeout(r,40));}throw Error('condition did not become true');}
test('isolated D-Bus discovery, updates, validation, disconnect and effect restart',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-bridge-')),socket=path.join(dir,'control.sock'),file=path.join(dir,'snapshot.json');let d,p;
 const uuid='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa';
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','kwin-version':'6.8.1',capabilities:['snapshot'],windows:[win(uuid)]};
 const write=()=>{const temp=file+'.new';fs.writeFileSync(temp,JSON.stringify(s));fs.renameSync(temp,file);};
 try {
  d=await launch(daemon,['--socket',socket,'--observe-kwin']);assert.equal(json(socket,'doctor').mode,'observation');
  assert.equal(call(socket,'bridge','windows').status,3);
  write();p=await launch(provider,[file]);await until(()=>json(socket,'doctor')['bridge-connected']);
  const first=json(socket,'bridge','windows');assert.equal(first.windows[0].id,'1');assert.equal(first.windows[0].frame.x,-1080);
  assert.equal(json(socket,'doctor')['desktop-control'],false);assert.equal(call(socket,'-m','window','--swap','2').status,1);
  s.sequence='2';s.windows[0].title='updated';write();await until(()=>json(socket,'bridge','status').sequence==='2');assert.equal(json(socket,'bridge','windows').windows[0].title,'updated');
  s.sequence='1';write();await until(()=>json(socket,'bridge','status').error==='stale bridge sequence');assert.equal(call(socket,'bridge','windows').status,3);
  s.sequence='3';write();await until(()=>json(socket,'doctor')['bridge-connected']);
  await stop(p);await until(()=>!json(socket,'doctor')['bridge-connected']);assert.equal(json(socket,'bridge','status')['window-count'],0);
  s.instance='22222222-2222-4222-8222-222222222222';s.sequence='1';s.windows.push(win('bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb'));write();
  p=await launch(provider,[file]);await until(()=>json(socket,'doctor')['bridge-connected']);const restored=json(socket,'bridge','windows');assert.deepEqual(restored.windows.map(w=>w.id),['1','2']);
  s.sequence='2';s.windows=[s.windows[1]];write();await until(()=>json(socket,'bridge','status').sequence==='2');assert.equal(json(socket,'bridge','windows').windows[0].id,'2');
  // Old snapshot-only providers remain usable; context commands explicitly reject missing capability.
  assert.equal(call(socket,'bridge','outputs').status,3);
  const desktop='44444444-4444-4444-8444-444444444444';
  s.sequence='3';s.capabilities=['snapshot','contexts'];s.outputs=[{id:'stable-DP-1',name:'DP-1',scale:1.25,geometry:{x:-1080,y:0,w:1080,h:1920},'work-area':{x:-1080,y:40,w:1080,h:1880},'current-desktop':desktop,'current-activity':'work'}];write();
  await until(()=>json(socket,'bridge','status')['output-count']===1);
  assert.equal(json(socket,'bridge','outputs').outputs[0]['work-area'].y,40);
  const plan=json(socket,'bridge','plan','DP-1','2');assert.equal(plan['dry-run'],true);assert.equal(plan.placements[0].frame.y,50);assert.equal(plan.placements[0].frame.x,-1070);
  assert.equal(call(socket,'bridge','plan','DP-1','2','2').status,2);
  assert.equal(json(socket,'doctor').generation,'0');
  s.sequence='4';s.capabilities.push('events');s['event-sequence']='0';s['test-events']=[];write();
  await until(()=>json(socket,'doctor')['event-stream'].ready);
  s.sequence='5';s['event-sequence']='1';s['test-events']=[{ordinal:'1',type:'window_drag_start',data:{uuid:s.windows[0].uuid}}];write();
  await until(()=>json(socket,'doctor')['event-stream'].cursor==='1');
  assert.equal(json(socket,'bridge','events').events[0].type,'window_drag_start');
  s.sequence='6';s['event-sequence']='400';s['test-events']=[];s['test-event-gap']=true;write();
  await until(()=>Number(json(socket,'doctor')['event-stream'].resyncs)>=1);
  s['test-event-gap']=false;write();await until(()=>json(socket,'doctor')['event-stream'].ready&&json(socket,'doctor')['event-stream'].cursor==='400');
  assert.deepEqual(json(socket,'bridge','events').events,[]);assert.equal(json(socket,'doctor').generation,'0');
  s={'test-stall':true};write();await until(()=>!json(socket,'doctor')['bridge-connected']);
  assert.ok(json(socket,'bridge','status').error.length>0);
  const started=Date.now();assert.equal(json(socket,'doctor')['desktop-control'],false);assert.ok(Date.now()-started<750,'pending bridge call blocked the command service');
  assert.equal(json(socket,'inspect').windows.length,0);assert.equal(json(socket,'doctor').generation,'0');
 }finally{await stop(p);await stop(d);fs.rmSync(dir,{recursive:true,force:true});}
});

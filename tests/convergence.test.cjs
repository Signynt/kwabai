const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+8000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,25));}throw Error('condition timed out');}
test('automatic tiling converges across app resizing, constraints, temporary ineligibility and closure during configure',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-robust-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock');let p,d;
 const desktop='22222222-2222-4222-8222-222222222222';
 const uuid=n=>`aaaaaaaa-aaaa-4aaa-8aaa-${String(n).padStart(12,'0')}`;
 const output=(id,name,x,w,h)=>({id,name,scale:1,geometry:{x,y:0,w,h},'work-area':{x,y:0,w,h},'current-desktop':desktop,'current-activity':'work'});
 const window=(n,out)=>({uuid:uuid(n),app:n===3?'Tor Browser':'kitty',title:'test',output:out,desktops:[desktop],activities:['work'],frame:{x:out==='DP-1'?100:-900,y:100,w:600,h:500},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:n===1});
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','mouse','notifications'],'geometry-window-limit':20,
 outputs:[output('screen-a','DP-1',0,2560,1440),output('screen-b','DP-2',-1080,1080,1920)],windows:[window(1,'DP-1'),window(2,'DP-2'),window(3,'DP-1'),window(4,'DP-1')]};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const json=(...args)=>{const r=spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const ok=(...args)=>{const r=spawnSync(cli,['--socket',socket,'-m',...args],{encoding:'utf8',timeout:5000});assert.equal(r.status,0,r.stderr);};
 const status=out=>json('context','status',out);
 const ready=out=>{const r=status(out);assert.notEqual(r.management.state,'stopped',r.management.error);return ['active','idle'].includes(r.management.state)&&(!r.active||r.active['context-generation']===r['stored-generation']);};
 const mutate=fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);write();};
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',path.join(dir,'contexts.json')]);await until(()=>json('doctor')['event-stream'].ready);
  ok('config','layout','bsp');await until(()=>ready('DP-1')&&ready('DP-2'));
  const show=out=>json('context','show',out),live=n=>json('bridge','windows').windows.find(w=>w.uuid===uuid(n));
  // A newly mapped window never acknowledges its requested frame, then closes
  // while the native apply is pending. No rollback should block other outputs.
  mutate(s=>{s.windows.push({...window(7,'DP-1'),'test-ignore-frame':true});s.windows.push(window(8,'DP-2'));});
  await until(()=>{const j=status('DP-1').latest;return j?.state==='pending'&&j['managed-windows'].includes(uuid(7));});
  mutate(s=>s.windows=s.windows.filter(w=>w.uuid!==uuid(7)));
  await until(()=>ready('DP-1')&&ready('DP-2')&&show('DP-1').members.length===3&&show('DP-2').members.length===2);
  assert.notEqual(status('DP-1').latest.state,'restore_failed');
  // Respect maximum hints and redistribute the BSP allocation, rather than
  // stopping or leaving the client permanently unmanaged.
  mutate(s=>{const w=s.windows.find(w=>w.uuid===uuid(3));w.maximum={w:900,h:2000};w.frame.w=900;});
  await until(()=>ready('DP-1')&&live(3).frame.w===900&&json('context','plan','DP-1').placements.find(w=>w.uuid===uuid(3))['tile-frame'].w===900);
  const tree=show('DP-1').tree;
  mutate(s=>s.windows.find(w=>w.uuid===uuid(3)).frame.w=820);
  await until(()=>ready('DP-1')&&live(3).frame.w===820&&JSON.stringify(show('DP-1').tree)!==JSON.stringify(tree));
  assert.equal(show('DP-1').members.length,3);assert.deepEqual(status('DP-1').management['isolated-windows'],[]);
  // Temporary mapping/resizeability changes remove only this live membership;
  // admission resumes normally when the client becomes eligible again.
  mutate(s=>s.windows.find(w=>w.uuid===uuid(3)).candidate=false);
  await until(()=>ready('DP-1')&&show('DP-1').members.length===2);assert.equal(ready('DP-2'),true);
  mutate(s=>s.windows.find(w=>w.uuid===uuid(3)).candidate=true);
  await until(()=>ready('DP-1')&&show('DP-1').members.length===3);
  // The busy response is a transient pre-write refusal, not a reason to stop.
  mutate(s=>s['test-reject-pending-once']=true);ok('config','window_gap','12');
  await until(()=>ready('DP-1')&&ready('DP-2')&&!json('doctor')['geometry-busy']);
  assert.equal(JSON.parse(fs.readFileSync(file))['test-reject-pending-once'],undefined);
  assert.equal(json('context','managers')['enabled-count'],2);
  // Closing all managed windows during repeated reflows leaves healthy, empty
  // contexts; subsequent arrivals start tiling without a daemon restart.
  for(const n of [1,2,3,4,8]){mutate(s=>s.windows=s.windows.filter(w=>w.uuid!==uuid(n)));await new Promise(r=>setTimeout(r,12));}
  await until(()=>ready('DP-1')&&ready('DP-2')&&show('DP-1').members.length===0&&show('DP-2').members.length===0);
  mutate(s=>{s.windows.push(window(9,'DP-1'),window(10,'DP-2'));});await until(()=>ready('DP-1')&&ready('DP-2')&&show('DP-1').members.length===1&&show('DP-2').members.length===1);
 }catch(e){console.error('FIXTURE',dir);try{console.error(JSON.stringify(json('doctor')));console.error(JSON.stringify(json('context','status','DP-1')));}catch{}throw e;}finally{await stop(d);await stop(p);if(!process.env.KWABAI_KEEP_TEST)fs.rmSync(dir,{recursive:true,force:true});}
});

const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+8000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,25));}throw Error('condition timed out');}
test('global BSP resumes after snapshot failures and isolates drifting clients without stopping peer displays',async()=>{
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
  // A single malformed startup snapshot must suspend writes, not permanently
  // disable every controller. Same-instance recovery keeps native ownership.
  const beforeFrames=JSON.parse(fs.readFileSync(file)).windows.map(w=>w.frame);
  mutate(s=>s.windows[2].minimum.w=-1);
  await until(()=>!json('doctor')['bridge-connected']);
  await until(()=>json('context','managers').contexts.every(c=>c.enabled&&c.state==='waiting-bridge'));
  await new Promise(r=>setTimeout(r,200));assert.deepEqual(JSON.parse(fs.readFileSync(file)).windows.map(w=>w.frame),beforeFrames);
  mutate(s=>s.windows[2].minimum.w=100);
  await until(()=>json('doctor')['bridge-connected']&&ready('DP-1')&&ready('DP-2'));
  assert.match(json('doctor')['event-stream']['last-error'],/invalid bridge size/);assert.ok(Number(json('doctor')['event-stream'].failures)>0);
  // Tor changes an already confirmed frame while a peer closes. A replacement
  // must be rejected before writes, release ownership, float only the drifting
  // client, and reflow peers. A simultaneous arrival on DP-2 still gets tiled.
  mutate(s=>{s.windows.find(w=>w.uuid===uuid(3)).frame.x+=45;s.windows=s.windows.filter(w=>w.uuid!==uuid(4));s.windows.push(window(5,'DP-2'));});
  const torFrame=JSON.parse(fs.readFileSync(file)).windows.find(w=>w.uuid===uuid(3)).frame;
  await until(()=>ready('DP-1')&&ready('DP-2')&&json('context','show','DP-1').members.length===2&&json('context','show','DP-2').members.length===2);
  await until(()=>!json('doctor')['geometry-busy']);
  const live=json('bridge','windows').windows;assert.equal(live.find(w=>w.uuid===uuid(3)).candidate,true,'self-resizing client stays eligible and managed');
  assert.ok(live.find(w=>w.uuid===uuid(1)).frame.w>0);
  assert.deepEqual(status('DP-1').management['isolated-windows'],[]);
  const tor=live.find(w=>w.uuid===uuid(3));assert.equal(json('-m','query','--windows','is-floating','--window',tor.id)['is-floating'],false);

  assert.equal(json('context','managers')['enabled-count'],2);assert.deepEqual(json('doctor')['layout-policy'].errors,{});
  // Isolation is per UUID, not an application-wide Tor exclusion.
  mutate(s=>s.windows.push({...window(6,'DP-1'),app:'Tor Browser'}));await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===3);
 }catch(e){console.error('FIXTURE',dir);try{console.error(JSON.stringify(json('doctor')));console.error(JSON.stringify(json('context','status','DP-1')));}catch{}throw e;}finally{await stop(d);await stop(p);if(!process.env.KWABAI_KEEP_TEST)fs.rmSync(dir,{recursive:true,force:true});}
});

const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('notified lifecycle updates bypass polling and missed hints recover',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-notifications-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock');let p,d;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};



 s.capabilities.push('geometry-retention','geometry-lifecycle','notifications');s['geometry-window-limit']=20;
 const write=()=>{s.sequence=String(BigInt(s.sequence)+1n);fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const status=()=>json(socket,'context','status','DP-1'),show=()=>json(socket,'context','show','DP-1');
 const ready=()=>status().management.state==='active'&&status().active?.['context-generation']===show().generation;
 const count=()=>BigInt(json(socket,'doctor')['event-stream'].notifications);
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control']);await until(()=>json(socket,'doctor')['event-stream'].ready);json(socket,'context','add','DP-1','1');
  json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);
  const second={...s.windows[0],focused:false,uuid:'44444444-4444-4444-8444-444444444444'},timings=[];
  for(let n=0;n<6;++n){
   const before=count(),started=Date.now();if(n%2)s.windows.pop();else s.windows.push(second);write();
   await until(()=>show().members.length===s.windows.length&&ready());const elapsed=Date.now()-started;timings.push(elapsed);assert.ok(count()>before,'change signals must be received');assert.ok(elapsed<450,`notified update took ${elapsed}ms; expected to bypass the 500ms poll`);
  }
  // An arrival while a previous geometry operation is pending must not be lost.
  s.windows.push(second);write();await until(()=>show().members.length===2);s.windows.push({...second,uuid:'55555555-5555-4555-8555-555555555555'});write();await until(()=>show().members.length===3&&ready());assert.equal(status().management.enabled,true);
  // Signals are hints: suppressing one must still recover through the safety poll.
  s['test-suppress-notifications']=true;write();await new Promise(r=>setTimeout(r,80));const before=count();s.windows.pop();write();await until(()=>show().members.length===2&&ready());assert.equal(count(),before);
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);assert.equal(status()['active-count'],0);
  console.log('notified admission/closure + confirmation durations (ms):',timings.join(', '));
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

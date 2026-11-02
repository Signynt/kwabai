const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('daemon retention remains bounded across more than two full windows of updates',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-retention-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock');let p,d;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};

 s.capabilities.push('geometry-retention');s['geometry-window-limit']=20;s['test-geometry-fast']=true;fs.writeFileSync(file,JSON.stringify(s));
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control']);await until(()=>json(socket,'doctor')['event-stream'].ready);json(socket,'context','add','DP-1','1');
  const status=()=>json(socket,'context','status','DP-1');let first,last;
  for(let n=0;n<85;n++) {
   await new Promise(resolve=>setImmediate(resolve));
   if(n)json(socket,'context','gap','DP-1',String(10+n%2));
   const receipt=json(socket,'context','apply','DP-1');if(!first)first=receipt.id;last=receipt.id;
   await until(()=>status().active?.state==='applied'&&status().active?.id===last);
   const history=json(socket,'doctor')['geometry-history'];assert.equal(history.enabled,true);assert.ok(history.retained<=32);assert.equal(history['request-sequence'],String(n+1));
  }
  assert.equal(call(socket,'bridge','transaction',first).status,2,'expired cache IDs must fail explicitly');assert.equal(status()['active-count'],1);assert.equal(status().active.id,last);
  json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);assert.equal(status()['active-count'],0);
  // A fresh apply still works after many compacted updates and restoration.
  json(socket,'context','apply','DP-1');await until(()=>status().active?.state==='applied');json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

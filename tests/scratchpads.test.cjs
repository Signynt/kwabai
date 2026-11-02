const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+8000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,25));}throw Error('condition timed out');}
test('Yabai scratchpads detach tiles, hide, summon, recover, survive daemon restart and obey rules',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-scratchpads-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock'),stateFile=path.join(dir,'contexts.json');let p,d;
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','layers','scratchpads','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications','mouse','geometry-forget'],'geometry-window-limit':20,
 outputs:[{id:'screen-a',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:0,w:2560,h:1440},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[1,2,3].map(n=>({uuid:`aaaaaaaa-aaaa-4aaa-8aaa-${String(n).padStart(12,'0')}`,app:n===3?'systemsettings':'kitty',title:'test',output:'DP-1',desktops:[],activities:[],layer:'normal',frame:{x:n*100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:n===1}))};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:8000});const json=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const windows=()=>json('bridge','windows').windows,w=id=>windows().find(w=>w.id===String(id));
 const ok=(...args)=>{const r=call('-m',...args);assert.equal(r.status,0,r.stderr);assert.equal(r.stdout,'');};
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);write();await until(()=>json('doctor').bridge.sequence>=s.sequence);};
 const policyReady=()=>{const q=json('doctor')['layer-policy'];assert.deepEqual(q.errors,{});return !q.pending;};
 const ready=()=>{const q=json('context','status','DP-1');assert.notEqual(q.management.state,'stopped',q.management.error);return q.management.state==='active'&&q.active?.['context-generation']===q['stored-generation'];};

 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);
  ok('config','layout','bsp');await until(ready);
  ok('window','1','--scratchpad','term');await until(()=>ready()&&json('context','show','DP-1').members.length===2);
  assert.equal(w(1).scratchpad,'term');assert.equal(w(1)['scratchpad-hidden'],false);const frame=w(1).frame;
  let result=call('-m','window','2','--scratchpad','term');assert.notEqual(result.status,0);assert.match(result.stderr,/already assigned/);assert.equal(w(2).scratchpad,'');
  ok('window','--toggle','term');assert.equal(w(1)['scratchpad-hidden'],true);assert.equal(ready(),true);assert.deepEqual(w(1).frame,frame);
  ok('window','--toggle','term');assert.equal(w(1)['scratchpad-hidden'],false);assert.equal(w(1).focused,true);await until(()=>w(1).layer==='above');
  assert.equal(json('-m','query','--windows','scratchpad,scratchpad-hidden','--window','1').scratchpad,'term');
  ok('window','1','--scratchpad','notes');assert.notEqual(call('-m','window','--toggle','term').status,0);ok('window','2','--scratchpad','term');await until(()=>ready()&&json('context','show','DP-1').members.length===1);
  // X11 minimization-backed scratchpads retain their panel task identity.
  await mutate(s=>{s.windows[1]['test-x11']=true;s.windows[1]['skip-taskbar']=false;});
  ok('window','--toggle','term');assert.equal(w(2)['scratchpad-hidden'],true);assert.equal(w(2).minimized,true);assert.equal(w(2)['skip-taskbar'],false);assert.equal(w(2).hidden,false);
  ok('window','--toggle','term');assert.equal(w(2)['scratchpad-hidden'],false);assert.equal(w(2).minimized,false);assert.equal(w(2)['skip-taskbar'],false);
  ok('window','--toggle','notes');ok('window','--toggle','term');ok('window','--scratchpad','recover');await until(()=>!w(1)['scratchpad-hidden']&&!w(2)['scratchpad-hidden']);
  // A visible scratchpad on another output is summoned, rather than hidden.
  await mutate(s=>{s.outputs.push({...s.outputs[0],id:'screen-b',name:'DP-2',geometry:{x:2560,y:0,w:1920,h:1080},'work-area':{x:2560,y:0,w:1920,h:1080}});s['test-active-output']='DP-2';});
  ok('window','--toggle','notes');assert.equal(w(1).output,'DP-2');assert.equal(w(1)['scratchpad-hidden'],false);
  await mutate(s=>s['test-active-output']='DP-1');ok('window','--toggle','notes');assert.equal(w(1).output,'DP-1');
  // A hidden scratchpad can be summoned after a KDE desktop change.
  ok('window','--toggle','notes');const second='33333333-3333-4333-8333-333333333333';await mutate(s=>{s.outputs[0]['current-desktop']=second;for(const w of s.windows)if(!w.hidden)w.desktops=[second];});
  ok('window','--toggle','notes');assert.deepEqual(w(1).desktops,[second]);assert.equal(w(1).focused,true);
  await until(ready);ok('window','--toggle','notes');await stop(d);assert.equal(JSON.parse(fs.readFileSync(file)).windows[0].hidden,true);
  d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json('doctor')['event-stream'].ready);
  assert.equal(w(1).scratchpad,'notes');ok('window','--toggle','notes');assert.equal(w(1)['scratchpad-hidden'],false);
  // Clearing an assignment shows the window and permits automatic admission.
  ok('config','layout','bsp');await until(ready);ok('window','1','--scratchpad');await until(()=>ready()&&json('context','show','DP-1').members.length===2);assert.equal(w(1).scratchpad,'');
  await mutate(s=>s['test-scratchpad-refuse']=true);result=call('-m','window','--toggle','term');assert.notEqual(result.status,0);assert.match(result.stderr,/KDE rejected/);assert.equal(ready(),true);await mutate(s=>delete s['test-scratchpad-refuse']);
  ok('rule','--add','label=settings-pad','app=^systemsettings$','scratchpad=settings');await until(()=>w(3).scratchpad==='settings'&&ready()&&json('context','show','DP-1').members.length===1);
  assert.equal(json('-m','rule','--list').find(r=>r.label==='settings-pad').scratchpad,'settings');assert.equal(json('doctor')['scratchpad-policy'].errors.constructor,Object);
  ok('window','3','--scratchpad');await until(()=>ready()&&w(3).scratchpad==='');await new Promise(r=>setTimeout(r,250));assert.equal(w(3).scratchpad,'','manual removal overrides unchanged rule');
  // Closing frees the label. New matching windows receive persisted rules.
  await mutate(s=>s.windows=s.windows.filter(w=>w.uuid!==s.windows[1].uuid));await until(()=>windows().length===2);ok('window','1','--scratchpad','term');await until(ready);
  const saved=fs.readFileSync(stateFile,'utf8');for(const args of [['window','--scratchpad','float'],['window','--scratchpad','one','extra'],['rule','--add','scratchpad='],['window','--toggle','missing']]){assert.notEqual(call('-m',...args).status,0);assert.equal(fs.readFileSync(stateFile,'utf8'),saved);}
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

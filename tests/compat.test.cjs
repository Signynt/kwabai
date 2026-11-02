const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);p.stdout.resume();resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('yabai-compatible scripts, regex rules, numeric queries and confirmed command exits',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-compat-')),runtime=path.join(dir,'kwabai'),file=path.join(dir,'snapshot.json'),socket=path.join(runtime,'control.sock'),stateFile=path.join(dir,'contexts.json');fs.mkdirSync(runtime,{mode:0o700});let p,d;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};




 s.capabilities.push('geometry-retention','geometry-lifecycle','geometry-release','notifications','focus');s['geometry-window-limit']=20;s['test-geometry-fast']=true;s['test-geometry-write']=true;s.windows.push({...s.windows[0],uuid:'44444444-4444-4444-8444-444444444444',focused:false});
 const write=()=>{s.sequence=String(BigInt(s.sequence)+1n);fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const status=()=>json(socket,'context','status','DP-1');const ready=()=>status().management.state==='active';
 const refresh=async()=>{write();await until(()=>json(socket,'doctor').bridge.sequence===s.sequence);};const sync=()=>Object.assign(s,JSON.parse(fs.readFileSync(file,'utf8')));
 const compatible=(...args)=>call(socket,'-m',...args);const succeeds=(...args)=>{const r=compatible(...args);assert.equal(r.status,0,r.stderr);assert.equal(r.stdout,'','successful mutations must be quiet');};
 const env={...process.env,XDG_RUNTIME_DIR:dir,PATH:path.join(__dirname,'../scripts/compat')+':'+process.env.PATH};
 try{
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);
  assert.equal(compatible('config','insert_window_border_color').stdout,'0xffd75f5f\n');
  succeeds('config','insert_feedback_color','0x80AABBCC');succeeds('config','window_border_radius','12');assert.equal(compatible('config','window_border_radius').stdout,'12\n');assert.equal(compatible('config','insert_feedback_color').stdout,'0x80aabbcc\n');
  const configSaved=fs.readFileSync(stateFile,'utf8');for(const value of ['#ffffff','0x123','0xffzzzzzz','-1','0x100000000']){assert.equal(compatible('config','insert_window_border_color',value).status,1);assert.equal(fs.readFileSync(stateFile,'utf8'),configSaved);}
  let rows=JSON.parse(compatible('query','--windows').stdout);assert.equal(rows.length,2);assert.equal(typeof rows[0].id,'number');assert.equal(typeof rows[0]['is-floating'],'boolean');assert.equal(rows[0]['is-floating'],true);assert.equal(rows[0]['kwabai-output'],undefined);
  let r=call(socket,'--message','query','--windows','id,app','--window');assert.equal(r.status,0,r.stderr);assert.deepEqual(JSON.parse(r.stdout),{id:1,app:'kitty'});assert.equal(compatible('window','--focus','east').status,1);
  const script=path.join(dir,'existing-config.sh');fs.writeFileSync(script,`#!/bin/sh
set -e
yabai -m rule --add app='^systemsettings$' manage=off 'label=Settings policy'
yabai -m query --windows id,app
`);
  r=spawnSync('sh',[script],{env,encoding:'utf8',timeout:10000});assert.equal(r.status,0,r.stderr);assert.equal(JSON.parse(r.stdout)[0].id,1);let rules=JSON.parse(compatible('rule','--list').stdout);assert.equal(rules[0].app,'^systemsettings$');assert.equal(rules[0].label,'Settings policy');assert.equal(rules[0].manage,false);succeeds('rule','--add','app=^systemsettings$','manage=off','label=Settings policy');assert.equal(JSON.parse(compatible('rule','--list').stdout).length,1,'same labels must replace instead of duplicating');
  succeeds('rule','--add','app=^org[.]example[.]tool$','title=^Dialog [[:digit:]]+$','manage=off');rules=JSON.parse(compatible('rule','--list').stdout);assert.equal(rules[1].label,'');succeeds('rule','--remove','1');
  const disabledState=fs.readFileSync(stateFile,'utf8');assert.equal(compatible('window','--swap','east').status,1);assert.equal(compatible('window','--ratio','abs:0.6').status,1);assert.equal(compatible('space','--rotate','90').status,1);assert.equal(compatible('space','--gap','abs:20').status,1);assert.equal(compatible('space','--padding','abs:1:2:3:4').status,1);assert.equal(compatible('window','--focus','2').status,1);assert.equal(fs.readFileSync(stateFile,'utf8'),disabledState);
  const saved=fs.readFileSync(stateFile,'utf8');for(const args of [['rule','--add','app=[','manage=off'],['rule','--add','app=.*','sticky=on','manage=off'],['signal','--add','event=window_created','action=echo no'],['space','--rotate','45'],['query','--windows','--space','1']]){r=compatible(...args);assert.equal(r.status,1);assert.equal(r.stdout,'');assert.notEqual(r.stderr,'');assert.equal(fs.readFileSync(stateFile,'utf8'),saved);}
  json(socket,'context','add','DP-1','1');json(socket,'context','add','DP-1','2');for(const placement of json(socket,'context','plan','DP-1').placements)s.windows.find(w=>w.uuid===placement.uuid).frame=placement.frame;await refresh();await stop(d);
  d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',stateFile]);await until(()=>json(socket,'doctor')['event-stream'].ready);assert.equal(compatible('config','insert_feedback_color').stdout,'0x80aabbcc\n');assert.equal(compatible('config','window_border_radius').stdout,'12\n');assert.equal(JSON.parse(compatible('rule','--list').stdout)[0].label,'Settings policy');json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);
  const focusedId=()=>JSON.parse(compatible('query','--windows','id,has-focus').stdout).find(w=>w['has-focus']).id;
  const focusSaved=fs.readFileSync(stateFile,'utf8'),focusInode=fs.statSync(stateFile).ino,focusGeometry=status().active.id;
  succeeds('window','--focus','2');assert.equal(focusedId(),2);succeeds('window','1','--focus');assert.equal(focusedId(),1);
  succeeds('window','--focus');succeeds('window','--focus','1');assert.equal(focusedId(),1);
  for(const args of [['window','--focus','999'],['window','999','--focus'],['window','--focus','0'],['window','--focus','next'],['window','1','--focus','2']]){r=compatible(...args);assert.equal(r.status,1);assert.equal(r.stdout,'');assert.equal(focusedId(),1);}
  assert.equal(fs.readFileSync(stateFile,'utf8'),focusSaved);assert.equal(fs.statSync(stateFile).ino,focusInode);assert.equal(status().active.id,focusGeometry);assert.equal(status().management.enabled,true);sync();
  rows=JSON.parse(compatible('query','--windows').stdout);assert.equal(rows[0]['is-floating'],false);succeeds('window','--focus','east');assert.equal(focusedId(),2);sync();succeeds('window','--focus','west');assert.equal(focusedId(),1);sync();assert.equal(compatible('window','--focus','west').status,1);
  // Synchronous CLI bursts must yield occasionally so the fixture can drain daemon logs.
  await new Promise(resolve=>setImmediate(resolve));
  const frames=()=>Object.fromEntries(json(socket,'bridge','windows').windows.map(w=>[w.id,w.frame]));
  const original=frames();const beforeInvalid=fs.readFileSync(stateFile,'utf8');
  for(const args of [['window','--swap','west'],['window','1','--swap','1'],['window','--swap','recent'],['window','--swap','999'],['window','--swap','east','extra']]){r=compatible(...args);assert.equal(r.status,1,r.stdout);assert.equal(r.stdout,'');assert.equal(fs.readFileSync(stateFile,'utf8'),beforeInvalid,'invalid swap must not save an edit');}
  succeeds('window','--swap','east');assert.deepEqual(frames()['1'],original['2']);assert.deepEqual(frames()['2'],original['1']);assert.equal(json(socket,'bridge','windows').windows.find(w=>w.id==='1').focused,true);await until(ready);
  // Focus is on 1 at the right edge; east must resolve from the explicit source 2.
  succeeds('window','2','--swap','east');assert.deepEqual(frames(),original);await until(ready);
  // Compatible commands apply immediately even when automatic management is off.
  json(socket,'context','manage','DP-1','off');succeeds('window','1','--swap','2');assert.deepEqual(frames()['1'],original['2']);assert.equal(status().management.enabled,false);
  succeeds('window','--swap','west');assert.deepEqual(frames(),original);assert.equal(status().management.enabled,false);
  const unmanagedFocusJob=status().active.id;succeeds('window','--focus','2');assert.equal(focusedId(),2);succeeds('window','1','--focus');assert.equal(focusedId(),1);assert.equal(status().active.id,unmanagedFocusJob);assert.equal(status().management.enabled,false);sync();
  await new Promise(resolve=>setImmediate(resolve));
  // Ratio has parent/first-child semantics, independent of the selected side.
  const ratioOf=()=>json(socket,'context','show','DP-1').tree.nodes.find(n=>n.first!=='0').ratio;
  const initialWidth=frames()['1'].w;
  succeeds('window','--ratio','abs:0.6');assert.equal(ratioOf(),0.6);assert.ok(frames()['1'].w>initialWidth);assert.equal(status().management.enabled,false);
  succeeds('window','2','--ratio','rel:0.1');assert.ok(Math.abs(ratioOf()-0.7)<1e-10);assert.ok(frames()['1'].w>initialWidth,'second-child selection still increases first-child share');
  succeeds('window','--ratio','abs:50');assert.equal(ratioOf(),0.9);const atLimit=fs.readFileSync(stateFile,'utf8'),limitJob=status().active.id,limitInode=fs.statSync(stateFile).ino;
  succeeds('window','2','--ratio','rel:1');succeeds('window','--ratio','rel:0');assert.equal(fs.readFileSync(stateFile,'utf8'),atLimit,'no-op ratio must not rewrite state');assert.equal(status().active.id,limitJob);assert.equal(fs.statSync(stateFile).ino,limitInode,'no-op ratio must not replace the state file');
  succeeds('window','2','--ratio','abs:-10');assert.equal(ratioOf(),0.1);succeeds('window','--ratio','abs:0.5');assert.deepEqual(frames(),original);
  const validRatioState=fs.readFileSync(stateFile,'utf8');for(const value of ['rel:nan','abs:inf','abs:0.5:extra','bad:0.5','rel:','0.6']){r=compatible('window','--ratio',value);assert.equal(r.status,1);assert.equal(r.stdout,'');assert.equal(fs.readFileSync(stateFile,'utf8'),validRatioState);}
  await new Promise(resolve=>setImmediate(resolve));
  // Compatible space actions use the focused output; explicit space IDs remain unsupported.
  const beforeSpace=fs.readFileSync(stateFile,'utf8');for(const args of [['space','2','--rotate','90'],['space','--rotate','45'],['space','--mirror','x'],['space','--equalize','z-axis']]){r=compatible(...args);assert.equal(r.status,1);assert.equal(fs.readFileSync(stateFile,'utf8'),beforeSpace);}
  succeeds('window','--ratio','abs:0.6');const asymmetric=frames();
  succeeds('space','--rotate','90');assert.ok(frames()['1'].y>frames()['2'].y,'90 must put the former left child below the former right');assert.ok(frames()['1'].h>frames()['2'].h);
  succeeds('space','--mirror','x-axis');assert.ok(frames()['1'].y<frames()['2'].y);assert.ok(frames()['1'].h<frames()['2'].h,'mirror retains slot ratios instead of carrying window proportions');
  succeeds('space','--mirror','x-axis');succeeds('space','--rotate','270');assert.deepEqual(frames(),asymmetric);
  succeeds('space','--mirror','y-axis');assert.equal(frames()['2'].x,asymmetric['1'].x);assert.equal(frames()['2'].w,asymmetric['1'].w);succeeds('space','--mirror','y-axis');assert.deepEqual(frames(),asymmetric);
  succeeds('space','--rotate','180');assert.ok(frames()['1'].x>frames()['2'].x);succeeds('space','--rotate','180');assert.deepEqual(frames(),asymmetric);
  const noAxisChange=fs.readFileSync(stateFile,'utf8'),axisInode=fs.statSync(stateFile).ino;succeeds('space','--equalize','x-axis');assert.equal(fs.readFileSync(stateFile,'utf8'),noAxisChange);assert.equal(fs.statSync(stateFile).ino,axisInode);
  succeeds('space','--equalize','y-axis');assert.equal(ratioOf(),0.5);assert.deepEqual(frames(),original);const equalizedJob=status().active.id;succeeds('space','--equalize');assert.equal(status().active.id,equalizedJob);assert.equal(status().management.enabled,false);assert.equal(json(socket,'bridge','windows').windows.find(w=>w.id==='1').focused,true);
  await new Promise(resolve=>setImmediate(resolve));
  // Compatible spacing resolves relative edits atomically against current settings.
  const stored=()=>json(socket,'context','show','DP-1');const separation=()=>{const f=frames();return f['2'].x-f['1'].x-f['1'].w;};
  succeeds('space','--gap','abs:30');assert.equal(stored().gap,30);assert.equal(separation(),30);assert.equal(frames()['1'].x,original['1'].x);
  succeeds('space','--gap','rel:+10');assert.equal(stored().gap,40);assert.equal(separation(),40);
  succeeds('space','--gap','rel:-1000');assert.equal(stored().gap,0);assert.equal(separation(),0);const zeroGap=fs.readFileSync(stateFile,'utf8'),zeroGapInode=fs.statSync(stateFile).ino,zeroJob=status().active.id;
  succeeds('space','--gap','rel:-1');succeeds('space','--gap','abs:0');assert.equal(fs.readFileSync(stateFile,'utf8'),zeroGap);assert.equal(fs.statSync(stateFile).ino,zeroGapInode);assert.equal(status().active.id,zeroJob);
  succeeds('space','--gap','abs:+0010');assert.deepEqual(frames(),original);await new Promise(resolve=>setImmediate(resolve));
  succeeds('space','--padding','abs:20:40:60:80');assert.deepEqual(stored().padding,[20,80,40,60]);let padded=frames();assert.equal(padded['1'].x,60);assert.equal(padded['1'].y,60);assert.equal(padded['2'].x+padded['2'].w,2480);assert.equal(padded['1'].y+padded['1'].h,1400);
  let disk=JSON.parse(fs.readFileSync(stateFile,'utf8'));assert.deepEqual(disk.contexts[0].padding,[20,80,40,60]);assert.equal(disk.contexts[0].gap,10);
  succeeds('space','--padding','rel:-30:5:10:-100');assert.deepEqual(stored().padding,[0,0,45,70]);padded=frames();assert.equal(padded['1'].x,70);assert.equal(padded['1'].y,40);assert.equal(padded['2'].x+padded['2'].w,2560);assert.equal(padded['1'].y+padded['1'].h,1395);
  const noPaddingChange=fs.readFileSync(stateFile,'utf8'),paddingInode=fs.statSync(stateFile).ino,paddingJob=status().active.id;succeeds('space','--padding','rel:0:0:0:0');succeeds('space','--padding','abs:0:45:70:0');assert.equal(fs.readFileSync(stateFile,'utf8'),noPaddingChange);assert.equal(fs.statSync(stateFile).ino,paddingInode);assert.equal(status().active.id,paddingJob);
  await new Promise(resolve=>setImmediate(resolve));succeeds('space','--padding','rel:-1000:-1000:-1000:-1000');assert.deepEqual(stored().padding,[0,0,0,0]);succeeds('space','--padding','abs:10:10:10:10');assert.deepEqual(frames(),original);assert.equal(status().management.enabled,false);
  const beforeInvalidSpacing=fs.readFileSync(stateFile,'utf8');for(const args of [['space','--gap','abs:-1'],['space','--gap','rel:1.5'],['space','--gap','abs:2147483648'],['space','--gap','rel:2147483647'],['space','--gap','abs:2147483647'],['space','--padding','abs:1:2:3'],['space','--padding','abs:1:-2:3:4'],['space','--padding','abs:1000:1000:10:10'],['space','--padding','rel:0:0:2147483647:0']]){r=compatible(...args);assert.equal(r.status,1,r.stdout);assert.equal(r.stdout,'');assert.equal(fs.readFileSync(stateFile,'utf8'),beforeInvalidSpacing);}
  await new Promise(resolve=>setImmediate(resolve));
  // Manual movement cannot be overwritten by a swap, and cannot mutate saved intent.
  sync();s.windows[0].frame.x+=50;await refresh();const beforeMoved=fs.readFileSync(stateFile,'utf8');r=compatible('window','--swap','east');assert.equal(r.status,1);assert.equal(fs.readFileSync(stateFile,'utf8'),beforeMoved);s.windows[0].frame.x-=50;await refresh();
  json(socket,'context','manage','DP-1','on','--lifecycle');await until(ready);
  succeeds('window','2','--ratio','abs:0.6');await until(ready);succeeds('window','--ratio','abs:0.5');await until(ready);succeeds('space','--rotate','90');await until(ready);succeeds('space','--rotate','270');await until(ready);
  const currentGenerationApplied=()=>{const q=status();return q.management.state==='active'&&q.active['context-generation']===q['stored-generation'];};
  succeeds('space','--gap','abs:24');await until(currentGenerationApplied);succeeds('space','--padding','abs:11:22:33:44');await until(currentGenerationApplied);
  json(socket,'context','undo','DP-1');await until(currentGenerationApplied);assert.deepEqual(stored().padding,[10,10,10,10]);assert.equal(stored().gap,24);
  json(socket,'context','undo','DP-1');await until(currentGenerationApplied);assert.equal(stored().gap,10);
  json(socket,'context','redo','DP-1');await until(currentGenerationApplied);json(socket,'context','redo','DP-1');await until(currentGenerationApplied);assert.equal(stored().gap,24);assert.deepEqual(stored().padding,[11,44,22,33]);
  succeeds('space','--gap','abs:10');succeeds('space','--padding','abs:10:10:10:10');await until(currentGenerationApplied);assert.deepEqual(frames(),original);
  await new Promise(resolve=>setImmediate(resolve));
  succeeds('window','2','--toggle','float');assert.equal(JSON.parse(compatible('query','--windows','--window','2').stdout)['is-floating'],true);assert.equal(compatible('window','--focus','2').status,1);assert.equal(focusedId(),1);sync();await refresh();const singleSaved=fs.readFileSync(stateFile,'utf8');r=compatible('window','1','--ratio','abs:0.6');assert.equal(r.status,1);assert.match(r.stderr,/root node/);assert.equal(fs.readFileSync(stateFile,'utf8'),singleSaved);succeeds('window','2','--toggle','float');assert.equal(JSON.parse(compatible('query','--windows','--window','2').stdout)['is-floating'],false);
  sync();s.windows.push({...s.windows[0],uuid:'55555555-5555-4555-8555-555555555555',app:'systemsettings',focused:false});await refresh();assert.equal(json(socket,'context','show','DP-1').members.length,2,'POSIX manage=off rule must exclude matching arrivals');
  const excludedId=json(socket,'bridge','windows').windows.find(w=>w.app==='systemsettings').id;assert.equal(compatible('window','--focus',excludedId).status,1);assert.equal(focusedId(),1);
  // A normal floating source on this output may explicitly focus a tiled target.
  sync();for(const w of s.windows)w.focused=w.app==='systemsettings';await refresh();succeeds('window','--focus','1');assert.equal(focusedId(),1);sync();
  // A normal window on another output must not cause implicit cross-output focus.
  sync();s.outputs.push({...s.outputs[0],id:'other-screen',name:'DP-2',geometry:{x:2560,y:0,w:1080,h:1920},'work-area':{x:2560,y:0,w:1080,h:1920}});s.windows.push({...s.windows[0],uuid:'66666666-6666-4666-8666-666666666666',output:'DP-2',focused:false});await refresh();const otherId=json(socket,'bridge','windows').windows.find(w=>w.output==='DP-2').id;assert.equal(compatible('window','--focus',otherId).status,1);assert.equal(focusedId(),1);
  s['test-focus-refuse']=true;await refresh();r=compatible('window','--focus','2');assert.equal(r.status,1);assert.equal(r.stdout,'');assert.match(r.stderr,/rejected window activation/);delete s['test-focus-refuse'];s['test-focus-ignore']=true;await refresh();r=compatible('window','--focus','2');assert.equal(r.status,1);assert.match(r.stderr,/timed_out/);assert.equal(focusedId(),1);delete s['test-focus-ignore'];await refresh();
  s['test-geometry-reject']=true;await refresh();r=compatible('window','2','--toggle','float');assert.equal(r.status,1,'native rejection must not report submission as success');assert.equal(r.stdout,'');delete s['test-geometry-reject'];await refresh();json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);
  // Retile through the compatible spelling after restoration, including unapplied stored members.
  succeeds('window','2','--toggle','float');assert.equal(JSON.parse(compatible('query','--windows','--window','2').stdout)['is-floating'],false);json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);
  succeeds('window','1','--toggle','float');assert.equal(JSON.parse(compatible('query','--windows','--window','1').stdout)['is-floating'],false);json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);
  succeeds('window','1','--toggle','float');sync();s['test-geometry-reject']=true;await refresh();r=compatible('space','--gap','abs:17');assert.equal(r.status,1);assert.equal(r.stdout,'');assert.match(r.stderr,/stale test request/);delete s['test-geometry-reject'];await refresh();json(socket,'context','reset','DP-1');await until(()=>status()['active-count']===0);
  succeeds('rule','--remove','Settings policy');assert.deepEqual(JSON.parse(compatible('rule','--list').stdout),[]);assert.equal(JSON.parse(fs.readFileSync(stateFile)).version,8);
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

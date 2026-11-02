const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process'),{once}=require('node:events');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const [daemon,cli,provider]=process.argv.slice(2);
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>reject(Error('startup '+err)),5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
async function until(f){const end=Date.now()+7000;while(Date.now()<end){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}
test('global BSP configuration admits contexts, applies exclusions first, and inherits across switching/hotplug',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-multi-')),file=path.join(dir,'snapshot.json'),socket=path.join(dir,'control.sock');let p,d;
 const desktop='22222222-2222-4222-8222-222222222222',other='99999999-9999-4999-8999-999999999999';
 const output=(id,name,x,w,h)=>({id,name,scale:1,geometry:{x,y:0,w,h},'work-area':{x,y:0,w,h},'current-desktop':desktop,'current-activity':'work'});
 const window=(uuid,out,ds=[desktop],activities=['work'])=>({uuid,app:'kitty',title:'test',output:out,desktops:ds,activities,frame:{x:out==='DP-1'?100:-900,y:100,w:500,h:500},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:out==='DP-1'});
 let s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.1',capabilities:['snapshot','contexts','events','geometry','geometry-update','geometry-retention','geometry-lifecycle','geometry-release','notifications'],'geometry-window-limit':20,'test-geometry-write':true,'test-geometry-exclusive':true,
 outputs:[output('screen-a','DP-1',0,2560,1440),output('screen-b','DP-2',-1080,1080,1920)],windows:[window('33333333-3333-4333-8333-333333333333','DP-1'),window('44444444-4444-4444-8444-444444444444','DP-2')]};
 const write=()=>{fs.writeFileSync(file+'.new',JSON.stringify(s));fs.renameSync(file+'.new',file);};write();
 const call=(...args)=>spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});
 const json=(...args)=>{const r=spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);};
 const managers=()=>json('context','managers');const status=out=>json('context','status',out);
 const mutate=async fn=>{s=JSON.parse(fs.readFileSync(file));fn(s);s.sequence=String(BigInt(s.sequence)+1n);const seq=s.sequence;write();await until(()=>BigInt(json('doctor').bridge.sequence)>=BigInt(seq));};
 const config=(name,value)=>{const args=['-m','config',name];if(value!==undefined)args.push(value);const r=call(...args);assert.equal(r.status,0,r.stderr);return r.stdout.trim();};
 const ready=out=>{const r=status(out);assert.notEqual(r.management.state,'stopped',r.management.error);return ['active','idle'].includes(r.management.state)&&(!r.active||r.active['context-generation']===r['stored-generation']);};
 // Public IDs are session-local and can change after the popup closes and daemon restarts.
 const snapshot=()=>json('bridge','windows').windows.map(({id,...w})=>w);
 try {
  const sys={...s.windows[0],uuid:'55555555-5555-4555-8555-555555555555',app:'systemsettings',title:'Audio',focused:false},info={...s.windows[1],uuid:'66666666-6666-4666-8666-666666666666',title:'Document Info',focused:false};s.windows.push(sys,info);for(const w of s.windows)w.desktops=[];s['test-geometry-write']=true;s['test-geometry-exclusive']=true;write();
  p=await start(provider,[file]);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',path.join(dir,'contexts.json')]);await until(()=>json('doctor')['event-stream'].ready);
  assert.equal(config('layout'),'float');assert.equal(managers()['enabled-count'],0);const original=snapshot();
  assert.equal(call('context','configure','begin').status,0);config('layout','bsp');config('top_padding','0');config('bottom_padding','10');config('left_padding','10');config('right_padding','10');config('window_gap','10');config('insert_feedback_color','0xff33a4f5');
  for(const rule of [['app=^systemsettings$','title=.*','manage=off'],['title=.*Info','manage=off']]){const r=call('-m','rule','--add',...rule);assert.equal(r.status,0,r.stderr);}
  await new Promise(r=>setTimeout(r,250));assert.deepEqual(snapshot(),original,'layout must not admit windows before the later configuration rules');assert.equal(call('context','configure','end').status,0);
  await until(()=>ready('DP-1')&&ready('DP-2'));assert.equal(managers()['enabled-count'],2);assert.equal(json('context','show','DP-1').members.length,1);assert.equal(json('context','show','DP-2').members.length,1);assert.deepEqual(json('context','show','DP-1').padding,[0,10,10,10]);assert.equal(json('context','show','DP-2').gap,10);assert.deepEqual(snapshot().find(w=>w.uuid===sys.uuid).frame,sys.frame);assert.deepEqual(snapshot().find(w=>w.uuid===info.uuid).frame,info.frame);
  // Normal windows with popup hints float by default; configuration changes
  // release/re-admit them without stopping either output. Explicit rules win.
  assert.equal(config('float_popups'),'on');
  const popupId='aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa';
  await mutate(s=>s.windows.push({...s.windows[0],uuid:popupId,app:'shelly-ui',title:'Review PKGBUILD changes',focused:false,'is-transient':true}));
  await until(()=>snapshot().some(w=>w.uuid===popupId));
  assert.equal(json('context','reconcile','DP-1').excluded.find(w=>w.uuid===popupId).reason,'rule-float');
  assert.equal(json('context','show','DP-1').members.length,1);
  config('float_popups','off');await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===2);
  config('float_popups','on');await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===1);
  assert.equal(call('-m','rule','--add','label=popup-override','app=^shelly-ui$','manage=on').status,0);
  await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===2);
  assert.equal(call('-m','rule','--remove','popup-override').status,0);
  await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===1);
  await mutate(s=>s.windows=s.windows.filter(w=>w.uuid!==popupId));
  // Changing one setting updates every managed tree and future contexts.
  config('window_gap','24');await until(()=>ready('DP-1')&&ready('DP-2'));assert.equal(json('context','show','DP-1').gap,24);
  const before=fs.readFileSync(path.join(dir,'contexts.json'),'utf8');for(const [name,value] of [['float_popups','yes'],['layout','stack'],['window_gap','-1'],['top_padding','NaN'],['left_padding','999999']]){assert.notEqual(call('-m','config',name,value).status,0);assert.equal(fs.readFileSync(path.join(dir,'contexts.json'),'utf8'),before);}
  const layer=call('-m','rule','--add','title=.*','layer=above','manage=off');assert.notEqual(layer.status,0);assert.match(layer.stderr,/layer rules require/);assert.notEqual(call('-m','config','window_shadow','off').status,0);
  await mutate(s=>{s.outputs.push({...s.outputs[0],id:'screen-c',name:'DP-3',geometry:{x:2560,y:0,w:1280,h:1440},'work-area':{x:2560,y:0,w:1280,h:1440}});s.windows.push({...s.windows[0],uuid:'77777777-7777-4777-8777-777777777777',output:'DP-3',focused:false,frame:{x:2700,y:100,w:800,h:600}});});await until(()=>ready('DP-3'));assert.equal(json('context','show','DP-3').gap,24);assert.deepEqual(json('context','show','DP-3').padding,[0,10,10,10]);
  // The compositor may reject Forget while work is pending before its latest
  // status has reached the daemon. Preserve ownership, refresh and retry; a
  // single refusal must not permanently jam every display's layout policy.
  const desktop=s.outputs[0]['current-desktop'];await mutate(s=>{s['test-forget-pending-once']=true;for(const o of s.outputs)o['current-desktop']='99999999-9999-4999-8999-999999999999';});await until(()=>ready('DP-1')&&ready('DP-2')&&ready('DP-3'));assert.equal(json('context','show','DP-1').desktop,'99999999-9999-4999-8999-999999999999');assert.equal(json('context','show','DP-1').members.length,1,'sticky windows must be rehomed, not claimed by an inactive tree');
  assert.equal(JSON.parse(fs.readFileSync(file))['test-forget-refusals'],1);assert.deepEqual(json('doctor')['layout-policy'].errors,{});
  await mutate(s=>{for(const o of s.outputs)o['current-desktop']=desktop;});await until(()=>ready('DP-1')&&ready('DP-2')&&ready('DP-3'));assert.equal(json('context','show','DP-2').members.length,1);
  // KWin marks a window on an inactive desktop as candidate=false. External
  // Move to Desktop must still release ownership and reflow the source.
  const movedUuid=s.windows[0].uuid;
  await mutate(s=>{const w=s.windows.find(w=>w.uuid===movedUuid);w.desktops=[other];w.candidate=false;w.frame.x+=33;});
  await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===0);
  assert.equal(status('DP-1')['active-count'],0);assert.deepEqual(snapshot().find(w=>w.uuid===movedUuid).frame,s.windows.find(w=>w.uuid===movedUuid).frame,'source release must not write to the departed window');assert.deepEqual(json('doctor')['layout-policy'].errors,{});
  assert.equal(call('-m','query','--windows','id').status,0);
  await mutate(s=>{for(const o of s.outputs)o['current-desktop']=other;s.windows.find(w=>w.uuid===movedUuid).candidate=true;});
  await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===1);
  await mutate(s=>{for(const o of s.outputs)o['current-desktop']=desktop;const w=s.windows.find(w=>w.uuid===movedUuid);w.candidate=false;});
  await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===0);
  await mutate(s=>{const w=s.windows.find(w=>w.uuid===movedUuid);w.desktops=[];w.candidate=true;});
  await until(()=>['DP-1','DP-2','DP-3'].every(ready)&&json('context','show','DP-1').members.length===1);
  // Reconfiguring BSP must also re-evaluate exclusions for live cached members.
  assert.equal(call('context','configure','begin').status,0);await until(()=>!json('doctor')['geometry-busy']);assert.equal(call('-m','rule','--add','label=temporary','title=^test$','manage=off').status,0);config('layout','bsp');assert.equal(call('context','configure','end').status,0);await until(()=>['DP-1','DP-2','DP-3'].every(out=>ready(out)&&json('context','show',out).members.length===0));
  // Batch rule removal with layout configuration: removing the exclusion
  // otherwise starts admission before the following config command arrives.
  assert.equal(call('context','configure','begin').status,0);await until(()=>!json('doctor')['geometry-busy']);assert.equal(call('-m','rule','--remove','temporary').status,0);config('layout','bsp');assert.equal(call('context','configure','end').status,0);await until(()=>['DP-1','DP-2','DP-3'].every(out=>ready(out)&&json('context','show',out).members.length===1));
  assert.equal(call('-m','rule','--add','title=.*Info','manage=off').status,0);assert.equal(JSON.parse(call('-m','rule','--list').stdout).length,2,'reloading anonymous exclusions must not accumulate duplicates');
  // KWin can move windows onto a surviving output when a display disconnects.
  await mutate(s=>{s.outputs=s.outputs.filter(o=>o.name!=='DP-3');const w=s.windows.find(w=>w.output==='DP-3');w.output='DP-1';w.frame={x:200,y:200,w:800,h:600};});await until(()=>ready('DP-1')&&json('context','show','DP-1').members.length===2);assert.equal(ready('DP-2'),true);assert.deepEqual(json('doctor')['layout-policy'].errors,{});
  const tiled=snapshot();config('layout','float');await until(()=>!json('doctor')['layout-policy'].releasing&&['DP-1','DP-2'].every(out=>status(out)['active-count']===0));assert.deepEqual(snapshot(),tiled);assert.equal(managers()['enabled-count'],0);
  await stop(d);d=await start(daemon,['--socket',socket,'--observe-kwin','--enable-control','--context-state',path.join(dir,'contexts.json')]);await until(()=>json('doctor')['event-stream'].ready);assert.equal(config('layout'),'float');assert.equal(managers()['enabled-count'],0);assert.deepEqual(snapshot(),tiled);
 }finally{await stop(d);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

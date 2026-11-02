const {test}=require('node:test');const assert=require('node:assert/strict');const {spawn,spawnSync}=require('node:child_process');const {once}=require('node:events');const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const daemon=process.argv[2],cli=process.argv[3],provider=process.argv[4];
async function start(binary,args){const p=spawn(binary,args);let err='';p.stderr.on('data',b=>err+=b);await new Promise((resolve,reject)=>{const t=setTimeout(()=>{p.kill('SIGKILL');reject(Error('startup timeout '+err));},5000);p.once('exit',c=>{clearTimeout(t);reject(Error('startup '+c+' '+err));});p.stdout.once('data',()=>{clearTimeout(t);resolve();});});return p;}
async function stop(p){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill();await done;}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
async function until(f){const deadline=Date.now()+6000;while(Date.now()<deadline){if(f())return;await new Promise(r=>setTimeout(r,30));}throw Error('condition timed out');}

const helper=path.resolve(__dirname,'../scripts/start-session.sh');
test('session launcher defaults, bridge loading, persistence, readiness and ownership',async()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-start-')),file=path.join(dir,'snapshot.json'),bin=path.join(dir,'bin'),build=path.join(dir,'build'),runtime=path.join(dir,'runtime'),stateHome=path.join(dir,'state-home'),config=path.join(dir,'qdbus.json'),log=path.join(dir,'calls.jsonl');let p,h;
 const s={protocol:1,instance:'11111111-1111-4111-8111-111111111111',sequence:'1','event-sequence':'0','kwin-version':'6.8.2',capabilities:['snapshot','contexts','events','geometry','geometry-no-restore','geometry-update'],outputs:[{id:'screen',name:'DP-1',scale:1,geometry:{x:0,y:0,w:2560,h:1440},'work-area':{x:0,y:40,w:2560,h:1400},'current-desktop':'22222222-2222-4222-8222-222222222222','current-activity':'work'}],windows:[{uuid:'33333333-3333-4333-8333-333333333333',app:'kitty',title:'test',output:'DP-1',desktops:[],activities:[],frame:{x:100,y:100,w:800,h:600},minimum:{w:100,h:100},candidate:true,minimized:false,fullscreen:false,focused:true}]};

 s['geometry-window-limit']=20;s.capabilities.push('geometry-retention','geometry-lifecycle','geometry-release','scratchpads','floating-grid');
 const effect='kwabai_probe_test123';let settings={loaded:[],load:true,snapshot:s,installed:'6.8.2',running:'6.8.2',unavailable:0};fs.writeFileSync(file,JSON.stringify(s));fs.mkdirSync(bin);fs.mkdirSync(build);fs.mkdirSync(runtime,{mode:0o700});fs.symlinkSync(daemon,path.join(build,'kwabaid'));fs.symlinkSync(cli,path.join(build,'kwabai'));fs.writeFileSync(path.join(build,'kwabai-effect-id'),effect+'\n');fs.writeFileSync(path.join(build,'kwabai-native-build.json'),JSON.stringify({schema:1,'effect-id':effect,'kwin-version':'6.8.2'}));fs.mkdirSync(path.join(build,'plugins/kwin/effects/plugins'),{recursive:true});fs.writeFileSync(path.join(build,'plugins/kwin/effects/plugins',effect+'.so'),'mock native module');fs.writeFileSync(path.join(build,'kwabai-plugin-check'),`#!/bin/sh
echo '{"kwin-version":"6.8.2","effect-id":"kwabai_probe_test123"}'
`,{mode:0o755});
 const qdbus=`#!/usr/bin/env python3
import json,sys
from pathlib import Path
c=json.loads(Path(${JSON.stringify(config)}).read_text())
with open(${JSON.stringify(log)},"a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")
method=sys.argv[3]
if method.endswith("loadedEffects"):
 if c.get("unavailable",0)>0:
  c["unavailable"]-=1
  Path(${JSON.stringify(config)}).write_text(json.dumps(c))
  sys.exit(1)
 print("\\n".join(c["loaded"]))
elif method.endswith("supportInformation"): print("KWin version: "+c["running"])
elif method.endswith("loadEffect"): print("true" if c["load"] else "false")
elif method.endswith("Snapshot"): print(json.dumps(c["snapshot"]))
else: sys.exit(1)
`;
 fs.writeFileSync(path.join(bin,'qdbus6'),qdbus,{mode:0o755});
 fs.writeFileSync(path.join(bin,'kwin_wayland'),'#!/usr/bin/env python3\nimport json\nfrom pathlib import Path\nprint("kwin "+json.loads(Path('+JSON.stringify(config)+').read_text())["installed"])\n',{mode:0o755});
 const env={...process.env,PATH:bin+path.delimiter+process.env.PATH,XDG_RUNTIME_DIR:runtime,XDG_STATE_HOME:stateHome,XDG_CONFIG_HOME:path.join(dir,'configuration')};
 fs.mkdirSync(path.join(env.XDG_CONFIG_HOME,'kwabai'),{recursive:true});fs.writeFileSync(path.join(env.XDG_CONFIG_HOME,'kwabai/kwabairc'),'# isolated empty configuration\n');
 const socket=path.join(runtime,'kwabai/control.sock'),contextFile=path.join(stateHome,'kwabai/contexts.json');
 const write=()=>fs.writeFileSync(config,JSON.stringify(settings));write();
 const launch=async(extra=[])=>{h=spawn(helper,['--build-dir',build,'--timeout','3',...extra],{env});let output='',error='';h.stdout.on('data',b=>output+=b);h.stderr.on('data',b=>error+=b);await until(()=>{if(h.exitCode!==null)throw Error('launcher exited '+h.exitCode+' '+error+' '+output);return output.includes('Session ready');});return {output:()=>output,error:()=>error};};
 const failure=async(extra=[])=>{const r=spawnSync(helper,['--build-dir',build,'--timeout','1','--kwin-timeout','1',...extra],{env,encoding:'utf8',timeout:8000});assert.notEqual(r.status,0,r.stdout);return r;};
 try{
  p=await start(provider,[file]);settings.unavailable=2;write();const session=await launch();assert.equal(JSON.parse(fs.readFileSync(config)).unavailable,0,'login waits for KWin to become available');assert.equal(json(socket,'doctor')['desktop-control'],false);assert.ok(session.output().includes(contextFile));assert.equal(fs.statSync(path.dirname(contextFile)).mode&0o777,0o700);
  const calls=fs.readFileSync(log,'utf8');assert.ok(calls.includes('loadEffect'));assert.ok(!calls.includes('unloadEffect'));assert.ok(!calls.includes('sudo'));
  json(socket,'context','add','DP-1','1');json(socket,'context','padding','DP-1','20');let saved=fs.readFileSync(contextFile,'utf8');
  const duplicate=await failure();assert.match(duplicate.stderr,/already answers/);assert.equal(json(socket,'doctor')['bridge-connected'],true,'duplicate must leave existing daemon alive');
  const interrupted=once(h,'exit');h.kill('SIGINT');await interrupted;h=null;assert.equal(fs.readFileSync(contextFile,'utf8'),saved);assert.notEqual(call(socket,'doctor').status,0);
  fs.rmSync(path.dirname(socket),{recursive:true,force:true});settings.loaded=['blur',effect,'contrast'];write();fs.writeFileSync(log,'');await launch(['--enable-control']);assert.equal(json(socket,'doctor')['desktop-control'],true);assert.equal(json(socket,'doctor').management.enabled,false);assert.deepEqual(json(socket,'context','show','DP-1').padding,[20,20,20,20]);assert.equal(json(socket,'context','status','DP-1').history.undo,0);assert.ok(!fs.readFileSync(log,'utf8').includes('loadEffect"'));
  await stop(h);h=null;
  // Shell configuration runs after ready IPC, including paths with spaces and
  // metacharacters. Its CLI commands use this session's custom socket/PATH.
  const rc=path.join(dir,'config space;literal.sh');fs.writeFileSync(rc,'set -eu\nkwabai -m config window_border_radius 9\nkwabai doctor >/dev/null\n');
  await launch(['--enable-control','--config',rc]);assert.equal(call(socket,'-m','config','window_border_radius').stdout.trim(),'9');await stop(h);h=null;saved=fs.readFileSync(contextFile,'utf8');
  // The reported user rule must be accepted during service-style startup.
  fs.writeFileSync(rc,"set -eu\nkwabai -m rule --add label=whatsapp app='elecwhat' title='.*' scratchpad=whatsapp grid=11:11:1:1:9:9\n");
  await launch(['--enable-control','--config',rc]);assert.equal(JSON.parse(call(socket,'-m','rule','--list').stdout).find(r=>r.label==='whatsapp').grid,'11:11:1:1:9:9');assert.equal(call(socket,'-m','rule','--remove','whatsapp').status,0);await stop(h);h=null;saved=fs.readFileSync(contextFile,'utf8');
  fs.writeFileSync(rc,'#!/usr/bin/env python3\nimport os,subprocess\nassert os.environ["KWABAI_SOCKET"]\nsubprocess.run(["kwabai","-m","config","window_border_radius","7"],check=True)\n',{mode:0o755});fs.chmodSync(rc,0o755);
  await launch(['--enable-control','--config',rc]);assert.equal(call(socket,'-m','config','window_border_radius').stdout.trim(),'7');await stop(h);h=null;saved=fs.readFileSync(contextFile,'utf8');
  // Custom sockets propagate into shell commands, and service readiness is
  // notified only after configuration has finished successfully.
  const notifySocket=path.join(dir,'notify.sock'),notifyLog=path.join(dir,'notification'),customSocket=path.join(dir,'custom/control.sock');
  const receiver=await start('python3',['-c','import socket,pathlib; s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM); s.bind('+JSON.stringify(notifySocket)+'); print("ready",flush=True); pathlib.Path('+JSON.stringify(notifyLog)+').write_bytes(s.recv(4096))']);
  env.NOTIFY_SOCKET=notifySocket;fs.writeFileSync(rc,'#!/bin/sh\nset -eu\nkwabai -m config window_border_radius 8\n');await launch(['--enable-control','--config',rc,'--socket',customSocket]);await until(()=>fs.existsSync(notifyLog));assert.match(fs.readFileSync(notifyLog,'utf8'),/READY=1/);assert.equal(call(customSocket,'-m','config','window_border_radius').stdout.trim(),'8');assert.notEqual(call(socket,'doctor').status,0);await stop(h);h=null;await stop(receiver);delete env.NOTIFY_SOCKET;saved=fs.readFileSync(contextFile,'utf8');
  fs.writeFileSync(rc,'#!/bin/sh\nexit 17\n');const broken=await failure(['--enable-control','--config',rc]);assert.equal(broken.status,2,broken.stderr);assert.match(broken.stderr,/status 17/);assert.notEqual(call(socket,'doctor').status,0,'config failure cleans up its daemon');
  const pidFile=path.join(dir,'config-pid');fs.writeFileSync(rc,'#!/bin/sh\necho $$ > "'+pidFile+'"\nsleep 90\n');const timedConfig=await failure(['--config',rc,'--config-timeout','1']);assert.equal(timedConfig.status,2,timedConfig.stderr);assert.match(timedConfig.stderr,/Configuration timed out/);const configPid=Number(fs.readFileSync(pidFile,'utf8'));assert.throws(()=>process.kill(configPid,0));
  const missing=await failure(['--config',path.join(dir,'missing')]);assert.equal(missing.status,2);assert.match(missing.stderr,/does not exist/);
  // --no-config bypasses a broken discovered script.
  fs.writeFileSync(path.join(env.XDG_CONFIG_HOME,'kwabai/kwabairc'),'exit 99\n');await launch(['--no-config']);await stop(h);h=null;fs.writeFileSync(path.join(env.XDG_CONFIG_HOME,'kwabai/kwabairc'),'# empty\n');
  fs.writeFileSync(rc,fs.readFileSync(path.resolve(__dirname,'../examples/kwabairc')));fs.chmodSync(rc,0o600);await launch(['--enable-control','--config',rc]);await until(()=>{const r=call(socket,'context','status','DP-1');return r.status===0&&JSON.parse(r.stdout).management.state==='active';});assert.equal(call(socket,'-m','config','layout').stdout.trim(),'bsp');assert.deepEqual(json(socket,'context','show','DP-1').padding,[0,10,10,10]);assert.equal(JSON.parse(call(socket,'-m','rule','--list').stdout).length,2);await stop(h);h=null;saved=fs.readFileSync(contextFile,'utf8');
  settings.installed='6.8.3';write();fs.writeFileSync(log,'');const stale=await failure();assert.equal(stale.status,78,stale.stderr);assert.match(stale.stderr,/Rebuild required/);assert.ok(!fs.readFileSync(log,'utf8').includes('loadEffect"'),'version mismatch must not attempt native loading');
  settings.installed='6.8.2';settings.running='6.8.1';write();assert.equal((await failure()).status,78);settings.running='6.8.2';
  settings.unavailable=100;write();assert.match((await failure()).stderr,/login deadline/);settings.unavailable=0;
  settings.loaded=[effect,'kwabai_probe_old'];write();assert.match((await failure()).stderr,/older kwabai effect/);assert.notEqual(call(socket,'doctor').status,0);
  settings.loaded=[];settings.load=false;write();assert.match((await failure()).stderr,/reload-effect/);
  settings.loaded=[effect];settings.load=true;settings.snapshot={...s,capabilities:['snapshot']};write();assert.match((await failure()).stderr,/incompatible/);
  settings.snapshot=s;write();const insecure=path.join(dir,'insecure');fs.mkdirSync(insecure,{mode:0o755});fs.chmodSync(insecure,0o755);assert.match((await failure(['--state-dir',insecure])).stderr,/0700/);
  // Native preflight can succeed while daemon observation fails: timeout must clean up only its child.
  await stop(p);p=null;const timed=await failure();assert.match(timed.stderr,/timed out/);assert.notEqual(call(socket,'doctor').status,0);assert.equal(fs.readFileSync(contextFile,'utf8'),saved);
 }finally{await stop(h);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

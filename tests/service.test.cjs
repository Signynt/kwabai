const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const cli=process.argv[2],helper=path.resolve(__dirname,'../scripts/service.py');
test('service lifecycle flags install an inactive user unit, preserve state and refuse unmanaged files',()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-service-')),bin=path.join(dir,'mock-bin'),log=path.join(dir,'calls.jsonl'),config=path.join(dir,'config'),state=path.join(dir,'saved-state.json');fs.mkdirSync(bin);fs.writeFileSync(state,'persistent layouts');
 fs.writeFileSync(path.join(bin,'systemctl'),'#!/usr/bin/env python3\nimport json,sys\nwith open('+JSON.stringify(log)+',"a") as f: f.write(json.dumps(sys.argv[1:])+"\\n")\n',{mode:0o755});
 const env={...process.env,XDG_CONFIG_HOME:config,PATH:bin+path.delimiter+process.env.PATH};const call=(...args)=>spawnSync(cli,args,{encoding:'utf8',env,timeout:5000});const succeeds=(...args)=>{const r=call(...args);assert.equal(r.status,0,r.stderr);return r;};const unit=path.join(config,'systemd/user/kwabai.service');const calls=()=>fs.readFileSync(log,'utf8').trim().split('\n').map(s=>JSON.parse(s));
 try{
  succeeds('--install-service');let text=fs.readFileSync(unit,'utf8');assert.match(text,/Type=notify/);assert.match(text,/RestartPreventExitStatus=2 78/);assert.match(text,/RestartSec=15/);assert.match(text,/TimeoutStartSec=120/);assert.match(text,/WantedBy=graphical-session.target/);assert.match(text,/--enable-control --background/);assert.ok(!text.includes('RuntimeDirectory='),'service must not remove an unrelated foreground session socket directory');assert.deepEqual(calls(),[['--user','daemon-reload']]);
  succeeds('--start-service');succeeds('--restart-service');succeeds('--service-status');succeeds('--stop-service');assert.deepEqual(calls().slice(1),[['--user','reset-failed','kwabai.service'],['--user','enable','--now','kwabai.service'],['--user','reset-failed','kwabai.service'],['--user','restart','kwabai.service'],['--user','status','--no-pager','kwabai.service'],['--user','stop','kwabai.service']]);
  assert.equal(call('--start-service','extra').status,2);succeeds('--uninstall-service');assert.equal(fs.existsSync(unit),false);assert.equal(fs.readFileSync(state,'utf8'),'persistent layouts');assert.deepEqual(calls().slice(-2),[['--user','disable','--now','kwabai.service'],['--user','daemon-reload']]);
  fs.writeFileSync(unit,'# a manually maintained unit\n');const before=calls().length;assert.notEqual(call('--install-service').status,0);assert.notEqual(call('--uninstall-service').status,0);assert.equal(calls().length,before);assert.equal(fs.readFileSync(unit,'utf8'),'# a manually maintained unit\n');fs.unlinkSync(unit);fs.symlinkSync(state,unit);assert.notEqual(call('--install-service').status,0);assert.equal(fs.readFileSync(state,'utf8'),'persistent layouts');
  const special=path.join(dir,'build space%$"\\');const rendered=spawnSync('python3',[helper,'print','--build-dir',special],{encoding:'utf8',env});assert.equal(rendered.status,0,rendered.stderr);assert.match(rendered.stdout,/space%%\$\$\\"\\\\/);
  assert.equal(call('-c',path.join(dir,'missing')).status,2,'-c dispatches the same validated foreground launcher');
 }finally{fs.rmSync(dir,{recursive:true,force:true});}
});

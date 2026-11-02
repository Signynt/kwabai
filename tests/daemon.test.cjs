const {test}=require('node:test');
const assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const net=require('node:net');
const {once}=require('node:events');
const daemon=process.argv[2],cli=process.argv[3];
function temp(){return fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-daemon-'));}
function call(socket,...args){return spawnSync(cli,['--socket',socket,...args],{encoding:'utf8',timeout:5000});}
function json(socket,...args){const r=call(socket,...args);assert.equal(r.status,0,r.stderr);return JSON.parse(r.stdout);}
function launch(args){const p=spawn(daemon,args,{stdio:['ignore','pipe','pipe']});p.errors='';p.stderr.on('data',b=>p.errors+=b);return p;}
async function start(args){const p=launch(args);await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{p.kill('SIGKILL');reject(Error('readiness timeout: '+p.errors));},5000);p.once('exit',code=>{clearTimeout(timer);reject(Error('startup failed '+code+': '+p.errors));});p.stdout.once('data',b=>{clearTimeout(timer);try{assert.equal(JSON.parse(b).ready,true);resolve();}catch(e){reject(e);}});});return p;}
async function stop(p,signal='SIGTERM'){if(!p||p.exitCode!==null||p.signalCode!==null)return;const done=once(p,'exit');p.kill(signal);await done;}
function frame(obj){const body=Buffer.from(JSON.stringify(obj));const header=Buffer.alloc(4);header.writeUInt32BE(body.length);return Buffer.concat([header,body]);}
async function raw(socket,bytes,fragment=false){return new Promise((resolve,reject)=>{const s=net.connect(socket);let result=Buffer.alloc(0);s.setTimeout(5000,()=>{s.destroy();reject(Error('socket timed out'));});s.on('error',reject);s.on('connect',()=>{if(fragment){s.write(bytes.subarray(0,2));setTimeout(()=>s.write(bytes.subarray(2,7)),5);setTimeout(()=>s.write(bytes.subarray(7)),10);}else s.write(bytes);});s.on('data',b=>result=Buffer.concat([result,b]));s.on('end',()=>{try{assert.ok(result.length>=4);assert.equal(result.length,4+result.readUInt32BE());resolve(JSON.parse(result.subarray(4)));}catch(e){reject(e);}});});}
async function exitCode(args){const p=launch(args);const [code]=await once(p,'exit');return {code,errors:p.errors};}
test('daemon transactions persist across restart and preserve state after invalid mutations',async()=>{
 const dir=temp(),socket=path.join(dir,'control.sock'),state=path.join(dir,'state.json');let p;
 try {
  p=await start(['--socket',socket,'--simulate','--state',state]);
  assert.equal(fs.statSync(socket).mode&0o777,0o600);
  assert.equal(json(socket,'doctor').mode,'simulation');
  for(const args of [['add','1'],['add','2','1','east','0.6'],['add','3','1','south'],['stack','3','1']])assert.equal(call(socket,'model',...args).status,0);
  const before=json(socket,'inspect');assert.equal(before.generation,'4');assert.equal(before.windows.length,3);assert.equal(before.windows.filter(w=>w.visible).length,2);
  const disk=fs.readFileSync(state,'utf8');assert.equal(fs.statSync(state).mode&0o777,0o600);
  for(const args of [['add','1'],['gap','-1'],['area','0','0','5','5'],['warp','1','2','bad-direction'],['ratio','1','x','NaN']])assert.equal(call(socket,'model',...args).status,2);
  assert.deepEqual(json(socket,'inspect'),before);assert.equal(fs.readFileSync(state,'utf8'),disk);
  const duplicate=await exitCode(['--socket',socket,'--simulate']);assert.equal(duplicate.code,1);assert.equal(json(socket,'doctor').generation,'4');
  const stateConflict=await exitCode(['--socket',path.join(dir,'other.sock'),'--simulate','--state',state]);assert.equal(stateConflict.code,1);
  await stop(p);assert.equal(fs.existsSync(socket),false);
  p=await start(['--socket',socket,'--simulate','--state',state]);assert.deepEqual(json(socket,'inspect'),before);
  assert.equal(call(socket,'model','warp','3','2','west','0.4').status,0);assert.equal(json(socket,'doctor').generation,'5');
  await stop(p,'SIGKILL');p=await start(['--socket',socket,'--simulate','--state',state]);assert.equal(json(socket,'doctor').generation,'5');
 }finally{await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});
test('framing, request replay and concurrent clients operate on one ordered model',async()=>{
 const dir=temp(),socket=path.join(dir,'control.sock');let p;
 try {
  p=await start(['--socket',socket,'--simulate']);
  const request={version:1,id:'same-operation',args:['model','add','1']};
  const first=await raw(socket,frame(request),true);assert.equal(first.status,0);
  assert.deepEqual(await raw(socket,frame(request)),first);assert.equal(json(socket,'doctor').generation,'1');
  const conflict=await raw(socket,frame({...request,args:['model','add','2']}));assert.equal(conflict.status,2);
  for(const bad of [{version:2,id:'v2',args:['doctor']},{version:1,id:'bad',args:[42]},{version:1,id:'empty',args:[]}])assert.equal((await raw(socket,frame(bad))).status,2);
  const oversized=Buffer.alloc(4);oversized.writeUInt32BE(1024*1024+1);assert.equal((await raw(socket,oversized)).error,'invalid_frame');
  const invalid=Buffer.from([0,0,0,1,0x78]);assert.equal((await raw(socket,invalid)).error,'invalid_frame');
  await Promise.all(Array.from({length:10},(_,i)=>new Promise((resolve,reject)=>{const c=spawn(cli,['--socket',socket,'model','add',String(i+2)]);let err='';c.stderr.on('data',b=>err+=b);c.once('exit',code=>code===0?resolve():reject(Error(err)));c.once('error',reject);})));assert.equal(json(socket,'inspect').windows.length,11);assert.equal(json(socket,'doctor').generation,'11');
  const idle=net.connect(socket);await once(idle,'connect');idle.write(Buffer.from([0]));await once(idle,'close');assert.equal(json(socket,'doctor').generation,'11');
 }finally{await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});
test('disconnected mode rejects simulated and live mutations; startup protects files',async()=>{
 const dir=temp(),socket=path.join(dir,'control.sock');let p;
 try {
  p=await start(['--socket',socket]);assert.equal(json(socket,'doctor')['bridge-connected'],false);
  assert.equal(call(socket,'model','add','1').status,3);assert.equal(call(socket,'-m','window','--swap','2').status,1);
  assert.equal(json(socket,'inspect').generation,'0');await stop(p);
  fs.writeFileSync(socket,'do not replace');assert.equal((await exitCode(['--socket',socket])).code,1);assert.equal(fs.readFileSync(socket,'utf8'),'do not replace');fs.unlinkSync(socket);
  const bad=path.join(dir,'bad.json');fs.writeFileSync(bad,'{}',{mode:0o600});assert.equal((await exitCode(['--socket',socket,'--simulate','--state',bad])).code,2);assert.equal(fs.readFileSync(bad,'utf8'),'{}');
  fs.chmodSync(dir,0o755);assert.equal((await exitCode(['--socket',socket])).code,1);
 }finally{await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

test('failed durable commit leaves the model and generation untouched',async()=>{
 const dir=temp(),socket=path.join(dir,'control.sock'),store=path.join(dir,'store');let p;
 fs.mkdirSync(store,{mode:0o700});
 try {
  p=await start(['--socket',socket,'--simulate','--state',path.join(store,'state.json')]);
  const before=json(socket,'inspect');fs.chmodSync(store,0o500);
  const failed=call(socket,'model','add','1');assert.equal(failed.status,1,failed.stderr);assert.deepEqual(json(socket,'inspect'),before);
  fs.chmodSync(store,0o700);assert.equal(call(socket,'model','add','1').status,0);assert.equal(json(socket,'doctor').generation,'1');
  const idle=net.connect(socket);idle.on('error',()=>{});await once(idle,'connect');await stop(p);assert.equal(p.exitCode,0);idle.destroy();
 }finally{fs.chmodSync(store,0o700);await stop(p);fs.rmSync(dir,{recursive:true,force:true});}
});

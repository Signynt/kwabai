const {test}=require('node:test');
const assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const binary=process.argv[2];
function run(...args){return spawnSync(binary,args,{encoding:'utf8'});}
test('CLI saves and restores exact topology for both output orientations',()=>{
 const dir=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-cli-'));
 try {
  const file=path.join(dir,'session.json');
  const saved=run('layout-demo','--save',file);assert.equal(saved.status,0,saved.stderr);
  const restored=run('layout-demo','--load',file);assert.equal(restored.status,0,restored.stderr);assert.deepEqual(JSON.parse(restored.stdout),JSON.parse(saved.stdout));
  const portrait=run('layout-demo','--portrait','--load',file);assert.equal(portrait.status,0,portrait.stderr);
  const windows=JSON.parse(portrait.stdout).windows;assert.equal(windows.length,5);assert.equal(windows[0].frame.x,-1070);
  fs.writeFileSync(file,'{}');const bad=run('layout-demo','--load',file);assert.equal(bad.status,2);assert.equal(bad.stdout,'');
 } finally {fs.rmSync(dir,{recursive:true,force:true});}
});
test('CLI rejects unsupported commands and malformed options',()=>{
 for(const args of [['layout-demo','--save'],['layout-demo','--save','--portrait'],['layout-demo','--unknown'],['layout-demo','--save','a','--save','b']]){
  const result=run(...args);assert.equal(result.status,2,JSON.stringify(args));assert.equal(result.stdout,'');
 }
});

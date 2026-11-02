const {test}=require('node:test'),assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
test('KWin upgrade reload preflights metadata, cleans stale builds and never unloads an incompatible bridge',()=>{
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'kwabai-upgrade-')),scripts=path.join(root,'scripts'),bin=path.join(root,'bin'),build=path.join(root,'build'),state=path.join(root,'state.json'),log=path.join(root,'calls');
 fs.mkdirSync(scripts);fs.mkdirSync(bin);fs.mkdirSync(build);
 for(const name of ['reload-effect.sh','native_compat.py'])fs.copyFileSync(path.resolve(__dirname,'../scripts',name),path.join(scripts,name));
 let config;
 const write=()=>fs.writeFileSync(state,JSON.stringify(config));
 const stub=`#!/usr/bin/env python3
import json,sys,pathlib,os
root=pathlib.Path(${JSON.stringify(root)}); state=root/'state.json'; c=json.loads(state.read_text()); name=pathlib.Path(sys.argv[0]).name
with open(root/'calls','a') as f: f.write(json.dumps([name]+sys.argv[1:])+'\\n')
build=root/'build'
def artifact():
 data={'schema':1,'kwin-version':c['installed'],'effect-id':'kwabai_probe_kwin_'+c['installed'].replace('.','_')+'_test'}
 (build/'kwabai-native-build.json').write_text(json.dumps(data));(build/'kwabai-effect-id').write_text(data['effect-id']+'\\n')
 p=build/'plugins/kwin/effects/plugins';p.mkdir(parents=True,exist_ok=True);(p/(data['effect-id']+'.so')).write_text('fixture')
if name=='kwin_wayland': print('kwin '+c['installed'])
elif name=='cmake':
 if '--build' not in sys.argv: artifact()
 if '--clean-first' in sys.argv and not c.get('permanent'): c['bad']=False;state.write_text(json.dumps(c))
elif name=='kwabai-plugin-check':
 if c.get('bad'): print('stale embedded plugin IID',file=sys.stderr);sys.exit(1)
 data=json.loads((build/'kwabai-native-build.json').read_text())
 if c.get('staleChecker'): data['kwin-version']='6.8.2'
 print(json.dumps(data))
elif name=='qdbus6':
 method=sys.argv[3]
 if method.endswith('supportInformation'): print('KWin version: '+c['running'])
 elif method.endswith('loadedEffects'): print('blur\\nkwabai_probe_old')
 elif method.endswith('unloadEffect'): pass
 elif method.endswith('loadEffect'): print('true')
 elif method.endswith('Snapshot'): print(json.dumps({'capabilities':['contexts','events','geometry','geometry-no-restore','geometry-update','geometry-retention','geometry-lifecycle','notifications','focus','geometry-release','output-move','desktop-move','mouse'],'geometry-window-limit':20,'outputs':[]}))
 else: sys.exit(1)
elif name=='sudo': pass
else: sys.exit(1)
`;
 for(const name of ['cmake','kwin_wayland','qdbus6','sudo'])fs.writeFileSync(path.join(bin,name),stub,{mode:0o755});fs.writeFileSync(path.join(build,'kwabai-plugin-check'),stub,{mode:0o755});
 const env={...process.env,PATH:bin+path.delimiter+process.env.PATH};
 const run=()=>spawnSync('sh',[path.join(scripts,'reload-effect.sh')],{encoding:'utf8',env,timeout:12000});
 const calls=()=>fs.readFileSync(log,'utf8').trim().split('\n').map(JSON.parse);
 const reset=(extra={})=>{config={installed:'6.8.2',running:'6.8.2',bad:false,...extra};write();fs.writeFileSync(log,'');};
 const unsafe=()=>calls().filter(a=>a[0]==='sudo'||a.some(s=>s.endsWith('unloadEffect'))||a.some(s=>s.endsWith('loadEffect')));
 try{
  reset();let r=run();assert.equal(r.status,0,r.stderr);assert.ok(calls().some(a=>a.includes('--fresh')));assert.ok(calls().some(a=>a.includes('--clean-first')));
  let seq=calls();assert.ok(seq.findIndex(a=>a[0]==='kwabai-plugin-check')<seq.findIndex(a=>a[0]==='sudo'));
  reset();r=run();assert.equal(r.status,0,r.stderr);assert.ok(!calls().some(a=>a.includes('--clean-first')),'matching healthy build remains incremental');
  reset({installed:'6.8.3',running:'6.8.3'});r=run();assert.equal(r.status,0,r.stderr);assert.ok(calls().some(a=>a.includes('--fresh')));
  reset({installed:'6.8.3',running:'6.8.3',bad:true});r=run();assert.equal(r.status,0,r.stderr);assert.ok(calls().some(a=>a.includes('--clean-first')),'stale metadata forces a clean retry');
  reset({installed:'6.8.3',running:'6.8.3',bad:true,permanent:true});r=run();assert.equal(r.status,78,r.stderr);assert.match(r.stderr,/rebuild required/);assert.deepEqual(unsafe(),[],'failed preflight must leave installed and loaded effects alone');
  reset({installed:'6.8.3',running:'6.8.3',staleChecker:true});r=run();assert.equal(r.status,78,r.stderr);assert.match(r.stderr,/Embedded native build identity is stale/);assert.deepEqual(unsafe(),[]);
  reset({installed:'6.8.3',running:'6.8.3'});fs.writeFileSync(path.join(build,'kwabai-native-build.json'),'{broken');r=run();assert.equal(r.status,0,r.stderr);assert.ok(calls().some(a=>a.includes('--fresh')));
  reset({installed:'6.8.3',running:'6.8.2'});r=run();assert.equal(r.status,78,r.stderr);assert.match(r.stderr,/session restart required/);assert.deepEqual(unsafe(),[]);
 }finally{fs.rmSync(root,{recursive:true,force:true});}
});

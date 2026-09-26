// Test-only upstream GPU capture. Dependencies stay outside the shipped player.
const fs = require('node:fs');
const path = require('node:path');
const [rootArg, depsArg, outputArg] = process.argv.slice(2);
const root = path.resolve(rootArg), deps = path.resolve(depsArg), output = path.resolve(outputArg);
(async () => {
  const { build } = require(path.join(deps, 'esbuild'));
  const browserSource = `
    import * as THREE from 'three';
    import {MapShaderMaterial} from ${JSON.stringify(path.join(root,'src/components/AudioVisualizer/CustomShaderMaterial.ts'))};
    import {themes} from ${JSON.stringify(path.join(root,'src/lib/themes.ts'))};
    import {deriveTerrainGridSettings} from ${JSON.stringify(path.join(root,'src/lib/groundEqSettings.ts'))};
    import {DEFAULT_CAMERA_POSITION} from ${JSON.stringify(path.join(root,'src/lib/sceneDefaults.ts'))};
    window.capture = (themeId, active) => {
      const grid = deriveTerrainGridSettings(46);
      // Matched single-sample baseline isolates shader/geometry from differing
      // driver MSAA resolve patterns; the live player still uses 4x MSAA.
      const renderer = new THREE.WebGLRenderer({alpha:true,antialias:false,preserveDrawingBuffer:true});
      renderer.setSize(1920,1080); renderer.setClearColor(0,0);
      const camera = new THREE.PerspectiveCamera(45,1920/1080,.1,1000);
      camera.position.fromArray(DEFAULT_CAMERA_POSITION); camera.lookAt(0,0,0); camera.updateMatrixWorld();
      const raw = {...MapShaderMaterial.uniforms,...themes[themeId],uTime:2,uSmoothness:1};
      delete raw.name; delete raw.id;
      if(active) Object.assign(raw,{uSubBass:1.05,uBass:.97,uLowMid:.3,uMid:.26,uHighMid:.3,
        uPresence:.5,uBrilliance:.3,uAir:.2,uWarmth:.8,uBrightness:.2,uSharpness:1.7,uDensity:.625,uEnergy:.25});
      raw.uRipples = Array.from({length:10},(_,i)=>({pos:new THREE.Vector2(i===0?8:-12,i===0?3:16),
        time:1.4,strength:active&&i<2?.9:0,isActive:active&&i<2?1:0,rippleType:i%2}));
      const uniforms = Object.fromEntries(Object.entries(raw).map(([k,v])=>[k,{value:v}]));
      const material = new THREE.ShaderMaterial({uniforms, vertexShader:MapShaderMaterial.vertexShader,
        fragmentShader:MapShaderMaterial.fragmentShader,transparent:true});
      const geometry = new THREE.BoxGeometry(grid.boxWidth,1,grid.boxWidth);
      const mesh = new THREE.InstancedMesh(geometry,material,grid.instanceCount);
      let n=0; const m = new THREE.Matrix4();
      for(let x=0;x<grid.gridSize;x++) for(let z=0;z<grid.gridSize;z++)
        mesh.setMatrixAt(n++,m.makeTranslation(x*grid.spacing-84,.5,z*grid.spacing-84));
      const scene = new THREE.Scene(); scene.add(mesh); renderer.render(scene,camera);
      const serialized={};
      for(const [key,value] of Object.entries(raw)) serialized[key]=value?.isColor?{rgb:value.toArray()}
        :key==='uRipples'?value.map(r=>({...r,pos:r.pos.toArray()})):value;
      const identity=new THREE.Matrix4().toArray();
      const fixture={schema:1,input:'reference-uniform-fixture',themeId,config:{grid},
        camera:{position:camera.position.toArray()},geometry:{type:'BoxGeometry',parameters:geometry.parameters},
        instances:{count:grid.instanceCount,matrices:Array.from(mesh.instanceMatrix.array)},uniforms:serialized,
        matrices:{model:identity,world:identity,modelView:camera.matrixWorldInverse.toArray(),
          view:camera.matrixWorldInverse.toArray(),cameraWorld:camera.matrixWorld.toArray(),
          projection:camera.projectionMatrix.toArray(),normal:new THREE.Matrix3().getNormalMatrix(camera.matrixWorldInverse).toArray()}};
      const png=renderer.domElement.toDataURL('image/png').split(',')[1];
      const gl=renderer.getContext(); const gpu=gl.getParameter(gl.RENDERER);
      geometry.dispose();material.dispose();renderer.dispose();renderer.forceContextLoss();
      return {fixture,png,gpu};
    };`;
  const bundle = await build({stdin:{contents:browserSource,resolveDir:root,loader:'ts'},bundle:true,format:'iife',write:false,
    plugins:[{name:'reference-only-adapter',setup(b){
      b.onResolve({filter:/^three$/},()=>({path:path.join(deps,'three/build/three.module.js')}));
      b.onResolve({filter:/^@react-three\/drei$/},()=>({path:'shader',namespace:'adapter'}));
      b.onLoad({filter:/.*/,namespace:'adapter'},()=>({contents:'export const shaderMaterial=(uniforms,vertexShader,fragmentShader)=>({uniforms,vertexShader,fragmentShader});'}));
    }}]});
  const {chromium}=require(path.join(deps,'playwright-core'));
  const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,
    args:['--use-angle=d3d11','--enable-webgl','--ignore-gpu-blocklist']});
  try {
    const page=await browser.newPage();
    page.on('pageerror',e=>console.error(e));
    await page.addScriptTag({content:bundle.outputFiles[0].text});
    fs.mkdirSync(output,{recursive:true});
    for(const theme of ['minimal-monochrome','neon-tokyo','glacier-day']) for(const active of [false,true]) {
      const result=await page.evaluate(([theme,active])=>window.capture(theme,active),[theme,active]);
      const name=theme+(active?'-active':'-idle');
      fs.writeFileSync(path.join(output,name+'.json'),JSON.stringify(result.fixture));
      fs.writeFileSync(path.join(output,name+'.png'),Buffer.from(result.png,'base64'));
      console.log(name,result.fixture.instances.count,result.gpu);
    }
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});

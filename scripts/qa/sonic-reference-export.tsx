import React from 'react';
import {createRoot, advance, extend} from '@react-three/fiber';
import * as THREE from 'three';
import {MapScene} from './src/components/AudioVisualizer/MapScene';
import {engine} from './src/lib/AudioEngine';
import {themes} from './src/lib/themes';
import {DEFAULT_CAMERA_POSITION} from './src/lib/sceneDefaults';

const theme = new URLSearchParams(location.search).get('theme') || 'nocturnal';
extend(THREE);
const eq = {bands:[90,92,50,50,50,50,50,48],motionSpeed:50,amplitude:50,terrainDensity:46,floatingBlocksEnabled:false};
let state:any;
const root = createRoot(document.querySelector('canvas')!);
await root.configure({frameloop:'never',dpr:1,size:{width:1920,height:1080,top:0,left:0},camera:{position:DEFAULT_CAMERA_POSITION,fov:45},gl:{preserveDrawingBuffer:true},onCreated:s=>state=s});
root.render(<MapScene themeColors={themes[theme]} groundEqSettings={eq} rotationSpeed={0} lyricsVisible={false}/>);
while(!state || !state.scene.children.length) await new Promise(r=>setTimeout(r,20));
await new Promise(r=>setTimeout(r,150));
const sampleRate=48000, finalSample=96000;
const ctx=new OfflineAudioContext(2,finalSample+1024,sampleRate);
const encoded=await (await fetch('/demo.mp3')).arrayBuffer();
const decoded=await ctx.decodeAudioData(encoded);
const analyser=ctx.createAnalyser(); analyser.fftSize=1024; analyser.smoothingTimeConstant=.8; analyser.minDecibels=-75;
const source=ctx.createBufferSource(); source.buffer=decoded; source.connect(analyser); analyser.connect(ctx.destination);source.start();
Object.assign(engine,{audioCtx:ctx,analyser,dataArray:new Uint8Array(512),isPlaying:true});
const trace:any[]=[];let analysisCalls=0,frequencyReads=0;
let actualFrameDelta:number|null=null;
for(const subscription of state.internal.subscribers){
 const callback=subscription.ref.current;
 subscription.ref.current=(s:any,delta:number,xrFrame:any)=>{
  actualFrameDelta=delta;return callback(s,delta,xrFrame);
 };
}
const originalData=engine.getAudioData.bind(engine);
const originalFrequency=analyser.getByteFrequencyData.bind(analyser);
analyser.getByteFrequencyData=(array:any)=>{frequencyReads++;originalFrequency(array);};
engine.getAudioData=()=>{analysisCalls++;const data=originalData();const timeDomain=new Float32Array(1024);analyser.getFloatTimeDomainData(timeDomain);trace.push({sampleIndex:Math.round(ctx.currentTime*sampleRate),time:now,analysisCalls,frequencyReads,spectrum:Array.from((engine as any).dataArray),timeDomain:Array.from(timeDomain),descriptors:{...data},terrainResponse:{deltaSeconds:actualFrameDelta}});return data;};
let now=0; Object.defineProperty(performance,'now',{value:()=>now*1000,configurable:true});
state.clock.getDelta=()=>0; state.clock.getElapsedTime=()=>state.clock.elapsedTime;
const snapshots:any[]=[];
function renderContext(){
 // Original App passes only camera to Canvas. Report actual context state,
 // since requesting antialias does not prove any particular sample count.
 const gl=state.gl.getContext();
 return {samples:gl.getParameter(gl.SAMPLES),sampleBuffers:gl.getParameter(gl.SAMPLE_BUFFERS),
  contextAttributes:gl.getContextAttributes(),drawingBufferWidth:gl.drawingBufferWidth,
  drawingBufferHeight:gl.drawingBufferHeight,defaultFramebuffer:gl.getParameter(gl.FRAMEBUFFER_BINDING)===null,
  version:gl.getParameter(gl.VERSION),renderer:gl.getParameter(gl.RENDERER),
  outputColorSpace:state.gl.outputColorSpace,toneMapping:state.gl.toneMapping,
  toneMappingExposure:state.gl.toneMappingExposure,pixelRatio:state.gl.getPixelRatio(),
  capture:'Canvas.toDataURL image/png; straight-alpha PNG',
  harnessOverrides:'DPR1, 1920x1080, manual frame clock, preserveDrawingBuffer=true'};
}
function snapshot(sampleIndex:number){
 state.scene.updateMatrixWorld(true);state.camera.updateMatrixWorld(true);
 let mesh:any; state.scene.traverse((o:any)=>{if(o.isInstancedMesh && o.count===24025)mesh=o;});
 if(!mesh)throw Error('reference terrain mesh missing');
 const uniforms:any={};for(const [key,entry] of Object.entries(mesh.material.uniforms) as any){
  if(key==='uTreble')continue;const v=entry.value;
  uniforms[key]=v?.isColor?{rgb:v.toArray()}:key==='uRipples'?v.map((r:any)=>({...r,pos:r.pos.toArray(),rippleType:r.rippleType||0})):v;
 }
return {schema:1,renderContext:renderContext(),input:'reference-uniform-fixture',provenance:{commit:'ec8ecbaec0c9c5094b6b1480df0d6b2d32d6349b',source:'demo.mp3',audio:'real decoded PCM via OfflineAudioContext analyser; original AudioEngine and MapScene',sampleRate,sampleIndex,theme},matrices:{model:mesh.matrix.toArray(),world:mesh.matrixWorld.toArray(),view:state.camera.matrixWorldInverse.toArray(),cameraWorld:state.camera.matrixWorld.toArray(),projection:state.camera.projectionMatrix.toArray(),normal:new THREE.Matrix3().getNormalMatrix(new THREE.Matrix4().multiplyMatrices(state.camera.matrixWorldInverse,mesh.matrixWorld)).toArray()},camera:{position:state.camera.position.toArray()},uniforms,geometry:{type:'BoxGeometry',parameters:mesh.geometry.parameters},config:{grid:{terrainSize:168}},instances:{count:mesh.count,matrices:Array.from(mesh.instanceMatrix.array)},png:state.gl.domElement.toDataURL('image/png')};
}
// 768 samples = 16ms, a render-quantum-exact deterministic cadence. Include 1s/2s explicitly.
async function run(){
 const indices=[...new Set([...Array.from({length:125},(_,i)=>(i+1)*768),48000,96000])].sort((a,b)=>a-b);
 for(const index of indices){const paused=ctx.suspend(index/sampleRate);if(index===indices[0])ctx.startRendering();else ctx.resume();await paused;now=ctx.currentTime;Object.assign(engine,{cachedFrameData:null,currentFrameId:index});advance(now,false,state);
  let material:any;state.scene.traverse((o:any)=>{if(o.isInstancedMesh&&o.count===24025)material=o.material;});
  if(!material||!trace.length||trace.at(-1).terrainResponse.deltaSeconds===null)throw Error('Missing actual MapScene frame callback');
  const fields=['uSubBass','uBass','uLowMid','uMid','uHighMid','uPresence','uBrilliance','uAir','uEnergy','uWarmth','uBrightness','uSharpness','uSmoothness','uDensity','uSpectralCentroid'];
  trace.at(-1).terrainResponse.uniforms=Object.fromEntries(fields.map(key=>[key,material.uniforms[key].value]));
  if(index===48000||index===96000)snapshots.push(snapshot(Math.round(now*sampleRate)));
 }
 ctx.resume();(window as any).parityTrace={sampleRate,channels:decoded.numberOfChannels,analysisCalls,frequencyReads,terrainSettings:{bands:eq.bands,enabledBands:Array(8).fill(true),motionSpeed:eq.motionSpeed},frames:trace,decodedChannels:Array.from({length:decoded.numberOfChannels},(_,c)=>Array.from(decoded.getChannelData(c).slice(0,finalSample+1024)))};(window as any).parityResult=snapshots;
}
run().catch(e=>(window as any).parityError=String(e.stack||e));

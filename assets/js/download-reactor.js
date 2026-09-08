/*
  Central Sonic Topography reactor algorithm adapted from:
  1) yin-yizhen/sonic-topography 1.1.1 (commit 3ff303e)
     License: Non-Commercial Learning License
     https://github.com/yin-yizhen/sonic-topography
  2) ww085213/Mineradio-LX-Music public/sonic-topography-preset.js
     License: GPL-3.0-only
     https://github.com/ww085213/Mineradio-LX-Music

  The player UI, settings, lyrics stage and waveform remain AgPlayer prototype code.
  This port is for learning/research/personal non-commercial evaluation unless
  the relevant copyright holders grant additional permission and all applicable
  GPL source/distribution obligations are satisfied.
*/
(() => {
'use strict';
const canvas=document.getElementById('download-reactor');
if(!canvas||!window.THREE)return;
const clamp=(v,a=0,b=1)=>Math.max(a,Math.min(b,v));
let renderer;
try{renderer=new THREE.WebGLRenderer({canvas,antialias:false,alpha:true,powerPreference:'low-power'});}catch(e){return;}
renderer.setPixelRatio(Math.min(devicePixelRatio||1,1.25));
const scene=new THREE.Scene(),camera=new THREE.PerspectiveCamera(46,1,.1,560),root=new THREE.Group();
scene.add(root);root.position.y=-8;
const SONIC_RIPPLE_MAX=10;
const SONIC_RIPPLE_LIFETIME=4.8;
const SONIC_RIPPLE_FADE_START=2.1;
const SONIC_GRID_SIZE=112;
const SONIC_BASE_SIZE=168;
const SONIC_FLOATING_COUNT=80;
const SONIC_DEFAULT_EQ=[90,92,50,50,50,50,50,48];
const sonicState={
  time:0,ripples:Array.from({length:SONIC_RIPPLE_MAX},()=>({x:0,z:0,start:-100,strength:0,white:false,color:new THREE.Color('#ffffff')})),rippleIndex:0,
  lastKickActive:false,lastSnareActive:false,floatingPulse:0,
  smooth:{subBass:0,bass:0,lowMid:0,mid:0,highMid:0,presence:0,brilliance:0,air:0},
  dummyPos:new THREE.Vector3(),dummyQuat:new THREE.Quaternion(),dummyScale:new THREE.Vector3(),dummyMat4:new THREE.Matrix4(),dummyEuler:new THREE.Euler(),
  floatingData:[]
};
const sonicSmoothstep01=v=>{const t=clamp(v);return t*t*(3-2*t)};
function sonicApplyEq(value,index,max=1){const eq=SONIC_DEFAULT_EQ[index],delta=(eq-50)/50;if(delta>=0)return clamp(value*(1+delta*1.8),0,max);const dull=Math.abs(delta);return clamp(Math.max(0,value-dull*.35)*(1-dull*.35),0,max)}

const sonicTerrainVS=`
precision highp float;
uniform float uTime;
uniform float uSubBass;
uniform float uBass;
uniform float uLowMid;
uniform float uMid;
uniform float uHighMid;
uniform float uSmoothness;
uniform float uDensity;
uniform float uEnergy;
uniform float uAmplitude;
uniform vec4 uRipples[${SONIC_RIPPLE_MAX}];
uniform vec3 uRippleColors[${SONIC_RIPPLE_MAX}];
varying vec2 vUv;
varying float vElevation;
varying float vDistance;
varying vec2 vRippleAnim;
varying vec3 vRippleColorMix;
varying vec3 vNormal;
varying float vRelativeY;
varying vec2 vInstancePos;
vec3 mod289(vec3 x){return x-floor(x*(1.0/289.0))*289.0;}
vec2 mod289(vec2 x){return x-floor(x*(1.0/289.0))*289.0;}
vec3 permute(vec3 x){return mod289(((x*34.0)+1.0)*x);}
float snoise(vec2 v){
  const vec4 C=vec4(0.211324865405187,0.366025403784439,-0.577350269189626,0.024390243902439);
  vec2 i=floor(v+dot(v,C.yy));
  vec2 x0=v-i+dot(i,C.xx);
  vec2 i1=(x0.x>x0.y)?vec2(1.0,0.0):vec2(0.0,1.0);
  vec4 x12=x0.xyxy+C.xxzz;x12.xy-=i1;
  i=mod289(i);
  vec3 p=permute(permute(i.y+vec3(0.0,i1.y,1.0))+i.x+vec3(0.0,i1.x,1.0));
  vec3 m=max(0.5-vec3(dot(x0,x0),dot(x12.xy,x12.xy),dot(x12.zw,x12.zw)),0.0);
  m=m*m;m=m*m;
  vec3 x=2.0*fract(p*C.www)-1.0;
  vec3 h=abs(x)-0.5;
  vec3 ox=floor(x+0.5);
  vec3 a0=x-ox;
  m*=1.79284291400159-0.85373472095314*(a0*a0+h*h);
  vec3 g;g.x=a0.x*x0.x+h.x*x0.y;g.yz=a0.yz*x12.xz+h.yz*x12.yw;
  return 130.0*dot(m,g);
}
float random(vec2 st){return fract(sin(dot(st.xy,vec2(12.9898,78.233)))*43758.5453123);}
void main(){
  vUv=uv;
  vNormal=normal;
  vec4 instancePos=instanceMatrix*vec4(0.0,0.0,0.0,1.0);
  vec2 pos2D=instancePos.xz;
  vInstancePos=pos2D;
  float centerDist=length(pos2D);
  vDistance=centerDist;
  float rnd=random(pos2D);
  vec2 movingPos=pos2D*0.05+vec2(uTime*0.1,uTime*0.05);
  float baseNoise=(snoise(movingPos)+1.0)*0.5;
  float wave=sin(pos2D.x*0.15+pos2D.y*0.1-uTime*0.6)*0.5+0.5;
  float globalFalloff=smoothstep(60.0,30.0,centerDist);
  float idleElevation=mix(baseNoise,wave,uSmoothness*0.5+0.2)*0.8*globalFalloff;
  float subRegion=smoothstep(25.0,0.0,centerDist);
  float subLift=uSubBass*subRegion*5.0;
  float bassNoise=snoise(pos2D*0.1-vec2(0.0,uTime*0.2));
  float bassRegion=smoothstep(35.0,5.0,centerDist+bassNoise*5.0);
  float bassLift=uBass*bassRegion*(smoothstep(0.0,1.0,rnd+uDensity*0.5))*4.0;
  float lowMidNoise=snoise(pos2D*0.05+vec2(uTime*0.1,0.0));
  float lowMidLift=uLowMid*(lowMidNoise*0.5+0.5)*2.5;
  float riverFlow=sin(pos2D.x*0.2+pos2D.y*0.2+snoise(pos2D*0.1)*2.0-uTime*2.0);
  float midLift=uMid*max(0.0,riverFlow)*3.0;
  float highMidRegion=smoothstep(10.0,45.0,centerDist);
  float highMidLift=0.0;
  if(fract(rnd*13.3)>0.8){highMidLift=uHighMid*highMidRegion*fract(rnd*7.7)*2.5;}
  float audioElevation=subLift+bassLift+lowMidLift+midLift+highMidLift;
  if(rnd>0.99){audioElevation+=uEnergy*5.0;}
  audioElevation*=globalFalloff;
  audioElevation=max(0.0,audioElevation-0.2);
  audioElevation*=uAmplitude;
  float elevation=idleElevation+audioElevation;
  float rippleElevation=0.0;
  float rippleIntensityNormal=0.0;
  float rippleIntensityWhite=0.0;
  vec3 rippleColorAccum=vec3(0.0);
  float rippleColorWeight=0.0;
  for(int i=0;i<${SONIC_RIPPLE_MAX};i++){
    vec4 rd=uRipples[i];
    if(rd.w!=0.0){
      float strength=abs(rd.w);
      bool whiteRipple=rd.w<0.0;
      float dist=length(pos2D-rd.xy);
      float timeSince=uTime-rd.z;
      float curSpeed=whiteRipple?18.0:13.0;
      float curWidth=whiteRipple?1.35:5.5;
      float curFadeDist=whiteRipple?12.0:26.0;
      float elevationScale=whiteRipple?1.15:3.35;
      float waveRadius=timeSince*curSpeed;
      float d=dist-waveRadius;
      float rippleWave=exp(-d*d/curWidth);
      float fade=exp(-waveRadius/curFadeDist);
      float lifeFade=1.0-smoothstep(2.10,4.80,timeSince);
      float rPulse=rippleWave*fade*lifeFade*strength;
      rippleElevation+=rPulse*elevationScale;
      rippleColorAccum+=uRippleColors[i]*rPulse;
      rippleColorWeight+=rPulse;
      if(whiteRipple){rippleIntensityWhite+=rPulse;}else{rippleIntensityNormal+=rPulse;}
    }
  }
  elevation+=rippleElevation;
  vRippleColorMix=rippleColorWeight>0.0001?rippleColorAccum/rippleColorWeight:vec3(1.0);
  vRippleAnim=vec2(clamp(rippleIntensityNormal,0.0,1.0),clamp(rippleIntensityWhite,0.0,1.0));
  vElevation=elevation;
  float yPos=position.y+0.5;
  vRelativeY=yPos;
  float totalHeight=1.0+elevation;
  vec3 pos=position;
  pos.y=-0.5+yPos*totalHeight;
  vec4 worldPosition=modelMatrix*instanceMatrix*vec4(pos,1.0);
  gl_Position=projectionMatrix*viewMatrix*worldPosition;
}`;

const sonicTerrainFS=`
precision highp float;
uniform float uTime;
uniform float uPresence;
uniform float uBrilliance;
uniform float uAir;
uniform float uWarmth;
uniform float uBrightness;
uniform float uSharpness;
uniform vec3 uBaseColor1;
uniform vec3 uBaseColor2;
uniform vec3 uFogColor;
uniform vec3 uCoolCore;
uniform vec3 uCoolEdge;
uniform vec3 uWarmCore;
uniform vec3 uWarmEdge;
uniform vec3 uRippleColor;
uniform float uGlowIntensity;
uniform float uClarity;
uniform float uSoftGlow;
uniform float uCenterHighlight;
uniform float uDepthFocus;
varying vec2 vUv;
varying float vElevation;
varying float vDistance;
varying vec2 vRippleAnim;
varying vec3 vRippleColorMix;
varying vec3 vNormal;
varying float vRelativeY;
varying vec2 vInstancePos;
float random(vec2 st){return fract(sin(dot(st.xy,vec2(12.9898,78.233)))*43758.5453123);}
void main(){
  bool isTop=vNormal.y>0.5;
  float distFromTop=1.0-vRelativeY;
  float rnd=random(vInstancePos);
  float centerDist=length(vInstancePos);
  float normElevation=clamp(vElevation/8.0,0.0,1.0);
  vec3 cBase1=uBaseColor1;
  vec3 cBase2=uBaseColor2;
  float warmBlend=smoothstep(0.0,1.0,uWarmth*1.5+(0.5-centerDist/80.0));
  vec3 zoneCore=mix(uCoolCore,uWarmCore,warmBlend);
  vec3 zoneEdge=mix(uCoolEdge,uWarmEdge,warmBlend);
  vec3 targetGlow=mix(zoneCore,zoneEdge,fract(rnd*11.0));
  float distFade=1.0-smoothstep(40.0,75.0,centerDist);
  vec3 brightCool=mix(uCoolCore,vec3(1.0),0.24);
  targetGlow=mix(targetGlow,brightCool,uBrightness*0.6);
  float centerMask=1.0-smoothstep(4.0,38.0,centerDist);
  vec3 currentGlow=mix(cBase2,targetGlow,normElevation)*uGlowIntensity*distFade*mix(0.42,1.0,uSoftGlow);
  currentGlow*=1.0+centerMask*uCenterHighlight*1.35;
  currentGlow=mix(currentGlow,vRippleColorMix,clamp(vRippleAnim.x*0.88,0.0,0.78));
  currentGlow=mix(currentGlow,mix(vRippleColorMix,vec3(1.0),0.22),clamp(vRippleAnim.y,0.0,0.92));
  vec3 bodyColor=mix(cBase1,cBase2,vRelativeY*distFade);
  vec3 finalColor;
  if(isTop){
    float topIntensity=smoothstep(0.0,0.4,normElevation);
    float twinkleDistFalloff=smoothstep(60.0,30.0,centerDist);
    float twinkleMultiplier=mix(twinkleDistFalloff,1.0,smoothstep(0.01,0.1,normElevation));
    if(fract(rnd*31.0)>0.95&&normElevation<0.1){topIntensity+=uAir*2.0*twinkleMultiplier;}
    finalColor=mix(cBase2,currentGlow,topIntensity);
    float edgeX=smoothstep(0.05,0.01,vUv.x)+smoothstep(0.95,0.99,vUv.x);
    float edgeY=smoothstep(0.05,0.01,vUv.y)+smoothstep(0.95,0.99,vUv.y);
    float edge=min(edgeX+edgeY,1.0);
    finalColor+=currentGlow*edge*mix(0.34,0.92,uClarity)*(topIntensity+0.20);
    float flashChance=smoothstep(0.3,1.0,uPresence);
    if(fract(rnd*53.0)>0.98-flashChance*0.1){
      float flashSync=sin(uTime*40.0+rnd*100.0)*0.5+0.5;
      finalColor+=mix(vec3(1.0),vec3(0.5,1.0,1.0),rnd)*flashSync*uPresence*(1.0+uSharpness*2.0)*twinkleMultiplier;
    }
    if(edge>0.5&&fract(rnd*89.0+uTime*2.0)>0.98){finalColor+=vec3(1.0)*uBrilliance*3.0*twinkleMultiplier;}
  }else{
    float verticalFalloff=mix(1.0,3.0,uSharpness);
    float sideGlow=smoothstep(0.5/verticalFalloff,0.0,distFromTop)*normElevation;
    if(normElevation<0.02)sideGlow=0.0;
    finalColor=mix(bodyColor,currentGlow,sideGlow*mix(0.86,1.34,uClarity));
    float rimGlow=smoothstep(0.03,0.0,distFromTop)*normElevation;
    finalColor+=currentGlow*rimGlow;
  }
  finalColor+=vRippleColorMix*vRippleAnim.x*mix(0.58,0.96,uSoftGlow);
  finalColor+=mix(vRippleColorMix,vec3(1.0),0.24)*vRippleAnim.y*1.28;
  float aerialFog=smoothstep(30.0,65.0,vDistance);
  vec3 atmosphericColor=mix(cBase1,cBase2,0.4);
  finalColor=mix(finalColor,atmosphericColor,aerialFog*0.35);
  float focusStart=mix(58.0,24.0,clamp(uDepthFocus,0.0,1.0));
  float depthVeil=smoothstep(focusStart,76.0,vDistance)*clamp(uDepthFocus,0.0,1.0);
  finalColor=mix(finalColor,uFogColor,depthVeil*0.58);
  float alphaFade=1.0-smoothstep(55.0,78.0,vDistance);
  float alphaBlend=1.0-alphaFade;
  finalColor=mix(finalColor,uFogColor,alphaBlend*0.45);
  finalColor=mix(finalColor,finalColor*finalColor,0.08*uClarity);
  gl_FragColor=vec4(finalColor,alphaFade);
}`;

const sonicFloatingVS=`
precision highp float;
uniform float uPulse;
uniform vec3 uRippleColor;
varying vec2 vUv;
varying float vElevation;
varying float vDistance;
varying vec2 vRippleAnim;
varying vec3 vRippleColorMix;
varying vec3 vNormal;
varying float vRelativeY;
varying vec2 vInstancePos;
void main(){
  vUv=uv;
  vNormal=normal;
  vec4 instancePos=instanceMatrix*vec4(0.0,0.0,0.0,1.0);
  vec2 pos2D=instancePos.xz;
  vInstancePos=pos2D;
  vDistance=length(pos2D);
  vRippleAnim=vec2(uPulse*0.8,uPulse*0.3);
  vRippleColorMix=uRippleColor;
  vElevation=uPulse*20.0;
  vRelativeY=position.y+0.5;
  vec4 worldPosition=modelMatrix*instanceMatrix*vec4(position,1.0);
  gl_Position=projectionMatrix*viewMatrix*worldPosition;
}`;

function sonicRippleUniforms(){return Array.from({length:SONIC_RIPPLE_MAX},()=>new THREE.Vector4(0,0,-100,0))}
function sonicRippleColors(){return Array.from({length:SONIC_RIPPLE_MAX},()=>new THREE.Color('#ffffff'))}
function sonicTerrainUniforms(){return{
  uTime:{value:0},uSubBass:{value:0},uBass:{value:0},uLowMid:{value:0},uMid:{value:0},uHighMid:{value:0},
  uPresence:{value:0},uBrilliance:{value:0},uAir:{value:0},uWarmth:{value:0},uBrightness:{value:0},uSharpness:{value:0},
  uSmoothness:{value:0},uDensity:{value:0},uEnergy:{value:0},uAmplitude:{value:1},uRipples:{value:sonicRippleUniforms()},uRippleColors:{value:sonicRippleColors()},
  uBaseColor1:{value:new THREE.Color(.01,.02,.04)},uBaseColor2:{value:new THREE.Color(.03,.05,.09)},uFogColor:{value:new THREE.Color(.01,.02,.04)},
  uCoolCore:{value:new THREE.Color(0,.3,1)},uCoolEdge:{value:new THREE.Color(.6,.2,1)},uWarmCore:{value:new THREE.Color(1,.2,.1)},
  uWarmEdge:{value:new THREE.Color(1,.6,0)},uRippleColor:{value:new THREE.Color(.2,.9,1)},uGlowIntensity:{value:.54},uClarity:{value:.75},uSoftGlow:{value:.28},uCenterHighlight:{value:.58},uDepthFocus:{value:.57}
}}
function sonicFloatingUniforms(){const u=sonicTerrainUniforms();u.uPulse={value:0};return u}

const sonicRoot=new THREE.Group();sonicRoot.name='sonic-topography-root';root.add(sonicRoot);
const sonicSpacing=SONIC_BASE_SIZE/SONIC_GRID_SIZE;
const sonicBoxWidth=sonicSpacing*0.985;
const sonicTerrainGeo=new THREE.BoxGeometry(sonicBoxWidth,1,sonicBoxWidth);
const sonicTerrainMat=new THREE.ShaderMaterial({uniforms:sonicTerrainUniforms(),vertexShader:sonicTerrainVS,fragmentShader:sonicTerrainFS,transparent:true,depthWrite:true,depthTest:true});
const sonicTerrain=new THREE.InstancedMesh(sonicTerrainGeo,sonicTerrainMat,SONIC_GRID_SIZE*SONIC_GRID_SIZE);sonicTerrain.name='sonic-topography-terrain';sonicTerrain.frustumCulled=false;
const sonicOffset=(SONIC_GRID_SIZE*sonicSpacing)/2;let sonicInstance=0;
for(let x=0;x<SONIC_GRID_SIZE;x++)for(let z=0;z<SONIC_GRID_SIZE;z++){sonicState.dummyMat4.makeTranslation(x*sonicSpacing-sonicOffset,.5,z*sonicSpacing-sonicOffset);sonicTerrain.setMatrixAt(sonicInstance++,sonicState.dummyMat4)}
sonicTerrain.instanceMatrix.needsUpdate=true;sonicRoot.add(sonicTerrain);

const sonicFloatingGeo=new THREE.BoxGeometry(1,1,1);
const sonicFloatingMat=new THREE.ShaderMaterial({uniforms:sonicFloatingUniforms(),vertexShader:sonicFloatingVS,fragmentShader:sonicTerrainFS,transparent:true,depthWrite:false,depthTest:true});
const sonicFloating=new THREE.InstancedMesh(sonicFloatingGeo,sonicFloatingMat,SONIC_FLOATING_COUNT);sonicFloating.name='sonic-topography-floating-blocks';sonicFloating.frustumCulled=false;sonicRoot.add(sonicFloating);
for(let i=0;i<SONIC_FLOATING_COUNT;i++){
  const ring=i/Math.max(1,SONIC_FLOATING_COUNT),angle=ring*Math.PI*2*5+Math.sin(i*12.9898)*.7,radius=14+((i*37)%62),height=6+((i*17)%19);
  sonicState.floatingData.push({x:Math.cos(angle)*radius,z:Math.sin(angle)*radius,y:height,baseScale:.75+((i*11)%9)*.05,phase:i*.73,rotationSpeed:.18+((i*7)%10)*.035});
  sonicState.dummyMat4.makeTranslation(0,-1000,0);sonicFloating.setMatrixAt(i,sonicState.dummyMat4)
}
sonicFloating.instanceMatrix.needsUpdate=true;


const reduced=matchMedia('(prefers-reduced-motion: reduce)');
let frame=0,last=0,time=0;
function resize(){renderer.setSize(innerWidth,innerHeight,false);camera.aspect=innerWidth/innerHeight;camera.updateProjectionMatrix();render(0);}
function render(dt){
 time+=dt;
 const orbit=time*.105, radius=innerWidth<650?190:136;
 camera.position.set(Math.sin(orbit)*radius,50+Math.sin(time*.12)*6,Math.cos(orbit)*radius);camera.lookAt(0,-6,0);
 for(const mat of [sonicTerrainMat,sonicFloatingMat]){
 const u=mat.uniforms;u.uTime.value=time;u.uSubBass.value=.35+Math.sin(time*1.1)*.13;u.uBass.value=.3+Math.sin(time*.85)*.12;
 u.uLowMid.value=.22;u.uMid.value=.18;u.uHighMid.value=.12;u.uPresence.value=.15;u.uBrilliance.value=.2;u.uAir.value=.13;
 u.uEnergy.value=.42;u.uCenterHighlight.value=.64;u.uClarity.value=.77;u.uSoftGlow.value=.2;u.uDepthFocus.value=.57;u.uGlowIntensity.value=.66;u.uAmplitude.value=1.15;u.uSmoothness.value=.8;u.uDensity.value=.5;
 u.uCoolCore.value.set('#5ad8ff');u.uCoolEdge.value.set('#433d91');u.uWarmCore.value.set('#ff5f96');u.uWarmEdge.value.set('#ffc56f');
 u.uBaseColor1.value.set('#07050d');u.uBaseColor2.value.set('#171025');u.uFogColor.value.set('#08060c');u.uRippleColor.value.set('#ffc56f');
 const r=u.uRipples.value;for(let i=0;i<3;i++){r[i].set(Math.sin(i*2.1)*18,Math.cos(i*2.1)*18,time-((time+i*1.6)%4.8),.55);u.uRippleColors.value[i].set(['#ff5f96','#ffc56f','#5ad8ff'][i]);}
 }
 sonicFloating.visible=false;
 renderer.render(scene,camera);
}
function tick(now){frame=0;if(document.hidden||reduced.matches)return;if(now-last>=40){render(Math.min((now-last)/1000,.08));last=now;}frame=requestAnimationFrame(tick);}
function sync(){cancelAnimationFrame(frame);frame=0;last=performance.now();render(0);if(!document.hidden&&!reduced.matches)frame=requestAnimationFrame(tick);}
addEventListener('resize',resize);document.addEventListener('visibilitychange',sync);
if(reduced.addEventListener)reduced.addEventListener('change',sync);else reduced.addListener(sync);
canvas.addEventListener('webglcontextlost',e=>{e.preventDefault();cancelAnimationFrame(frame);});
canvas.addEventListener('webglcontextrestored',sync);
resize();sync();
})();

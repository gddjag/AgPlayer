// Independent Web Audio FFT oracle, using the browser's actual AnalyserNode.
// node scripts/capture-sonic-fft-oracle.cjs <test node_modules> <audio fixture.json>
const fs=require('node:fs'),path=require('node:path');
const [deps,output]=process.argv.slice(2);
(async()=>{
 const {chromium}=require(path.resolve(deps,'playwright-core'));
 const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 try {
  const page=await browser.newPage();
  const frames=await page.evaluate(async()=>{
   const rate=48000, context=new OfflineAudioContext(1,rate*3,rate);
   const buffer=context.createBuffer(1,rate*3,rate), data=buffer.getChannelData(0);
   for(let i=0;i<data.length;i++){
    const t=i/rate;
    data[i]=.3*Math.sin(2*Math.PI*70*t)*Math.exp(-(t%.25)*35)
       +.08*Math.sin(2*Math.PI*110*t)+.003*Math.sin(2*Math.PI*2700*t);
   }
   const node=context.createBufferSource();node.buffer=buffer;
   const analyser=context.createAnalyser();analyser.fftSize=1024;
   analyser.smoothingTimeConstant=.8;analyser.minDecibels=-75;
   node.connect(analyser);analyser.connect(context.destination);node.start();
   const frames=[];
   for(let f=0;f<64;f++) {
    const suspended=context.suspend((1024+f*800)/rate);
    if(f===0) context.startRendering(); else context.resume();
    await suspended;
    const pcm=new Float32Array(1024),spectrum=new Uint8Array(512);
    analyser.getFloatTimeDomainData(pcm);analyser.getByteFrequencyData(spectrum);
    frames.push({pcm:Array.from(pcm),spectrum:Array.from(spectrum)});
   }
   context.resume();return frames;
  });
  const root=JSON.parse(fs.readFileSync(output,'utf8'));root.webAudioFft=frames;
  root.webAudioVersion=await browser.version();fs.writeFileSync(output,JSON.stringify(root));
  console.log(`Captured ${frames.length} real Web Audio windows, ${root.webAudioVersion}`);
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});

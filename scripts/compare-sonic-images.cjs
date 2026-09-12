// Compare actual renderer PNGs in associated color over both black and white.
const fs=require('node:fs'), path=require('node:path');
const [deps,a,b]=process.argv.slice(2);
const {PNG}=require(path.resolve(deps,'pngjs'));
const x=PNG.sync.read(fs.readFileSync(a)),y=PNG.sync.read(fs.readFileSync(b));
if(x.width!==y.width||x.height!==y.height) throw Error('Capture size mismatch');
let color=0,alpha=0,changed=0,max=0;
for(let i=0;i<x.data.length;i+=4){
 const ax=x.data[i+3]/255,ay=y.data[i+3]/255; let error=0;
 for(let c=0;c<3;c++) for(const bg of [0,255]){
  const d=Math.abs(x.data[i+c]*ax+bg*(1-ax)-y.data[i+c]*ay-bg*(1-ay));
  color+=d;error=Math.max(error,d);max=Math.max(max,d);
 }
 alpha+=Math.abs(x.data[i+3]-y.data[i+3]);if(error>8)changed++;
}
const n=x.width*x.height;
console.log(JSON.stringify({original:a,native:b,meanRGB:color/(n*6),meanAlpha:alpha/n,percentOver8:changed/n*100,max}));

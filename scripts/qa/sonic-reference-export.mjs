// Development-only: original source/dependencies/audio remain in an external study tree.
// Usage: node sonic-reference-export.mjs STUDY OUTPUT PLAYWRIGHT_MODULE [PORT=3017] [THEME=nocturnal|all] [--validate-only]
// Run STUDY's Vite dev server on PORT first. Never point STUDY at pristine source.
import {createRequire} from 'node:module';
import {mkdir,writeFile,readFile,realpath,access} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
const args=process.argv.slice(2),validateOnly=args.includes('--validate-only');
const [studyArg,outputArg,playwrightModule,port='3017',theme='nocturnal']=args.filter(a=>a!=='--validate-only');
const ids=['ink-wash','nocturnal','neon-tokyo','cyber-forest','minimal-monochrome','glacier-day','koi-pond','coral-reef','moss-glass','blue-hour','porcelain-teal','wine-signal','daybreak-lime'];
const study=studyArg&&await realpath(studyArg),output=outputArg&&path.resolve(outputArg);
if(!study||!output||!playwrightModule)throw Error('Expected STUDY OUTPUT PLAYWRIGHT_MODULE [PORT]');
if(!/^\d+$/.test(port)||Number(port)<1||Number(port)>65535)throw Error('Invalid local port');
if(theme!=='all'&&!ids.includes(theme))throw Error('Unknown reference theme');
if(path.resolve(study).toLowerCase().includes('pristine'))throw Error('Refuse to instrument pristine source');
const hashes={
 'package-lock.json':'B73FFD87B40A981D3B58DD9A34B0CB885EF4E22276D5A620B132CA04E47D5D71',
 'src/lib/AudioEngine.ts':'0D96A161A4DA5559189CD0D35F5774B9717867522D6E544C534AB68362FE60EA',
 'src/components/AudioVisualizer/MapScene.tsx':'9DDE45F3DD0398A6493B3BA7AB59A0EB0E12B39DE4BB551C2939BBB30EFFA4DF',
 'src/components/AudioVisualizer/CustomShaderMaterial.ts':'F0E544D638F8206E2135FAA7BB355CC0E467DA4EE70BAB773F1ADB36D61D0AB1',
 'public/demo.mp3':'2FC3CB408BD4A9CA36E7389E05372311347AD9D42C6A6917952157A7E03831A5',
};
for(const [file,hash] of Object.entries(hashes)){
 const bytes=await readFile(path.join(study,file));
 if(createHash('sha256').update(bytes).digest('hex').toUpperCase()!==hash)throw Error('Fixed ec8ec reference mismatch: '+file);
}
try{await access(output);throw Error('Output already exists; choose a new directory: '+output);}catch(e){if(e.code!=='ENOENT')throw e;}
const {chromium}=createRequire(import.meta.url)(playwrightModule);
if(validateOnly){console.log('Validated fixed reference, theme, unused output and Playwright; no writes/browser launch');process.exit(0);}
const lock=await readFile(path.join(study,'package-lock.json'));
async function installOwned(name,bytes){const target=path.join(study,name);try{await writeFile(target,bytes,{flag:'wx'});}catch(e){if(e.code!=='EEXIST')throw e;if(!(await readFile(target)).equals(Buffer.from(bytes)))throw Error('Existing harness differs; refusing overwrite: '+target);}}
const harness=await readFile(new URL('./sonic-reference-export.tsx',import.meta.url));
const harnessName='agplayer-qa-parity-'+createHash('sha256').update(harness).digest('hex').slice(0,16);
await installOwned(harnessName+'.tsx',harness);
await installOwned(harnessName+'.html',`<!doctype html><html><body style="margin:0"><canvas id="scene" width="1920" height="1080"></canvas><script type="module" src="/${harnessName}.tsx"></script></body></html>`);
await mkdir(output); // Exclusive new output; never merge into an earlier run.
const browser=await chromium.launch({channel:'msedge',headless:true});
const selectedThemes=theme==='all'?ids:[theme];
let traceWritten=false;
try{for(const theme of selectedThemes){
 const page=await browser.newPage({viewport:{width:1920,height:1080},deviceScaleFactor:1});
 await page.goto(`http://127.0.0.1:${port}/${harnessName}.html?theme=${theme}`);
 await page.waitForFunction(()=>window.parityResult||window.parityError,null,{timeout:60000});
 const rows=await page.evaluate(()=>window.parityError||window.parityResult);if(typeof rows==='string')throw Error(rows);
 for(const row of rows){const name=`${theme}-${row.provenance.sampleIndex}`;const png=row.png;delete row.png;row.matrices.modelView=[...row.matrices.view];await writeFile(path.join(output,name+'.json'),JSON.stringify(row));await writeFile(path.join(output,name+'.png'),Buffer.from(png.split(',')[1],'base64'));}
 if(!traceWritten){const trace=await page.evaluate(()=>window.parityTrace);await writeFile(path.join(output,'audio-trace.json'),JSON.stringify({...trace,theme}));traceWritten=true;}
 console.log(theme,rows.map(r=>r.uniforms.uEnergy));await page.close();
}}finally{await browser.close();}
if(!lock.equals(await readFile(path.join(study,'package-lock.json'))))throw Error('External reference lock changed during export');

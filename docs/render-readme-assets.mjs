import { mkdir, readFile, writeFile, copyFile } from 'node:fs/promises';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

// The hero embeds a frame exported by the production renderer. Diagrams are
// original vectors; generating these assets requires no additional packages.
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const output = resolve(root, 'image/readme');
const frames = resolve(root, process.argv[2] ?? 'build/readme-preview');
await mkdir(output, { recursive: true });
const frameBytes = await readFile(resolve(frames, 'expanded-dark.png'));
const frame = frameBytes.toString('base64');
const frameWidth = frameBytes.readUInt32BE(16) * 1.25;
const frameHeight = frameBytes.readUInt32BE(20) * 1.25;

const hero = `<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="680" viewBox="0 0 1280 680" role="img" aria-labelledby="title desc">
  <title id="title">trenches</title>
  <desc id="desc">The native brightness panel attached to the top edge of a display. Two independent sliders show 72% and 35%.</desc>
  <defs>
    <linearGradient id="canvas" x2="0" y2="1"><stop stop-color="#f7f7f9"/><stop offset="1" stop-color="#ececef"/></linearGradient>
    <linearGradient id="screen" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#babec6"/><stop offset=".5" stop-color="#e0e2e7"/><stop offset="1" stop-color="#adb2bc"/></linearGradient>
    <linearGradient id="wave" x2=".8" y2="1"><stop stop-color="#fafafa" stop-opacity=".78"/><stop offset="1" stop-color="#9199a8" stop-opacity=".25"/></linearGradient>
    <linearGradient id="stand" x2="1" y2="0"><stop stop-color="#9e9ea5"/><stop offset=".5" stop-color="#e0e0e5"/><stop offset="1" stop-color="#a9a9b0"/></linearGradient>
    <radialGradient id="ground"><stop stop-color="#18181b" stop-opacity=".13"/><stop offset="1" stop-color="#18181b" stop-opacity="0"/></radialGradient>
    <clipPath id="desktop"><rect x="126" y="58" width="1028" height="452" rx="9"/></clipPath>
  </defs>
  <rect width="1280" height="680" rx="28" fill="url(#canvas)"/>
  <ellipse cx="640" cy="604" rx="390" ry="24" fill="url(#ground)"/>
  <path d="M596 512h88l16 71h-120z" fill="url(#stand)"/>
  <path d="M546 581h188l23 13q3 5-8 5H531q-11 0-8-5z" fill="#bdbdc4"/>
  <rect x="114" y="46" width="1052" height="478" rx="20" fill="#202023"/>
  <g clip-path="url(#desktop)">
    <rect x="126" y="58" width="1028" height="452" fill="url(#screen)"/>
    <path d="M70 443C295 118 350 524 700 330S1050 177 1220 242V540H70z" fill="url(#wave)"/>
    <path d="M84 530C338 171 378 626 768 400S1035 267 1210 284" fill="none" stroke="#ffffff" stroke-opacity=".22" stroke-width="2"/>
    <path d="M136 476C364 235 450 562 792 405S1058 298 1152 314" fill="none" stroke="#7f8795" stroke-opacity=".12" stroke-width="1.5"/>
    <image href="data:image/png;base64,${frame}" x="${(1280 - frameWidth) / 2}" y="58" width="${frameWidth}" height="${frameHeight}"/>
  </g>
  <circle cx="640" cy="517" r="2" fill="#545459"/>
  <g fill="#5e5e68" font-family="Segoe UI,Arial,sans-serif" font-size="18" text-anchor="middle">
    <text x="640" y="649" letter-spacing="1">Ctrl + Alt + B</text>
  </g>
</svg>
`;
await writeFile(resolve(output, 'hero.svg'), hero);

const translations = {
  en: {
    title: 'Two ways to dim',
    desc: 'Software adds a dark overlay while the physical backlight stays unchanged. Hardware changes brightness through DDC/CI.',
    software: 'Dimming over the picture', hardware: 'Brightness through DDC/CI',
    overlay: 'Overlay', backlight: 'Backlight unchanged', control: 'Display brightness control',
  },
  ru: {
    title: 'Два способа затемнения',
    desc: 'Software накладывает затемнение поверх изображения, не меняя подсветку. Hardware управляет яркостью монитора через DDC/CI.',
    software: 'Затемнение поверх изображения', hardware: 'Яркость через DDC/CI',
    overlay: 'Затемняющий слой', backlight: 'Подсветка прежняя', control: 'Управление яркостью монитора',
  },
};

function monitor(x, hardware, t) {
  const tint = hardware ? 'dim' : 'bright';
  return `<g transform="translate(${x} 0)">
    <rect x="56" y="152" width="380" height="221" rx="13" fill="#252529"/>
    <rect x="65" y="161" width="362" height="200" rx="5" fill="url(#${tint})"/>
    <path d="M66 332C155 218 225 372 323 266S414 208 426 233V360H66z" fill="#fff" opacity="${hardware ? '.08' : '.28'}"/>
    ${hardware ? '' : '<rect x="65" y="161" width="362" height="200" rx="5" fill="#080809" opacity=".4"/>'}
    <rect x="218" y="373" width="56" height="19" fill="#c3c3ca"/>
    <rect x="185" y="391" width="122" height="5" rx="2.5" fill="#b4b4bd"/>
    ${hardware ? `<text x="246" y="271" fill="#f5f5f7" text-anchor="middle" font-size="19">DDC/CI</text>` : `<rect x="141" y="240" width="210" height="38" rx="19" fill="#080809" opacity=".75"/><text x="246" y="265" fill="#f5f5f7" text-anchor="middle" font-size="17">${t.overlay}</text>`}
    <text x="246" y="445" text-anchor="middle" fill="#62626b" font-size="17">${hardware ? t.control : t.backlight}</text>
  </g>`;
}

for (const [lang, t] of Object.entries(translations)) {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" xml:lang="${lang}" width="1040" height="480" viewBox="0 0 1040 480" role="img" aria-labelledby="title desc">
  <title id="title">${t.title}</title><desc id="desc">${t.desc}</desc>
  <defs>
    <linearGradient id="bright" x2="1" y2="1"><stop stop-color="#e5e7ec"/><stop offset="1" stop-color="#c7ccd5"/></linearGradient>
    <linearGradient id="dim" x2="1" y2="1"><stop stop-color="#777c87"/><stop offset="1" stop-color="#4c515b"/></linearGradient>
  </defs>
  <g font-family="Segoe UI,Arial,sans-serif">
    <rect x="0.5" y="0.5" width="503" height="479" rx="24" fill="#f5f5f7" stroke="#e9e9ed"/>
    <rect x="536.5" y="0.5" width="503" height="479" rx="24" fill="#f5f5f7" stroke="#e9e9ed"/>
    <text x="40" y="59" font-size="26" font-weight="600" fill="#171719">Software</text>
    <text x="576" y="59" font-size="26" font-weight="600" fill="#171719">Hardware</text>
    <text x="40" y="95" font-size="18" fill="#62626b">${t.software}</text>
    <text x="576" y="95" font-size="18" fill="#62626b">${t.hardware}</text>
    ${monitor(6, false, t)}
    ${monitor(542, true, t)}
  </g>
</svg>
`;
  await writeFile(resolve(output, `modes-${lang}.svg`), svg.replace(/[ \t]+$/gm, ''));
}
await copyFile(resolve(frames, 'hardware-light.png'), resolve(output, 'hardware.png'));
console.log('README artwork generated in image/readme');

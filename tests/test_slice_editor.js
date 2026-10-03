const fs = require('fs');
const vm = require('vm');
const source = fs.readFileSync('src/slice_editor.js', 'utf8');
const sandbox = { globalThis: {} };
vm.runInNewContext(source, sandbox);
const overlay = sandbox.globalThis.canvas_overlay;
const writes = [];
const pixels = new Set();
let now = 1000;
let marker3 = 250000;
const zoomReads = [];
const ctx = {
    width: 128, height: 64, state: {},
    getParam(key) {
        if (key === 'edit_a') return '0,80000,44100,0,125000,250000,375000,500000,625000,750000,875000';
        if (key === 'wave_a') return '8'.repeat(128);
        if (key === 'zoom_a_3') {
            const center = Math.round(80000 * marker3 / 1000000);
            zoomReads.push(center);
            return `${center - 1764},${center + 1764},${'8'.repeat(128)}`;
        }
        if (key === 'zoom_a_1') return `-1764,1764,${'8'.repeat(128)}`;
        return '';
    },
    setParam(key, value) {
        writes.push([key, value]);
        if (key === 'slice_a_3') marker3 = Number(value);
        return true;
    },
    shiftHeld() { return false; }, now() { return now; },
    clear() { pixels.clear(); },
    setPixel(x, y, on) { if (on) pixels.add(`${x},${y}`); },
    fillRect(x, y, w, h, on) {
        if (on) for (let yy = y; yy < y + h; yy++)
            for (let xx = x; xx < x + w; xx++) pixels.add(`${xx},${yy}`);
    },
    print() {}
};
overlay.onOpen(ctx, { param_key: 'edit_a' });
overlay.draw(ctx);
if (ctx.state.active !== -1 || !pixels.size) throw new Error('Overview missing');
overlay.onMidi(ctx, { data: [0x90, 2, 127] });
if (ctx.state.active !== 2) throw new Error('Touch did not zoom');
overlay.onMidi(ctx, { data: [0xb0, 73, 1] });
if (writes.length !== 2 || writes[0][0] !== 'reset_a' ||
    writes[1][0] !== 'slice_a_3' || Number(writes[1][1]) <= 250000)
    throw new Error('Third knob did not advance third start');
if (zoomReads.length < 2 || zoomReads[1] <= zoomReads[0])
    throw new Error('Zoom waveform did not scroll with the marker');
overlay.draw(ctx);
if (!pixels.has('64,9')) throw new Error('Zoom marker did not stay centered');
overlay.onMidi(ctx, { data: [0x90, 2, 0] });
if (ctx.state.active !== -1) throw new Error('Release did not restore overview');
overlay.onMidi(ctx, { data: [0x90, 0, 127] });
overlay.draw(ctx);
if (ctx.state.zoom.first >= 0 || !pixels.has('64,9'))
    throw new Error('First slice lacks centered zoom before the file start');
overlay.onMidi(ctx, { data: [0x90, 0, 0] });
for (const point of pixels) {
    const [x, y] = point.split(',').map(Number);
    if (x < 0 || x >= 128 || y < 0 || y >= 64) throw new Error(`Out of bounds ${point}`);
}
console.log('PASS: manual editor knob, touch zoom, and release');

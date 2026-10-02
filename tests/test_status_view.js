const fs = require('fs');
const vm = require('vm');
const source = fs.readFileSync('src/status_view.js', 'utf8');
const context = { globalThis: {} };
vm.runInNewContext(source, context);
const overlay = context.globalThis.canvas_overlay;

function render(detail) {
    const pixels = new Set();
    const state = {};
    const ctx = {
        width: 128, height: 64, state,
        getParam(key) { return key === 'status_info' ? detail : 'ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff'; },
        clear() { pixels.clear(); },
        setPixel(x, y, value) { if (value) pixels.add(`${x},${y}`); },
        fillRect(x, y, w, h, value) {
            if (value) for (let py = y; py < y + h; py++)
                for (let px = x; px < x + w; px++) pixels.add(`${px},${py}`);
        },
    };
    overlay.onOpen(ctx);
    overlay.draw(ctx);
    for (const point of pixels) {
        const [x, y] = point.split(',').map(Number);
        if (x < 0 || x >= 128 || y < 0 || y >= 64)
            throw new Error(`status drawing outside screen at ${point}`);
    }
    return pixels;
}

function rowHasValue(pixels, line) {
    const y = 5 + line * 14;
    for (let py = y; py < y + 5; py++)
        for (let px = 112; px < 128; px++)
            if (pixels.has(`${px},${py}`)) return true;
    return false;
}

for (const [detail, expected] of [
    ['A2,0,0,0', [false, false, false, false]],
    ['A2R4,0,0,0', [true, false, false, false]],
    ['A2R16,0,0,0', [true, false, false, false]],
    ['A2R32,0,0,0', [true, false, false, false]],
    ['B3S2,16,2,-12', [false, true, true, true]],
]) {
    const pixels = render(detail);
    for (let line = 0; line < 4; line++) {
        const y = 5 + line * 14;
        const labelVisible = [...pixels].some(point => {
            const [x, py] = point.split(',').map(Number);
            return x >= 68 && x < 111 && py >= y && py < y + 5;
        });
        if (!labelVisible || rowHasValue(pixels, line) !== expected[line])
            throw new Error(`${detail}: wrong fixed label or active value on line ${line}`);
    }
}

console.log('PASS: fullscreen status keeps four labels and only active values');

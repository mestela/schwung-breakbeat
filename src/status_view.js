/* Breakbeat's fullscreen two-loop overview. */
const MINI = {
    ' ': [0,0,0,0,0], 'A':[2,5,7,5,5], 'B':[6,5,6,5,6],
    'R':[6,5,6,5,5], 'S':[3,4,2,1,6], 'X':[5,5,2,5,5],
    '0':[7,5,5,5,7], '1':[2,6,2,2,7], '2':[7,1,7,4,7],
    '3':[7,1,7,1,7], '4':[5,5,7,1,1], '5':[7,4,7,1,7],
    '6':[7,4,7,5,7], '7':[7,1,2,2,2], '8':[7,5,7,5,7],
    '9':[7,5,7,1,7]
};

function miniPrint(ctx, x, y, text) {
    const str = String(text || '').toUpperCase();
    for (let i = 0; i < str.length; i++) {
        const rows = MINI[str[i]] || MINI[' '];
        for (let row = 0; row < 5; row++)
            for (let col = 0; col < 3; col++)
                if (rows[row] & (1 << (2 - col)))
                    ctx.setPixel(x + i * 4 + col, y + row, 1);
    }
}

function parseStatus(raw) {
    const match = /^([AB]) ([0-7]) (\d+)([XRS])$/.exec(String(raw || ''));
    return match ? { loop: match[1], slice: Number(match[2]),
                     factor: match[3], mode: match[4] }
                 : { loop: 'A', slice: -1, factor: '1', mode: 'X' };
}

function drawWave(ctx, wave, y, active) {
    const left = 4, width = 96, center = y + 9;
    ctx.fillRect(left, center, width, 1, 1);
    for (let zone = 0; zone <= 8; zone++)
        ctx.fillRect(left + zone * 12, y, 1, 20, 1);
    const bins = String(wave || '');
    for (let i = 0; i < 64 && i < bins.length; i++) {
        const value = parseInt(bins[i], 16);
        if (!Number.isFinite(value) || value === 0) continue;
        const half = Math.max(1, Math.round(value * 7 / 15));
        const x = left + Math.floor(i * width / 64);
        ctx.fillRect(x, center - half, 1, half * 2 + 1, 1);
    }
    if (active >= 0)
        ctx.fillRect(left + active * 12 + 1, y + 20, 11, 2, 1);
}

function drawOverview(ctx, status, waveA, waveB) {
    const current = parseStatus(status);
    ctx.clear();
    miniPrint(ctx, 0, 12, 'A');
    miniPrint(ctx, 0, 40, 'B');
    drawWave(ctx, waveA, 5, current.loop === 'A' ? current.slice : -1);
    drawWave(ctx, waveB, 33, current.loop === 'B' ? current.slice : -1);
    miniPrint(ctx, 103, 9, `${current.loop}${current.slice < 0 ? ' ' : current.slice}`);
    miniPrint(ctx, 103, 20, `${current.factor}${current.mode}`);
    miniPrint(ctx, 103, 37, current.mode === 'S' ? 'S' :
                            current.mode === 'R' ? 'R' : 'X');
}

globalThis.canvas_overlay = {
    onOpen(ctx) {
        ctx.state.status = ctx.getParam('status') || '';
        ctx.state.wave_a = ctx.getParam('wave_a') || '';
        ctx.state.wave_b = ctx.getParam('wave_b') || '';
    },
    onValues(ctx, { values }) {
        for (const key of ['status', 'wave_a', 'wave_b'])
            if (values[key] !== null && values[key] !== undefined)
                ctx.state[key] = values[key];
    },
    draw(ctx) {
        drawOverview(ctx, ctx.state.status, ctx.state.wave_a, ctx.state.wave_b);
    }
};

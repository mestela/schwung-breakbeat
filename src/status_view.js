/* Breakbeat's fullscreen two-loop overview. */
const MINI = {
    ' ': [0,0,0,0,0], ':':[0,2,0,2,0], '-':[0,0,7,0,0],
    '+':[0,2,7,2,0], 'A':[2,5,7,5,5], 'B':[6,5,6,5,6],
    'C':[3,4,4,4,3], 'D':[6,5,5,5,6], 'E':[7,4,6,4,7], 'G':[3,4,5,5,3],
    'H':[5,5,7,5,5], 'I':[7,2,2,2,7], 'L':[4,4,4,4,7],
    'N':[5,7,7,7,5], 'P':[6,5,6,4,4], 'R':[6,5,6,5,5],
    'S':[3,4,2,1,6], 'T':[7,2,2,2,2], 'X':[5,5,2,5,5],
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
    const fields = String(raw || '').split(',');
    const match = /^([AB])([0-7])(?:([RSPD])(\d+))?$/.exec(fields[0]);
    return match ? { loop: match[1], slice: Number(match[2]),
                     mode: match[3] || '', factor: Number(match[4]) || 0,
                     length: Number(fields[1]) || 0,
                     span: Number(fields[2]) || 0,
                     pitch: Number(fields[3]) || 0 }
                 : { loop: 'A', slice: -1, mode: '', factor: 0,
                     length: 0, span: 0, pitch: 0 };
}

function drawWave(ctx, wave, y, active) {
    const left = 6, width = 56, zoneWidth = 7, center = y + 9;
    ctx.fillRect(left, center, width, 1, 1);
    for (let zone = 0; zone <= 8; zone++)
        ctx.fillRect(left + zone * zoneWidth, y, 1, 20, 1);
    const bins = String(wave || '');
    for (let i = 0; i < 128 && i < bins.length; i++) {
        const value = parseInt(bins[i], 16);
        if (!Number.isFinite(value) || value === 0) continue;
        const half = Math.max(1, Math.round(value * 7 / 15));
        const x = left + Math.floor(i * width / bins.length);
        ctx.fillRect(x, center - half, 1, half * 2 + 1, 1);
    }
    if (active >= 0)
        ctx.fillRect(left + active * zoneWidth + 1, y + 20, zoneWidth - 1, 2, 1);
}

function drawOverview(ctx, detail, waveA, waveB) {
    const current = parseStatus(detail);
    ctx.clear();
    miniPrint(ctx, 0, 12, 'A');
    miniPrint(ctx, 0, 40, 'B');
    drawWave(ctx, waveA, 5, current.loop === 'A' ? current.slice : -1);
    drawWave(ctx, waveB, 33, current.loop === 'B' ? current.slice : -1);
    miniPrint(ctx, 46, 0, current.slice < 0 ? '' : `${current.loop}${current.slice}`);
    const labels = ['RETRIGGER:', 'STRTCH LEN:', 'STRTCH SLC:', 'STRCH PTCH:'];
    const values = [current.mode === 'R' ? `${current.factor}X` :
                    (current.mode === 'P' || current.mode === 'D') ?
                    `${current.factor}${current.mode}` : '',
                    current.mode === 'S' ? `${current.length}X` : '',
                    current.mode === 'S' ? String(current.span) : '',
                    current.mode === 'S' ? `${current.pitch >= 0 ? '+' : ''}${current.pitch}` : ''];
    for (let line = 0; line < labels.length; line++) {
        const y = 5 + line * 14;
        miniPrint(ctx, 68, y, labels[line]);
        miniPrint(ctx, 112, y, values[line]);
    }
}

globalThis.canvas_overlay = {
    onOpen(ctx) {
        ctx.state.status_info = ctx.getParam('status_info') || '';
        ctx.state.wave_a = ctx.getParam('wave_a') || '';
        ctx.state.wave_b = ctx.getParam('wave_b') || '';
    },
    onValues(ctx, { values }) {
        for (const key of ['status_info', 'wave_a', 'wave_b'])
            if (values[key] !== null && values[key] !== undefined)
                ctx.state[key] = values[key];
    },
    draw(ctx) {
        drawOverview(ctx, ctx.state.status_info, ctx.state.wave_a, ctx.state.wave_b);
    }
};

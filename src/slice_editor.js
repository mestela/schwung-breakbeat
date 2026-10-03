/* Manual eight-start waveform editor for Breakbeat A or B. */
const UNITS = 1000000;

function parseEdit(raw) {
    const parts = String(raw || '').split(',').map(Number);
    return { mode: parts[0] || 0, frames: parts[1] || 0,
             rate: parts[2] || 44100,
             positions: Array.from({ length: 8 }, (_, i) =>
                 Number.isFinite(parts[i + 3]) ? parts[i + 3] : i * 125000) };
}

function parseZoom(raw) {
    const parts = String(raw || '').split(',');
    return { first: Number(parts[0]) || 0, last: Number(parts[1]) || 0,
             peaks: parts[2] || '' };
}

function drawWave(ctx, peaks, top, height) {
    const center = top + Math.floor(height / 2);
    for (let x = 0; x < 128 && x < peaks.length; x++) {
        const value = parseInt(peaks[x], 16);
        if (!Number.isFinite(value) || value <= 0) continue;
        const half = Math.max(1, Math.round(value * (height / 2 - 1) / 15));
        ctx.fillRect(x, center - half, 1, half * 2 + 1, 1);
    }
}

function drawOverview(ctx, state) {
    const edit = state.edit;
    ctx.clear();
    ctx.print(1, 0, `BREAK ${state.loop.toUpperCase()}  ${edit.mode ? 'MANUAL' : 'GRID'}`, 1);
    ctx.fillRect(0, 31, 128, 1, 1);
    drawWave(ctx, state.wave, 11, 40);
    for (let i = 0; i < 8; i++) {
        const position = edit.mode ? edit.positions[i] : i * UNITS / 8;
        const x = Math.min(127, Math.floor(position * 127 / UNITS));
        for (let y = 9; y < 51; y += 3) ctx.setPixel(x, y, 1);
        ctx.print(Math.max(0, Math.min(123, x - 2)), 52, String(i + 1), 1);
    }
    ctx.print(1, 57, 'TOUCH KNOB TO ZOOM', 1);
}

function drawZoom(ctx, state) {
    const index = state.active;
    const edit = state.edit;
    const position = edit.positions[index];
    const frame = Math.round(edit.frames * position / UNITS);
    const gridFrame = edit.frames * index / 8;
    const offsetMs = edit.rate ? (frame - gridFrame) * 1000 / edit.rate : 0;
    const zoom = state.zoom;
    ctx.clear();
    ctx.print(1, 0, `${state.loop.toUpperCase()} SLICE ${index + 1}  ${offsetMs >= 0 ? '+' : ''}${offsetMs.toFixed(1)}MS`, 1);
    ctx.fillRect(0, 31, 128, 1, 1);
    drawWave(ctx, zoom.peaks, 11, 40);
    if (zoom.last > zoom.first) ctx.fillRect(64, 9, 1, 43, 1);
    ctx.print(1, 55, 'SHIFT: 0.1MS', 1);
}

globalThis.canvas_overlay = {
    onOpen(ctx, payload) {
        const key = String(payload.param_key || 'edit_a');
        ctx.state.loop = key.endsWith('b') ? 'b' : 'a';
        ctx.state.edit = parseEdit(ctx.getParam(`edit_${ctx.state.loop}`));
        ctx.state.wave = ctx.getParam(`wave_${ctx.state.loop}`) || '';
        ctx.state.active = -1;
        ctx.state.zoom = parseZoom('');
        ctx.state.touched = false;
        ctx.state.releaseAt = 0;
    },
    onValues(ctx, { values }) {
        const loop = ctx.state.loop;
        if (values[`edit_${loop}`] != null)
            ctx.state.edit = parseEdit(values[`edit_${loop}`]);
        if (values[`wave_${loop}`] != null)
            ctx.state.wave = values[`wave_${loop}`];
    },
    onMidi(ctx, { data }) {
        const [status, note, value] = data;
        const type = status & 0xf0;
        if ((type === 0x90 || type === 0x80) && note >= 0 && note < 8) {
            const down = type === 0x90 && value > 0;
            if (down) {
                ctx.state.active = note;
                ctx.state.touched = true;
                ctx.state.zoom = parseZoom(ctx.getParam(`zoom_${ctx.state.loop}_${note + 1}`));
            } else if (ctx.state.active === note) {
                ctx.state.active = -1;
                ctx.state.touched = false;
            }
            return;
        }
        if (type !== 0xb0 || note < 71 || note > 78) return;
        const delta = value <= 63 ? value : (value >= 65 ? value - 128 : 0);
        if (!delta) return;
        const index = note - 71;
        const edit = ctx.state.edit;
        if (!edit.frames) return;
        if (!edit.mode) {
            ctx.setParam(`reset_${ctx.state.loop}`, '1');
            edit.positions = Array.from({ length: 8 }, (_, i) => i * UNITS / 8);
            edit.mode = 1;
        }
        if (ctx.state.active !== index) {
            ctx.state.active = index;
            ctx.state.zoom = parseZoom(ctx.getParam(`zoom_${ctx.state.loop}_${index + 1}`));
        }
        const ms = ctx.shiftHeld() ? 0.1 : 1;
        const step = Math.max(1, Math.round(UNITS * edit.rate * ms / (edit.frames * 1000)));
        const lower = index ? edit.positions[index - 1] + 1 : 0;
        const upper = index < 7 ? edit.positions[index + 1] - 1 : UNITS;
        const next = Math.max(lower, Math.min(upper, edit.positions[index] + delta * step));
        edit.positions[index] = next;
        edit.mode = 1;
        ctx.setParam(`slice_${ctx.state.loop}_${index + 1}`, String(next));
        if (!ctx.state.touched) ctx.state.releaseAt = ctx.now() + 850;
        ctx.state.zoom = parseZoom(ctx.getParam(`zoom_${ctx.state.loop}_${index + 1}`));
    },
    tick(ctx) {
        if (!ctx.state.touched && ctx.state.active >= 0 &&
            ctx.state.releaseAt && ctx.now() >= ctx.state.releaseAt)
            ctx.state.active = -1;
    },
    draw(ctx) {
        if (ctx.state.active >= 0) drawZoom(ctx, ctx.state);
        else drawOverview(ctx, ctx.state);
    }
};

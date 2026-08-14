// m8web shim — taps the M8 display protocol inside the m8.run page.
//
// Installed via CDP Page.addScriptToEvaluateOnNewDocument, so it runs BEFORE
// m8.run's own script. It patches navigator.serial so every SerialPort handed
// to the page is wrapped:
//
//   port.readable  -> tee(); m8.run gets one branch, we decode the other
//   port.writable  -> shared real writer, so we can send control bytes too
//
// Nothing here touches m8.run's internals. That script is minified (single-letter
// identifiers, renamed on every deploy); the serial wire format is M8 firmware
// and does not change. So we hook the wire, not the app.
//
// Decoder is a direct port of ScreenGrid::handleFrame in
// src/tools/m8/M8Device.cpp — same SLIP constants, same opcodes, same PIXEL
// coordinates, so the field maps in src/tools/m8/ScreenModel.h port over 1:1.
//
// Exposes window.__m8:
//   ready()            -> bool, is a port tapped and open
//   grid()             -> {cells:[{x,y,ch,fg,bg}], highlights:[...], fw:{...}}
//   text()             -> {rows:[{y,text}], plain:"..."}  (main area only)
//   cursor()           -> {x,y,text} best-guess accent-cyan cursor cell
//   stats()            -> byte/frame counters + revision (bumps on every frame)
//   send(bytes)        -> raw write, e.g. [0x43, 0x14] for 'C' SHIFT+RIGHT
//   press(mask, hold)  -> 'C' mask / wait / 'C' 0x00   (m8_nav press semantics)
//   keyjazz(note, vel) -> 'K' note vel
//   requestFullRedraw()-> 'R'  (ask the device to resend the framebuffer)

(() => {
  if (window.__m8) return;

  const SLIP_END = 0xc0, SLIP_ESC = 0xdb, SLIP_ESC_END = 0xdc, SLIP_ESC_ESC = 0xdd;
  const MAIN_X_MAX = 260;                  // M8Device.h: main area vs. right gutter
  const CURSOR_RGB = [0, 252, 248];        // M8 default theme accent (cyan)

  const state = {
    cells: new Map(),        // key "y,x" -> {x,y,ch,fg:[r,g,b],bg:[r,g,b]}
    highlights: [],          // {x,y,w,h,color:[r,g,b]}
    lastColor: [0, 0, 0],
    fw: { hwType: 0, major: 0, minor: 0, patch: 0, fontMode: 0 },
    bytes: 0, frames: 0, revision: 0,
    writer: null, port: null, open: false, error: null,
  };

  // ---- decoder (port of ScreenGrid::handleFrame) ---------------------------

  function eraseRegion(x, y, w, h) {
    for (const k of [...state.cells.keys()]) {
      const c = state.cells.get(k);
      if (c.x >= x && c.x < x + w && c.y >= y && c.y < y + h) state.cells.delete(k);
    }
  }

  function handleFrame(f) {
    if (!f.length) return;
    state.frames++;
    switch (f[0]) {
      case 0xfd: {                                   // draw character
        if (f.length < 12) return;
        const x = f[2] | (f[3] << 8), y = f[4] | (f[5] << 8);
        state.cells.set(y + "," + x, {
          x, y, ch: f[1],
          fg: [f[6], f[7], f[8]], bg: [f[9], f[10], f[11]],
        });
        break;
      }
      case 0xfe: {                                   // draw rectangle
        const x = f[1] | (f[2] << 8), y = f[3] | (f[4] << 8);
        let w = 1, h = 1, col = state.lastColor.slice();
        if (f.length === 8)       { col = [f[5], f[6], f[7]]; }
        else if (f.length === 9)  { w = f[5] | (f[6] << 8); h = f[7] | (f[8] << 8); }
        else if (f.length >= 12)  { w = f[5] | (f[6] << 8); h = f[7] | (f[8] << 8);
                                    col = [f[9], f[10], f[11]]; }
        state.lastColor = col;
        const black = col[0] === 0 && col[1] === 0 && col[2] === 0;
        if (w >= 4 && h >= 4) {
          eraseRegion(x, y, w, h);
          if (black) {
            state.highlights = state.highlights.filter(
              r => !(r.x >= x && r.y >= y && r.x < x + w && r.y < y + h));
          } else {
            state.highlights.push({ x, y, w, h, color: col });
          }
        }
        break;
      }
      case 0xff: {                                   // system info
        if (f.length >= 5) {
          state.fw.hwType = f[1]; state.fw.major = f[2];
          state.fw.minor  = f[3]; state.fw.patch = f[4];
        }
        if (f.length >= 6) state.fw.fontMode = f[5];
        break;
      }
      default: break;
    }
    state.revision++;
  }

  // SLIP reassembly across chunk boundaries.
  let frame = [], esc = false;
  function feed(chunk) {
    state.bytes += chunk.length;
    for (let i = 0; i < chunk.length; i++) {
      const b = chunk[i];
      if (esc) {
        frame.push(b === SLIP_ESC_END ? SLIP_END : b === SLIP_ESC_ESC ? SLIP_ESC : b);
        esc = false;
      } else if (b === SLIP_ESC) {
        esc = true;
      } else if (b === SLIP_END) {
        if (frame.length) { handleFrame(frame); frame = []; }
      } else {
        frame.push(b);
      }
    }
  }

  // ---- text / cursor views ------------------------------------------------

  function textRows(mainOnly = true) {
    const byRow = new Map();
    for (const c of state.cells.values()) {
      if (mainOnly && c.x >= MAIN_X_MAX) continue;
      if (!byRow.has(c.y)) byRow.set(c.y, new Map());
      const g = (c.ch >= 32 && c.ch < 127) ? String.fromCharCode(c.ch) : " ";
      byRow.get(c.y).set(c.x, g);
    }
    const ys = [...byRow.keys()].sort((a, b) => a - b);
    return ys.map(y => {
      const cols = byRow.get(y);
      const xs = [...cols.keys()].sort((a, b) => a - b);
      let s = "";
      for (const x of xs) {
        const col = Math.floor(x / 8);              // 8px glyph pitch
        while (s.length < col) s += " ";
        s += cols.get(x);
      }
      return { y, text: s.replace(/\s+$/, "") };
    });
  }

  function cursorCell() {
    // Accent-cyan foreground in the main area, topmost then leftmost.
    // Non-space only: the M8 leaves stale cyan blanks at a vacated row when
    // the cursor moves away (it skips redrawing trailing blanks) -- taking a
    // cyan space picks that ghost row instead of the real cursor.
    // See M8_DRIVER_BUGS.md #5 and #6.
    let best = null;
    for (const c of state.cells.values()) {
      if (c.x >= MAIN_X_MAX) continue;
      if (c.ch === 32) continue;
      if (c.fg[0] !== CURSOR_RGB[0] || c.fg[1] !== CURSOR_RGB[1] || c.fg[2] !== CURSOR_RGB[2]) continue;
      if (!best || c.y < best.y || (c.y === best.y && c.x < best.x)) best = c;
    }
    if (!best) return null;
    const row = textRows(true).find(r => r.y === best.y);
    return { x: best.x, y: best.y, col: Math.floor(best.x / 8), text: row ? row.text : "" };
  }

  // ---- serial tap ---------------------------------------------------------

  function wrapPort(port) {
    if (!port || port.__m8wrapped) return port;
    const handler = {
      get(target, prop, recv) {
        if (prop === "__m8wrapped") return true;

        if (prop === "readable") {
          const real = target.readable;
          if (!real) return real;
          if (!target.__m8tee) {
            const [a, b] = real.tee();
            target.__m8tee = a;                     // hand this one to m8.run
            // Drain our branch forever. A tee stalls both branches if one is
            // never read, so this loop is not optional.
            (async () => {
              const rd = b.getReader();
              try {
                for (;;) {
                  const { value, done } = await rd.read();
                  if (done) break;
                  if (value) feed(value);
                }
              } catch (e) {
                state.error = String(e);
              } finally {
                try { rd.releaseLock(); } catch (_) {}
              }
            })();
          }
          return target.__m8tee;
        }

        if (prop === "writable") {
          const real = target.writable;
          if (!real) return real;
          if (!target.__m8writable) {
            // A WritableStream allows exactly one writer. Take it ourselves and
            // hand m8.run a forwarding shim, so both sides share one real writer.
            const realWriter = real.getWriter();
            state.writer = realWriter;
            target.__m8writable = {
              getWriter() {
                return {
                  write: (c) => realWriter.write(c),
                  close: () => realWriter.close(),
                  abort: (r) => realWriter.abort(r),
                  releaseLock: () => {},
                  get ready()  { return realWriter.ready; },
                  get closed() { return realWriter.closed; },
                  get desiredSize() { return realWriter.desiredSize; },
                };
              },
              get locked() { return true; },
            };
          }
          return target.__m8writable;
        }

        const v = target[prop];
        if (typeof v === "function") {
          return (...args) => {
            const r = v.apply(target, args);
            if (prop === "open") {
              state.open = true; state.port = target;
              if (r && typeof r.then === "function") r.then(() => { state.open = true; }, () => {});
            }
            if (prop === "close") { state.open = false; state.writer = null; }
            return r;
          };
        }
        return v;
      },
    };
    return new Proxy(port, handler);
  }

  if (navigator.serial) {
    const s = navigator.serial;
    const origGetPorts = s.getPorts.bind(s);
    const origRequest  = s.requestPort.bind(s);
    Object.defineProperty(navigator, "serial", {
      configurable: true,
      value: new Proxy(s, {
        get(t, p) {
          if (p === "getPorts")    return async (...a) => (await origGetPorts(...a)).map(wrapPort);
          if (p === "requestPort") return async (...a) => wrapPort(await origRequest(...a));
          const v = t[p];
          return typeof v === "function" ? v.bind(t) : v;
        },
      }),
    });
  }

  // ---- control surface ----------------------------------------------------

  const sleep = (ms) => new Promise(r => setTimeout(r, ms));

  async function send(bytes) {
    if (!state.writer) throw new Error("m8web: no serial writer yet (page not connected)");
    await state.writer.write(new Uint8Array(bytes));
  }

  window.__m8 = {
    ready: () => !!state.writer && state.frames > 0,
    grid: () => ({
      cells: [...state.cells.values()],
      highlights: state.highlights,
      fw: state.fw,
    }),
    text: (mainOnly = true) => {
      const rows = textRows(mainOnly);
      return { rows, plain: rows.map(r => r.text).join("\n") };
    },
    cursor: cursorCell,
    stats: () => ({
      bytes: state.bytes, frames: state.frames, revision: state.revision,
      cells: state.cells.size, highlights: state.highlights.length,
      open: state.open, hasWriter: !!state.writer, error: state.error, fw: state.fw,
    }),
    send,
    press: async (mask, holdMs = 40) => {
      await send([0x43, mask & 0xff]);        // 'C' <mask>
      await sleep(holdMs);
      await send([0x43, 0x00]);               // 'C' 0x00 release
      await sleep(30);
    },
    keyjazz: (note, vel = 0x7f) => send([0x4b, note & 0xff, vel & 0xff]),  // 'K'
    requestFullRedraw: () => send([0x52]),                                 // 'R'
    enableDisplay:     () => send([0x45]),                                 // 'E'
  };
})();

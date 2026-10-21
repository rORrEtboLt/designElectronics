// designElectronics web loader, shared by /app/ (full editor) and /embed/ (viewer).
//
// Link parameters (after #, joined with &):
//   d=<data>       the design itself: deflate-raw + base64url of its compact JSON
//   src=<url>      or a URL of a .dez file (the host must allow CORS, e.g. GitHub raw or a gist)
//   example=<n>    or a built-in example (0-6)
//   view=breadboard|schematic|pcb   start view
//   sim=1          start simulating
(function () {
  const EMBED = document.documentElement.dataset.mode === 'embed';
  const APP_BASE = new URL(EMBED ? '../app/' : './', location.href).href;
  const status = document.getElementById('status');
  const setStatus = (t, isError) => {
    if (!status) return;
    status.textContent = t;
    status.classList.toggle('error', !!isError);
    status.hidden = !t;
  };

  function params() {
    const out = {};
    for (const part of location.hash.replace(/^#/, '').split('&')) {
      if (!part) continue;
      const i = part.indexOf('=');
      const k = i < 0 ? part : part.slice(0, i);
      const v = i < 0 ? '' : part.slice(i + 1);
      out[k] = k === 'd' ? v : decodeURIComponent(v);
    }
    return out;
  }

  async function inflate(b64url) {
    let b64 = b64url.replace(/-/g, '+').replace(/_/g, '/');
    while (b64.length % 4) b64 += '=';
    const bytes = Uint8Array.from(atob(b64), c => c.charCodeAt(0));
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
    return await new Response(stream).text();
  }

  async function designText(p) {
    if (p.d) return await inflate(p.d);
    if (p.src) {
      const r = await fetch(p.src);
      if (!r.ok) throw new Error('Could not load ' + p.src + ' (' + r.status + ')');
      return await r.text();
    }
    return null;
  }

  async function start() {
    const p = params();
    let text = null;
    try {
      setStatus('Loading…');
      text = await designText(p);
    } catch (e) {
      setStatus('Could not open the design: ' + e.message, true);
      if (EMBED) return;
    }
    const args = [];
    if (EMBED) {
      args.push('--embed');
      // "Open in editor" opens the same design in the full web app
      const keep = location.hash.replace(/^#/, '').split('&').filter(s => /^(d|src|example)=/.test(s)).join('&');
      args.push('--editor-url', new URL('../app/', location.href).href + (keep ? '#' + keep : ''));
    }
    if (p.view) args.push('--view', p.view === 'schematic' ? 'sch' : p.view);
    if (p.sim === '1') args.push('--sim');
    if (p.example !== undefined && !text) args.push('--load-example', String(parseInt(p.example, 10) || 0));

    window.Module = {
      canvas: document.getElementById('canvas'),
      locateFile: f => APP_BASE + f,
      print: t => console.log(t),
      printErr: t => console.warn(t),
      onRuntimeInitialized() {
        if (text !== null) {
          Module.FS.writeFile('/design.dez', text);
          args.push('/design.dez');
        }
        setStatus('');
        Module.callMain(args);
      },
    };
    const s = document.createElement('script');
    s.src = APP_BASE + 'designElectronics.js';
    s.onerror = () => setStatus('Could not load the app.', true);
    document.body.appendChild(s);
  }

  // a new link in the address bar (e.g. pasted) reloads the design
  window.addEventListener('hashchange', () => location.reload());
  start();
})();

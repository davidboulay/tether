// A small in-page toast rendered inside a closed shadow root, so neither the
// page's CSS nor its scripts can restyle or reach into it. Used for "Code from
// iPhone filled" (with Undo) and for the "fill on this site?" prompt.

export const TOAST_HOST_ID = 'tether-otp-toast-host';
export const DEFAULT_TOAST_MS = 6000;

const STYLE = `
:host { all: initial; }
.wrap {
  position: fixed;
  right: 16px;
  bottom: 16px;
  z-index: 2147483647;
  max-width: min(360px, calc(100vw - 32px));
  box-sizing: border-box;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border-radius: 10px;
  font: 500 13px/1.35 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  color: #f5f5f5;
  background: #1f2328;
  border: 1px solid rgba(255,255,255,0.12);
  box-shadow: 0 6px 24px rgba(0,0,0,0.35);
  opacity: 0;
  transform: translateY(8px);
  transition: opacity 160ms ease-out, transform 160ms ease-out;
}
.wrap.show { opacity: 1; transform: none; }
.wrap.hide { opacity: 0; transform: translateY(8px); }
@media (prefers-color-scheme: light) {
  .wrap { color: #1f2328; background: #ffffff; border-color: rgba(0,0,0,0.12); }
}
@media (prefers-reduced-motion: reduce) {
  .wrap { transition: none; transform: none; }
}
.icon {
  flex: none;
  width: 18px; height: 18px;
  border-radius: 50%;
  background: #2f81f7;
  display: inline-flex; align-items: center; justify-content: center;
  color: #fff; font-size: 11px; font-weight: 700;
}
.msg { flex: 1 1 auto; min-width: 0; }
.actions { flex: none; display: flex; gap: 6px; }
button {
  all: unset;
  cursor: pointer;
  padding: 5px 10px;
  border-radius: 6px;
  font: inherit;
  font-weight: 600;
  color: inherit;
  background: rgba(127,127,127,0.18);
}
button.primary { background: #2f81f7; color: #fff; }
button:hover { filter: brightness(1.1); }
button:focus-visible { outline: 2px solid #2f81f7; outline-offset: 2px; }
button.close { padding: 4px 7px; opacity: 0.7; }
`;

function prefersReducedMotion(view) {
  try {
    return !!(view && view.matchMedia && view.matchMedia('(prefers-reduced-motion: reduce)').matches);
  } catch {
    return false;
  }
}

// Remove any toast this script previously put in the document.
export function dismissToast(doc) {
  const existing = doc.getElementById(TOAST_HOST_ID);
  if (existing) {
    const close = existing.__tetherToastClose;
    if (typeof close === 'function') close(true);
    else existing.remove();
  }
}

// opts: { message, actions: [{ label, primary, onClick }], timeoutMs, sticky }
// Returns { host, root, close(immediate) }. `root` is the closed shadow root;
// it is only reachable through this return value, never from the page.
export function showToast(doc, opts = {}) {
  dismissToast(doc);

  const view = doc.defaultView;
  const host = doc.createElement('div');
  host.id = TOAST_HOST_ID;
  host.setAttribute('data-tether', 'otp-toast');
  const root = host.attachShadow({ mode: 'closed' });

  const style = doc.createElement('style');
  style.textContent = STYLE;
  root.appendChild(style);

  const wrap = doc.createElement('div');
  wrap.className = 'wrap';
  wrap.setAttribute('role', 'status');
  wrap.setAttribute('aria-live', 'polite');
  wrap.setAttribute('aria-atomic', 'true');

  const icon = doc.createElement('span');
  icon.className = 'icon';
  icon.setAttribute('aria-hidden', 'true');
  icon.textContent = 'T';

  const msg = doc.createElement('span');
  msg.className = 'msg';
  msg.textContent = String(opts.message || '');

  const actions = doc.createElement('span');
  actions.className = 'actions';

  let closed = false;
  let timer = null;
  const reduced = prefersReducedMotion(view);

  function close(immediate = false) {
    if (closed) return;
    closed = true;
    if (timer) {
      (view || globalThis).clearTimeout(timer);
      timer = null;
    }
    if (immediate || reduced) {
      host.remove();
      return;
    }
    wrap.classList.remove('show');
    wrap.classList.add('hide');
    (view || globalThis).setTimeout(() => host.remove(), 200);
  }

  for (const a of opts.actions || []) {
    const btn = doc.createElement('button');
    btn.type = 'button';
    btn.textContent = a.label;
    if (a.primary) btn.classList.add('primary');
    btn.addEventListener('click', (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      try {
        a.onClick?.();
      } finally {
        close();
      }
    });
    actions.appendChild(btn);
  }

  const dismiss = doc.createElement('button');
  dismiss.type = 'button';
  dismiss.className = 'close';
  dismiss.setAttribute('aria-label', 'Dismiss');
  dismiss.textContent = '✕';
  dismiss.addEventListener('click', (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    opts.onDismiss?.();
    close();
  });
  actions.appendChild(dismiss);

  wrap.appendChild(icon);
  wrap.appendChild(msg);
  wrap.appendChild(actions);
  root.appendChild(wrap);

  // Escape dismisses the toast while focus is inside it.
  wrap.addEventListener('keydown', (ev) => {
    if (ev.key === 'Escape') {
      ev.stopPropagation();
      opts.onDismiss?.();
      close();
    }
  });

  (doc.body || doc.documentElement).appendChild(host);
  // Next frame so the transition runs; skip the dance under reduced motion.
  if (reduced || !view || !view.requestAnimationFrame) {
    wrap.classList.add('show');
  } else {
    view.requestAnimationFrame(() => wrap.classList.add('show'));
  }

  if (!opts.sticky) {
    const ms = Number.isFinite(opts.timeoutMs) ? opts.timeoutMs : DEFAULT_TOAST_MS;
    timer = (view || globalThis).setTimeout(() => close(), ms);
  }

  // Only the close function is parked on the host, so a later dismissToast()
  // can retire this toast without exposing its shadow root to the page.
  Object.defineProperty(host, '__tetherToastClose', { value: close, enumerable: false });
  return { host, root, close };
}

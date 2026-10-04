// Toolbar popup: connection state, the last code, and "Fill on this page".
// Plain DOM, bundled by esbuild together with the shared helpers.

import { describeHostState } from '../shared/hoststate.js';
import { maskCode, formatAge } from '../shared/format.js';

const api = typeof chrome !== 'undefined' ? chrome : (typeof browser !== 'undefined' ? browser : undefined);

// Why a manual fill did nothing, in the user's words.
export const FILL_REASONS = Object.freeze({
  no_code: 'No code to fill yet.',
  no_tab: 'No active tab.',
  no_tabs_api: 'Cannot reach tabs here.',
  no_content_script: 'Tether cannot run on this page.',
  no_fields: 'No code field found on this page.',
  hidden: 'Switch to the tab first.',
  not_filled: 'The code is already in the field.',
  autofill_off: 'Autofill is turned off.',
  asked: 'Answer the prompt on the page.',
});

export function fillOutcomeText(response) {
  if (response && response.filled) return 'Filled.';
  const reason = response && response.reason;
  return FILL_REASONS[reason] || 'Nothing was filled.';
}

function send(message) {
  return new Promise((resolve) => {
    try {
      const r = api.runtime.sendMessage(message, (response) => {
        void (api.runtime && api.runtime.lastError);
        resolve(response);
      });
      if (r && typeof r.then === 'function') r.then(resolve, () => resolve(undefined));
    } catch (e) {
      resolve(undefined);
    }
  });
}

async function copyText(text) {
  try {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      await navigator.clipboard.writeText(text);
      return true;
    }
  } catch (e) { /* fall through */ }
  try {
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.setAttribute('readonly', '');
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.appendChild(ta);
    ta.select();
    const ok = document.execCommand('copy');
    ta.remove();
    return ok;
  } catch (e) {
    return false;
  }
}

if (typeof document !== 'undefined' && document.getElementById('status-label')) {
  const $ = (id) => document.getElementById(id);
  const el = {
    dot: $('status-dot'),
    label: $('status-label'),
    detail: $('status-detail'),
    statusActions: $('status-actions'),
    retry: $('retry'),
    codeEmpty: $('code-empty'),
    codeBox: $('code-box'),
    codeValue: $('code-value'),
    codeMeta: $('code-meta'),
    show: $('show'),
    copy: $('copy'),
    fill: $('fill'),
    feedback: $('feedback'),
    settings: $('settings'),
  };

  let lastOtp = null;
  let revealed = false;
  let feedbackTimer = null;

  function feedback(text, cls = '') {
    el.feedback.textContent = text;
    el.feedback.className = 'feedback ' + cls;
    if (feedbackTimer) clearTimeout(feedbackTimer);
    if (text) feedbackTimer = setTimeout(() => { el.feedback.textContent = ''; el.feedback.className = 'feedback'; }, 4000);
  }

  function renderState(state) {
    const d = describeHostState(state);
    el.dot.className = 'dot ' + d.state;
    el.label.textContent = d.label;
    // Render the install hint as <code> so it is easy to select and copy.
    el.detail.textContent = '';
    const parts = d.detail.split('`');
    parts.forEach((part, i) => {
      if (i % 2 === 1) {
        const c = document.createElement('code');
        c.textContent = part;
        el.detail.appendChild(c);
      } else if (part) {
        el.detail.appendChild(document.createTextNode(part));
      }
    });
    el.statusActions.hidden = d.ok || d.state === 'unknown';
    el.retry.textContent = d.state === 'host-missing' ? 'Check again' : 'Retry connection';
  }

  function renderCode() {
    const has = !!(lastOtp && lastOtp.otp);
    el.codeEmpty.hidden = has;
    el.codeBox.hidden = !has;
    el.fill.disabled = !has;
    if (!has) return;
    el.codeValue.textContent = revealed ? lastOtp.otp : maskCode(lastOtp.otp);
    el.codeValue.setAttribute('aria-label', revealed ? `Last code ${lastOtp.otp.split('').join(' ')}` : 'Last code, hidden');
    el.show.textContent = revealed ? 'Hide' : 'Show';
    el.show.setAttribute('aria-pressed', revealed ? 'true' : 'false');
    const bits = [];
    const age = formatAge(lastOtp.ts);
    if (age) bits.push(`Arrived ${age}`);
    if (lastOtp.sender_domain) bits.push(`for ${lastOtp.sender_domain}`);
    el.codeMeta.textContent = bits.join(' · ');
  }

  async function refresh() {
    const r = await send({ action: 'get_state' });
    if (!r) {
      renderState('unknown');
      return;
    }
    renderState(r.hostState || 'unknown');
    lastOtp = r.lastOtp || null;
    renderCode();
  }

  el.show.addEventListener('click', () => {
    revealed = !revealed;
    renderCode();
  });

  el.copy.addEventListener('click', async () => {
    if (!lastOtp || !lastOtp.otp) return;
    const ok = await copyText(lastOtp.otp);
    feedback(ok ? 'Copied.' : 'Could not copy.', ok ? 'ok' : 'bad');
  });

  el.fill.addEventListener('click', async () => {
    el.fill.disabled = true;
    const r = await send({ action: 'fill_active_tab' });
    el.fill.disabled = !(lastOtp && lastOtp.otp);
    feedback(fillOutcomeText(r), r && r.filled ? 'ok' : 'bad');
    if (r && r.filled) setTimeout(() => window.close(), 700);
  });

  el.retry.addEventListener('click', async () => {
    renderState('unknown');
    await send({ action: 'reconnect_host' });
    // Give the host a moment to answer (or fail) before reading the state.
    setTimeout(refresh, 800);
  });

  el.settings.addEventListener('click', () => {
    if (api.runtime.openOptionsPage) api.runtime.openOptionsPage();
  });

  // Re-render the age line and pick up a code that arrives while open.
  setInterval(refresh, 5000);
  refresh();
}

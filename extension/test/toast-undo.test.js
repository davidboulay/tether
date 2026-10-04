import { describe, it, expect, vi, beforeEach } from 'vitest';
import { JSDOM } from 'jsdom';
import * as fs from 'fs';
import * as path from 'path';

const loadFixture = (filename) => {
  const html = fs.readFileSync(path.join(__dirname, 'fixtures', filename), 'utf8');
  return new JSDOM(html).window.document;
};

global.chrome = {
  runtime: { sendMessage: vi.fn(), onMessage: { addListener: vi.fn() }, lastError: null }
};

const { handleFillOtp, undoFill, captureFieldValues, hasVisibleOtpFields, _resetFilledOtps } =
  await import('../src/content/autofill.js');
const { showToast, dismissToast, TOAST_HOST_ID } = await import('../src/content/toast.js');
const { maskCode, formatAge } = await import('../src/shared/format.js');

describe('undo after a fill', () => {
  beforeEach(() => _resetFilledOtps());

  it('hands the caller an undo that restores the previous value', () => {
    const doc = loadFixture('std-autocomplete.html');
    const input = doc.querySelector('input');
    input.value = 'abc';
    let detail = null;

    const result = handleFillOtp(doc, { otp: '123456', otp_id: 1 }, { onFill: (d) => { detail = d; } });

    expect(result).toEqual({ filled: true });
    expect(input.value).toBe('123456');
    expect(detail.otpId).toBe(1);
    expect(detail.fields).toEqual([input]);
    expect(detail.previousValues).toEqual(['abc']);

    expect(detail.undo()).toBe(true);
    expect(input.value).toBe('abc');
    // one-shot
    expect(detail.undo()).toBe(false);
  });

  it('dispatches input and change on undo, like the fill does', () => {
    const doc = loadFixture('std-autocomplete.html');
    const input = doc.querySelector('input');
    let detail = null;
    handleFillOtp(doc, { otp: '123456', otp_id: 1 }, { onFill: (d) => { detail = d; } });

    const seen = [];
    input.addEventListener('input', () => seen.push('input'));
    input.addEventListener('change', () => seen.push('change'));
    detail.undo();

    expect(seen).toEqual(['input', 'change']);
    expect(input.value).toBe('');
  });

  it('restores every split field', () => {
    const doc = loadFixture('split-otp.html');
    const inputs = [...doc.querySelectorAll('input')].slice(0, 6);
    inputs[0].value = '9';
    let detail = null;
    handleFillOtp(doc, { otp: '123456', otp_id: 2 }, { onFill: (d) => { detail = d; } });
    expect(inputs.map((i) => i.value).join('')).toBe('123456');

    detail.undo();
    expect(inputs.map((i) => i.value)).toEqual(['9', '', '', '', '', '']);
  });

  it('lets the same otp_id be filled again after an undo', () => {
    const doc = loadFixture('std-autocomplete.html');
    let detail = null;
    handleFillOtp(doc, { otp: '123456', otp_id: 3 }, { onFill: (d) => { detail = d; } });
    detail.undo();
    expect(handleFillOtp(doc, { otp: '123456', otp_id: 3 }).filled).toBe(true);
  });

  it('exposes the pure helpers', () => {
    const doc = new JSDOM('<input value="a"><input value="b">').window.document;
    const fields = [...doc.querySelectorAll('input')];
    const snap = captureFieldValues(fields);
    expect(snap).toEqual(['a', 'b']);
    fields[0].value = 'x';
    fields[1].value = 'y';
    undoFill(fields, snap);
    expect(fields.map((f) => f.value)).toEqual(['a', 'b']);
  });

  it('reports whether the page can take a code', () => {
    expect(hasVisibleOtpFields(loadFixture('std-autocomplete.html'))).toBe(true);
    expect(hasVisibleOtpFields(new JSDOM('<input type="password">').window.document)).toBe(false);
    expect(hasVisibleOtpFields(new JSDOM('<input autocomplete="one-time-code" hidden>').window.document)).toBe(false);
  });
});

describe('toast', () => {
  it('renders a status region inside a shadow root and runs the action', () => {
    vi.useFakeTimers();
    const doc = new JSDOM('<body></body>').window.document;
    const onUndo = vi.fn();

    const t = showToast(doc, { message: 'Code from iPhone filled', actions: [{ label: 'Undo', onClick: onUndo }] });

    const host = doc.getElementById(TOAST_HOST_ID);
    expect(host).toBe(t.host);
    // Closed shadow root: the page cannot reach in.
    expect(host.shadowRoot).toBeNull();
    // Nothing of ours leaks into the light DOM.
    expect(host.textContent).toBe('');

    // The root is closed; only the handle returned to the caller can see it.
    const wrap = t.root.querySelector('.wrap');
    expect(wrap.getAttribute('role')).toBe('status');
    expect(wrap.getAttribute('aria-live')).toBe('polite');
    const undoBtn = [...wrap.querySelectorAll('button')].find((b) => b.textContent === 'Undo');
    expect(undoBtn.type).toBe('button');
    undoBtn.dispatchEvent(new doc.defaultView.MouseEvent('click', { bubbles: true, cancelable: true }));
    expect(onUndo).toHaveBeenCalledTimes(1);

    vi.runAllTimers();
    expect(doc.getElementById(TOAST_HOST_ID)).toBeNull();
    vi.useRealTimers();
  });

  it('auto-hides after 6 seconds', () => {
    vi.useFakeTimers();
    const doc = new JSDOM('<body></body>').window.document;
    showToast(doc, { message: 'hi' });
    vi.advanceTimersByTime(5999);
    expect(doc.getElementById(TOAST_HOST_ID)).not.toBeNull();
    vi.advanceTimersByTime(1 + 250);
    expect(doc.getElementById(TOAST_HOST_ID)).toBeNull();
    vi.useRealTimers();
  });

  it('replaces a previous toast and can be dismissed', () => {
    const doc = new JSDOM('<body></body>').window.document;
    showToast(doc, { message: 'one', sticky: true });
    showToast(doc, { message: 'two', sticky: true });
    expect(doc.querySelectorAll('#' + TOAST_HOST_ID)).toHaveLength(1);
    dismissToast(doc);
    // sticky toasts close immediately when replaced/dismissed via the handle
    expect(doc.getElementById(TOAST_HOST_ID)).toBeNull();
  });
});

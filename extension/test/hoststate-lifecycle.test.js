// The native port lifecycle drives hostState: a first message means
// connected, a disconnect is classified from its error text, and a code seen
// on the port is remembered for the popup.
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

let onMessage = null;
let onDisconnect = null;

const mockPort = {
  postMessage: vi.fn(),
  disconnect: vi.fn(),
  onMessage: { addListener: vi.fn((cb) => { onMessage = cb; }) },
  onDisconnect: { addListener: vi.fn((cb) => { onDisconnect = cb; }) }
};

beforeEach(() => {
  vi.useFakeTimers();
  onMessage = null;
  onDisconnect = null;
  global.browser = undefined;
  global.chrome = {
    runtime: { connectNative: vi.fn().mockReturnValue(mockPort), lastError: null },
    tabs: { query: vi.fn((_f, cb) => cb([])), sendMessage: vi.fn() }
  };
});
afterEach(() => vi.useRealTimers());

const {
  connectToNativeHost, reconnectNativeHost, getHostState, onHostStateChange,
  getLastOtp, clearLastOtp, onOtpReceived, _resetHostState, _resetOtpRequests
} = await import('../src/shared/native.js');

describe('host state lifecycle', () => {
  beforeEach(() => { _resetHostState(); _resetOtpRequests(); });

  it('starts unknown and becomes connected on the first frame from the host', () => {
    const seen = [];
    const off = onHostStateChange((s) => seen.push(s.state));
    expect(getHostState().state).toBe('unknown');

    connectToNativeHost();
    expect(getHostState().state).toBe('unknown');
    // tetherd answers the host's subscribe with protocol_info straight away.
    onMessage({ command: 'protocol_info', version: 1 });
    expect(getHostState().state).toBe('connected');
    expect(seen).toEqual(['connected']);
    off();
  });

  it('classifies a missing host from the disconnect error', () => {
    connectToNativeHost();
    onDisconnect({ error: { message: 'Specified native messaging host not found.' } });
    expect(getHostState()).toEqual({
      state: 'host-missing',
      error: 'Specified native messaging host not found.'
    });
  });

  it('reads runtime.lastError when the port carries no error (Chrome)', () => {
    connectToNativeHost();
    chrome.runtime.lastError = { message: 'Native host has exited.' };
    onDisconnect({});
    chrome.runtime.lastError = null;
    expect(getHostState().state).toBe('daemon-down');
  });

  it('treats a drop of a live connection as daemon-down', () => {
    connectToNativeHost();
    onMessage({ command: 'protocol_info' });
    expect(getHostState().state).toBe('connected');
    onDisconnect({});
    expect(getHostState().state).toBe('daemon-down');
  });

  it('schedules a reconnect with backoff and reconnectNativeHost resets it', () => {
    connectToNativeHost();
    expect(chrome.runtime.connectNative).toHaveBeenCalledTimes(1);
    onDisconnect({ error: { message: 'Native host has exited.' } });

    vi.advanceTimersByTime(2999);
    expect(chrome.runtime.connectNative).toHaveBeenCalledTimes(1);
    vi.advanceTimersByTime(1);
    expect(chrome.runtime.connectNative).toHaveBeenCalledTimes(2);

    // Second failure waits twice as long.
    onDisconnect({ error: { message: 'Native host has exited.' } });
    vi.advanceTimersByTime(5999);
    expect(chrome.runtime.connectNative).toHaveBeenCalledTimes(2);
    vi.advanceTimersByTime(1);
    expect(chrome.runtime.connectNative).toHaveBeenCalledTimes(3);

    // The popup's Retry connects now.
    onDisconnect({ error: { message: 'Native host has exited.' } });
    reconnectNativeHost();
    expect(chrome.runtime.connectNative).toHaveBeenCalledTimes(4);
  });

  it('remembers the last code and tells listeners', () => {
    const got = [];
    const off = onOtpReceived((o) => got.push(o));
    connectToNativeHost();
    expect(getLastOtp()).toBeNull();

    onMessage({ command: 'otp_available', otp: '123456', otp_id: 7, sender_domain: 'amazon.com' });
    expect(getLastOtp()).toMatchObject({ otp: '123456', otp_id: 7, sender_domain: 'amazon.com' });
    expect(getLastOtp().ts).toBeTypeOf('number');
    expect(got).toHaveLength(1);

    // The daemon replays the same id on reconnect: not a new arrival.
    onMessage({ command: 'otp_available', otp: '123456', otp_id: 7 });
    expect(got).toHaveLength(1);

    // An empty reply to request_otp is not a code.
    onMessage({ command: 'otp_available', otp: '', otp_id: 0 });
    expect(getLastOtp().otp).toBe('123456');

    clearLastOtp();
    expect(getLastOtp()).toBeNull();
    off();
  });
});

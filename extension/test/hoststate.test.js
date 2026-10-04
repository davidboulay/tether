import { describe, it, expect } from 'vitest';
import { deriveHostState, describeHostState, INSTALL_HINT, HOST_STATES } from '../src/shared/hoststate.js';

describe('deriveHostState', () => {
  it('recognises the Chrome "host not found" family as host-missing', () => {
    expect(deriveHostState('Specified native messaging host not found.')).toBe('host-missing');
    expect(deriveHostState('Access to the specified native messaging host is forbidden.')).toBe('host-missing');
    expect(deriveHostState('Invalid native messaging host name specified.')).toBe('host-missing');
    expect(deriveHostState('Failed to start native messaging host.')).toBe('host-missing');
  });

  it('recognises the Firefox "no such native application" as host-missing', () => {
    expect(deriveHostState('No such native application com.tether.extension')).toBe('host-missing');
  });

  it('treats a host that launched and exited as daemon-down', () => {
    // This is what `tether --native-host` does when tetherd's socket is absent:
    // it tries to spawn the daemon once, then exits 1 without any stdout.
    expect(deriveHostState('Native host has exited.')).toBe('daemon-down');
    expect(deriveHostState('Error when communicating with the native messaging host.')).toBe('daemon-down');
    expect(deriveHostState('An unexpected error occurred')).toBe('daemon-down');
  });

  it('treats a drop after messages flowed as daemon-down regardless of text', () => {
    expect(deriveHostState('', { hadMessages: true })).toBe('daemon-down');
    expect(deriveHostState('unknown reason', { hadMessages: true })).toBe('daemon-down');
  });

  it('treats a silent drop before any message as daemon-down, not host-missing', () => {
    expect(deriveHostState('unknown reason')).toBe('daemon-down');
    expect(deriveHostState(undefined)).toBe('daemon-down');
  });

  it('never lets a missing host be misread after messages', () => {
    // Cannot happen in practice, but the text wins so the hint stays right.
    expect(deriveHostState('Specified native messaging host not found.', { hadMessages: true })).toBe('host-missing');
  });
});

describe('describeHostState', () => {
  it('shows no badge when connected', () => {
    const d = describeHostState('connected');
    expect(d.badge).toBe('');
    expect(d.ok).toBe(true);
    expect(d.label).toBe('Connected to Tether');
  });

  it('shows a red "!" for the two failure states', () => {
    for (const s of ['daemon-down', 'host-missing']) {
      const d = describeHostState(s);
      expect(d.badge).toBe('!');
      expect(d.badgeColor).toMatch(/^#/);
      expect(d.ok).toBe(false);
      expect(d.title).toMatch(/^Tether: /);
    }
  });

  it('points a missing host at the real install verb', () => {
    const d = describeHostState('host-missing');
    expect(INSTALL_HINT).toBe('tether --install-extension-host');
    expect(d.detail).toContain(INSTALL_HINT);
    expect(d.title).toContain(INSTALL_HINT);
  });

  it('names the daemon when it is down', () => {
    expect(describeHostState('daemon-down').label).toBe('Tether daemon not running');
  });

  it('falls back to unknown for anything else', () => {
    expect(describeHostState('garbage').state).toBe('unknown');
    expect(describeHostState(undefined).badge).toBe('');
    expect(HOST_STATES).toContain('unknown');
  });
});

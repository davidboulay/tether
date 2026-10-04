import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  DEFAULT_SETTINGS,
  mergeSettings,
  isKnownSite,
  addKnownSite,
  MAX_KNOWN_SITES,
  loadSettings,
  saveSettings,
  rememberSite,
  loadKnownSites,
  SETTINGS_KEY,
  KNOWN_SITES_KEY,
} from '../src/shared/settings.js';

describe('mergeSettings', () => {
  it('has the documented defaults', () => {
    expect(DEFAULT_SETTINGS).toEqual({ autofill: true, toast: true, askUnknownSites: false });
  });

  it('returns the defaults for nothing stored', () => {
    expect(mergeSettings(undefined)).toEqual(DEFAULT_SETTINGS);
    expect(mergeSettings(null)).toEqual(DEFAULT_SETTINGS);
    expect(mergeSettings('junk')).toEqual(DEFAULT_SETTINGS);
  });

  it('overlays stored values and drops unknown keys', () => {
    const m = mergeSettings({ autofill: false, bogus: 1 });
    expect(m).toEqual({ autofill: false, toast: true, askUnknownSites: false });
    expect(m).not.toHaveProperty('bogus');
  });

  it('coerces non-boolean values', () => {
    expect(mergeSettings({ toast: 0, askUnknownSites: 'yes' })).toMatchObject({ toast: false, askUnknownSites: true });
  });

  it('does not mutate the defaults', () => {
    const m = mergeSettings({ autofill: false });
    m.toast = false;
    expect(DEFAULT_SETTINGS.autofill).toBe(true);
    expect(DEFAULT_SETTINGS.toast).toBe(true);
  });
});

describe('known sites', () => {
  it('matches hostnames case-insensitively and ignores trailing dots', () => {
    expect(isKnownSite('Login.Example.com.', ['login.example.com'])).toBe(true);
    expect(isKnownSite('other.example.com', ['login.example.com'])).toBe(false);
    expect(isKnownSite('', ['login.example.com'])).toBe(false);
    expect(isKnownSite('a.com', null)).toBe(false);
  });

  it('adds to the front without duplicates', () => {
    expect(addKnownSite('b.com', ['a.com'])).toEqual(['b.com', 'a.com']);
    expect(addKnownSite('A.com', ['b.com', 'a.com'])).toEqual(['a.com', 'b.com']);
    expect(addKnownSite('', ['a.com'])).toEqual(['a.com']);
  });

  it('caps the list', () => {
    const many = Array.from({ length: MAX_KNOWN_SITES }, (_, i) => `s${i}.com`);
    const out = addKnownSite('new.com', many);
    expect(out).toHaveLength(MAX_KNOWN_SITES);
    expect(out[0]).toBe('new.com');
    expect(out).not.toContain(`s${MAX_KNOWN_SITES - 1}.com`);
  });
});

describe('storage plumbing', () => {
  let store;
  beforeEach(() => {
    store = {};
    global.browser = undefined;
    global.chrome = {
      runtime: { lastError: null },
      storage: {
        sync: {
          get: vi.fn((keys, cb) => {
            const out = {};
            for (const k of keys) if (k in store) out[k] = store[k];
            cb(out);
          }),
          set: vi.fn((obj, cb) => { Object.assign(store, obj); cb(); }),
        },
        onChanged: { addListener: vi.fn(), removeListener: vi.fn() },
      },
    };
  });

  it('loads defaults from an empty sync area', async () => {
    expect(await loadSettings()).toEqual(DEFAULT_SETTINGS);
  });

  it('round-trips a partial save through storage.sync', async () => {
    const next = await saveSettings({ askUnknownSites: true });
    expect(next).toEqual({ autofill: true, toast: true, askUnknownSites: true });
    expect(store[SETTINGS_KEY]).toEqual(next);
    expect(await loadSettings()).toEqual(next);
  });

  it('remembers sites in storage.sync', async () => {
    await rememberSite('login.example.com');
    await rememberSite('shop.example.org');
    expect(store[KNOWN_SITES_KEY]).toEqual(['shop.example.org', 'login.example.com']);
    expect(await loadKnownSites()).toEqual(['shop.example.org', 'login.example.com']);
  });

  it('falls back to defaults when there is no storage API at all', async () => {
    global.chrome = { runtime: {} };
    expect(await loadSettings()).toEqual(DEFAULT_SETTINGS);
    expect(await loadKnownSites()).toEqual([]);
    expect(await saveSettings({ toast: false })).toMatchObject({ toast: false });
  });
});

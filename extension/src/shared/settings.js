// User settings, persisted in storage.sync so they follow the browser profile.
// Works in the background worker, content scripts and the extension pages.
// Everything degrades to the defaults when the storage API is absent (tests,
// or the Thunderbird build, whose manifest does not request "storage").

export const DEFAULT_SETTINGS = Object.freeze({
  // Fill a code into the page as soon as it arrives.
  autofill: true,
  // Show the in-page "Code from iPhone filled" toast with Undo.
  toast: true,
  // On a hostname that has never been filled before, ask first.
  askUnknownSites: false,
});

export const SETTINGS_KEY = 'settings';
export const KNOWN_SITES_KEY = 'knownSites';
export const MAX_KNOWN_SITES = 200;

// Merge whatever was stored over the defaults, dropping unknown keys and
// coercing each value to a boolean so a corrupt entry cannot disable a guard.
export function mergeSettings(stored) {
  const out = { ...DEFAULT_SETTINGS };
  if (!stored || typeof stored !== 'object') return out;
  for (const key of Object.keys(DEFAULT_SETTINGS)) {
    if (Object.prototype.hasOwnProperty.call(stored, key)) {
      out[key] = Boolean(stored[key]);
    }
  }
  return out;
}

function normalizeHost(host) {
  return String(host || '').trim().toLowerCase().replace(/\.+$/, '');
}

export function isKnownSite(host, knownSites) {
  const h = normalizeHost(host);
  if (!h || !Array.isArray(knownSites)) return false;
  return knownSites.includes(h);
}

// Returns a new list with `host` moved to the front, capped at MAX_KNOWN_SITES.
export function addKnownSite(host, knownSites) {
  const h = normalizeHost(host);
  const list = Array.isArray(knownSites) ? knownSites.filter((s) => typeof s === 'string' && s !== h) : [];
  if (!h) return list.slice(0, MAX_KNOWN_SITES);
  return [h, ...list].slice(0, MAX_KNOWN_SITES);
}

// ---------------------------------------------------------------------------
// Storage plumbing. Uses the callback form of chrome.storage, which Chrome,
// Firefox and Thunderbird all accept, and falls back to the promise-returning
// browser.storage where only that exists.
// ---------------------------------------------------------------------------

function storageApi() {
  if (typeof chrome !== 'undefined' && chrome.storage) return chrome;
  if (typeof browser !== 'undefined' && browser.storage) return browser;
  return null;
}

function syncArea() {
  const api = storageApi();
  if (!api) return null;
  return api.storage.sync || api.storage.local || null;
}

function call(area, method, arg) {
  return new Promise((resolve) => {
    try {
      const ret = area[method](arg, (result) => {
        void (typeof chrome !== 'undefined' && chrome.runtime && chrome.runtime.lastError);
        resolve(result);
      });
      if (ret && typeof ret.then === 'function') {
        ret.then(resolve, () => resolve(undefined));
      }
    } catch (e) {
      resolve(undefined);
    }
  });
}

export async function loadSettings() {
  const area = syncArea();
  if (!area) return { ...DEFAULT_SETTINGS };
  const result = await call(area, 'get', [SETTINGS_KEY]);
  return mergeSettings(result && result[SETTINGS_KEY]);
}

export async function saveSettings(partial) {
  const area = syncArea();
  const current = await loadSettings();
  const next = mergeSettings({ ...current, ...(partial || {}) });
  if (area) await call(area, 'set', { [SETTINGS_KEY]: next });
  return next;
}

export async function loadKnownSites() {
  const area = syncArea();
  if (!area) return [];
  const result = await call(area, 'get', [KNOWN_SITES_KEY]);
  const list = result && result[KNOWN_SITES_KEY];
  return Array.isArray(list) ? list.filter((s) => typeof s === 'string') : [];
}

export async function rememberSite(host) {
  const area = syncArea();
  const current = await loadKnownSites();
  const next = addKnownSite(host, current);
  // Already first in the list: nothing to write (storage.sync is rate limited).
  if (next.length === current.length && next[0] === current[0]) return current;
  if (area) await call(area, 'set', { [KNOWN_SITES_KEY]: next });
  return next;
}

export async function clearKnownSites() {
  const area = syncArea();
  if (area) await call(area, 'set', { [KNOWN_SITES_KEY]: [] });
  return [];
}

// Invoke `cb(settings)` whenever another context changes the settings.
export function onSettingsChanged(cb) {
  const api = storageApi();
  if (!api || !api.storage.onChanged) return () => {};
  const listener = (changes, areaName) => {
    if (areaName !== 'sync' && areaName !== 'local') return;
    if (changes && changes[SETTINGS_KEY]) {
      cb(mergeSettings(changes[SETTINGS_KEY].newValue));
    }
  };
  api.storage.onChanged.addListener(listener);
  return () => api.storage.onChanged.removeListener(listener);
}

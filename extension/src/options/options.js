// Options page: three toggles persisted in storage.sync, plus a way to forget
// the sites remembered for "ask before filling".

import {
  loadSettings,
  saveSettings,
  loadKnownSites,
  clearKnownSites,
  onSettingsChanged,
} from '../shared/settings.js';

const KEYS = ['autofill', 'toast', 'askUnknownSites'];

if (typeof document !== 'undefined' && document.getElementById('form')) {
  const $ = (id) => document.getElementById(id);
  const saved = $('saved');
  const knownCount = $('known-count');
  const clearKnown = $('clear-known');
  let savedTimer = null;

  function announce(text) {
    saved.textContent = text;
    if (savedTimer) clearTimeout(savedTimer);
    savedTimer = setTimeout(() => { saved.textContent = ''; }, 2500);
  }

  function render(settings) {
    for (const key of KEYS) $(key).checked = !!settings[key];
  }

  async function renderKnown() {
    const list = await loadKnownSites();
    const n = list.length;
    knownCount.textContent = n === 0
      ? 'No sites remembered yet.'
      : `${n} site${n === 1 ? '' : 's'} remembered as used before.`;
    clearKnown.disabled = n === 0;
  }

  for (const key of KEYS) {
    $(key).addEventListener('change', async (ev) => {
      const next = await saveSettings({ [key]: ev.target.checked });
      render(next);
      announce('Saved.');
    });
  }

  clearKnown.addEventListener('click', async () => {
    await clearKnownSites();
    await renderKnown();
    announce('Remembered sites cleared.');
  });

  onSettingsChanged(render);
  loadSettings().then(render);
  renderKnown();
}

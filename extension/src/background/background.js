// Background worker script
// This runs in the background of the browser or mail client

// Import the native messaging module if supported (MV3 modules)
// For MV2 (Thunderbird), we might need to load this differently or bundle it.
import {
  connectToNativeHost,
  reconnectNativeHost,
  sendToNativeHost,
  registerOtpRequest,
  clearOtpRequest,
  getHostState,
  onHostStateChange,
  getLastOtp,
  clearLastOtp,
  onOtpReceived,
} from '../shared/native.js';
import { describeHostState } from '../shared/hoststate.js';
import { loadSettings } from '../shared/settings.js';

const api = typeof chrome !== 'undefined' ? chrome : (typeof browser !== 'undefined' ? browser : undefined);

// ---------------------------------------------------------------------------
// Toolbar badge: nothing while connected, a red "!" when the daemon or the
// native host is missing, and a tooltip that says which. Thunderbird has no
// action API for this build, so every call is guarded.
// ---------------------------------------------------------------------------
function actionApi() {
  if (!api) return null;
  return api.action || api.browserAction || null;
}

function applyBadge(state) {
  const a = actionApi();
  if (!a) return;
  const d = describeHostState(state);
  try {
    a.setBadgeText?.({ text: d.badge || '' });
    if (d.badgeColor) a.setBadgeBackgroundColor?.({ color: d.badgeColor });
    a.setBadgeTextColor?.({ color: '#ffffff' });
    a.setTitle?.({ title: d.title });
  } catch (e) {
    // Some engines throw when a tab-scoped call lands during shutdown; ignore.
  }
}

// Only on change: the badge the browser already shows survives a service
// worker restart, and the connect attempt below settles the state within ms.
onHostStateChange(({ state }) => applyBadge(state));

// Keep the last code in session storage too, so it survives the service worker
// being torn down between events (MV3). storage.session never touches disk.
const SESSION_KEY = 'lastOtp';
function sessionArea() {
  return api && api.storage && api.storage.session ? api.storage.session : null;
}

onOtpReceived((otp) => {
  const area = sessionArea();
  if (!area) return;
  try {
    const r = area.set({ [SESSION_KEY]: otp }, () => { void (api.runtime && api.runtime.lastError); });
    if (r && typeof r.catch === 'function') r.catch(() => {});
  } catch (e) { /* ignore */ }
});

function readLastOtp() {
  return new Promise((resolve) => {
    const inMemory = getLastOtp();
    if (inMemory) return resolve(inMemory);
    const area = sessionArea();
    if (!area) return resolve(null);
    try {
      const r = area.get([SESSION_KEY], (result) => {
        void (api.runtime && api.runtime.lastError);
        resolve(result && result[SESSION_KEY] ? result[SESSION_KEY] : null);
      });
      if (r && typeof r.then === 'function') {
        r.then((result) => resolve(result && result[SESSION_KEY] ? result[SESSION_KEY] : null), () => resolve(null));
      }
    } catch (e) {
      resolve(null);
    }
  });
}

function forgetLastOtp() {
  clearLastOtp();
  const area = sessionArea();
  if (!area) return;
  try {
    const r = area.remove([SESSION_KEY], () => { void (api.runtime && api.runtime.lastError); });
    if (r && typeof r.catch === 'function') r.catch(() => {});
  } catch (e) { /* ignore */ }
}

let nativePort = connectToNativeHost();

function isValidHostname(host) {
  return typeof host === 'string' && host.length > 0 && host.length <= 253;
}

// Send the last code to the tab the user is looking at and report back.
function fillActiveTab(sendResponse) {
  readLastOtp().then((otp) => {
    if (!otp || !otp.otp) {
      sendResponse({ filled: false, reason: 'no_code' });
      return;
    }
    if (!api.tabs || !api.tabs.query) {
      sendResponse({ filled: false, reason: 'no_tabs_api' });
      return;
    }
    api.tabs.query({ active: true, currentWindow: true }, (tabs) => {
      void (api.runtime && api.runtime.lastError);
      const tab = tabs && tabs[0];
      if (!tab || typeof tab.id !== 'number') {
        sendResponse({ filled: false, reason: 'no_tab' });
        return;
      }
      api.tabs.sendMessage(
        tab.id,
        { action: 'fill_otp', otp: otp.otp, otp_id: otp.otp_id, manual: true },
        (response) => {
          // No content script here (about:, the web store, a PDF viewer, ...).
          if (api.runtime && api.runtime.lastError) {
            sendResponse({ filled: false, reason: 'no_content_script' });
            return;
          }
          if (response && response.filled) {
            sendToNativeHost({ command: 'consume_otp', otp_id: otp.otp_id || 0 });
            sendResponse({ filled: true });
          } else {
            sendResponse({ filled: false, reason: (response && response.reason) || 'not_filled' });
          }
        }
      );
    });
  });
}

// Listen for messages from content scripts or the mail extractor
if (api && api.runtime) {
  api.runtime.onMessage.addListener((request, sender, sendResponse) => {
    if (!request || typeof request !== 'object') return;

    const fromTab = sender && sender.tab && typeof sender.tab.id === 'number';
    // The popup and the options page are our own extension pages; they have no
    // tab (popup) and carry our own extension id.
    const fromOwnPage = !fromTab && sender && sender.id === api.runtime.id;

    if (fromOwnPage) {
      if (request.action === 'get_state') {
        readLastOtp().then((lastOtp) => {
          const hs = getHostState();
          sendResponse({ hostState: hs.state, hostError: hs.error, lastOtp });
        });
        return true; // async
      }
      if (request.action === 'reconnect_host') {
        reconnectNativeHost();
        const hs = getHostState();
        sendResponse({ hostState: hs.state, hostError: hs.error });
        return;
      }
      if (request.action === 'fill_active_tab') {
        fillActiveTab(sendResponse);
        return true; // async
      }
      if (request.action === 'clear_last_otp') {
        forgetLastOtp();
        sendResponse({ status: 'cleared' });
        return;
      }
      return;
    }

    // Only accept the OTP messages from one of our own content scripts
    // running in a tab. Web pages and other extensions cannot reach us here,
    // but validate the sender explicitly anyway.
    if (!fromTab) return;

    if (request.action === "found_otp_in_email") {
      // Legacy path from older mail builds; the mail extractor now talks to the
      // daemon directly. Kept for compatibility.
      if (typeof request.otp === 'string' && request.otp.length > 0) {
        sendToNativeHost({
          command: "new_otp",
          otp: request.otp,
          source: typeof request.source === 'string' ? request.source : ''
        });
      }
      sendResponse({ status: "sent_to_daemon" });
      return;
    }

    if (request.action === "request_otp_for_site") {
      if (!isValidHostname(request.url)) {
        sendResponse({ status: "invalid_host" });
        return;
      }
      const tabId = sender.tab.id;
      const host = request.url;
      // Honour "Autofill automatically" here too, so a page is never put on
      // the delivery list while the user has turned autofill off.
      loadSettings().then((settings) => {
        if (!settings.autofill) {
          clearOtpRequest(tabId);
          sendResponse({ status: "autofill_disabled" });
          return;
        }
        // Remember which tab asked, so the OTP is delivered back to it alone.
        registerOtpRequest(tabId, host);
        // Query the C++ daemon for an OTP.
        sendToNativeHost({ command: "request_otp", url: host });
        sendResponse({ status: "requested" });
      });
      return true; // async
    }

    if (request.action === "consume_otp") {
      clearOtpRequest(sender.tab.id);
      sendToNativeHost({ command: "consume_otp", otp_id: request.otp_id || 0 });
      sendResponse({ status: "consumed" });
      return;
    }
  });
}

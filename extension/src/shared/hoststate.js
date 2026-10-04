// Native host connection state, derived from the native messaging port
// lifecycle. Pure functions so they can be unit tested without a browser.
//
// The port can fail in three distinct ways and the browser only tells us
// through the disconnect error text:
//
//   host-missing  The browser could not find or was not allowed to run the
//                 com.tether.extension manifest. Nothing was ever launched.
//                 Fix: `tether --install-extension-host` (or the distro
//                 package), then reload the extension.
//   daemon-down   The host wrapper ran `tether --native-host`, which could not
//                 reach tetherd's socket (it tries to spawn the daemon once,
//                 then exits 1 without writing anything to stdout). The port
//                 connects and then drops straight away. The same state is
//                 used when a live connection is lost, i.e. tetherd stopped.
//   connected     The host forwarded at least one message from tetherd (the
//                 daemon answers the host's subscribe with protocol_info at
//                 once), so the whole chain is up.
//   unknown       Before the first connection attempt has settled.

export const HOST_STATES = Object.freeze(['connected', 'daemon-down', 'host-missing', 'unknown']);

export const INSTALL_HINT = 'tether --install-extension-host';

// Exact and prefix matches for the browsers' "no such host" family of errors.
// Chrome: chrome/browser/extensions/api/messaging/native_message_port.cc
// Firefox: toolkit/components/extensions/NativeMessaging.sys.mjs
const HOST_MISSING_PATTERNS = [
  /specified native messaging host not found/i,
  /access to the specified native messaging host is forbidden/i,
  /invalid native messaging host name specified/i,
  /failed to start native messaging host/i,
  /no such native application/i,
  /native messaging host .* is not registered/i,
];

// The host launched and then went away. Chrome reports a clean exit as
// "Native host has exited."; a crash or a half-written frame as "Error when
// communicating with the native messaging host."; Firefox says "An unexpected
// error occurred". These all mean the wrapper ran but tetherd was not there
// (or stopped), so they are grouped as daemon-down.
const DAEMON_DOWN_PATTERNS = [
  /native host has exited/i,
  /error when communicating with the native messaging host/i,
  /an unexpected error occurred/i,
  /native application .* exited/i,
];

// Classify a disconnect. `hadMessages` is true when the port delivered at
// least one message from tetherd before dropping, i.e. we were connected.
export function deriveHostState(errorMessage, { hadMessages = false } = {}) {
  const msg = String(errorMessage || '');

  for (const re of HOST_MISSING_PATTERNS) {
    if (re.test(msg)) return 'host-missing';
  }
  for (const re of DAEMON_DOWN_PATTERNS) {
    if (re.test(msg)) return 'daemon-down';
  }
  // A port that was alive and then dropped: the daemon stopped.
  if (hadMessages) return 'daemon-down';
  // The port opened (so the host manifest exists) and closed without a word.
  // That is exactly what `tether --native-host` does when tetherd's socket is
  // absent, so treat an unrecognised error as the daemon being down rather
  // than as a missing host.
  return 'daemon-down';
}

// Plain-English description of a state, shared by the badge and the popup.
export function describeHostState(state) {
  switch (state) {
    case 'connected':
      return {
        state,
        label: 'Connected to Tether',
        detail: 'Codes that arrive on your iPhone will be offered to pages asking for one.',
        badge: '',
        badgeColor: null,
        title: 'Tether: connected',
        ok: true,
      };
    case 'daemon-down':
      return {
        state,
        label: 'Tether daemon not running',
        detail: 'The native host started but could not reach tetherd. Start it with `systemctl --user start tetherd` or `tether --status`, then retry.',
        badge: '!',
        badgeColor: '#d1242f',
        title: 'Tether: daemon not running. Start tetherd and retry.',
        ok: false,
      };
    case 'host-missing':
      return {
        state,
        label: 'Native host not installed',
        detail: `The browser cannot find the Tether native messaging host. Run \`${INSTALL_HINT}\`, then reload this extension.`,
        badge: '!',
        badgeColor: '#d1242f',
        title: `Tether: native host not installed. Run ${INSTALL_HINT} and reload the extension.`,
        ok: false,
      };
    default:
      return {
        state: 'unknown',
        label: 'Connecting to Tether…',
        detail: 'Waiting for the native host to answer.',
        badge: '',
        badgeColor: null,
        title: 'Tether: connecting…',
        ok: false,
      };
  }
}

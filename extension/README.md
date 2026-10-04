# Tether Extension

This is the cross-platform WebExtension for Tether. The extension spans two different environments with distinct permission requirements:
1. **Mail Clients (Thunderbird/Betterbird)**: Extracts OTPs from emails.
2. **Web Browsers (Chrome/Firefox)**: Autofills extracted OTPs on webpages.

Both extensions communicate with the local `tetherd` native daemon via standard Native Messaging APIs.

## Directory Structure
- `manifest-browser.json`: The Manifest V3 definition for Chrome/Firefox browsers.
- `manifest-mail.json`: The Manifest V2/V3 definition for Thunderbird/Betterbird.
- `src/background/`: Service worker / background scripts handling native messaging and state.
- `src/content/`: Scripts injected into web pages to locate OTP input fields and fill them, plus the in-page toast.
- `src/popup/`, `src/options/`: Toolbar popup and options page (browser build only; plain HTML/CSS/JS, bundled by `build.sh`).
- `src/mail/`: Thunderbird-specific scripts utilizing `messenger.*` APIs to parse emails for OTP codes.
- `src/shared/`: Code shared across both environments (Native Messaging wrapper, host state, settings).
- `host/`: Contains the Native Messaging Host JSON definition needed to register `tetherd` with browsers.

## How it Works
1. **In Thunderbird**: The extension monitors incoming emails or displayed messages. `src/mail/extractor.js` parses the text for an OTP and sends it through `src/shared/native.js` directly to the `tetherd` daemon.
2. **The Daemon (`tetherd`)**: Acts as a state manager and vault.
3. **In the Browser**: When you navigate to a page asking for an OTP, `src/content/autofill.js` recognizes the field and requests the OTP from `tetherd`. The daemon returns the OTP, which is then injected into the DOM.

## Browser UI

The browser extension has a toolbar popup, an options page, and an in-page toast.

- **Toolbar popup** (`src/popup/`): shows whether the extension can reach Tether, the last code that arrived (masked, with *Show* and *Copy*), when it arrived and which sender domain it was pinned to, and a **Fill on this page** button that fills the active tab the same way the content script does. The gear opens the options page.
- **Badge**: no badge while connected; a red `!` when the Tether daemon is not running or the native messaging host is not installed. Hover the icon for the reason. The state comes from the native messaging port: a first frame from the host means `connected`; a disconnect whose error says the host could not be found or run (`Specified native messaging host not found`, `Access to the specified native messaging host is forbidden`, Firefox's `No such native application`) means `host-missing`, and any other drop means `daemon-down` (`tether --native-host` exits without output when `tetherd`'s socket is absent). The background retries with backoff (3 s doubling to 60 s), and the popup has a *Retry* button. Logic lives in `src/shared/hoststate.js`.
- **Options** (`src/options/`, opens inline in the extensions page): *Autofill automatically* (default on; off means codes are only filled from the popup), *Show a toast when a code is filled* (default on), and *Ask before filling on sites I have not used before* (default off; the first fill on a hostname shows an in-page prompt with *Fill* / *Not now*, and every successful fill remembers the hostname). Settings persist in `storage.sync` (`src/shared/settings.js`); the content script and the background both honour them.
- **Toast** (`src/content/toast.js`): "Code from iPhone filled" with an **Undo** button, auto-hides after 6 s, rendered in a closed shadow root so site CSS cannot restyle it, `role="status"`, honours `prefers-reduced-motion` and `prefers-color-scheme`. Undo puts the previous field values back and dispatches the same `input`/`change` events the fill does.

The Thunderbird build shares `background.js` but has no toolbar action or `storage` permission; every UI call is guarded, so it keeps behaving as before.

If the extension cannot find the host, install it with `tether --install-extension-host` (portable builds; distro packages register it system-wide) and reload the extension.

## Installation (Native Messaging Host)

To allow the extension to talk to the local `tether` binary, you must register the native messaging host manifest.

### 1. Locate the manifests
After building the project, the manifests are generated in your build directory:
- `build/com.tether.extension.chrome.json`
- `build/com.tether.extension.mozilla.json`

### 2. Create Symlinks
**Important:** Most browsers (especially Firefox) require the manifest filename in the config directory to **exactly match** the `"name"` field inside the JSON (`com.tether.extension`).

#### Firefox / Thunderbird
```bash
mkdir -p ~/.mozilla/native-messaging-hosts/
ln -s $(pwd)/build/com.tether.extension.mozilla.json ~/.mozilla/native-messaging-hosts/com.tether.extension.json
```

#### Chromium / Google Chrome
1. Find your Extension ID in `chrome://extensions`.
2. Ensure it is listed in the `allowed_origins` field of the manifest.
3. Link the file:
```bash
mkdir -p ~/.config/chromium/NativeMessagingHosts/
ln -s $(pwd)/build/com.tether.extension.chrome.json ~/.config/chromium/NativeMessagingHosts/com.tether.extension.json
```

# Bluetooth quickstart

One page: get messages, contacts, and notifications from the iPhone working, and
know what to run when they are not. The long form, with the reasons behind each
step, calls, AirPods, and the history, is [BLUETOOTH.md](BLUETOOTH.md).

## The happy path

1. **Install** Tether (package, AppImage, or Flatpak, see the
   [README](../README.md#installation)) and check that BlueZ is running:
   `systemctl status bluetooth`.
2. **System setup.** Run `tether bt setup` and run the commands it prints, in a
   terminal. It prints only what this machine still needs: the adapter class (iOS
   offers its permission toggles only to an A/V Hands-Free device) and BlueZ's
   experimental bearer API (it carries notifications and must be on *before* you
   pair). Re-run `tether bt setup` until it says `Bluetooth setup is complete.
   Nothing to do.` The GTK app shows the same list on the Devices tab with a
   *Copy commands* button.
3. **Pair.** In the GTK app, Devices tab, pick the iPhone under BLUETOOTH and press
   *Pair over Bluetooth*. From a terminal, `tether bt devices` finds the address,
   then `tether bt pair <addr>`. Confirm that the same code shows on both ends.
4. **Grant the two toggles on the iPhone.** Settings > Bluetooth > (i) next to
   this computer: turn on *Show Message Notifications* and *Sync Contacts*. They
   can take a few minutes to appear. *Show iPhone Permissions* in the app, or
   `tether bt solicit`, asks the phone to show them again.
5. **Verify** with `tether bt connection`. Working is `BR/EDR: yes`,
   `Messages (MAP): yes`, `Contacts (PBAP): yes`, and, in full mode, `LE: yes`
   and `Notifications: yes`. `tether bt threads` lists the conversations.

## If you see ... run ...

The first column quotes what `tether bt status`, `tether bt connection`,
`tether bt pair`, or the Devices tab prints. The second column is the command,
the third what its result means. When in doubt, start with `tether bt connection`:
its last lines say in plain words what the daemon is waiting on.

| If you see | Run | What the result means |
|---|---|---|
| `Bluetooth: unavailable (is bluetoothd running?)` | `systemctl status bluetooth`, then `sudo systemctl enable --now bluetooth` | BlueZ was not running; Tether cannot see the adapter without it |
| `N steps left before the iPhone Bluetooth features work` | the commands `tether bt setup` printed, then `tether bt setup` again | Setup is complete when it answers `Nothing to do` |
| `class=wrong` in `tether bt status` | `sudo systemctl enable --now tether-btclass@hci0` (portable build: the command `tether bt setup` prints) | Until `class=ok`, the iPhone never offers its two toggles |
| `Mode: compatibility` in `tether bt status` | `bluetoothctl --version`, `systemctl cat bluetooth \| grep experimental`, `tether bt setup` | Messages and contacts only. Notifications need BlueZ 5.86+ with `--experimental` |
| `Bond: BR/EDR only` in `tether bt status` | `tether bt status` again and read `secure-connections` | `OFF` means re-pairing cannot help until it is on. Otherwise: Forget This Device on the iPhone, `tether bt unpair <addr>`, pair again |
| `Notifications: mirroring off` in `tether bt status` | `tether bt ancs on`, then `tether bt solicit` | Mirroring was off, which also takes the request for the toggles off air |
| `Tether: off` in `tether bt status` | `tether bt enable on` | Tether was told not to connect to this iPhone |
| `History: paused - the desktop keyring has no key to offer yet` | unlock the keyring, or `tether bt retention plaintext` | Encrypted history waits for the keyring; plaintext does not |
| `Controller hciN is not present; using the first powered one instead` | `tether bt adapter auto`, or `tether bt adapter <hciN>` from the list it prints | The pinned controller is gone; `<- in use` marks the one Tether picked |
| `iPhone not known to BlueZ.` | unlock the iPhone, open Settings > Bluetooth, then `tether bt devices` | BlueZ has never seen the phone; it only advertises with that screen open |
| `iPhone is not paired.` or `No paired iPhone selected.` | `tether bt pair <addr>` | Pairing is what selects the phone Tether connects to |
| `Device <addr> is not visible to BlueZ. Unlock the iPhone and open its Bluetooth settings` | that, then `tether bt pair <addr>` again | Discovery found nothing; the phone must be awake on its Bluetooth screen |
| `No Bluetooth adapter is available.` or `No Bluetooth adapter is present.` | `bluetoothctl list`, `rfkill list bluetooth`, `bluetoothctl power on` | No powered controller; rfkill or power is the usual cause |
| `BlueZ refused to start scanning.` or `BlueZ is holding a discovery session that never ended` | `sudo systemctl restart bluetooth`, then pair again | Only a BlueZ restart clears a stuck discovery session |
| `Could not register a Bluetooth pairing agent.` | close other Bluetooth tools (`bluetoothctl`, a settings panel), then pair again | Another program holds the pairing agent slot |
| `Could not advertise for ANCS` or `Could not prepare the ANCS advertisement.` | `sudo systemctl restart bluetooth`; if it persists, `tether bt adapter <hciN>` | BlueZ refused the advertisement; a controller with no LE advertising cannot do notifications at all |
| `The pairing code could not be shown for confirmation: this computer has no display` | `tether bt pair <addr>` from a terminal | The code is confirmed on the terminal instead |
| `Pairing was not confirmed on this computer.` | `tether bt pair <addr>` and answer the code prompt | The comparison was declined or timed out here, not on the phone |
| `The iPhone declined the pairing request.` | `tether bt pair <addr>` and tap Pair on the iPhone | The phone said no, or nobody answered its prompt |
| `The iPhone refused the connection before pairing started` | delete every entry for this computer on the iPhone (there can be two), `tether bt unpair <addr>`, pair again | A stale record on the phone blocks a clean retry. `tether bt pair <addr> --explicit-pair` forces a bond that carries messages but never notifications |
| `Pairing did not complete. Confirm the prompt on the iPhone.` | unlock the iPhone, then `tether bt pair <addr>` | The pairing prompt on the phone went unanswered |
| `Paired with <name>.` but no toggles after a few minutes | `tether bt solicit`, then close and reopen the (i) page on the iPhone | iOS shows the toggles only while the advertisement is on air |
| `Messages (MAP): no  [forbidden]` or `Contacts (PBAP): no  [forbidden]` | turn on the named toggle on the iPhone; `tether bt solicit` if it is not shown | A permission, not a pairing problem. Do not re-pair |
| `[busy]`, or `another computer is using the iPhone's map session` | disconnect the other computer (`bluetoothctl disconnect <addr>` there) | The iPhone serves one messages session at a time; this one reconnects on its own |
| `[no_record]`, or `could not be fetched from the iPhone` | check the other computer and the toggle; `systemctl status tether-btclass@hci0`, `tether bt status` for `class=` | A refused record is usually the toggle or another host; a reset class looks the same. Re-pairing is not the fix |
| `[unavailable]`, or `is unreachable; waiting for the iPhone to come back` | nothing; bring the iPhone in range and unlock it | The profile reconnects when the phone answers |
| `[no_daemon]`, or `needs BlueZ's OBEX daemon` | install `bluez-obex` (Arch) or `bluez-obexd` (Debian, Ubuntu), then `systemctl --user status obex` | obexd is socket-activated; it only has to be installed |
| `[other]`, or `could not be opened` | `journalctl --user -u obex -n 50`, `tether bt diagnostics` | An OBEX error the daemon could not classify; the report is for an issue |
| `Connecting to the iPhone over Bluetooth...` for minutes | unlock the iPhone; `journalctl --user -u tetherd -n 50` | The phone is not answering the link; the log says whether it refuses or is absent |
| `The iPhone keeps refusing the Bluetooth connection.` | turn Bluetooth off and back on **on the iPhone** | Clears a wedged phone even with the permissions on. Re-pairing is not the fix |
| `BlueZ reports the Bluetooth link down because this computer offers the iPhone no profile` | nothing | `BR/EDR: no` is cosmetic on a machine with no audio stack; messages and contacts work regardless |
| `Connected. Notification mirroring is disabled.` | `tether bt ancs on` | Mirroring was switched off; messages and contacts were never affected |
| `Connected. BlueZ is not exposing an LE bearer` | `tether bt setup`; if a step is listed, run it, then Forget This Device, `tether bt unpair <addr>`, pair again | The experimental API was off when the bond was made, so it has no LE half |
| `Waiting for the iPhone to open the LE link that carries notifications` | wait a minute; check *Share System Notifications* under (i) on the iPhone | The phone opens that link itself once it is asked |
| `This computer is not putting the notification request on air` | `tether bt status` for `advertising=`; `tether bt adapter <hciN>`; `sudo systemctl restart bluetooth` | Nothing on the iPhone changes this; the controller or BlueZ does |
| `This computer had pinned the bond to BR/EDR` | wait a minute; `tether bt diagnostics` shows `preferred_bearer` | It is being undone; LE comes up by itself |
| `The iPhone is not answering on LE. Its Bluetooth is wedged` | turn Bluetooth off and back on **on the iPhone** | Messages and contacts are unaffected; re-pairing does nothing |
| `The iPhone is not offering notifications on this link. Asking it again...` | wait; `tether bt solicit`; check *Share System Notifications* on the iPhone | The LE link is up but the phone has not granted the notification service yet |
| `LE: yes` and `Notifications: yes` but nothing arrives | `tether bt notifications`; `tether bt ancs-content on` for titles and bodies | The list shows what the phone sends; content is off until that setting is on |
| `Bluetooth is unavailable on this machine.` (Devices tab) | `systemctl status bluetooth`, then `tether bt status` | Same as the first row: BlueZ is down or there is no adapter |
| `This machine cannot carry the Bluetooth features.` (Devices tab) | `tether bt status` and read the reasons it lists; `tether bt adapter <hciN>` | The controller lacks BR/EDR, LE, or advertising; another controller may not |
| `Bluetooth is switched off for this iPhone.` (Devices tab) | tick *Connect to this iPhone over Bluetooth*, or `tether bt enable on` | Tether was told not to reconnect |
| `No iPhone found. Unlock the phone and open Settings > Bluetooth` (Devices tab) | that, then *Scan for devices* | The phone advertises only with that screen open |
| `tether bt calls` reports call control off | `tether bt calls-enable on`, then read the Calls section of [BLUETOOTH.md](BLUETOOTH.md#calls) | Calls need BlueZ's hands-free profile; PipeWire usually holds it |
| The iPhone's audio moved to this computer | the WirePlumber roles in [Keeping the phone's audio on the phone](BLUETOOTH.md#keeping-the-phones-audio-on-the-phone) | iOS routes to any bonded speaker; Tether only brings the link up |
| Something not listed here | `tether bt diagnostics`, `journalctl -u bluetooth -n 100`, `journalctl --user -u tetherd -n 100` | The report is redacted and made for pasting into an issue |

## What each toggle on the iPhone does

All three live under Settings > Bluetooth > (i) next to this computer, and all
are off until you turn them on. *Show Message Notifications* grants the messages
profile (MAP): without it, `tether bt connection` shows `Messages (MAP): no [forbidden]`
and the Messages tab stays empty. *Sync Contacts* grants the phonebook (PBAP):
without it, senders show as numbers instead of names. *Share System Notifications*
appears once the phone has opened the LE link and governs notification mirroring
(ANCS): without it, `Notifications` stays `no` while messages keep working. iOS shows
the first two only while Tether is advertising and only to an adapter with the
A/V Hands-Free class, which is what `tether bt setup` arranges. None of them is a
pairing step: turning one on never needs a re-pair, and re-pairing never turns one on.

## Reset everything

Use this when a pairing has gone stale on either side and nothing above moved it.
The rule is **unpair on both ends**: the Linux bond and the iPhone's record have to
go together, or the leftover one blocks the next attempt. From a source checkout:

```bash
./scripts/tether-reset.sh bt --dry-run   # shows what would be deleted
./scripts/tether-reset.sh bt             # stops tetherd, removes the BlueZ bond,
                                         # deletes bluetooth.json, groups.json, the
                                         # message journal, the contact cache, and
                                         # the history key (file or keyring entry)
```

`wifi` instead of `bt` clears the Wi-Fi pairing (TLS keys, known hosts, pending
requests) and `all` clears both plus the daemon log. The script cannot touch the
phone: on the iPhone, Settings > Bluetooth > (i) next to this computer > *Forget
This Device*, and check for a second entry with the same name, since a failed
pairing can leave two. Without a checkout, the same thing by hand is `tether bt
unpair <addr>` (it prints the same reminder about the phone) and then Forget This
Device on the iPhone.

Afterwards, start the daemon again (`systemctl --user restart tetherd` when the
user unit is enabled; otherwise the next `tether` command starts one), confirm
`tether bt setup` still says `Nothing to do`, and go back to step 3 of the happy
path.

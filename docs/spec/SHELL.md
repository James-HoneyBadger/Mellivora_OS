# Shell, desktop and user experience (normative)

**Normative from:** M6. **Governs:** what the user sees, how a file opens a
program, how two programs exchange a dragged object, what the clipboard carries,
and how a notification reaches the person it is for.

The tree already has a compositing desktop: surfaces, damage rectangles,
Alt+Tab, workspaces, clipboard, file dialogs and notifications (`burrows.inc`),
and a shell with 97 built-ins. What it does not have is a **namespace** — a
single view of files, devices, shares and settings that the shell, the dialogs
and the associations all agree on.

## 1. The user's model

1. There is one place files live: the shell namespace. A drive letter, a network
   share, a device and a settings page are all locations in it.
2. A file opens with a program chosen by its type, and the user can change that
   choice for their account or for the machine.
3. Anything can be dragged anywhere it makes sense, and what happens is defined
   by the target, not by the source.
4. A notification is a message from a program to a person, and it queues,
   expires and can be acted on.
5. Nothing the desktop does can wedge the machine: a hung application is a hung
   window, not a hung session ([RELIABILITY.md](RELIABILITY.md#3-fault-containment)).

## 2. The shell namespace

```text
\                       the namespace root; not the same as a file system root
├── Desktop             the user's desktop directory
├── Documents, Downloads, Pictures, Music, Videos
├── Computer
│   ├── C:              a volume, a mount point
│   └── D:              another one, or a share
├── Network
│   ├── <machine>
│   │   └── <share>     a remote volume mounted as a location
├── Devices             audio endpoints, printers, USB devices (read-only)
├── Control Panel       settings pages, each backed by Sett keys (CONFIG.md)
└── Recycle Bin         deleted objects, restorable, per volume
```

| Rule | Contract |
|---|---|
| One parser | The shell, the file dialogs and the file-system API resolve a path through the same code ([S8](README.md#decisions-this-set-records)) |
| Path forms | A namespace path (`\Computer\C:\Users\…`) and a legacy path (`C:\Users\…`, `/Users/…`); the legacy forms are aliases resolved by the same parser, so the shell and a POSIX-style program agree |
| Long names | 32 767 code units, 255 per component ([ABI.md](ABI.md#8-text-and-names)) |
| Virtual folders | `Computer`, `Network`, `Devices`, `Control Panel` enumerate objects that are not files; an application asks the same "what is in here" question and gets the right answer for each |
| Recycle Bin | A delete to the bin is a move, not a copy; restore returns the object to its original path with its attributes; a delete that bypasses the bin says so |
| Properties | Every location exposes an object's properties through the same property sheet, backed by the object's own fields |

## 3. Associations

A **type** is identified by an extension, a MIME-like content type or both; a
**handler** is a program with a command line template and a verb.

| Piece | Rule |
|---|---|
| Registration | Machine-wide under `\Registry\Machine\Software\Classes`, per-user override under the user's hive; the user's wins |
| Verbs | `open` (default), `edit`, `print`, `play`, `explore`, plus a type's own; the default verb is explicit, not "the first one" |
| Command line | A template with `%1` (the file) and `%*` (all arguments); the association substitutes, and the result is parsed by the shell's own splitter, so quoting is one implementation |
| Unknown type | "Open with" offers the programs that declared the type, plus a browse; the choice is stored per user |
| Change | Changing a default is a Sett transaction and takes effect for the next launch, not for the current session's already-running windows |
| Security | A low-integrity program cannot register itself as the handler for a high-integrity type ([SECURITY.md](SECURITY.md#4-integrity-and-mandatory-policy)) |

## 4. Drag and drop, and the clipboard

Both carry **data objects**: a set of formats, each a section or a small inline
value ([IPC.md](IPC.md#2-ports)).

| Piece | Contract |
|---|---|
| Data object | `{format, medium}` list, with a preferred order; a format is `CF_UNICODETEXT`, `CF_BITMAP`, `CF_FILEDESCRIPTORLIST`, or a registered name |
| Delayed rendering | A source may offer a format it produces only on request; the target asks, the source fills a section, and a refusal is a status the target handles |
| Drop protocol | `DRAG_ENTER`, `DRAG_OVER` (returns the effect the target will accept), `DRAG_DROP`, `DRAG_LEAVE`, each with the keyboard/mouse state and the source's allowed effects (copy, move, link) |
| Feedback | The cursor shows the effect; a target that refuses shows "no", it does not silently ignore the drop |
| Cross-process | Through ports and sections; no pointer crosses a process boundary |
| Security | A drop from a lower integrity level to a higher one is refused, and the target can ask for the source's identity to decide |
| Clipboard | One clipboard per session; `Open`/`SetData`/`GetData`/`Close` with a **format listener** so a viewer does not poll; the content is snapshotted on close, so a source that exits does not empty the clipboard |
| Clipboard history | Off by default; when on, it refuses formats marked `SECRET` ([SECURITY.md](SECURITY.md#9-secrets)) |
| Large payloads | Above 64 KiB the data object carries a section; a 100 MB copy is a handle, not a message |

## 5. Windows and the desktop

| Piece | Contract |
|---|---|
| Compositor | Kernel-side for this plan ([D7](../ROADMAP.md#architectural-decisions)); every window is a surface, damage rectangles drive the redraw |
| Window | Title, icon, minimum and maximum size, resizable, topmost, tool window, per-monitor DPI |
| Z-order | Per desktop, most-recently-used first; Alt+Tab shows the MRU order of the current workspace |
| Workspaces | At least four; a window belongs to one; moving a window between them is a drag on the overview |
| Input | Focus follows the click by default; a keyboard shortcut may follow the mouse; a window that hangs is greyed, not frozen ([RELIABILITY.md](RELIABILITY.md#2-hung-task-detection)) |
| Frame rate | 60 Hz target at 1280×1024 on the QEMU graphics path; a slow frame is counted, not hidden |
| Theming | Colours, metrics and the font come from Sett keys, read once per window and re-read on a change notification ([CONFIG.md](CONFIG.md#4-change-notification)) |
| Accessibility | Keyboard reachability for every control, a high-contrast theme, and a scalable font size; the compositor's scaling is the single place DPI is applied |

## 6. Notifications

| Piece | Contract |
|---|---|
| Queue | Per user, ordered, with a visible list; an application may update its own notification in place |
| Lifetime | Default 8 s for an informational notification, until dismissed for one that needs an action |
| Action | A notification may carry one action (open a location, run a program); the action runs with the notifying application's token, not the desktop's |
| Rate limit | An application that emits more than 5 per minute is coalesced into one, and told |
| Do not disturb | A session state that suppresses informational notifications and still delivers critical ones |
| Audit | A notification from a service to a user is not a security boundary; it is a message with an icon |

## 7. Conformance

M6 does not close until:

1. `shell_test` resolves 200 paths through every form (namespace, drive, POSIX,
   share, device, virtual folder) and gets the same object for equivalent forms.
2. `assoc_test` registers a type, opens it, changes the default for one user,
   and proves the other user is unaffected.
3. `dnd_test` drags a file between two processes (copy and move), drags text into
   an editor, and refuses a drop from low to high integrity.
4. `clipboard_test` copies text, a bitmap and a file list; a source that exits is
   still on the clipboard; a `SECRET` format never enters the history.
5. `desktop_test` (existing) survives Alt+Tab, four workspaces, a hung
   application and an Exit Desktop.
6. A theme change repaints every window without a restart, and a notification
   with an action runs it under the notifying application's identity.

## 8. As built (the gap)

| Contract | Tree today |
|---|---|
| Shell namespace | File-system paths and a few virtual names; no `Computer`, `Network` or `Devices` view |
| Associations | None; a program is run by name |
| Drag and drop | None |
| Clipboard | Text and bitmap, no format listener, no cross-session rule |
| Workspaces | Four, working |
| Notifications | A queue with icons, working |
| Themes | Named colour sets, working |
| Accessibility, DPI | None |

The compositor, surfaces, damage rectangles, workspaces, clipboard basics, file
dialogs and notification queue are real and tested; the namespace, associations
and data-object protocols are new in M6.

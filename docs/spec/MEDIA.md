# Devices and media (normative)

**Normative from:** M8. **Governs:** sound that several programs can use at
once, video that plays, USB devices that appear and disappear, and a printer that
queues and cancels.

The tree today has AC'97 and SB16 with one stream per open, a VBE framebuffer, a
PS/2 keyboard and pointer, and no USB, no video playback and no printing. The
scorecard's last row — "play media" — is `partial` because of exactly that.

## 1. Audio

### 1.1 The graph

```text
application -> stream -> (format conversion -> mix -> effects) -> endpoint -> device
```

| Object | Meaning |
|---|---|
| Endpoint | One physical output or input, with its own volume and mute (`\Devices\Audio\<n>`) |
| Stream | One application's audio, with its own format, volume and session |
| Session | A group of streams that share a volume and a lifetime — one application, or one desktop session |
| Mixer | The kernel's mixing point, one per endpoint |

### 1.2 Contracts

| Property | Contract |
|---|---|
| Formats | `u8`/`i16`/`i32`/float32, 1–8 channels, 8 000–192 000 Hz accepted; the mixer converts |
| Mixing | **At least 32 concurrent streams** per endpoint, summed with saturation, no clipping of the sum evaluated in float |
| Latency | ≤ 20 ms from write to speaker at the default buffer; ≤ 5 ms when the stream asks for exclusive low-latency mode |
| Drift | Each stream's clock is corrected against the endpoint's; a resampler that drifts audibly is a bug |
| Exclusive mode | One stream may take an endpoint, with a reason; others are refused, not silently muted |
| Per-stream volume | Applied before mixing, so one loud program cannot drown another |
| Session | Ends when its last stream closes; the endpoint returns to idle |
| Silence | An idle endpoint is powered down ([PLATFORM.md](PLATFORM.md#6-power)) and takes < 5 ms to wake, with no click |
| Capture | The same graph in reverse, one client at a time by default, more with an explicit share |
| Glitch policy | An underrun fills with silence and is counted; it never repeats the last block, which would sound like damage |

### 1.3 What this replaces

One stream per open, on one device, mixed by nobody. Four programs playing sound
today produce one winner and three failures.

## 2. Video

| Piece | Contract |
|---|---|
| Surface | A section-backed bitmap with a declared format, mapped into the application ([VMM.md](VMM.md#6-sections)); the compositor reads the same pages |
| Scaling and colour | Converted on present: RGB565, RGB555, RGB32, and YUV 4:2:0/4:2:2 for decoded frames |
| Sync | Present is tied to the compositor's frame clock, not to a timer of the player's own; a late frame is dropped, never queued forever |
| Decode | **One codec family first**: a single intra-frame format and one motion format, both with a fixed reference implementation in the tree, chosen so the desktop can play a clip without a plugin |
| Playback rate | Audio is the master clock when both are present; drop on late, never resample the audio |
| Overlay | Not required; the compositor's damage rectangles are the fast path ([SHELL.md](SHELL.md#5-windows-and-the-desktop)) |

Video is not a framework for every container. It is one working path that proves
the surfaces, the clocks and the compositor, and a documented place for the next
codec ([S1](README.md#decisions-this-set-records)).

## 3. USB

| Piece | Contract |
|---|---|
| Host controllers | UHCI, OHCI and EHCI; the controller is a device object with its own driver ([DRIVERS.md](DRIVERS.md)) |
| Enumeration | Port reset, address assignment, descriptor reads with bounded retries; a device that misbehaves is disabled with its port named |
| Classes | HID (keyboard, pointer, gamepad), mass storage (bulk-only), audio, video; a class driver registers like any other |
| Power | A port is suspended when its device is idle; resume is < 100 ms |
| Surprise removal | Outstanding I/O fails with `MV_DEVICE_REMOVED`; the device tree is updated and the user is told ([DRIVERS.md](DRIVERS.md#8-power-and-pnp)) |
| Hot-plug | A device appearing during a soak must not leak a handle, a frame or an IRP |

## 4. Printing

| Piece | Contract |
|---|---|
| Spooler | A service ([SERVICES.md](SERVICES.md)) owning queues and jobs |
| Queue | Per printer: paused, offline, error states; jobs ordered by priority then arrival |
| Job | Create, spool to a file, schedule, print, cancel, restart; a cancelled job stops within one page |
| Driver | A printer driver converts the spool file to the device's page description; the interface is one entry point plus a capability record |
| Status | Pages printed, pages remaining, error text, all readable from the management console and from `print` |
| Page description | One working path first: raster at the device resolution, so any driver that can draw can print |
| Security | Queue ACLs; a user sees their own jobs and, with `CAP_PROC_AUDIT`, everyone's |

## 5. Conformance

M8 does not close until:

1. `audio_test` plays eight streams at once from eight processes with no dropouts,
   each with its own volume, and the sum is correct.
2. An underrun is counted and silent; an exclusive-mode claim refuses the others
   and says so.
3. `video_test` plays a clip at its own clock, drops late frames rather than
   delaying them, and the audio stays in sync over ten minutes.
4. `usb_test` enumerates, uses and removes a keyboard, a mass-storage device and
   an audio device, with no leak after 100 cycles.
5. `print_test` queues three jobs, cancels the second, and the first and third
   print; the cancel takes effect within one page.
6. The whole suite leaves no stream, endpoint, port or IRP open.

## 6. As built (the gap)

| Contract | Tree today |
|---|---|
| Multi-stream mixing, per-stream volume | One stream per open |
| Audio sessions | None |
| Video decode | None |
| USB | None |
| Printing | None |
| Surfaces | The compositor has surfaces and damage rectangles ([SHELL.md](SHELL.md)) |

AC'97 and SB16 playback work, and the framebuffer and compositor are real; the
mixing, USB and printing layers are new in M8.

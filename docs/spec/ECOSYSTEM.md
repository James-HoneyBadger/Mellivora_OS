# Ecosystem, packaging and release (normative)

**Normative from:** M10. **Governs:** how a program is packaged, installed,
registered, updated and removed; what the SDK promises; and how an ABI and a
release are allowed to change.

The tree today has `hbpkg` archives, per-app manifests with a capability list,
a generated app catalogue and 37 samples. What it does not have is
**registration**: an installer writes files, and nothing knows what those files
do, what they replaced, or how to remove them cleanly.

## 1. Package

A package is an archive plus a manifest, and both are signed:

```text
package.hbpkg
├── manifest.json          identity, dependencies, capabilities, files, hashes
├── manifest.sig           publisher signature over the manifest's hash
├── files/…                the payload, at paths relative to the install root
└── scripts/…              optional: pre-install, post-install, pre-remove, post-remove
```

| Manifest field | Contract |
|---|---|
| `id` | Reverse-DNS, stable forever |
| `name`, `version` | Version is `major.minor.patch`; a pre-release suffix sorts before the release |
| `publisher` | A key id from the trust store, and a display name |
| `target` | Minimum system ABI level and architecture |
| `dependencies` | `{id, range}` — the resolver's input |
| `capabilities` | The privileges and paths the package wants, in plain language plus machine form ([SECURITY.md](SECURITY.md#6-application-capability-manifests)) |
| `files` | Path, size, SHA-256, mode, owner |
| `registration` | Associations, services, startup entries, shell integration — **declared, not performed by a script** |
| `uninstall` | What removal must do beyond deleting files: which keys to remove, which user data to keep |

A package that performs its own registration in a script is refused: registration
is declared so removal can be exact. Scripts are for the cases the model does not
cover, they run with the user's token, and they are shown to the user before
they run.

## 2. Install, resolve, remove

| Step | Contract |
|---|---|
| Resolve | Build the dependency closure, choose versions that satisfy every range, refuse an unsatisfiable set with the conflicting pair named |
| Verify | Signature over the manifest, then the manifest's hashes over every file; a mismatch refuses the whole package |
| Stage | Extract into a staging directory on the target volume |
| Commit | One transaction ([STORAGE.md](STORAGE.md#5-transactions-and-recovery)): files moved into place, registration written, the package's record written last |
| Rollback | Any failure moves the staged files back and removes the registration; a half-installed package cannot exist |
| Update | Install the new version beside the old, switch the registration, and keep the old version until the next successful boot — the same mechanism a restore point uses ([RELIABILITY.md](RELIABILITY.md#6-restore-points)) |
| Remove | Registration removed first, then files, then the record; user data is kept unless the user chooses otherwise, and the choice is recorded |
| Shared files | Two packages may not own the same path; the resolver refuses the conflict rather than letting the last writer win |

**Downgrade** is an install of an older version, allowed only if the package
declares compatibility and the user confirms; the ABI level check runs first.

## 3. Registration

| Kind | Stored in | Removed by |
|---|---|---|
| Association | `\Registry\Machine\Software\Classes` ([SHELL.md](SHELL.md#3-associations)) | The package's record |
| Service | `\Registry\Machine\System\Services` ([SERVICES.md](SERVICES.md)) | The record, and the service must be stopped first |
| Startup entry | The Sett, per user or machine | The record |
| Shell integration | Associations, plus a namespace extension only if declared | The record |
| Path entry | Per package directory, never a shared directory | The record |

Every entry carries the package id, so an audit can answer "what did this
package touch" and a removal can be exact.

## 4. Trust

| Piece | Contract |
|---|---|
| Trust store | Publisher keys, added explicitly by an administrator; a self-signed key is a warning, not an error, and the warning is shown at install |
| Unsigned package | Refused by default; an administrator may allow it for one install, audited |
| Bundle | A package that installs other packages must present each one's signature; the bundle's signature is not transitive trust |
| Revocation | A revoked key refuses new installs and warns about installed packages; it does not silently remove them |
| Reproducibility | A package built twice from the same inputs produces the same archive hash, so a mirror can be verified |

## 5. SDK

| Deliverable | Contents |
|---|---|
| Headers | `c/include/mv/` — one header per subsystem, no private types leaked ([ABI.md](ABI.md)) |
| Libraries | The freestanding runtime, the syscall view, the object and IPC helpers, a small string/format library; no libc claim |
| Samples | Buildable, runnable, and each one is a test in `make check-samples` |
| Tools | The compiler invocation, the linker script, the packaging tool, the manifest generator, the signing tool |
| Documentation | A programming guide that builds a window, a service and a package from scratch; the ABI document is the reference, not a tutorial |
| Contract | A sample that compiles against the SDK and does not run is an SDK bug |

The SDK's promise is narrow on purpose: **the ABI is stable, the libraries are
convenience.** A program that calls the syscalls directly is a first-class
citizen; a program that uses the SDK can be rebuilt against a newer SDK without
a source change.

## 6. ABI and release policy

| Change | Allowed? | Requires |
|---|---|---|
| Add a syscall | Yes | A new number, a documented status, a test |
| Add a field to a versioned structure | Yes | Append, bump the version, keep the old reader working |
| Change a field's meaning | No | A new call or a new structure; the old one stays |
| Remove a syscall | No, ever | Deprecate in documentation; it keeps working ([S3](README.md#decisions-this-set-records)) |
| Change a structure's layout | No | A new version, and the old one keeps parsing |
| Change a default | Yes | A Sett migration and a release note |
| Remove a Sett key | No | Preserve it, ignore it, stop documenting it |

An **ABI level** is an integer that rises when a program built against the
previous level can still run — which it always can — and it exists so a package
can say "I need at least level *n*". The loader refuses a package that needs a
higher level than the system reports, with the level named.

## 7. Release

| Piece | Contract |
|---|---|
| Version | One source of truth (`tools/validate_version_strings.py`), checked by `make lint` |
| Images | `lite` and full, both built from the same tree by the same targets |
| Changelog | Every user-visible change, with the wave or the fix it belongs to |
| Gates | A release passes every gate in [RELIABILITY.md](RELIABILITY.md#9-the-gates) plus the parity scorecard, and no scorecard row may move backwards |
| Reproducibility | The image, the packages and the SDK archive are hash-stable from a tagged tree |
| Migration | An installed system upgrades in place: restore point, package updates, reboot, rollback available |

## 8. Conformance

M10 does not close until:

1. `pkg_test` installs, updates, downgrades and removes a three-package
   dependency chain, and the machine's files, registration and Sett keys return
   to their pre-install state after removal.
2. A package with a bad signature, a tampered file and an unsatisfiable
   dependency is refused with the reason, and nothing on the volume changed.
3. A package that declares a service and an association registers both, and its
   removal leaves neither.
4. An interrupted install at every commit step rolls back and leaves no staged
   files.
5. Every SDK sample builds from the SDK alone and runs under `make check-samples`.
6. The parity scorecard's row 5 ("install software") moves from `partial` to
   `pass` and stays there.

## 9. As built (the gap)

| Contract | Tree today |
|---|---|
| Manifest with dependencies, capabilities, hashes | A manifest with an app id and a capability list, and an archive |
| Signature and trust store | None |
| Registration and exact removal | None; `hbpkg` installs files |
| Dependency resolution | None |
| In-place update with rollback | None |
| ABI level and policy | The 187 calls are frozen in practice; nothing declares it |
| SDK | Samples, headers and a guide; no stable contract |

`hbpkg`, the manifests, the generated catalogue and the sample suite are real and
tested; the resolver, the registration model and the trust layer are new in M10.

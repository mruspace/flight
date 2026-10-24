# Security

## Reporting a problem

Email [contact@mru.space](mailto:contact@mru.space) with "security" in the
subject. Please do not open a public issue for a security problem. We reply by
email.

## Scope

Mru Flight has no qualification evidence. The demo has run twice in orbit, as a
user-space experiment on ESA's OPS-SAT PRETTY (October 2026); that is not
qualification. Do not use it to control anything that can cause harm.

In scope:

- The decision core (`quorum`) and its C interface (`quorum-ffi`): a wrong verdict
  for some set of replies, or a crash.
- The demo (`quorum`) and the bench: a way to make the voter deliver a value that
  no replica agreed on.
- The F´ components in `fprime/Mru`.

## Supported versions

Only the latest release gets fixes.

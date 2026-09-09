# Application text input

Implementation status (2026-09-09): integrated; unit/build verification is separate
from real OS IME, browser/device and remote application acceptance.

The floating toolbar contains **输入文字（本机输入法）**. A detected editable target
can display a dismissible corner hint; it never opens or focuses the editor.
The user opens a visible native textarea and explicitly clicks **发送文字**.
Enter remains composition/newline, and submitting never appends Enter.

## Carrier and authorization

- User-confirmed superseding decision: only add messages on existing channels.
  No new WebSocket, route, connection, channel or ticket exchange is created.
- Existing authenticated `media_data_channel` carries all text messages
  (`610`–`615`) using its existing `px.Message` + TLV control path. Unlike the
  separate unreliable ordinary-input channel, it is fully reliable and ordered.
- The existing channel is explicitly created with `ordered: true` and no partial
  reliability options. A runtime gate additionally requires open state,
  `ordered === true`, `maxRetransmits === null` and `maxPacketLifeTime === null`.
- Text retains the parent connection's existing Console authentication, instance
  and lease; there is no additional occupant or change to media transport.
- Capability/target generations remain decimal strings, avoiding JavaScript
  integer precision loss. Observers/no-input tickets have no usable send entry.

## Input and failure behavior

Opening the editor releases tracked keys/buttons and suppresses ordinary input
before requesting the begin barrier. Only its matching acknowledgement binds
the workflow and enables Send. Ordinary messages carry the acknowledged input
generation after the corresponding end barrier. Closing during begin or during
an outstanding submission defers the end barrier until its predecessor finishes.

The pre-existing hidden textarea remains available for ordinary CEF English
text outside the editor, but is detached throughout text editing/barrier waits;
it cannot send the visible editor's composition twice. Real window blur releases
all tracked keys (not only modifiers). Detach and permission loss also release.

The panel exits pointer lock without automatically restoring it. It lives inside
the page fullscreen root. Video-element-only fullscreen must be exited before
opening. Native textarea focus happens in the opening user gesture; visualViewport
resize/scroll adjusts keyboard avoidance. These code paths still require actual
mobile-browser/device acceptance.

Drafts are in-memory and scoped to the application instance. Composition and
early typing survive the begin-barrier acknowledgement. Permission loss and
instance change clear drafts; transport loss preserves the current draft but
does not retry a submission. An uncertain barrier keeps ordinary input fenced
until primary reconnection. No clipboard or global keyboard fallback exists.

Advisory target hints poll at 750 ms when no barrier/submission is outstanding
and the existing channel send buffer is below 64 KiB. Target changes while editing disable
Send until the user closes the panel and chooses the current input target again.
Submitted means the backend accepted the text API call, not proof of displayed
text; remote visual/content acceptance is required separately.

## Verification and delivery

The retired, uncommitted dedicated-WebSocket attempt is preserved intact under
`backup/application_text_websocket_retirement_20260909_2345/`, with a manifest.
It is reference-only and excluded from production and tests.

Run `npm run test:unit` and `npm run build` from this directory. The build runs the
repository protobuf synchronization first. Transport tests cover generation
fences, no retry, wrong instance/lease replies, timeout, polling cancellation,
queued close, composition/draft preservation and repeated disposal. Input tests
cover held non-modifier release, suppression and ordinary English before/after.

The production bundle is `web/px_web_client/dist`; the official collection is
`build_official/dist/web_client` (see the release script's frontend mapping).
Publish all generated assets and compare relative-file SHA-256 values. Render's
deployed `web_client` directory also needs the same files for remote browser
acceptance. Do not infer this deployed state merely from a successful Vite build.

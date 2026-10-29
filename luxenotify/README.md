# LuxeNotify

LuxeNotify is the GTK4 notification service for the Luxe desktop. It owns
`org.freedesktop.Notifications` on the session bus and implements the standard
desktop notification methods and signals.

The initial service supports notification replacement, application and image
icons, body markup, action buttons, default actions, progress values, urgency,
resident notifications, expiration, primary-monitor placement, and stacked X11
notification windows. Run `luxenotify --about` for credits or `luxenotify
--version` for the installed version.

Public bus ownership lives in a GTK-free broker. Cards are rendered by a
temporary GTK4/Cairo child that receives requests over an authenticated private
inherited socket and exits shortly after the last card closes. Notification IDs
continue across child restarts, and cards left open by a crashed child close
with reason 3. A narrow `org.luxe.Notifications` interface reports only the
active-notification count to LuxePanel. Notification history, Do Not Disturb and
per-application controls are not implemented.

The renderer owns the live notification windows and their timeout/action
lifecycle. The broker serializes requests, allocates the next ID range, and
caches the renderer's active count for immediate control-interface reads; its
active-ID set exists only to publish close events if the renderer exits. A
method rejection is returned to that caller without restarting the renderer.
A transport failure or timeout has an unknown outcome, so the broker fails
that call, terminates and reaps the peer, closes its remaining cards, and sends
queued work only after the old peer is gone. Requests are limited to 16 MiB
each, 32 MiB queued, and 64 queued calls. Icon decode work is limited to four
concurrent loads and 4 MiB per encoded icon.

# LuxePolkit

`luxepolkit` registers the session authentication listener using GLib/GIO and
libpolkit, without linking or initializing GTK. It holds that registration while
the session is active. When Polkit requests authentication it launches
`/usr/libexec/luxepolkit-dialog`, an unprivileged GTK4/Cairo child, and reaps it
after success, cancellation or failure. Existing desktop/autostart aliases stay.

The authority cookie, details and permitted identities travel only over an
inherited private pipe, bounded to 1 MiB. No password travels to the listener:
the dialog retains the original PolkitAgentSession/PAM helper protocol and wipes
its submitted entry buffer. Neither listener nor child grants authorization;
Polkit validates the trusted helper response. Policies and privileged helper
ownership are unchanged. Do not install the dialog setuid.

Cancellation is dispatched onto the request's main context, signals the child,
then force-stops it after two seconds if needed. Linux parent-death signaling
also kills a child if the listener exits, even if its GUI loop is stuck. Only one prompt may be active;
concurrent requests fail rather than competing for input grabs. Missing helpers,
empty identities and invalid/oversized requests fail closed. Registration errors
are reported to stderr and exit nonzero (no resident error GUI).
The child must report initialization within 15 seconds; that deadline is
removed once it is ready, so it does not limit time spent entering a password.

The child reads the current desktop scheme on every request and follows changes
while open. The GTK4 dialog retains password-ready focus, retry, About credits,
and X11 keyboard/pointer grabs. Password entry stays disabled until both grabs
succeed. The child retries for three seconds if another client owns a grab,
then fails authentication rather than proceeding without it. Authentication on
non-X11 displays is not currently supported by this input policy. X11 grabs
are not a security boundary against other clients authorized to use the display.

Tests use isolated displays and fake PAM conversations, not real passwords or
host authorization. Actual fresh-login/Polkit authentication and Chromebook RAM
acceptance are the installer's follow-up checks.

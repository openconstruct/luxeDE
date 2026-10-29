/* luxeappearance-obconf build configuration (CMake).
   Copyright (C) 2026 Jerry Howell. SPDX-License-Identifier: GPL-2.0-or-later */

/* Build against GTK4 */
#cmakedefine ENABLE_GTK4

/* always defined to indicate that i18n is enabled */
#cmakedefine ENABLE_NLS

/* Gettext package. */
#cmakedefine GETTEXT_PACKAGE "@GETTEXT_PACKAGE@"

/* Name of package */
#cmakedefine PACKAGE "@PACKAGE@"

/* Version number of package (mirrors the VERSION compile definition). */
#define VERSION "@LUXE_ABOUT_VERSION@"

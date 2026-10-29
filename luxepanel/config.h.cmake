/* luxepanel build configuration (CMake). */

/* Define to disable using ALSA in 'volume' plugin. */
#cmakedefine DISABLE_ALSA

/* Disable menu related routines */
#cmakedefine DISABLE_MENU

/* Disable plugin loading */
#cmakedefine DISABLE_PLUGINS_LOADING

/* Build against GTK4 */
#cmakedefine ENABLE_GTK4

/* always defined to indicate that i18n is enabled */
#cmakedefine ENABLE_NLS

/* Gettext package. */
#cmakedefine GETTEXT_PACKAGE "@GETTEXT_PACKAGE@"

/* Name of package */
#cmakedefine PACKAGE "@PACKAGE@"

/* Version number of package */
#cmakedefine VERSION "@PROJECT_VERSION@"

/* Defined to enable the taskbar's file-launcher fallback paths */
#cmakedefine ENABLE_TASKBAR_LAUNCHER

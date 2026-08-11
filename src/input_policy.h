/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SUZYCUBE_INPUT_POLICY_H
#define SUZYCUBE_INPUT_POLICY_H

/* Cheap handheld sticks can rest well outside SDL's nominal center.  The
 * separate thresholds keep an active stick stable while still requiring a
 * deliberate movement to leave neutral. */
#define SC_INPUT_STICK_ENTER_DEADZONE 0.40f
#define SC_INPUT_STICK_EXIT_DEADZONE 0.30f
#define SC_INPUT_TRIGGER_DEADZONE 0.05f

#endif

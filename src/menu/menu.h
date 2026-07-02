/**
 * @file menu.h
 * @brief Menu Subsystem
 * @ingroup menu
 */

#ifndef MENU_H__
#define MENU_H__

#include "boot/boot.h"
#include "menu_state.h"

/**
 * @brief Runs the menu subsystem.
 *
 * This function initializes and runs the menu subsystem using the provided
 * boot parameters.
 *
 * @param boot_params A pointer to the boot parameters.
 */
void menu_run(boot_params_t *boot_params);

/**
 * @brief Quick-boot the most recently played ROM or disk.
 *
 * Reads the top of the history list (index 0 is the most recent entry, see
 * bookkeeping.c) and routes directly to the matching load view, skipping the
 * load/info screen. The load view resolves the path from load_history_id;
 * setting the matching load_pending flag makes its display boot immediately on
 * the next frame (the same mechanism the autoload feature uses in startup.c).
 * No-op (with an error sound) when there is no history yet.
 *
 * Shared by the browser (hold-Z quick-boot) and startup (auto-boot last played).
 *
 * @param menu Pointer to the menu structure.
 * @return true if a boot was triggered, false otherwise.
 */
bool menu_boot_last_played(menu_t *menu);

#endif // MENU_H__

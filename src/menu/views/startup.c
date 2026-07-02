#include "../menu.h"
#include "utils/fs.h"
#include "views.h"


static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach_clear(d, NULL);
    rdpq_detach_show();
}


// Number of poll passes used to sample the auto-boot override at startup.
// The joypad state needs a few polls to settle, and this is the ONLY escape
// from auto-boot, so we sample generously across a short window (~a few frames)
// rather than a single instant to make a held button reliably detected.
#define BOOT_LAST_OVERRIDE_POLLS (20)

/**
 * @brief Check whether the auto-boot override (hold any button) is being held.
 *
 * Auto-boot-last-played is the only feature that boots without user input, and
 * this override is the sole way to reach the menu when it is enabled, so the
 * detection is deliberately forgiving: it polls repeatedly over a short window
 * and reports held as soon as ANY button appears on ANY port. Accepting any
 * button (rather than a specific one) means a user who enabled this long ago and
 * forgot the escape can simply mash the controller during boot to reach the menu.
 *
 * @return true if any button is held on any port during the sampling window.
 */
static bool boot_last_override_held (void) {
    for (int pass = 0; pass < BOOT_LAST_OVERRIDE_POLLS; pass++) {
        joypad_poll();
        JOYPAD_PORT_FOREACH (port) {
            if (joypad_get_buttons_held(port).raw) {
                return true;
            }
        }
        wait_ms(1);
    }
    return false;
}

void view_startup_init (menu_t *menu) {
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    // FIXME: rather than use a controller button, would it be better to use the cart button?
    JOYPAD_PORT_FOREACH (port) {
        joypad_poll();
        joypad_buttons_t b_held = joypad_get_buttons_held(port);

        if (menu->settings.rom_autoload_enabled && b_held.start) {
            menu->settings.rom_autoload_enabled = false;
            menu->settings.rom_autoload_path = "";
            menu->settings.rom_autoload_filename = "";
            settings_save(&menu->settings);
        }
    }
    if (menu->settings.rom_autoload_enabled) {
        menu->browser.directory = path_init(menu->storage_prefix, menu->settings.rom_autoload_path);
        menu->load.rom_path = path_clone_push(menu->browser.directory, menu->settings.rom_autoload_filename);
        menu->load_pending.rom_file = true;
        menu->next_mode = MENU_MODE_LOAD_ROM;

        return;
    }
#endif

    // Auto-boot Last Played: optionally boot the most recently played game
    // directly on menu startup, skipping the browser.
    //
    // The menu cannot reliably tell a cold power-on from a RESET-button reboot
    // (the flashcart boot handoff always looks like a warm reset, and the menu's
    // own game-launch path tears down RDRAM refresh, so no persistent RAM/config
    // signal survives). Rather than a fragile timing heuristic, auto-boot simply
    // fires on every menu startup when enabled, and holding ANY button during
    // startup is the guaranteed escape to the menu (the setting stays enabled).
    // This is sampled generously (boot_last_override_held). Empty history or any
    // load error falls through to the normal menu below.
    if (menu->settings.boot_last_played_enabled && !boot_last_override_held()) {
        if (menu_boot_last_played(menu)) {
            return;
        }
    }

    if (menu->settings.first_run) {
        menu->settings.first_run = false;
        settings_save(&menu->settings);
        menu->next_mode = MENU_MODE_CREDITS;
    }
    else {
        menu->next_mode = MENU_MODE_BROWSER;
    }
}

void view_startup_display (menu_t *menu, surface_t *display) {
    draw(menu, display);
}

#include <stdarg.h>
#include "../bookkeeping.h"
#include "../fonts.h"
#include "../ui_components/constants.h"
#include "../sound.h"
#include "views.h"


typedef enum {
    BOOKKEEPING_TAB_CONTEXT_HISTORY,
    BOOKKEEPING_TAB_CONTEXT_FAVORITE,
    BOOKKEEPING_TAB_CONTEXT_NONE
} bookkeeping_tab_context_t;


/** @brief Pixel height of a single rendered list entry (two text lines). */
#define ENTRY_HEIGHT (19 * 2)

/**
 * @brief Number of entries visible at once before the list scrolls.
 *
 * Each entry occupies two text lines, so half as many entries fit as the file
 * browser shows single-line rows (LIST_ENTRIES).
 */
#define FAVORITES_VISIBLE (LIST_ENTRIES / 2)


static bookkeeping_tab_context_t tab_context = BOOKKEEPING_TAB_CONTEXT_NONE;
static int selected_item = -1;
static bookkeeping_item_t *item_list;
static uint16_t item_max = 0;
/** @brief True while the "remove favorite?" confirmation modal is showing. */
static bool confirm_remove = false;


static void item_reset_selected(menu_t *menu) {
    selected_item = -1;

    for(uint16_t i=0; i<item_max; i++) {
        if(item_list[i].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            selected_item = i;
            break;
        }
    }
}

static void item_move_next() {
    int last = selected_item;

    do
    {
        selected_item++;

        if(selected_item >= item_max) {
            selected_item = last;
            break;
        } else if(item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            sound_play_effect(SFX_CURSOR);
            break;
        }
    } while (true);
}

static void item_move_previous() {
    int last = selected_item;
    do
    {
        selected_item--;

        if(selected_item < 0) {
            selected_item = last;
            break;
        } else if(item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            sound_play_effect(SFX_CURSOR);
            break;
        }
    } while (true);
}

/**
 * @brief Compute the first entry index to draw so the selection stays on screen.
 *
 * Mirrors the file browser's windowing: keep the selection roughly centred and
 * clamp so the window never scrolls past either end of the list.
 */
static int list_starting_position(void) {
    int starting_position = 0;

    if(item_max > FAVORITES_VISIBLE && selected_item >= (FAVORITES_VISIBLE / 2)) {
        starting_position = selected_item - (FAVORITES_VISIBLE / 2);
        if(starting_position >= item_max - FAVORITES_VISIBLE) {
            starting_position = item_max - FAVORITES_VISIBLE;
        }
    }

    return starting_position;
}

/**
 * @brief Handle input while the remove-favorite confirmation modal is open.
 *
 * A confirms the removal, anything that reads as "back" cancels. All other
 * input is swallowed so the list underneath does not react.
 */
static void process_confirm_remove(menu_t *menu) {
    if(menu->actions.enter) {
        bookkeeping_favorite_remove(&menu->bookkeeping, selected_item);
        item_reset_selected(menu);
        confirm_remove = false;
        sound_play_effect(SFX_SETTING);
    } else if(menu->actions.back) {
        confirm_remove = false;
        sound_play_effect(SFX_EXIT);
    }
}

static void process(menu_t *menu) {
    if(confirm_remove) {
        process_confirm_remove(menu);
        return;
    }

    // Favorites "manage" mode: hold R and use C-up/C-down to reorder, or R+B to
    // remove (with confirmation). Handled before plain navigation so the C-button
    // presses are consumed as moves rather than falling through to anything else.
    if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE && menu->actions.options_held && selected_item != -1) {
        if(menu->actions.c_up) {
            selected_item = bookkeeping_favorite_move_up(&menu->bookkeeping, selected_item);
            sound_play_effect(SFX_CURSOR);
            return;
        } else if(menu->actions.c_down) {
            selected_item = bookkeeping_favorite_move_down(&menu->bookkeeping, selected_item);
            sound_play_effect(SFX_CURSOR);
            return;
        } else if(menu->actions.back) {
            confirm_remove = true;
            sound_play_effect(SFX_SETTING);
            return;
        }
    }

    if(menu->actions.go_down) {
        item_move_next();
    } else if(menu->actions.go_up) {
        item_move_previous();
    } else if(menu->actions.enter && selected_item != -1) {

        if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->load.load_favorite_id = selected_item;
            menu->load.load_history_id = -1;
        } else if(tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->load.load_history_id = selected_item;
            menu->load.load_favorite_id = -1;
        }

        if(item_list[selected_item].bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
            menu->next_mode = MENU_MODE_LOAD_DISK;
            sound_play_effect(SFX_ENTER);
        } else if(item_list[selected_item].bookkeeping_type == BOOKKEEPING_TYPE_ROM) {
            menu->next_mode = MENU_MODE_LOAD_ROM;
            sound_play_effect(SFX_ENTER);
        }
    } else if (menu->actions.go_left) {
        if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->next_mode = MENU_MODE_HISTORY;
        } else if(tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->next_mode = MENU_MODE_BROWSER;
        }
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_right) {
        if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->next_mode = MENU_MODE_BROWSER;
        } else if(tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->next_mode = MENU_MODE_FAVORITE;
        }
        sound_play_effect(SFX_CURSOR);
    }
}

static void draw_list(menu_t *menu, surface_t *display) {
    int starting_position = list_starting_position();

    // Scrollbar tracks the selection against the full list, like the browser.
    ui_components_list_scrollbar_draw(selected_item < 0 ? 0 : selected_item, item_max, FAVORITES_VISIBLE);

    if(selected_item != -1) {
        float highlight_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT +  TEXT_OFFSET_VERTICAL + ((selected_item - starting_position) * ENTRY_HEIGHT);

        ui_components_box_draw(
            VISIBLE_AREA_X0,
            highlight_y,
            VISIBLE_AREA_X0 + FILE_LIST_HIGHLIGHT_WIDTH,
            highlight_y + (ENTRY_HEIGHT - 1),
            FILE_LIST_HIGHLIGHT_COLOR
        );
    }

    char buffer[1024];
    buffer[0] = 0;

    for(int i = starting_position; i < starting_position + FAVORITES_VISIBLE && i < item_max; i++) {
        if(path_has_value(item_list[i].primary_path)) {
            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%d  : %s\n", (i+1), path_last_get(item_list[i].primary_path));
        } else {
            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%d  : \n", (i+1));
        }

        if(path_has_value(item_list[i].secondary_path)) {
            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "     %s\n", path_last_get(item_list[i].secondary_path));
        } else {
            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "\n");
        }
    }

    int nbytes = strlen(buffer);
    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2),
            .align = ALIGN_LEFT,
            .valign = VALIGN_TOP,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_OFFSET_VERTICAL,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT +  TEXT_OFFSET_VERTICAL,
        buffer,
        nbytes
    );
}

static void draw(menu_t *menu, surface_t *display) {
    rdpq_attach(display, NULL);

    ui_components_background_draw();

    if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
        ui_components_tabs_common_draw(2);
    } else if(tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
        ui_components_tabs_common_draw(1);
    }

    ui_components_layout_draw_tabbed();

    draw_list(menu, display);

    if(selected_item != -1) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Load Game\n"
            "\n"
        );

        if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            ui_components_actions_bar_text_draw(
                STL_DEFAULT,
                ALIGN_RIGHT, VALIGN_TOP,
                "Hold R + C▲▼: Reorder\n"
                "Hold R + B: Remove\n"
            );
        }
    }

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "◀ Change Tab ▶\n"
        "\n"
    );

    if(confirm_remove) {
        ui_components_messagebox_draw(
            "Remove this favorite?\n"
            "\n"
            "A: Remove      B: Cancel"
        );
    }

    rdpq_detach_show();
}

void view_favorite_init (menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_FAVORITE;
    item_list = menu->bookkeeping.favorite_items;
    item_max = FAVORITES_COUNT;
    confirm_remove = false;

    item_reset_selected(menu);
}

void view_favorite_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}

void view_history_init (menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_HISTORY;
    item_list = menu->bookkeeping.history_items;
    item_max = HISTORY_COUNT;
    confirm_remove = false;

    item_reset_selected(menu);
}

void view_history_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}

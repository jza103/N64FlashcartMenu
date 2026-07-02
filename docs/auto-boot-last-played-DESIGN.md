# Auto-boot Last Played — design notes & the reset-vs-power-on investigation

> Status: **v1 shipped on this branch** (`contrib/autoboot-last-played`).
> This document records the feature as shipped **and** the full investigation
> into the one problem we could *not* solve — telling a console **RESET** apart
> from a **cold power-on** — including every approach tried and why each failed.
> Kept as an engineering record; this branch is intentionally **not** submitted
> upstream.

## What the feature does (v1, as shipped)

An optional setting, **Auto-boot Last Played** (default **off**), in the settings
editor. When enabled, each time the menu starts it boots the most recently played
game (`bookkeeping.history_items[0]`) directly, skipping the browser — reusing the
same load-view machinery as the hold-Z quick-boot and the existing autoload path.

- **Escape to the menu:** hold **any** button on any controller port during
  startup. Sampled generously (`boot_last_override_held()` in `startup.c`).
  "Any button" is deliberate: a user who enables this, walks away for months, and
  forgets the escape can simply mash the controller during boot.
- **Enabling shows a confirmation** messagebox spelling out the escape.
- Empty history or any load error falls through to the normal menu.
- The setting persists (`boot_last_played_enabled`, settings `schema_revision`
  bumped 1 → 2).

### Files touched (v1)
- `menu.c` / `menu.h` — `menu_boot_last_played()` extracted here so it is shared
  by the browser (hold-Z) and startup (auto-boot).
- `views/startup.c` — the auto-boot gate + `boot_last_override_held()`.
- `views/settings_editor.c` — the toggle + confirmation messagebox.
- `settings.c` / `settings.h` — the persisted setting + schema bump.
- `views/browser.c` — `boot_last_played()` moved out to `menu.c` (call-site now
  `menu_boot_last_played()`).

## The hard problem: RESET vs cold power-on

The *desired* behavior was: **cold power-on → auto-boot the last game**, but
**pressing RESET → return to the menu** (so RESET is an "escape hatch"). This
turned out to be **not reliably achievable** on this hardware/firmware. Every
mechanism that could distinguish the two was eliminated empirically. v1 therefore
drops the distinction: auto-boot fires on *every* menu start, and hold-any-button
is the escape.

### Background: how an N64 boots (RDRAM, IPL3, NMI)

A few terms used below:

- **RDRAM** — the N64's main memory. It is *dynamic* RAM: it only retains data as
  long as it is periodically *refreshed*. Cut power and it loses its contents
  within seconds. It must also be *trained/initialized* early in boot before it
  is reliably usable.
- **IPL3** — the third stage of the N64 boot ROM (the "boot code" that runs before
  the game). Among other things it initializes RDRAM and then jumps to the game.
  On a flashcart, the *flashcart's* IPL3 is what loads and launches the menu ROM.
- **NMI (Non-Maskable Interrupt)** — what the **RESET button** generates. Crucial
  detail: on the N64 the RESET button sends an NMI **to the CPU only**. The RCP
  (which contains the RDRAM controller) is **not** reset. So on a RESET, RDRAM
  contents are *not* cleared by hardware — unlike a power-off, which loses them.
  This asymmetry is what we tried (and failed) to exploit.
- **Warm vs cold reset** — libdragon exposes `sys_reset_type()` returning
  `RESET_COLD` (power-on) or `RESET_WARM` (NMI/reset button). This value comes
  from a byte IPL3 writes at boot.

### The boot handoff in *this* codebase (why RDRAM can't carry a flag)

When the menu launches a game, `src/boot/boot.c` → `src/boot/reboot.S` performs
the handoff. The key lines in `reboot.S`:

```asm
    bnez $a0, reset_rdram_skip  # skip when cheats are enabled (skip_rdram_reset)
    bnez $s5, reset_rdram_skip  # skip when reset type is NMI
reset_rdram:
    li   $t0, RI_ADDRESS
    sw   $zero, RI_REFRESH($t0) # <-- disables RDRAM refresh (destroys contents)
    sw   $zero, RI_SELECT($t0)
reset_rdram_skip:
    ...
    li   $t3, IPL3_ENTRY
    jr   $t3                    # hand off to IPL3
```

In `boot.c` the flag is `skip_rdram_reset = cheats_installed` and `reset_type` is
hard-coded to `BOOT_RESET_TYPE_COLD` (= 0). For a normal (no-cheats) launch, both
`bnez` branches fall through, so the code **zeroes the RDRAM refresh register on
every game launch**. That forces IPL3 to fully re-train RDRAM — which *destroys*
any value the menu left in RDRAM. So the menu's own boot path wipes RDRAM every
time it launches a game.

## Approaches tried, and why each failed

1. **`sys_reset_type() == RESET_COLD` gate.**
   *Failed.* On hardware it **always** returns `RESET_WARM`, on both a true cold
   power-on and a RESET, on a real N64. Reason (confirmed in libdragon source):
   `sys_reset_type()` just returns a byte IPL3 writes at boot; the flashcart's own
   IPL3 hands off to the menu via a warm-reset-style path, so the menu always sees
   WARM. The gate could never fire.

2. **SC64 `CFG_ID_BOOT_MODE` register as a "was reset" flag.**
   *Failed.* The SC64 driver's init writes `BOOT_MODE_MENU` (`sc64.c`, inside
   `flashcart_init()`) **before** our startup code runs, clobbering anything we
   might have stored there.

3. **SD marker file** (write a flag on launch; presence at startup ⇒ came from a
   reset).
   *Failed (ambiguous).* A plain marker cannot tell "reset back into the menu"
   from "powered off during a game, powered on later" — both leave the marker set.
   Result: auto-boot effectively only fires every *other* power cycle.

4. **RDRAM magic-word** (stamp a value in RDRAM; if present next boot ⇒ RAM
   survived ⇒ RESET; if decayed ⇒ power-on). Elegant in principle because an NMI
   preserves RDRAM but a power-off does not.
   *Failed on hardware — verdict "GARBAGE" every time.* Two independent reasons:
   (a) our own `reboot.S` disables RDRAM refresh on every game launch (above), and
   (b) the flashcart's *inbound* IPL3 re-initializes RDRAM when it re-enters the
   menu. We control neither well enough to preserve a word across the full
   round trip (menu → game → reset → menu).

5. **Force `skip_rdram_reset` to preserve RDRAM** (make launches take the
   cheats-style path that skips the refresh-reset, gated so only the opted-in
   feature is affected).
   *Failed twice.* Even with the skip forced, the RDRAM word still read back as
   **GARBAGE** (the flashcart's inbound re-init clobbers it regardless). And
   worse, forcing the skip on a normal cold boot produced a **blank screen** —
   the RDRAM refresh-reset is *required* for a game to run correctly on a real
   N64; skipping it leaves memory improperly initialized. So the approach is both
   ineffective *and* unsafe.

6. **SC64 RTC timestamp** (store launch time, compare elapsed time at next
   startup: small gap ⇒ reset, large gap ⇒ power-on). The RTC is battery-backed
   and survives everything.
   *Rejected — fundamentally ambiguous.* The measurable gap is
   `launch_time + play-session length + off-time`. We can only timestamp when the
   menu *launches* a game and when it *next runs*; we cannot timestamp when the
   game *stopped* (the game is running then, not our code). So:
   - "play 5 min, reset" (gap ≈ 5 min) vs "play 5 min, power off, return in 5 min"
     (gap ≈ 5 min) — indistinguishable, and
   - "play **2 hours** then reset" looks exactly like "play 5 min, power off,
     return **2 hours** later".
   No threshold separates a reset-after-a-long-session from a delayed power-on.

### Does the cart know the RESET button was pressed? No.

- The SC64 only tracks its **own on-cart button** (`BUTTON_STATE`/`BUTTON_MODE`),
  not the N64 console's RESET button. There is no config/setting/command that
  records reset-vs-power-cycle (per the SC64 docs).
- The N64 CPU knows the NMI bit only *transiently* during boot; the flashcart's
  IPL3 consumes/resets it before the menu runs — the same reason (1) fails.

## An important testing caveat (USB power)

SC64 config — including Fast Reboot's `CFG_ID_BOOT_MODE = BOOT_MODE_ROM` — is
"preserved on power cycle **only when powered from USB**." With a USB cable
attached, the **cart never loses power**, so a "cold" power-on is *not* cold to
the cart: Fast Reboot's ROM boot mode survives and the flashcart boots the game
directly (no menu, no progress bar) even across a real console power-cycle.

**All cold-vs-reset testing must be done with USB unplugged.** With USB attached,
"power-on" and "reset" are electrically indistinguishable to the cart, which
silently invalidates the very behavior under test. (This confounded several early
results before it was identified.)

## Conclusion

On a real N64 with the SC64, there is no reliable, cart-agnostic signal available
to the menu that distinguishes a RESET from a cold power-on:

- `sys_reset_type()` → always WARM (flashcart handoff).
- SC64 config registers → clobbered by the driver before our code runs.
- RDRAM → destroyed by our own boot path *and* the flashcart's inbound re-init.
- RTC → survives everything equally, so elapsed-time is ambiguous.
- The cart does not expose the console RESET state.

v1 therefore ships the honest behavior: **auto-boot on every menu start when
enabled; hold any button to reach the menu.** This is consistent with how Fast
Reboot already behaves (a fast power-cycle boots the ROM directly), and the
escape is always available.

---

*Developed with AI assistance; all approaches were reviewed and verified on real
N64 + SummerCart64 hardware.*

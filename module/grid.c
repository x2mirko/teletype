#include "grid.h"
#include "font.h"
#include "globals.h"
#include "state.h"
#include "teletype.h"
#include "timers.h"
#include "util.h"

#define GRID_MAX_KEY_PRESSED 10
#define GRID_KEY_HOLD_DELAY 700
#define GRID_KEY_REPEAT_RATE 40

static const u8 glyph[16][6] = {
    {
    0b000000,
    0b000000,
    0b000000,
    0b000000,
    0b000000,
    0b000000
    },
    {
    0b000000,
    0b011110,
    0b010010,
    0b010010,
    0b011110,
    0b000000
    },
    {
    0b000000,
    0b011110,
    0b010010,
    0b010010,
    0b010010,
    0b010010
    },
    {
    0b010010,
    0b010010,
    0b010010,
    0b010010,
    0b010010,
    0b010010
    },
    {
    0b010010,
    0b010010,
    0b010010,
    0b010010,
    0b011110,
    0b000000
    },
    {
    0b000000,
    0b011111,
    0b010000,
    0b010000,
    0b011111,
    0b000000
    },
    {
    0b000000,
    0b111111,
    0b000000,
    0b000000,
    0b111111,
    0b000000
    },
    {
    0b000000,
    0b111110,
    0b000010,
    0b000010,
    0b111110,
    0b000000
    },
    {
    0b000000,
    0b011111,
    0b010000,
    0b010000,
    0b010000,
    0b010000
    },
    {
    0b000000,
    0b111111,
    0b000000,
    0b000000,
    0b000000,
    0b000000
    },
    {
    0b000000,
    0b111110,
    0b000010,
    0b000010,
    0b000010,
    0b000010
    },
    {
    0b010000,
    0b010000,
    0b010000,
    0b010000,
    0b010000,
    0b010000
    },
    {
    0b000010,
    0b000010,
    0b000010,
    0b000010,
    0b000010,
    0b000010
    },
    {
    0b010000,
    0b010000,
    0b010000,
    0b010000,
    0b011111,
    0b000000
    },
    {
    0b000000,
    0b000000,
    0b000000,
    0b000000,
    0b111111,
    0b000000
    },
    {
    0b000010,
    0b000010,
    0b000010,
    0b000010,
    0b111110,
    0b000000
    }
};

typedef struct {
    u8 used;
    u8 key;
    u8 x;
    u8 y;
    scene_state_t *ss;
    softTimer_t timer;
} hold_repeat_info;

static u16 size_x = 16, size_y = 8;
static u8 screen[GRID_MAX_DIMENSION][GRID_MAX_DIMENSION/2];
static hold_repeat_info held_keys[GRID_MAX_KEY_PRESSED];
static u8 timers_uninitialized = 1;

static void hold_repeat_timer_callback(void* o);
static void grid_process_key_hold_repeat(scene_state_t *ss, u8 x, u8 y, u8 is_hold);
static void grid_screen_refresh_ctrl(scene_state_t *ss, u8 page, u8 x1, u8 y1, u8 x2, u8 y2);
static void grid_screen_refresh_led(scene_state_t *ss, u8 full_grid, u8 page, u8 x1, u8 y1, u8 x2, u8 y2);
static void grid_screen_refresh_info(scene_state_t *ss, u8 page, u8 x1, u8 y1, u8 x2, u8 y2);
static bool grid_within_area(u8 x, u8 y, grid_common_t *gc);
static void grid_fill_area(u8 x, u8 y, u8 w, u8 h, u8 level);
static void grid_fill_area_scr(u8 x, u8 y, u8 w, u8 h, u8 level, u8 page);

void grid_process_key(scene_state_t *ss, u8 _x, u8 _y, u8 z, u8 ignore_rotate) {
    if (timers_uninitialized) {
        timers_uninitialized = 0;
        for (u8 i = 0; i < GRID_MAX_KEY_PRESSED; i++)
            held_keys[i].used = 0;
    }
    
    u8 x = SG.rotate && !ignore_rotate ? monome_size_x() - _x - 1 : _x;
    u8 y = SG.rotate && !ignore_rotate ? monome_size_y() - _y - 1 : _y;

    u8 key = (y << 4) | x;
    if (z) {
        for (u8 i = 0; i < GRID_MAX_KEY_PRESSED; i++)
            if (!held_keys[i].used || held_keys[i].key == key) {
                held_keys[i].used = 1;
                held_keys[i].key = key;
                held_keys[i].x = x;
                held_keys[i].y = y;
                held_keys[i].ss = ss;
                timer_add(&held_keys[i].timer, GRID_KEY_HOLD_DELAY,
                    &hold_repeat_timer_callback, (void *)&held_keys[i]);
                break;
            }
    } else {
        for (u8 i = 0; i < GRID_MAX_KEY_PRESSED; i++)
            if (held_keys[i].key == key) {
                timer_remove(&held_keys[i].timer);
                held_keys[i].used = 0;
            }
    }

    u8 refresh = 0;
    u8 scripts[SCRIPT_COUNT];
    for (u8 i = 0; i < SCRIPT_COUNT; i++) scripts[i] = 0;
    
    for (u8 i = 0; i < GRID_XYPAD_COUNT; i++) {
        if (z && GXYC.enabled && SG.group[GXYC.group].enabled && grid_within_area(x, y, &GXYC)) {
            GXY.value_x = x - GXYC.x;
            GXY.value_y = y - GXYC.y;
            if (GXYC.script != -1) scripts[GXYC.script] = 1;
            SG.latest_group = GXYC.group;
            if (SG.group[GXYC.group].script != -1) scripts[SG.group[GXYC.group].script] = 1;
            refresh = 1;
        }
    }

    u16 value;
    s8 held;
    if (z) {
        for (u8 i = 0; i < GRID_FADER_COUNT; i++) {
            if (GFC.enabled && SG.group[GFC.group].enabled && grid_within_area(x, y, &GFC)) {
                held = -1;
                if (GF.type & 1) {
                    for (u8 j = 0; j < GRID_MAX_KEY_PRESSED; j++)
                        if (held_keys[j].used && (held_keys[j].y != y) && 
                            grid_within_area(held_keys[j].x, held_keys[j].y, &GFC)) {
                            held = j;
                            break;
                        }
                } else {
                    for (u8 j = 0; j < GRID_MAX_KEY_PRESSED; j++)
                        if (held_keys[j].used && (held_keys[j].x != x) &&
                            grid_within_area(held_keys[j].x, held_keys[j].y, &GFC)) {
                            held = j;
                            break;
                        }
                }
                
                switch (GF.type) {
                    case FADER_CH_BAR:
                    case FADER_CH_DOT:
                        if (held == -1) {
                            GF.slide = 0;
                            GF.value = x - GFC.x;
                        } else {
                            GF.slide = 1;
                            GF.slide_acc = 0;
                            GF.slide_end = x - GFC.x;
                            GF.slide_delta = 16;
                            GF.slide_dir = GF.slide_end > GF.value;
                        }
                        break;
                    case FADER_CV_BAR:
                    case FADER_CV_DOT:
                        if (held == -1) {
                            GF.slide = 0;
                            GF.value = GFC.h + GFC.y - y - 1;
                        } else {
                            GF.slide = 1;
                            GF.slide_acc = 0;
                            GF.slide_end = GFC.h + GFC.y - y - 1;
                            GF.slide_delta = 16;
                            GF.slide_dir = GF.slide_end > GF.value;
                        }
                        break;
                    case FADER_FH_BAR:
                    case FADER_FH_DOT:
                        if (held != -1 && (held_keys[held].x == GFC.x || held_keys[held].x == (GFC.x + GFC.w - 1)))
                            held = -1;
                        if (held == -1) {
                            GF.slide = 0;
                            if (x == GFC.x) {
                                if (GF.value) GF.value--;
                            } else if (x == GFC.x + GFC.w - 1) {
                                if (GF.value < GFC.level) GF.value++;
                            } else {
                                value = ((((x - GFC.x - 1) << 1) + 1) * GFC.level) / (GFC.w - 2);
                                GF.value = (value >> 1) + (value & 1);
                            }
                        } else {
                            GF.slide = 1;
                            GF.slide_acc = 0;
                            if (x == GFC.x)
                                value = 0;
                            else if (x == (GFC.x + GFC.w - 1))
                                value = GFC.level;
                            else {
                                value = ((((x - GFC.x - 1) << 1) + 1) * GFC.level) / (GFC.w - 2);
                                value = (value >> 1) + (value & 1);
                            }
                            GF.slide_end = value;
                            value = ((GFC.w - 2) << 4) / GFC.level;
                            if (value == 0) value = 1;
                            GF.slide_delta = value;
                            GF.slide_dir = GF.slide_end > GF.value;
                        }
                        break;
                    case FADER_FV_BAR:
                    case FADER_FV_DOT:
                        if (held != -1 && (held_keys[held].y == GFC.y || held_keys[held].y == (GFC.y + GFC.h - 1)))
                            held = -1;
                        if (held == -1) {
                            GF.slide = 0;
                            if (y == GFC.y) {
                                if (GF.value < GFC.level) GF.value++;
                            } else if (y == GFC.y + GFC.h - 1) {
                                if (GF.value) GF.value--;
                            } else {
                                value = ((((GFC.h + GFC.y - y - 2) << 1) + 1) * GFC.level) / (GFC.h - 2);
                                GF.value = (value >> 1) + (value & 1);
                            }
                        } else {
                            GF.slide = 1;
                            GF.slide_acc = 0;
                            if (y == GFC.y)
                                value = GFC.level;
                            else if (y == (GFC.y + GFC.h - 1))
                                value = 0;
                            else {
                                value = ((((GFC.h + GFC.y - y - 2) << 1) + 1) * GFC.level) / (GFC.h - 2);
                                value = (value >> 1) + (value & 1);
                            }
                            GF.slide_end = value;
                            value = ((GFC.h - 2) << 4) / GFC.level;
                            if (value == 0) value = 1;
                            GF.slide_delta = value;
                            GF.slide_dir = GF.slide_end > GF.value;
                        }
                        break;
                }
                
                if (GFC.script != -1) scripts[GFC.script] = 1;
                SG.latest_fader = i;
                SG.latest_group = GFC.group;
                if (SG.group[GFC.group].script != -1) scripts[SG.group[GFC.group].script] = 1;
                refresh = 1;
            }
        }
    }

    for (u16 i = 0; i < GRID_BUTTON_COUNT; i++) {
        if (GBC.enabled && SG.group[GBC.group].enabled && grid_within_area(x, y, &GBC)) {
            if (GB.latch) {
                if (z) {
                    GB.state = !GB.state;
                    if (GBC.script != -1) scripts[GBC.script] = 1;
                }
            } else {
                GB.state = z;
                if (GBC.script != -1) scripts[GBC.script] = 1;
            }
            SG.latest_button = i;
            SG.latest_group = GBC.group;
            if (SG.group[GBC.group].script != -1) scripts[SG.group[GBC.group].script] = 1;
            refresh = 1;
        }
    }
    
    for (u8 i = 0; i < SCRIPT_COUNT; i++)
        if (scripts[i]) run_script(ss, i);

    if (refresh) SG.grid_dirty = SG.scr_dirty = 1;
}

void grid_process_key_hold_repeat(scene_state_t *ss, u8 x, u8 y, u8 is_hold) {
    u8 refresh = 0;
    u8 scripts[SCRIPT_COUNT];
    for (u8 i = 0; i < SCRIPT_COUNT; i++) scripts[i] = 0;
    
    u8 update = 0;
    for (u8 i = 0; i < GRID_FADER_COUNT; i++) {
        if (GFC.enabled && SG.group[GFC.group].enabled && grid_within_area(x, y, &GFC)) {
            update = 0;
            if (GF.type == FADER_FH_BAR || GF.type == FADER_FH_DOT) {
                if (x == GFC.x) {
                    if (GF.value) GF.value--;
                    update = 1;
                } else if (x == GFC.x + GFC.w - 1) {
                    if (GF.value < GFC.level) GF.value++;
                    update = 1;
                }
            } else if (GF.type == FADER_FV_BAR || GF.type == FADER_FV_DOT) {
                if (y == GFC.y) {
                    if (GF.value < GFC.level) GF.value++;
                    update = 1;
                } else if (y == GFC.y + GFC.h - 1) {
                    if (GF.value) GF.value--;
                    update = 1;
                }
            }
            
            if (update) {
                if (GFC.script != -1) scripts[GFC.script] = 1;
                SG.latest_fader = i;
                SG.latest_group = GFC.group;
                if (SG.group[GFC.group].script != -1) scripts[SG.group[GFC.group].script] = 1;
                refresh = 1;
            }
        }
    }
    
    for (u8 i = 0; i < SCRIPT_COUNT; i++)
        if (scripts[i]) run_script(ss, i);

    if (refresh) SG.grid_dirty = SG.scr_dirty = 1;
}

void hold_repeat_timer_callback(void* o) {
    hold_repeat_info* hr = o;
    u8 is_hold = hr->used == 1;
    if (is_hold) {
        timer_set(&hr->timer, GRID_KEY_REPEAT_RATE);
        hr->used = 2;
    }
    grid_process_key_hold_repeat(hr->ss, hr->x, hr->y, is_hold);
}

void grid_process_fader_slew(scene_state_t *ss) {
    u8 refresh = 0;
    u8 scripts[SCRIPT_COUNT];
    for (u8 i = 0; i < SCRIPT_COUNT; i++) scripts[i] = 0;
    
    for (u8 i = 0; i < GRID_FADER_COUNT; i++) {
        if (!GF.slide) continue;
        GF.slide_acc++;
        if (GF.slide_acc >= GF.slide_delta) {
            GF.slide_acc = 0;
            if (GF.slide_dir)
                GF.value++;
            else
                GF.value--;
            if ((GF.slide_dir && GF.value >= GF.slide_end) ||
                (!GF.slide_dir && GF.value <= GF.slide_end)) {
                GF.value = GF.slide_end;
                GF.slide = 0;
            }
            SG.latest_fader = i;
            SG.latest_group = GFC.group;
            if (GFC.script != -1) run_script(ss, GFC.script);
            if (SG.group[GFC.group].script != -1) scripts[SG.group[GFC.group].script] = 1;
            refresh = 1;
        }
    }

    for (u8 i = 0; i < SCRIPT_COUNT; i++)
        if (scripts[i]) run_script(ss, i);

    if (refresh) SG.grid_dirty = SG.scr_dirty = 1;
}

bool grid_within_area(u8 x, u8 y, grid_common_t *gc) {
    return x >= gc->x && x < (gc->x + gc->w) && y >= gc->y && y < (gc->y + gc->h);
}

void grid_refresh(scene_state_t *ss) {
    size_x = monome_size_x();
    size_y = monome_size_y();

    grid_fill_area(0, 0, size_x, size_y, 0);

    u16 x, y;
    for (u8 i = 0; i < GRID_XYPAD_COUNT; i++) {
        if (GXYC.enabled && SG.group[GXYC.group].enabled) {
            if (GXY.value_x || GXY.value_y) {
                x = GXYC.x + GXY.value_x;
                y = GXYC.y + GXY.value_y;
                grid_fill_area(GXYC.x, y, GXYC.w, 1, GXYC.level);
                grid_fill_area(x, GXYC.y, 1, GXYC.h, GXYC.level);
                grid_fill_area(x, y, 1, 1, 15);
            }
        }
    }

    u16 fv, ff, fp;
    for (u8 i = 0; i < GRID_FADER_COUNT; i++) {
        if (GFC.enabled && SG.group[GFC.group].enabled) {
            switch (GF.type) {
                case FADER_CH_BAR:
                    grid_fill_area(GFC.x, GFC.y, GF.value + 1, GFC.h, 15);
                    grid_fill_area(GFC.x + GF.value + 1, GFC.y, GFC.w - GF.value - 1, GFC.h, GFC.level);
                    break;
                case FADER_CV_BAR:
                    grid_fill_area(GFC.x, GFC.y, GFC.w, GFC.h - GF.value - 1, GFC.level);
                    grid_fill_area(GFC.x, GFC.y + GFC.h - GF.value - 1, GFC.w, GF.value + 1, 15);
                    break;
                case FADER_CH_DOT:
                    grid_fill_area(GFC.x + GF.value, GFC.y, 1, GFC.h, 15);
                    break;
                case FADER_CV_DOT:
                    grid_fill_area(GFC.x, GFC.y + GFC.h - GF.value - 1, GFC.w, 1, 15);
                    break;
                case FADER_FH_BAR:
                    fv = (((GFC.w - 2) << 4) * GF.value) / GFC.level;
                    ff = fv >> 4;
                    fp = fv & 15;
                    grid_fill_area(GFC.x, GFC.y, ff + 1, GFC.h, 15);
                    if (fp) grid_fill_area(GFC.x + ff + 1, GFC.y, 1, GFC.h, fp);
                    grid_fill_area(GFC.x + GFC.w - 1, GFC.y, 1, GFC.h, 15);
                    break;
                case FADER_FV_BAR:
                    fv = (((GFC.h - 2) << 4) * GF.value) / GFC.level;
                    ff = fv >> 4;
                    fp = fv & 15;
                    grid_fill_area(GFC.x, GFC.y + GFC.h - ff - 1, GFC.w, ff + 1, 15);
                    if (fp) grid_fill_area(GFC.x, GFC.y + GFC.h - ff - 2, GFC.w, 1, fp);
                    grid_fill_area(GFC.x, GFC.y, GFC.w, 1, 15);
                    break;
                case FADER_FH_DOT:
                    grid_fill_area(GFC.x, GFC.y, 1, GFC.h, 15);
                    grid_fill_area(GFC.x + GFC.w - 1, GFC.y, 1, GFC.h, 15);
                    fv = (((GFC.w - 2) << 4) * GF.value) / GFC.level;
                    ff = fv >> 4;
                    fp = fv & 15;
                    if (fp)
                        grid_fill_area(GFC.x + ff + 1, GFC.y, 1, GFC.h, fp);
                    else if (ff)
                        grid_fill_area(GFC.x + ff, GFC.y, 1, GFC.h, 15);
                    break;
                case FADER_FV_DOT:
                    grid_fill_area(GFC.x, GFC.y + GFC.h - 1, GFC.w, 1, 15);
                    grid_fill_area(GFC.x, GFC.y, GFC.w, 1, 15);
                    fv = (((GFC.h - 2) << 4) * GF.value) / GFC.level;
                    ff = fv >> 4;
                    fp = fv & 15;
                    if (fp)
                        grid_fill_area(GFC.x, GFC.y + GFC.h - ff - 2, GFC.w, 1, fp);
                    else if (ff)
                        grid_fill_area(GFC.x, GFC.y + GFC.h - ff - 1, GFC.w, 1, 15);
                    break;
            }
        }
    }

    for (u16 i = 0; i < GRID_BUTTON_COUNT; i++)
        if (GBC.enabled && SG.group[GBC.group].enabled)
            grid_fill_area(GBC.x, GBC.y, GBC.w, GBC.h, GB.state ? 15 : GBC.level);
    
    u16 led;
    for (u16 i = 0; i < size_x; i++)
        for (u16 j = 0; j < size_y; j++) {
            led = j * size_x + i;
            if (led >= MONOME_MAX_LED_BYTES) continue;

            if (SG.leds[i][j] >= 0)
                monomeLedBuffer[led] = SG.leds[i][j];
            else if (SG.leds[i][j] == LED_DIM)
                monomeLedBuffer[led] >>= 1;
            else if (SG.leds[i][j] == LED_BRI) {
                monomeLedBuffer[led] <<= 1;
                if (monomeLedBuffer[led] > 15)
                    monomeLedBuffer[led] = 15;
                else if (monomeLedBuffer[led] < 1)
                    monomeLedBuffer[led] = 1;
            }

            if (monomeLedBuffer[led] < SG.dim)
                monomeLedBuffer[led] = 0;
            else
                monomeLedBuffer[led] -= SG.dim;
        }

    if (SG.rotate) {
        u16 total = size_x * size_y;
        if (total > MONOME_MAX_LED_BYTES) total = MONOME_MAX_LED_BYTES;
        u8 temp;
        for (u16 i = 0; i < (total >> 1); i++) {
            temp = monomeLedBuffer[i];
            monomeLedBuffer[i] = monomeLedBuffer[total - i - 1];
            monomeLedBuffer[total - i - 1] = temp;
        }
    }

    SG.grid_dirty = 0;
}

void grid_fill_area(u8 x, u8 y, u8 w, u8 h, u8 level) {
    if (level == LED_OFF) return;

    u16 index;
    u16 x_end = min(size_x, x + w);
    u16 y_end = min(size_y, y + h);

    if (level == LED_DIM) {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y; _y < y_end; _y++) {
                index = _x + _y * size_x;
                if (index < MONOME_MAX_LED_BYTES) monomeLedBuffer[index] >>= 1;
            }
    }
    else if (level == LED_BRI) {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y; _y < y_end; _y++) {
                index = _x + _y * size_x;
                if (index < MONOME_MAX_LED_BYTES) {
                    monomeLedBuffer[index] <<= 1;
                    if (monomeLedBuffer[index] > 15)
                        monomeLedBuffer[index] = 15;
                }
            }
    }
    else {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y; _y < y_end; _y++) {
                index = _x + _y * size_x;
                if (index < MONOME_MAX_LED_BYTES)
                    monomeLedBuffer[index] = level;
            }
    }
}

///////////////////////////////////////// screen functions

void grid_screen_refresh(scene_state_t *ss, screen_grid_mode mode, u8 page, u8 ctrl, u8 x1, u8 y1, u8 x2, u8 y2) {
    switch (mode) {
        case GRID_MODE_EDIT:
            grid_screen_refresh_led(ss, 0, page, x1, y1, x2, y2);
            if (ctrl) grid_screen_refresh_ctrl(ss, page, x1, y1, x2, y2);
            grid_screen_refresh_info(ss, page, x1, y1, x2, y2);
            break;
        case GRID_MODE_FULL:
            grid_screen_refresh_led(ss, 1, page, x1, y1, x2, y2);
            break;
        case GRID_MODE_OFF:
        case GRID_MODE_LAST: break;
    }
    SG.scr_dirty = 0;
}

void grid_screen_refresh_ctrl(scene_state_t *ss, u8 page, u8 x1, u8 y1, u8 x2,
                              u8 y2) {
    grid_fill_area_scr(0, 0, GRID_MAX_DIMENSION, GRID_MAX_DIMENSION, 0, 0);
    
    u8 last_x, last_y;
    
    for (u16 i = 0; i < GRID_BUTTON_COUNT; i++) {
        if (!SG.group[GBC.group].enabled || !GBC.enabled) continue;
        last_x = GBC.x + GBC.w - 1;
        last_y = GBC.y + GBC.h - 1;
        if (GBC.w == 1 && GBC.h == 1) {
            grid_fill_area_scr(GBC.x, GBC.y, 1, 1, 1, page);
        } else if (GBC.w == 1 && GBC.h > 1) {
            grid_fill_area_scr(GBC.x, GBC.y, 1, 1, 2, page);
            grid_fill_area_scr(GBC.x, GBC.y + 1, 1, GBC.h - 2, 3, page);
            grid_fill_area_scr(GBC.x, last_y, 1, 1, 4, page);
        } else if (GBC.w > 1 && GBC.h == 1) {
            grid_fill_area_scr(GBC.x, GBC.y, 1, 1, 5, page);
            grid_fill_area_scr(GBC.x + 1, GBC.y, GBC.w - 2, 1, 6, page);
            grid_fill_area_scr(last_x, GBC.y, 1, 1, 7, page);
        } else {
            grid_fill_area_scr(GBC.x, GBC.y, 1, 1, 8, page);
            grid_fill_area_scr(GBC.x + 1, GBC.y, GBC.w - 2, 1, 9, page);
            grid_fill_area_scr(last_x, GBC.y, 1, 1, 10, page);
            grid_fill_area_scr(GBC.x, GBC.y + 1, 1, GBC.h - 2, 11, page);
            grid_fill_area_scr(last_x, GBC.y + 1, 1, GBC.h - 2, 12, page);
            grid_fill_area_scr(GBC.x, last_y, 1, 1, 13, page);
            grid_fill_area_scr(GBC.x + 1, last_y, GBC.w - 2, 1, 14, page);
            grid_fill_area_scr(last_x, last_y, 1, 1, 15, page);
        }
    }

    for (u16 i = 0; i < GRID_FADER_COUNT; i++) {
        if (!SG.group[GFC.group].enabled || !GFC.enabled) continue;
        last_x = GFC.x + GFC.w - 1;
        last_y = GFC.y + GFC.h - 1;
        if (GFC.w == 1 && GFC.h == 1) {
            grid_fill_area_scr(GFC.x, GFC.y, 1, 1, 1, page);
        } else if (GFC.w == 1 && GFC.h > 1) {
            grid_fill_area_scr(GFC.x, GFC.y, 1, 1, 2, page);
            grid_fill_area_scr(GFC.x, GFC.y + 1, 1, GFC.h - 2, 3, page);
            grid_fill_area_scr(GFC.x, last_y, 1, 1, 4, page);
        } else if (GFC.w > 1 && GFC.h == 1) {
            grid_fill_area_scr(GFC.x, GFC.y, 1, 1, 5, page);
            grid_fill_area_scr(GFC.x + 1, GFC.y, GFC.w - 2, 1, 6, page);
            grid_fill_area_scr(last_x, GFC.y, 1, 1, 7, page);
        } else {
            grid_fill_area_scr(GFC.x, GFC.y, 1, 1, 8, page);
            grid_fill_area_scr(GFC.x + 1, GFC.y, GFC.w - 2, 1, 9, page);
            grid_fill_area_scr(last_x, GFC.y, 1, 1, 10, page);
            grid_fill_area_scr(GFC.x, GFC.y + 1, 1, GFC.h - 2, 11, page);
            grid_fill_area_scr(last_x, GFC.y + 1, 1, GFC.h - 2, 12, page);
            grid_fill_area_scr(GFC.x, last_y, 1, 1, 13, page);
            grid_fill_area_scr(GFC.x + 1, last_y, GFC.w - 2, 1, 14, page);
            grid_fill_area_scr(last_x, last_y, 1, 1, 15, page);
        }
    }

    u8 l, _y, __y;
    u16 d;
    for (u16 y = 0; y < 48; y++) {
        l = y >> 3;
        d = 10 + ((y & 7) << 7);
        _y = y / 6;
        __y = y % 6;
        for (u16 x = 0; x < 96; x++)
            if ((1 << (5 - (x%6))) & glyph[screen[x/6][_y]][__y]) line[l].data[x + d] = 10;
    }

    return;
}

void grid_screen_refresh_led(scene_state_t *ss, u8 full_grid, u8 page, u8 x1,
                             u8 y1, u8 x2, u8 y2) {
    grid_fill_area_scr(0, 0, GRID_MAX_DIMENSION, GRID_MAX_DIMENSION, 0, 0);

    u16 x, y;
    for (u8 i = 0; i < GRID_XYPAD_COUNT; i++) {
        if (GXYC.enabled && SG.group[GXYC.group].enabled) {
            if (GXY.value_x || GXY.value_y) {
                x = GXYC.x + GXY.value_x;
                y = GXYC.y + GXY.value_y;
                grid_fill_area_scr(GXYC.x, y, GXYC.w, 1, GXYC.level, page);
                grid_fill_area_scr(x, GXYC.y, 1, GXYC.h, GXYC.level, page);
                grid_fill_area_scr(x, y, 1, 1, 15, page);
            }
        }
    }

    u16 fv, ff, fp;
    for (u8 i = 0; i < GRID_FADER_COUNT; i++) {
        if (GFC.enabled && SG.group[GFC.group].enabled) {
            switch (GF.type) {
                case FADER_CH_BAR:
                    grid_fill_area_scr(GFC.x, GFC.y, GF.value + 1, GFC.h, 15, page);
                    grid_fill_area_scr(GFC.x + GF.value + 1, GFC.y, GFC.w - GF.value - 1, GFC.h, GFC.level, page);
                    break;
                case FADER_CV_BAR:
                    grid_fill_area_scr(GFC.x, GFC.y, GFC.w, GFC.h - GF.value - 1, GFC.level, page);
                    grid_fill_area_scr(GFC.x, GFC.y + GFC.h - GF.value - 1, GFC.w, GF.value + 1, 15, page);
                    break;
                case FADER_CH_DOT:
                    grid_fill_area_scr(GFC.x + GF.value, GFC.y, 1, GFC.h, 15, page);
                    break;
                case FADER_CV_DOT:
                    grid_fill_area_scr(GFC.x, GFC.y + GFC.h - GF.value - 1, GFC.w, 1, 15, page);
                    break;
                case FADER_FH_BAR:
                    fv = (((GFC.w - 2) << 4) * GF.value) / GFC.level;
                    ff = fv >> 4;
                    fp = fv & 15;
                    grid_fill_area_scr(GFC.x, GFC.y, ff + 1, GFC.h, 15, page);
                    if (fp) grid_fill_area_scr(GFC.x + ff + 1, GFC.y, 1, GFC.h, fp, page);
                    grid_fill_area_scr(GFC.x + GFC.w - 1, GFC.y, 1, GFC.h, 15, page);
                    break;
                case FADER_FV_BAR:
                    fv = (((GFC.h - 2) << 4) * GF.value) / GFC.level;
                    ff = fv >> 4;
                    fp = fv & 15;
                    grid_fill_area_scr(GFC.x, GFC.y + GFC.h - ff - 1, GFC.w, ff + 1, 15, page);
                    if (fp) grid_fill_area_scr(GFC.x, GFC.y + GFC.h - ff - 2, GFC.w, 1, fp, page);
                    grid_fill_area_scr(GFC.x, GFC.y, GFC.w, 1, 15, page);
                    break;
                case FADER_FH_DOT:
                    grid_fill_area_scr(GFC.x, GFC.y, 1, GFC.h, 15, page);
                    grid_fill_area_scr(GFC.x + GFC.w - 1, GFC.y, 1, GFC.h, 15, page);
                    fv = (((GFC.w - 2) << 4) * GF.value) / GFC.level;
                    ff = fv >> 4;
                    fp = fv & 15;
                    if (fp)
                        grid_fill_area_scr(GFC.x + ff + 1, GFC.y, 1, GFC.h, fp, page);
                    else if (ff)
                        grid_fill_area_scr(GFC.x + ff, GFC.y, 1, GFC.h, 15, page);
                    break;
                case FADER_FV_DOT:
                    grid_fill_area_scr(GFC.x, GFC.y + GFC.h - 1, GFC.w, 1, 15, page);
                    grid_fill_area_scr(GFC.x, GFC.y, GFC.w, 1, 15, page);
                    fv = (((GFC.h - 2) << 4) * GF.value) / GFC.level;
                    ff = fv >> 4;
                    fp = fv & 15;
                    if (fp)
                        grid_fill_area_scr(GFC.x, GFC.y + GFC.h - ff - 2, GFC.w, 1, fp, page);
                    else if (ff)
                        grid_fill_area_scr(GFC.x, GFC.y + GFC.h - ff - 1, GFC.w, 1, 15, page);
                    break;
            }
        }
    }

    for (u16 i = 0; i < GRID_BUTTON_COUNT; i++)
        if (GBC.enabled && SG.group[GBC.group].enabled)
            grid_fill_area_scr(GBC.x, GBC.y, GBC.w, GBC.h, GB.state ? 15 : GBC.level, page);
    
    u16 pd = page ? 8 : 0;
    s8 l;
    for (u16 i = 0; i < GRID_MAX_DIMENSION; i++)
        for (u16 j = 0; j < GRID_MAX_DIMENSION/2; j++) {
            l = SG.leds[i][j + pd];
            if (l >= 0)
                screen[i][j] = l;
            else if (l == LED_DIM)
                screen[i][j] >>= 1;
            else if (l == LED_BRI) {
                screen[i][j] <<= 1;
                if (screen[i][j] > 15)
                    screen[i][j] = 15;
                else if (screen[i][j] < 1)
                    screen[i][j] = 1;
            }
        }

    u16 _y, cell, size, left;
    if (full_grid) {
        cell = 8;
        size = 5;
        left = 0;
        for (int i = 0; i < 8; i++) region_fill(&line[i], 0);
    }
    else {
        cell = 6;
        size = 4;
        left = 10;
        for (int i = 0; i < 6; i++) region_fill(&line[i], 0);
    }

    u8 _line;
    u16 _data;
    for (u16 x = 0; x < GRID_MAX_DIMENSION; x++)
        for (u16 y = 0; y < GRID_MAX_DIMENSION/2; y++)
            for (u16 j = 0; j < size; j++) {
                _y = y * cell + j + 1;
                _line = _y >> 3;
                for (u16 i = 0; i < size; i++) {
                    _data = left + x * cell + i + ((_y & 7) << 7) + 1;
                    if (_line > 7 || _data > 1023) continue;
                    if (screen[x][y] == 0) {
                        if (i == 0 || i == size - 1 || j == 0 || j == size - 1)
                            line[_line].data[_data] = 1;
                    }
                    else
                        line[_line].data[_data] = screen[x][y];
                }
            }

    // draw selected area

    u8 _x1, _x2;
    u16 _y1, _y2;
    _x1 = min(x1, x2) * cell;
    _y1 = min(y1, y2) * cell;
    if (full_grid) {
        _x2 = (max(x1, x2) + 1) * cell - 2;
        _y2 = (max(y1, y2) + 1) * cell - 2;
    } else {
        _x2 = (max(x1, x2) + 1) * cell - 1;
        _y2 = (max(y1, y2) + 1) * cell - 1;
    }
    
    u8 show_y1, show_y2;
    show_y1 = show_y2 = true;
    
    u16 p = cell << 3;
    if (page) {
        if (_y2 < p) return;
        if (_y1 < p) {
            show_y1 = false;
            _y1 = p;
        };
        _y1 -= p;
        _y2 -= p;
    } else {
        if (_y1 >= p) return;
        if (_y2 >= p) {
            show_y2 = false;
            _y2 = p - 1;
        }
    }
    
    if (show_y1) {
        _line = _y1 >> 3;
        _data = left + ((_y1 & 7) << 7);
        for (u16 x = _x1; x <= _x2; x++)
            line[_line].data[_data + x] = x & 1 ? 4 : 15;
    }
    if (show_y2) {
        _line = _y2 >> 3;
        _data = left + ((_y2 & 7) << 7);
        if (full_grid)
            for (u16 x = _x1 + 1; x <= _x2; x++)
                line[_line].data[_data + x] = x & 1 ? 4 : 15;
        else
            for (u16 x = _x1 + 1; x <= _x2; x++)
                line[_line].data[_data + x] = x & 1 ? 15 : 4;
    }
    
    if (full_grid)
        for (u16 y = _y1; y <= _y2; y++) {
            line[y >> 3].data[left + ((y & 7) << 7) + _x1] =
                line[y >> 3].data[left + ((y & 7) << 7) + _x2] = y & 1 ? 4 : 15;
        }
    else
        for (u16 y = _y1; y <= _y2; y++) {
            line[y >> 3].data[left + ((y & 7) << 7) + _x1] = y & 1 ? 4 : 15;
            line[y >> 3].data[left + ((y & 7) << 7) + _x2] = y & 1 ? 15 : 4;
        }
}                

static void grid_screen_refresh_info(scene_state_t *ss, u8 page, u8 x1, u8 y1,
                                     u8 x2, u8 y2) {
    char s[32];
    u8 area_x, area_y, area_w, area_h;

    if (x1 < x2) {
        area_x = x1;
        area_w = x2 + 1 - x1;
    }
    else {
        area_x = x2;
        area_w = x1 + 1 - x2;
    }
    if (y1 < y2) {
        area_y = y1;
        area_h = y2 + 1 - y1;
    }
    else {
        area_y = y2;
        area_h = y1 + 1 - y2;
    }

    s[1] = 0;
    s[0] = 'G';
    font_string_region_clip_right(&line[0], s, 127, 0, 1, 0);
    s[0] = 'X';
    font_string_region_clip_right(&line[2], s, 127, 0, 1, 0);
    s[0] = 'Y';
    font_string_region_clip_right(&line[3], s, 127, 0, 1, 0);
    s[0] = 'W';
    font_string_region_clip_right(&line[4], s, 128, 0, 1, 0);
    s[0] = 'H';
    font_string_region_clip_right(&line[5], s, 127, 0, 1, 0);

    itoa(ss->grid.current_group, s, 10);
    font_string_region_clip_right(&line[0], s, 117, 0, 8, 0);
    itoa(area_x, s, 10);
    font_string_region_clip_right(&line[2], s, 117, 0, 8, 0);
    itoa(area_y, s, 10);
    font_string_region_clip_right(&line[3], s, 117, 0, 8, 0);
    itoa(area_w, s, 10);
    font_string_region_clip_right(&line[4], s, 117, 0, 8, 0);
    itoa(area_h, s, 10);
    font_string_region_clip_right(&line[5], s, 117, 0, 8, 0);

    for (u16 j = 0; j < 9; j += 2) line[j >> 3].data[119 + ((j & 7) << 7)] = 1;
    for (u16 j = 17; j < 48; j += 2)
        line[j >> 3].data[119 + ((j & 7) << 7)] = 1;

    // icons
    
    if (page == 0) {
        for (u16 i = 0; i < 5; i++) line[0].data[i] = line[0].data[i + 128] = 10;
        line[0].data[0 + 256] = 1;
        line[0].data[4 + 256] = 1;
        line[0].data[0 + 384] = 1;
        line[0].data[4 + 384] = 1;
        for (u16 i = 512; i < 517; i++) line[0].data[i] = 1;
    } else {
        for (u16 i = 0; i < 5; i++) line[0].data[i] = 1;
        line[0].data[0 + 128] = 1;
        line[0].data[4 + 128] = 1;
        line[0].data[0 + 256] = 1;
        line[0].data[4 + 256] = 1;
        for (u16 i = 384; i < 389; i++) line[0].data[i] = line[0].data[i + 128] = 10;
    }

    u8 l = ss->grid.rotate ? 10 : 1;
    line[1].data[0 + 0] = l;
    line[1].data[1 + 0] = l;
    line[1].data[2 + 0] = l;
    line[1].data[2 + 128] = l;
    line[1].data[0 + 256] = l;
    line[1].data[2 + 256] = l;
    line[1].data[4 + 256] = l;
    line[1].data[1 + 384] = l;
    line[1].data[2 + 384] = l;
    line[1].data[3 + 384] = l;
    line[1].data[2 + 512] = l;
}

void grid_fill_area_scr(u8 x, u8 y, u8 w, u8 h, u8 level, u8 page) {
    if (level == LED_OFF) return;

    u16 x_end = min(GRID_MAX_DIMENSION, x + w);
    u16 y1, y2;
    y1 = y;
    y2 = min(GRID_MAX_DIMENSION, y + h) - 1;
    
    if (page) {
        if (y2 < 8) return;
        if (y1 < 8) y1 = 8;
        y1 -= 8;
        y2 -= 8;
    } else {
        if (y1 >= 8) return;
        if (y2 >= 8) y2 = 7;
    }
    
    if (level == LED_DIM) {
        for (u16 _x = x; _x < x_end; _x++) {
            for (u16 _y = y1; _y <= y2; _y++)
                screen[_x][_y] >>= 1;
        }
    }
    else if (level == LED_BRI) {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y1; _y <= y2; _y++) {
                screen[_x][_y] <<= 1; 
                if (screen[_x][_y] > 15) screen[_x][_y] = 15;
            }
    }
    else {
        for (u16 _x = x; _x < x_end; _x++)
            for (u16 _y = y1; _y <= y2; _y++)
                screen[_x][_y] = level;
    }
}

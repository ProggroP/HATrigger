// ============================================================
//  HA Trigger - Home Assistant Zwei-Tasten-Schaltapp
//  Zielplattformen: Emery (200x228, eckig) und Gabbro (260x260, rund)
//
//  Bis zu SCREEN_COUNT Bildschirme mit je zwei Aktionen und eigener
//  Status-Entity. Angezeigt werden nur Bildschirme, auf denen
//  mindestens eine Aktion belegt ist.
//
//  Oben / Unten = Aktion, optional mit Sicherheitsabfrage.
//  Mitte = zum naechsten Bildschirm blaettern.
//  Der Status wird alle POLL_INTERVAL_MS selbst geholt.
//  Bestaetigung: Mitte = ausfuehren, jede andere Taste = Abbruch,
//  ohne Tastendruck Abbruch nach CONFIRM_TIMEOUT_MS.
//
//  Die Uhr haelt die vollstaendige Konfiguration persistent, inklusive
//  URL und Token, und liefert sie der JS-Seite auf Anfrage zurueck.
//  Gezeichnet wird ausschliesslich in canvas_update() bzw.
//  confirm_update() - keine TextLayer.
// ============================================================

#include <pebble.h>

// ------------------------------------------------------------
//  TIMING
// ------------------------------------------------------------
#define CONFIRM_TIMEOUT_MS   6000
#define CONFIRM_TICK_MS      80
#define BUSY_RESET_MS        9000
#define BUSY_ANIM_MS         220
#define INITIAL_SYNC_MS      300
#define POLL_INTERVAL_MS     15000

// ------------------------------------------------------------
//  BILDSCHIRME
// ------------------------------------------------------------
#define SCREEN_COUNT         5
#define SLOT_TOP             0
#define SLOT_BOT             1
#define SLOT_COUNT           2

// ------------------------------------------------------------
//  PUFFERGROESSEN
//  persist_write_string speichert maximal PERSIST_DATA_MAX_LENGTH
//  (256) Bytes je Eintrag, Nullterminator eingerechnet. Ein HA
//  Long-Lived Token liegt bei rund 190 Zeichen und passt damit.
// ------------------------------------------------------------
#define URL_LEN              96
#define TOKEN_LEN            256
#define ENTITY_LEN           48
#define SERVICE_LEN          32
#define LABEL_LEN            24
#define STATUS_LEN           48
#define CONFIRM_LINE_LEN     40
#define THEME_LEN            8
#define CLOCK_LEN            12

// AppMessage: so viel wie die Firmware hergibt, aber nicht mehr als
// noetig. Clay schickt die gesamte Konfiguration in einer Nachricht:
// URL und Token rund 360 Bytes, jeder Bildschirm bis zu rund 330 Bytes
// samt Tupel-Koepfen, zusammen gut 2 KB. Die Rueckgabe an das Telefon
// geht bildschirmweise raus und kommt mit 1 KB Outbox aus.
#define INBOX_WANTED         4096
#define OUTBOX_WANTED        1024

// ------------------------------------------------------------
//  PERSIST KEYS
//  Bildschirm 1 behaelt die Schluessel der Zwei-Aktionen-Fassung
//  (3..11), damit eine bestehende Konfiguration nach dem Update
//  unveraendert dort landet. Die Bildschirme 2..5 liegen ab
//  PK_SCREEN_BASE in Zehnerbloecken. Feldreihenfolge siehe ScreenField.
// ------------------------------------------------------------
#define PK_HA_URL             1
#define PK_HA_TOKEN           2
#define PK_LEGACY_BASE        3
#define PK_THEME_DARK        12
#define PK_CURRENT_SCREEN    13
#define PK_SCREEN_BASE      100
#define PK_SCREEN_STRIDE     10

typedef enum {
  F_STATUS  = 0,
  F_LABEL   = 1,   // + 4 * Slot
  F_SERVICE = 2,
  F_ENTITY  = 3,
  F_CONFIRM = 4
} ScreenField;

#define FIELD(f, slot)       ((f) == F_STATUS ? F_STATUS : (f) + 4 * (slot))

// ------------------------------------------------------------
//  LAYOUT (plattformspezifisch)
//  Die Action-Rail sitzt rechts vor den Knoepfen. Auf Gabbro liegt
//  sie als abgerundeter Balken innerhalb des Kreises, auf Emery als
//  klassische Leiste ueber die volle Hoehe.
// ------------------------------------------------------------
#if defined(PBL_PLATFORM_GABBRO)
  #define RAIL_RECT        GRect(198, 62, 34, 136)
  #define RAIL_RADIUS      16
  #define RAIL_CORNERS     GCornersAll

  #define HEADER_RECT      GRect(43, 14, 120, 26)
  #define CARD_TOP_RECT    GRect(32, 48, 158, 44)
  #define STATUS_RECT      GRect(16, 100, 176, 58)
  #define CARD_BOT_RECT    GRect(32, 166, 158, 44)

  #define RING_RECT        GRect(8, 8, 244, 244)
  #define RING_THICKNESS   14
  #define CFM_TITLE_RECT   GRect(50, 62, 160, 26)
  #define CFM_NAME_RECT    GRect(38, 92, 184, 62)
  #define CFM_HINT_RECT    GRect(44, 158, 172, 44)

  #define FONT_HEADER      FONT_KEY_GOTHIC_18
  #define FONT_CARD        FONT_KEY_GOTHIC_24_BOLD
  #define FONT_STATUS_BIG  FONT_KEY_BITHAM_30_BLACK
  #define FONT_STATUS_SML  FONT_KEY_GOTHIC_24_BOLD
  #define FONT_CFM_NAME    FONT_KEY_GOTHIC_28_BOLD
  #define FONT_CFM_SML     FONT_KEY_GOTHIC_18
#else
  // Emery, und als Fallback jede andere eckige Plattform
  #define RAIL_RECT        GRect(166, 0, 34, 228)
  #define RAIL_RADIUS      14
  #define RAIL_CORNERS     GCornersLeft

  #define HEADER_RECT      GRect(16, 6, 134, 24)
  #define CARD_TOP_RECT    GRect(8, 32, 150, 44)
  #define STATUS_RECT      GRect(6, 84, 154, 68)
  #define CARD_BOT_RECT    GRect(8, 160, 150, 44)

  #define RING_RECT        GRect(0, 14, 200, 200)
  #define RING_THICKNESS   12
  #define CFM_TITLE_RECT   GRect(20, 46, 160, 24)
  #define CFM_NAME_RECT    GRect(14, 74, 172, 62)
  #define CFM_HINT_RECT    GRect(18, 140, 164, 44)

  #define FONT_HEADER      FONT_KEY_GOTHIC_18
  #define FONT_CARD        FONT_KEY_GOTHIC_24_BOLD
  #define FONT_STATUS_BIG  FONT_KEY_GOTHIC_28_BOLD
  #define FONT_STATUS_SML  FONT_KEY_GOTHIC_24_BOLD
  #define FONT_CFM_NAME    FONT_KEY_GOTHIC_28_BOLD
  #define FONT_CFM_SML     FONT_KEY_GOTHIC_18
#endif

#define CARD_RADIUS        8
#define CARD_STRIPE_W      5
#define CARD_EDGE_W        1
#define CARD_TEXT_INSET    14
#define CARD_DOT_R         4
#define STATUS_RULE_H      4
// Seitenpunkte in der Rail vor der mittleren Taste: senkrecht, einer je
// sichtbarem Bildschirm, der aktuelle gefuellt. Sie zeigen zugleich,
// dass die mittlere Taste blaettert. Fuenf Punkte passen auf Gabbro
// knapp zwischen die beiden Dreiecke.
#define PAGE_DOT_R         3
#define PAGE_DOT_GAP       11
#define BUSY_DOT_R         3
#define BUSY_DOT_GAP       12
// Ab dieser Laenge weicht der Statustext auf die kleinere Schrift aus
#define STATUS_BIG_MAX_LEN 7
// Systemschriften sitzen optisch tief; das gleicht die Zentrierung aus
#define TEXT_BASELINE_FIX  3

// ------------------------------------------------------------
//  FARBEN
//  Gabbro stellt die Palette entsaettigt dar, deshalb durchweg
//  kraeftige Grundfarben statt zarter Zwischentoene.
// ------------------------------------------------------------
//  Die Karten sind in beiden Themes dasselbe Dunkelblau mit weissem
//  Text. Nur Hintergrund und Rail wechseln. Eine helle Karte mit weisser
//  Schrift kam im Light Theme auf 2,6:1 und war kaum lesbar, dunkel auf
//  hell gestellt sind es 13:1.
//
//  Auf schwarzem Grund traegt die Flaeche den Kontrast nicht mit: gegen
//  Schwarz steht dieses Blau bei 1,6:1, ein noch dunkleres bei 1,1:1.
//  Die Kartenform kommt deshalb vom hellen Rahmen, nicht von der
//  Flaechenhelligkeit -- so bleibt der Text bei 13:1 und die Karte
//  trotzdem sichtbar.
//
//  Die Signalfarben gibt es je Theme einmal: Gruen und Gelb stehen auf
//  Weiss bei 1,4:1 bzw. 1,9:1, auf Schwarz dagegen bei 15:1 bzw. 20:1.
//  Auf hellem Grund braucht es deshalb die dunklen Varianten. Kein Paar
//  liegt jetzt unter 5:1.
#define COL_DARK_BG          GColorBlack
#define COL_DARK_CARD        GColorDukeBlue
#define COL_DARK_STRIPE      GColorElectricBlue
#define COL_DARK_TEXT        GColorWhite
#define COL_DARK_DIM         GColorLightGray
#define COL_DARK_RAIL        GColorWhite
#define COL_DARK_GLYPH       GColorBlack
#define COL_DARK_OK          GColorGreen
#define COL_DARK_ERR         GColorSunsetOrange
#define COL_DARK_BUSY        GColorYellow

#define COL_LIGHT_BG         GColorWhite
#define COL_LIGHT_CARD       GColorDukeBlue
#define COL_LIGHT_STRIPE     GColorVividCerulean
#define COL_LIGHT_TEXT       GColorBlack
#define COL_LIGHT_DIM        GColorDarkGray
#define COL_LIGHT_RAIL       GColorBlack
#define COL_LIGHT_GLYPH      GColorWhite
#define COL_LIGHT_OK         GColorDarkGreen
#define COL_LIGHT_ERR        GColorDarkCandyAppleRed
#define COL_LIGHT_BUSY       GColorWindsorTan

// Kartentext steht immer auf dunklem, gefuelltem Grund
#define COL_CARD_TEXT        GColorWhite
// Der Punkt der Sicherheitsabfrage sitzt ebenfalls auf der Karte und
// bleibt deshalb in beiden Themes hell
#define COL_CARD_DOT         GColorChromeYellow

// ------------------------------------------------------------
//  ZUSTANDSART DES STATUSTEXTES
// ------------------------------------------------------------
typedef enum {
  STATUS_NEUTRAL = 0,
  STATUS_BUSY    = 1,
  STATUS_OK      = 2,
  STATUS_ERROR   = 3
} StatusKind;

// ------------------------------------------------------------
//  AUSGEHENDE NACHRICHTEN
//  Eine kleine Warteschlange, damit sich Aktion, Statusabfrage und
//  Konfigurations-Rueckgabe nicht gegenseitig mit APP_MSG_BUSY
//  abschiessen. Die Konfiguration geht in Teilen raus: erst URL,
//  Token und Theme, dann ein Teil je Bildschirm.
// ------------------------------------------------------------
typedef enum {
  OUT_NONE = 0,
  OUT_REQUEST_UPDATE,
  OUT_ACTION,
  OUT_CONFIG_A,
  OUT_CONFIG_SCREEN
} OutKind;

// Konfiguration (Teil A + ein Teil je Bildschirm) plus Luft fuer
// Aktion und Statusabfrage
#define OUT_QUEUE_LEN (SCREEN_COUNT + 4)

typedef struct {
  OutKind kind;
  uint8_t arg;
  uint8_t screen;
} OutItem;

static OutItem s_out_queue[OUT_QUEUE_LEN];
static uint8_t s_out_head  = 0;
static uint8_t s_out_count = 0;
static bool    s_out_busy  = false;

// ------------------------------------------------------------
//  KONFIGURATION (persistent)
// ------------------------------------------------------------
typedef struct {
  char label[LABEL_LEN];
  char service[SERVICE_LEN];
  char entity[ENTITY_LEN];
  bool confirm;
} Action;

typedef struct {
  char   status_entity[ENTITY_LEN];
  Action act[SLOT_COUNT];
} Screen;

// Message Keys je Bildschirm. MESSAGE_KEY_* sind Variablen, keine
// Konstanten, die Tabelle wird deshalb in init_keys() gefuellt.
typedef struct {
  uint32_t status;
  uint32_t label[SLOT_COUNT];
  uint32_t service[SLOT_COUNT];
  uint32_t entity[SLOT_COUNT];
  uint32_t confirm[SLOT_COUNT];
} ScreenKeys;

static char   s_ha_url[URL_LEN]     = "";
static char   s_ha_token[TOKEN_LEN] = "";
static Screen s_screens[SCREEN_COUNT];
static bool   s_dark                = true;
static uint8_t s_screen             = 0;   // aktuell gezeigter Bildschirm

static ScreenKeys s_keys[SCREEN_COUNT];

// ------------------------------------------------------------
//  LAUFZEIT
// ------------------------------------------------------------
static Window *s_main_window;
static Layer  *s_canvas_layer;

static Window *s_confirm_window;
static Layer  *s_confirm_layer;

static AppTimer *s_confirm_timer = NULL;
static AppTimer *s_busy_timer    = NULL;
static AppTimer *s_anim_timer    = NULL;
static AppTimer *s_poll_timer    = NULL;

static char s_status[STATUS_LEN]         = "Connecting...";
static char s_cfm_name[CONFIRM_LINE_LEN] = "";
static char s_clock[CLOCK_LEN]           = "";

static StatusKind s_status_kind  = STATUS_BUSY;
static uint8_t    s_pending      = 0;   // 1 = oben, 2 = unten
static int32_t    s_cfm_left_ms  = CONFIRM_TIMEOUT_MS;
static uint8_t    s_anim_phase   = 0;

static GPath *s_tri_up     = NULL;
static GPath *s_tri_down   = NULL;

static const GPathInfo TRI_UP_INFO = {
  .num_points = 3,
  .points = (GPoint[]) { {0, -8}, {9, 6}, {-9, 6} }
};

static const GPathInfo TRI_DOWN_INFO = {
  .num_points = 3,
  .points = (GPoint[]) { {0, 8}, {-9, -6}, {9, -6} }
};

// ------------------------------------------------------------
//  FARBWAHL
// ------------------------------------------------------------
static GColor color_bg(void)     { return s_dark ? COL_DARK_BG     : COL_LIGHT_BG;     }
static GColor color_card(void)   { return s_dark ? COL_DARK_CARD   : COL_LIGHT_CARD;   }
static GColor color_stripe(void) { return s_dark ? COL_DARK_STRIPE : COL_LIGHT_STRIPE; }
static GColor color_text(void)   { return s_dark ? COL_DARK_TEXT   : COL_LIGHT_TEXT;   }
static GColor color_dim(void)    { return s_dark ? COL_DARK_DIM    : COL_LIGHT_DIM;    }
static GColor color_rail(void)   { return s_dark ? COL_DARK_RAIL   : COL_LIGHT_RAIL;   }
static GColor color_glyph(void)  { return s_dark ? COL_DARK_GLYPH  : COL_LIGHT_GLYPH;  }

static GColor color_busy(void)   { return s_dark ? COL_DARK_BUSY   : COL_LIGHT_BUSY;   }

static GColor color_state(void) {
  switch (s_status_kind) {
    case STATUS_OK:    return s_dark ? COL_DARK_OK  : COL_LIGHT_OK;
    case STATUS_ERROR: return s_dark ? COL_DARK_ERR : COL_LIGHT_ERR;
    case STATUS_BUSY:  return color_busy();
    default:           return color_text();
  }
}

// ------------------------------------------------------------
//  NEUZEICHNEN
// ------------------------------------------------------------
static void mark_dirty(void) {
  if (s_canvas_layer)  layer_mark_dirty(s_canvas_layer);
  if (s_confirm_layer) layer_mark_dirty(s_confirm_layer);
}

// ------------------------------------------------------------
//  TEXTHILFEN
// ------------------------------------------------------------
static void set_status_kind(const char *txt, StatusKind kind) {
  strncpy(s_status, txt, STATUS_LEN - 1);
  s_status[STATUS_LEN - 1] = '\0';
  s_status_kind = kind;
  mark_dirty();
}

static bool config_complete(void) {
  return (s_ha_url[0] != '\0') && (s_ha_token[0] != '\0');
}

// ------------------------------------------------------------
//  BILDSCHIRM-HILFEN
// ------------------------------------------------------------
// Belegt ist eine Aktion, sobald Service oder Entity gesetzt ist -
// ein Script ohne Argumente hat nur einen Service, ein Script-Entity
// allein reicht der JS-Seite ebenfalls. Ein Label allein zaehlt nicht.
static bool action_set(const Action *a) {
  return (a->service[0] != '\0') || (a->entity[0] != '\0');
}

static bool screen_visible(uint8_t i) {
  return action_set(&s_screens[i].act[SLOT_TOP]) ||
         action_set(&s_screens[i].act[SLOT_BOT]);
}

static uint8_t visible_count(void) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) if (screen_visible(i)) n++;
  return n;
}

// Liegt der aktuelle Bildschirm nach einer Konfigurationsaenderung
// brach, auf den ersten belegten ausweichen. Ist gar keiner belegt,
// bleibt es bei Bildschirm 1.
static void fix_current_screen(void) {
  if (s_screen < SCREEN_COUNT && screen_visible(s_screen)) return;
  s_screen = 0;
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    if (screen_visible(i)) { s_screen = i; return; }
  }
}

// Anzeigename einer Aktion: Label, sonst der Objektteil der Entity mit
// Leerzeichen statt Unterstrichen (light.living_room -> living room),
// sonst der Service. Der Puffer wird bei jedem Aufruf ueberschrieben,
// das Ergebnis also sofort zeichnen oder kopieren.
static const char *action_name(const Action *a) {
  static char s_name[ENTITY_LEN];

  if (a->label[0]) return a->label;
  if (a->entity[0]) {
    const char *dot = strchr(a->entity, '.');
    const char *src = (dot && dot[1]) ? dot + 1 : a->entity;
    size_t i = 0;
    for (; src[i] && i < ENTITY_LEN - 1; i++) s_name[i] = (src[i] == '_') ? ' ' : src[i];
    s_name[i] = '\0';
    return s_name;
  }
  if (a->service[0]) return a->service;
  return "Action";
}

static void update_clock(void) {
  clock_copy_time_string(s_clock, CLOCK_LEN);
}

// ------------------------------------------------------------
//  ZEICHENHILFEN
// ------------------------------------------------------------
// Text vertikal in der Box zentrieren; Pebble legt Systemschriften
// mit etwas Luft nach oben an, TEXT_BASELINE_FIX gleicht das aus.
static void draw_text_boxed(GContext *ctx, const char *txt, GFont font,
                            GRect box, GTextAlignment align,
                            GTextOverflowMode overflow, GColor color) {
  if (!txt || !txt[0]) return;
  graphics_context_set_text_color(ctx, color);
  GSize sz = graphics_text_layout_get_content_size(txt, font, box, overflow, align);
  int dy = (box.size.h - sz.h) / 2 - TEXT_BASELINE_FIX;
  if (dy < 0) dy = 0;
  GRect r = GRect(box.origin.x, box.origin.y + dy,
                  box.size.w, box.size.h - dy);
  graphics_draw_text(ctx, txt, font, r, overflow, align, NULL);
}

// Eine Aktionskarte: gefuelltes abgerundetes Rechteck, farbiger
// Streifen links, Label linksbuendig, Punkt rechts wenn die Aktion
// eine Sicherheitsabfrage verlangt.
static void draw_card(GContext *ctx, GRect rect, const char *label, bool confirm) {
  graphics_context_set_fill_color(ctx, color_card());
  graphics_fill_rect(ctx, rect, CARD_RADIUS, GCornersAll);

  graphics_context_set_stroke_color(ctx, color_stripe());
  graphics_context_set_stroke_width(ctx, CARD_EDGE_W);
  graphics_draw_round_rect(ctx, rect, CARD_RADIUS);

  graphics_context_set_fill_color(ctx, color_stripe());
  graphics_fill_rect(ctx,
      GRect(rect.origin.x, rect.origin.y + CARD_RADIUS,
            CARD_STRIPE_W, rect.size.h - 2 * CARD_RADIUS),
      0, GCornerNone);

  int right_pad = confirm ? (CARD_DOT_R * 2 + 10) : CARD_TEXT_INSET;
  GRect text_rect = GRect(rect.origin.x + CARD_TEXT_INSET, rect.origin.y,
                          rect.size.w - CARD_TEXT_INSET - right_pad, rect.size.h);
  draw_text_boxed(ctx, label, fonts_get_system_font(FONT_CARD), text_rect,
                  GTextAlignmentLeft, GTextOverflowModeTrailingEllipsis,
                  COL_CARD_TEXT);

  if (confirm) {
    graphics_context_set_fill_color(ctx, COL_CARD_DOT);
    graphics_fill_circle(ctx,
        GPoint(rect.origin.x + rect.size.w - CARD_TEXT_INSET,
               rect.origin.y + rect.size.h / 2),
        CARD_DOT_R);
  }
}

// Drei Punkte unter dem Statustext, der hellste wandert
static void draw_busy_dots(GContext *ctx, int cx, int cy) {
  for (int i = 0; i < 3; i++) {
    graphics_context_set_fill_color(ctx,
        (i == s_anim_phase % 3) ? color_busy() : color_dim());
    graphics_fill_circle(ctx, GPoint(cx + (i - 1) * BUSY_DOT_GAP, cy), BUSY_DOT_R);
  }
}

static void draw_status_block(GContext *ctx, GRect rect) {
  GColor c = color_state();
  bool busy = (s_status_kind == STATUS_BUSY);

  graphics_context_set_fill_color(ctx, c);
  graphics_fill_rect(ctx, GRect(rect.origin.x, rect.origin.y,
                                rect.size.w, STATUS_RULE_H), 0, GCornerNone);

  int text_h = rect.size.h - 2 * STATUS_RULE_H - (busy ? BUSY_DOT_R * 2 + 2 : 0);
  GRect text_rect = GRect(rect.origin.x, rect.origin.y + STATUS_RULE_H,
                          rect.size.w, text_h);

  const char *key = (strlen(s_status) <= STATUS_BIG_MAX_LEN)
                    ? FONT_STATUS_BIG : FONT_STATUS_SML;
  draw_text_boxed(ctx, s_status, fonts_get_system_font(key), text_rect,
                  GTextAlignmentCenter, GTextOverflowModeWordWrap, c);

  if (busy) {
    draw_busy_dots(ctx, rect.origin.x + rect.size.w / 2,
                   rect.origin.y + rect.size.h - STATUS_RULE_H - BUSY_DOT_R - 1);
  } else {
    graphics_context_set_fill_color(ctx, c);
    graphics_fill_rect(ctx,
        GRect(rect.origin.x, rect.origin.y + rect.size.h - STATUS_RULE_H,
              rect.size.w, STATUS_RULE_H), 0, GCornerNone);
  }
}

// Seitenpunkte: einer je sichtbarem Bildschirm, senkrecht um c
// zentriert. Bei nur einem Bildschirm gibt es nichts zu blaettern und
// die Rail bleibt in der Mitte leer.
static void draw_page_dots(GContext *ctx, GPoint c, GColor col) {
  uint8_t n = visible_count();
  if (n < 2) return;

  int y = c.y - ((n - 1) * PAGE_DOT_GAP) / 2;
  graphics_context_set_fill_color(ctx, col);
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 1);

  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    if (!screen_visible(i)) continue;
    GPoint p = GPoint(c.x, y);
    if (i == s_screen) graphics_fill_circle(ctx, p, PAGE_DOT_R);
    else               graphics_draw_circle(ctx, p, PAGE_DOT_R);
    y += PAGE_DOT_GAP;
  }
}

// Die seitliche Bedienleiste vor den drei Knoepfen
static void draw_rail(GContext *ctx) {
  GRect rail = RAIL_RECT;
  graphics_context_set_fill_color(ctx, color_rail());
  graphics_fill_rect(ctx, rail, RAIL_RADIUS, RAIL_CORNERS);

  int cx     = rail.origin.x + rail.size.w / 2;
  int y_up   = rail.origin.y + rail.size.h / 5;
  int y_mid  = rail.origin.y + rail.size.h / 2;
  int y_down = rail.origin.y + (rail.size.h * 4) / 5;

  GColor g = color_glyph();

  graphics_context_set_fill_color(ctx, g);
  gpath_move_to(s_tri_up, GPoint(cx, y_up));
  gpath_draw_filled(ctx, s_tri_up);

  draw_page_dots(ctx, GPoint(cx, y_mid), g);

  graphics_context_set_fill_color(ctx, g);
  gpath_move_to(s_tri_down, GPoint(cx, y_down));
  gpath_draw_filled(ctx, s_tri_down);
}

// ------------------------------------------------------------
//  HAUPTFENSTER: einziger Zeichen-Einstiegspunkt
// ------------------------------------------------------------
static void canvas_update(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, color_bg());
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

  draw_text_boxed(ctx, s_clock, fonts_get_system_font(FONT_HEADER),
                  HEADER_RECT, GTextAlignmentCenter,
                  GTextOverflowModeTrailingEllipsis, color_dim());

  // Eine unbelegte Aktion auf einem sichtbaren Bildschirm bekommt keine
  // Karte - die Taste daneben tut dann auch nichts
  const Screen *scr = &s_screens[s_screen];
  const Action *top = &scr->act[SLOT_TOP];
  const Action *bot = &scr->act[SLOT_BOT];
  bool none = !screen_visible(s_screen);   // noch gar nichts eingerichtet

  if (none || action_set(top)) draw_card(ctx, CARD_TOP_RECT, action_name(top), top->confirm);
  draw_status_block(ctx, STATUS_RECT);
  if (none || action_set(bot)) draw_card(ctx, CARD_BOT_RECT, action_name(bot), bot->confirm);

  draw_rail(ctx);
}

// ------------------------------------------------------------
//  KOMMUNIKATION
// ------------------------------------------------------------
static void busy_reset(void *ctx) {
  s_busy_timer = NULL;
  set_status_kind("No response", STATUS_ERROR);
}

static void stop_busy_timer(void) {
  if (s_busy_timer) {
    app_timer_cancel(s_busy_timer);
    s_busy_timer = NULL;
  }
}

static void start_busy_timer(void) {
  stop_busy_timer();
  s_busy_timer = app_timer_register(BUSY_RESET_MS, busy_reset, NULL);
}

static void anim_tick(void *ctx) {
  s_anim_timer = NULL;
  if (s_status_kind != STATUS_BUSY) return;
  s_anim_phase++;
  mark_dirty();
  s_anim_timer = app_timer_register(BUSY_ANIM_MS, anim_tick, NULL);
}

static void set_busy(const char *txt) {
  set_status_kind(txt, STATUS_BUSY);
  start_busy_timer();
  if (!s_anim_timer) s_anim_timer = app_timer_register(BUSY_ANIM_MS, anim_tick, NULL);
}

static void out_pump(void);

static void out_enqueue(OutKind kind, uint8_t arg) {
  if (s_out_count >= OUT_QUEUE_LEN) return;
  uint8_t slot = (s_out_head + s_out_count) % OUT_QUEUE_LEN;
  s_out_queue[slot].kind   = kind;
  s_out_queue[slot].arg    = arg;
  s_out_queue[slot].screen = s_screen;
  s_out_count++;
  out_pump();
}

// Eine noch nicht gesendete Statusabfrage fuer denselben Bildschirm
// reicht - ohne Telefon stapelten sich sonst alle 15 s neue an
static bool update_queued(void) {
  for (uint8_t i = 0; i < s_out_count; i++) {
    const OutItem *it = &s_out_queue[(s_out_head + i) % OUT_QUEUE_LEN];
    if (it->kind == OUT_REQUEST_UPDATE && it->screen == s_screen) return true;
  }
  return false;
}

static void request_update(void) {
  if (!update_queued()) out_enqueue(OUT_REQUEST_UPDATE, 0);
}

static void out_drop_front(void) {
  if (s_out_count == 0) return;
  s_out_head = (s_out_head + 1) % OUT_QUEUE_LEN;
  s_out_count--;
}

// Baut die naechste Nachricht auf. Rueckgabe false, wenn ein
// dict_write scheiterte - dann ist die Nachricht unvollstaendig und
// darf nicht rausgehen.
static bool build_message(DictionaryIterator *iter, const OutItem *item) {
  DictionaryResult r = DICT_OK;

  switch (item->kind) {
    // Jede Anfrage traegt ihren Bildschirm mit; die Antwort kommt damit
    // markiert zurueck und wird verworfen, wenn inzwischen geblaettert wurde
    case OUT_REQUEST_UPDATE:
      if (r == DICT_OK) r = dict_write_uint8(iter, MESSAGE_KEY_REQUEST_UPDATE, 1);
      if (r == DICT_OK) r = dict_write_uint8(iter, MESSAGE_KEY_SCREEN, item->screen);
      break;

    case OUT_ACTION:
      if (r == DICT_OK) r = dict_write_uint8(iter, MESSAGE_KEY_ACTION_TRIGGER, item->arg);
      if (r == DICT_OK) r = dict_write_uint8(iter, MESSAGE_KEY_SCREEN, item->screen);
      break;

    // Teil A: die grossen Felder, die eine knappe Outbox sprengen wuerden
    case OUT_CONFIG_A:
      if (r == DICT_OK) r = dict_write_cstring(iter, MESSAGE_KEY_HA_URL,      s_ha_url);
      if (r == DICT_OK) r = dict_write_cstring(iter, MESSAGE_KEY_HA_TOKEN,    s_ha_token);
      if (r == DICT_OK) r = dict_write_cstring(iter, MESSAGE_KEY_THEME,       s_dark ? "DARK" : "LIGHT");
      if (r == DICT_OK) r = dict_write_uint8(iter,   MESSAGE_KEY_CONFIG_PUSH, 1);
      break;

    // Ein Teil je Bildschirm. CONFIG_PUSH=2 am letzten signalisiert der
    // JS-Seite das Ende, SCREEN nennt dann den gerade gezeigten.
    case OUT_CONFIG_SCREEN: {
      uint8_t i = item->arg;
      const Screen     *sc = &s_screens[i];
      const ScreenKeys *k  = &s_keys[i];
      bool last = (i == SCREEN_COUNT - 1);

      if (r == DICT_OK) r = dict_write_cstring(iter, k->status, sc->status_entity);
      for (uint8_t s = 0; s < SLOT_COUNT && r == DICT_OK; s++) {
        const Action *a = &sc->act[s];
        if (r == DICT_OK) r = dict_write_cstring(iter, k->label[s],   a->label);
        if (r == DICT_OK) r = dict_write_cstring(iter, k->service[s], a->service);
        if (r == DICT_OK) r = dict_write_cstring(iter, k->entity[s],  a->entity);
        if (r == DICT_OK) r = dict_write_uint8(iter,   k->confirm[s], a->confirm ? 1 : 0);
      }
      if (r == DICT_OK) r = dict_write_uint8(iter, MESSAGE_KEY_CONFIG_PUSH, last ? 2 : 1);
      if (last && r == DICT_OK) r = dict_write_uint8(iter, MESSAGE_KEY_SCREEN, s_screen);
      break;
    }

    default:
      return false;
  }

  return (r == DICT_OK);
}

static void out_pump(void) {
  if (s_out_busy || s_out_count == 0) return;

  const OutItem *item = &s_out_queue[s_out_head];

  DictionaryIterator *iter;
  AppMessageResult res = app_message_outbox_begin(&iter);
  if (res == APP_MSG_BUSY) return;              // outbox_sent stoesst erneut an
  if (res != APP_MSG_OK) {
    out_drop_front();
    set_status_kind("Send failed", STATUS_ERROR);
    return;
  }

  if (!build_message(iter, item)) {
    out_drop_front();
    set_status_kind("Data too large", STATUS_ERROR);
    return;
  }

  if (app_message_outbox_send() != APP_MSG_OK) {
    out_drop_front();
    set_status_kind("Send failed", STATUS_ERROR);
    return;
  }

  s_out_busy = true;
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  s_out_busy = false;
  out_drop_front();
  out_pump();
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason,
                          void *context) {
  s_out_busy = false;
  out_drop_front();
  stop_busy_timer();
  set_status_kind("No phone", STATUS_ERROR);
  out_pump();
}

static void send_config(void) {
  out_enqueue(OUT_CONFIG_A, 0);
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) out_enqueue(OUT_CONFIG_SCREEN, i);
}

static void send_action(uint8_t idx) {
  if (!config_complete()) {
    set_status_kind("Not configured", STATUS_ERROR);
    return;
  }
  set_busy("Sending...");
  vibes_short_pulse();
  out_enqueue(OUT_ACTION, idx);
}

// ------------------------------------------------------------
//  PERSISTENZ
// ------------------------------------------------------------
static uint32_t pk(uint8_t screen, uint8_t field) {
  if (screen == 0) return PK_LEGACY_BASE + field;
  return PK_SCREEN_BASE + screen * PK_SCREEN_STRIDE + field;
}

static void read_str(uint32_t key, char *dst, size_t len) {
  if (persist_exists(key)) persist_read_string(key, dst, len);
}

// Standard wie in der Zwei-Aktionen-Fassung: oben mit Abfrage, unten ohne
static void default_config(void) {
  memset(s_screens, 0, sizeof(s_screens));
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    s_screens[i].act[SLOT_TOP].confirm = true;
  }
}

static void load_config(void) {
  default_config();

  read_str(PK_HA_URL,   s_ha_url,   URL_LEN);
  read_str(PK_HA_TOKEN, s_ha_token, TOKEN_LEN);

  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    Screen *sc = &s_screens[i];
    read_str(pk(i, F_STATUS), sc->status_entity, ENTITY_LEN);
    for (uint8_t s = 0; s < SLOT_COUNT; s++) {
      Action *a = &sc->act[s];
      read_str(pk(i, FIELD(F_LABEL, s)),   a->label,   LABEL_LEN);
      read_str(pk(i, FIELD(F_SERVICE, s)), a->service, SERVICE_LEN);
      read_str(pk(i, FIELD(F_ENTITY, s)),  a->entity,  ENTITY_LEN);
      uint32_t kc = pk(i, FIELD(F_CONFIRM, s));
      if (persist_exists(kc)) a->confirm = persist_read_bool(kc);
    }
  }

  if (persist_exists(PK_THEME_DARK))     s_dark   = persist_read_bool(PK_THEME_DARK);
  if (persist_exists(PK_CURRENT_SCREEN)) s_screen = persist_read_int(PK_CURRENT_SCREEN);
  fix_current_screen();
}

// persist_write_string liefert die geschriebene Byte-Zahl oder einen
// negativen Statuscode. Nur URL und Token werden gemeldet - laufen die
// ins Leere, funktioniert nach einem Neustart gar nichts mehr.
//
// Budget: persist fasst 4 KB je App. Voll ausgeschoepft brauchen URL,
// Token und fuenf Bildschirme rund 1,7 KB Nutzdaten.
static bool save_config(void) {
  bool ok = true;

  if (persist_write_string(PK_HA_URL,   s_ha_url)   < 0) ok = false;
  if (persist_write_string(PK_HA_TOKEN, s_ha_token) < 0) ok = false;

  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    const Screen *sc = &s_screens[i];
    persist_write_string(pk(i, F_STATUS), sc->status_entity);
    for (uint8_t s = 0; s < SLOT_COUNT; s++) {
      const Action *a = &sc->act[s];
      persist_write_string(pk(i, FIELD(F_LABEL, s)),   a->label);
      persist_write_string(pk(i, FIELD(F_SERVICE, s)), a->service);
      persist_write_string(pk(i, FIELD(F_ENTITY, s)),  a->entity);
      persist_write_bool(pk(i, FIELD(F_CONFIRM, s)),   a->confirm);
    }
  }

  persist_write_bool(PK_THEME_DARK, s_dark);
  return ok;
}

static void save_screen(void) {
  persist_write_int(PK_CURRENT_SCREEN, s_screen);
}

// ------------------------------------------------------------
//  BESTAETIGUNGSFENSTER
// ------------------------------------------------------------
static void confirm_update(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, color_bg());
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

  // Ablaufender Ring: volle Spur, darauf der verbleibende Anteil
  graphics_context_set_fill_color(ctx, color_dim());
  graphics_fill_radial(ctx, RING_RECT, GOvalScaleModeFitCircle, RING_THICKNESS,
                       DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(360));

  int32_t left = s_cfm_left_ms;
  if (left < 0) left = 0;
  int32_t sweep = (360 * left) / CONFIRM_TIMEOUT_MS;
  graphics_context_set_fill_color(ctx, color_busy());
  graphics_fill_radial(ctx, RING_RECT, GOvalScaleModeFitCircle, RING_THICKNESS,
                       DEG_TO_TRIGANGLE(0), DEG_TO_TRIGANGLE(sweep));

  draw_text_boxed(ctx, "Confirm?", fonts_get_system_font(FONT_CFM_SML),
                  CFM_TITLE_RECT, GTextAlignmentCenter,
                  GTextOverflowModeTrailingEllipsis, color_dim());

  draw_text_boxed(ctx, s_cfm_name, fonts_get_system_font(FONT_CFM_NAME),
                  CFM_NAME_RECT, GTextAlignmentCenter,
                  GTextOverflowModeWordWrap, color_text());

  draw_text_boxed(ctx, "Select to run\nany other cancels",
                  fonts_get_system_font(FONT_CFM_SML),
                  CFM_HINT_RECT, GTextAlignmentCenter,
                  GTextOverflowModeWordWrap, color_dim());
}

static void confirm_cancel(void) {
  s_pending = 0;
  window_stack_remove(s_confirm_window, true);
  set_status_kind("Cancelled", STATUS_NEUTRAL);
}

static void confirm_tick(void *ctx) {
  s_confirm_timer = NULL;
  s_cfm_left_ms -= CONFIRM_TICK_MS;

  if (s_cfm_left_ms <= 0) {
    confirm_cancel();
    return;
  }

  if (s_confirm_layer) layer_mark_dirty(s_confirm_layer);
  s_confirm_timer = app_timer_register(CONFIRM_TICK_MS, confirm_tick, NULL);
}

static void cfm_select_click(ClickRecognizerRef rec, void *ctx) {
  uint8_t action = s_pending;
  s_pending = 0;
  window_stack_remove(s_confirm_window, true);
  if (action) send_action(action);
}

static void cfm_cancel_click(ClickRecognizerRef rec, void *ctx) {
  confirm_cancel();
}

static void cfm_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, cfm_select_click);
  window_single_click_subscribe(BUTTON_ID_UP,     cfm_cancel_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   cfm_cancel_click);
  window_single_click_subscribe(BUTTON_ID_BACK,   cfm_cancel_click);
}

static void confirm_window_load(Window *win) {
  Layer *root = window_get_root_layer(win);
  s_confirm_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_confirm_layer, confirm_update);
  layer_add_child(root, s_confirm_layer);
}

static void confirm_window_unload(Window *win) {
  layer_destroy(s_confirm_layer);
  s_confirm_layer = NULL;
}

static void confirm_window_disappear(Window *win) {
  if (s_confirm_timer) {
    app_timer_cancel(s_confirm_timer);
    s_confirm_timer = NULL;
  }
}

static void show_confirm(uint8_t action, const char *name) {
  s_pending     = action;
  s_cfm_left_ms = CONFIRM_TIMEOUT_MS;

  strncpy(s_cfm_name, name, CONFIRM_LINE_LEN - 1);
  s_cfm_name[CONFIRM_LINE_LEN - 1] = '\0';

  if (!s_confirm_window) {
    s_confirm_window = window_create();
    window_set_click_config_provider(s_confirm_window, cfm_click_config);
    window_set_window_handlers(s_confirm_window, (WindowHandlers) {
      .load      = confirm_window_load,
      .unload    = confirm_window_unload,
      .disappear = confirm_window_disappear
    });
  }

  window_stack_push(s_confirm_window, true);
  vibes_short_pulse();

  if (s_confirm_timer) app_timer_cancel(s_confirm_timer);
  s_confirm_timer = app_timer_register(CONFIRM_TICK_MS, confirm_tick, NULL);
}

// ------------------------------------------------------------
//  KNOEPFE
// ------------------------------------------------------------
// slot 0 = oben, 1 = unten; zur JS-Seite geht er als 1 bzw. 2
static void trigger(uint8_t slot) {
  if (!config_complete()) {
    set_status_kind("Not configured", STATUS_ERROR);
    return;
  }
  const Action *a = &s_screens[s_screen].act[slot];
  if (!action_set(a)) return;              // keine Karte, keine Aktion

  if (a->confirm) show_confirm(slot + 1, action_name(a));
  else            send_action(slot + 1);
}

static void up_click(ClickRecognizerRef rec, void *ctx)   { trigger(SLOT_TOP); }
static void down_click(ClickRecognizerRef rec, void *ctx) { trigger(SLOT_BOT); }

// ------------------------------------------------------------
//  AUTOMATISCHE STATUSABFRAGE
//  Laeuft still: kein "Refreshing..." und kein Busy-Timer, sonst
//  flackerte die Anzeige alle 15 Sekunden. Waehrend die
//  Sicherheitsabfrage oben liegt, ruht sie.
// ------------------------------------------------------------
static void poll_tick(void *ctx);

static void restart_poll_timer(void) {
  if (s_poll_timer) app_timer_cancel(s_poll_timer);
  s_poll_timer = app_timer_register(POLL_INTERVAL_MS, poll_tick, NULL);
}

static void poll_tick(void *ctx) {
  s_poll_timer = NULL;
  if (config_complete() && window_stack_get_top_window() == s_main_window) {
    request_update();
  }
  restart_poll_timer();
}

// Mitte: naechster belegter Bildschirm, nach dem letzten wieder der erste
static void select_click(ClickRecognizerRef rec, void *ctx) {
  if (visible_count() < 2) return;

  uint8_t next = s_screen;
  do {
    next = (next + 1) % SCREEN_COUNT;
  } while (!screen_visible(next));

  s_screen = next;
  save_screen();

  // Der Status des alten Bildschirms darf nicht stehen bleiben
  stop_busy_timer();
  if (!config_complete()) {
    set_status_kind("Not configured", STATUS_ERROR);
    return;
  }
  set_status_kind("...", STATUS_NEUTRAL);
  request_update();
  restart_poll_timer();
}

static void main_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void main_window_load(Window *win) {
  Layer *root = window_get_root_layer(win);
  s_canvas_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas_layer, canvas_update);
  layer_add_child(root, s_canvas_layer);
}

static void main_window_unload(Window *win) {
  layer_destroy(s_canvas_layer);
  s_canvas_layer = NULL;
}

// ------------------------------------------------------------
//  APPMESSAGE-EMPFANG
//  Clay schickt je nach Feldtyp Strings oder Ganzzahlen. Ohne
//  Typpruefung liest value->cstring bei einem Integer-Tupel ins Leere.
// ------------------------------------------------------------
static bool copy_tuple(DictionaryIterator *iter, uint32_t key,
                       char *dst, size_t len, bool *truncated) {
  Tuple *t = dict_find(iter, key);
  if (!t || t->type != TUPLE_CSTRING || t->length == 0) return false;

  // cstring ist ein Array der Laenge 0 am Ende des Tupels; ueber einen
  // eigenen Zeiger gelesen, statt direkt indiziert, bleibt der Compiler ruhig.
  const char *src = (const char *)t->value->cstring;

  // Der Wert ist nicht zwingend nullterminiert, deshalb ueber length gehen
  size_t src_len = t->length;
  if (src[src_len - 1] == '\0') src_len--;
  if (truncated && src_len > len - 1) *truncated = true;

  size_t n = (src_len < len - 1) ? src_len : len - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
  return true;
}

static bool copy_bool(DictionaryIterator *iter, uint32_t key, bool *dst) {
  Tuple *t = dict_find(iter, key);
  if (!t) return false;

  if (t->type == TUPLE_CSTRING) {
    // "", "0" und "false" gelten als aus
    const char *src = (const char *)t->value->cstring;
    char c = src[0];
    *dst = !(c == '\0' || c == '0' || c == 'f' || c == 'F');
    return true;
  }
  if (t->type == TUPLE_INT || t->type == TUPLE_UINT) {
    *dst = (t->value->int32 != 0);
    return true;
  }
  return false;
}

// Ganzzahl-Tupel unabhaengig von der Breite lesen: JS schickt Zahlen
// als int32, die Uhr selbst als uint8
static int32_t tuple_int(const Tuple *t) {
  if (t->type == TUPLE_CSTRING) return atoi((const char *)t->value->cstring);
  switch (t->length) {
    case 1:  return (t->type == TUPLE_INT) ? t->value->int8  : t->value->uint8;
    case 2:  return (t->type == TUPLE_INT) ? t->value->int16 : t->value->uint16;
    default: return t->value->int32;
  }
}

static StatusKind kind_from_tuple(DictionaryIterator *iter) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_STATUS_KIND);
  if (!t) return STATUS_NEUTRAL;
  const char *src = (const char *)t->value->cstring;
  int32_t v = (t->type == TUPLE_CSTRING) ? (src[0] - '0') : t->value->int32;
  if (v < 0 || v > STATUS_ERROR) return STATUS_NEUTRAL;
  return (StatusKind)v;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  // Das Telefon fragt nach der auf der Uhr gespeicherten Konfiguration
  if (dict_find(iter, MESSAGE_KEY_REQUEST_CONFIG)) {
    send_config();
    return;
  }

  bool changed   = false;
  bool truncated = false;

  if (copy_tuple(iter, MESSAGE_KEY_HA_URL,   s_ha_url,   URL_LEN,   &truncated)) changed = true;
  if (copy_tuple(iter, MESSAGE_KEY_HA_TOKEN, s_ha_token, TOKEN_LEN, &truncated)) changed = true;

  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    Screen           *sc = &s_screens[i];
    const ScreenKeys *k  = &s_keys[i];
    if (copy_tuple(iter, k->status, sc->status_entity, ENTITY_LEN, NULL)) changed = true;
    for (uint8_t s = 0; s < SLOT_COUNT; s++) {
      Action *a = &sc->act[s];
      if (copy_tuple(iter, k->label[s],   a->label,   LABEL_LEN,   NULL)) changed = true;
      if (copy_tuple(iter, k->service[s], a->service, SERVICE_LEN, NULL)) changed = true;
      if (copy_tuple(iter, k->entity[s],  a->entity,  ENTITY_LEN,  NULL)) changed = true;
      if (copy_bool(iter,  k->confirm[s], &a->confirm))                   changed = true;
    }
  }

  char theme[THEME_LEN] = "";
  if (copy_tuple(iter, MESSAGE_KEY_THEME, theme, THEME_LEN, NULL)) {
    s_dark  = (theme[0] != 'L' && theme[0] != 'l');
    changed = true;
  }

  // Ein Status fuer einen anderen Bildschirm ist veraltet - es wurde
  // geblaettert, waehrend die Anfrage lief. Ohne SCREEN (etwa
  // "Not configured") gilt er immer.
  Tuple *st = dict_find(iter, MESSAGE_KEY_STATUS_TEXT);
  Tuple *sn = dict_find(iter, MESSAGE_KEY_SCREEN);
  if (st && st->type == TUPLE_CSTRING &&
      (!sn || tuple_int(sn) == s_screen)) {
    stop_busy_timer();
    set_status_kind(st->value->cstring, kind_from_tuple(iter));
  }

  if (changed) {
    bool saved = save_config();
    uint8_t before = s_screen;
    fix_current_screen();
    if (s_screen != before) save_screen();
    if (!st) {
      if (truncated) {
        set_status_kind("URL/token too long", STATUS_ERROR);
      } else if (!saved) {
        set_status_kind("Save failed", STATUS_ERROR);
      } else if (!config_complete()) {
        set_status_kind("Not configured", STATUS_ERROR);
      } else {
        // Die Konfiguration ist gerade vollstaendig geworden - ohne
        // diese Abfrage bliebe der alte Fehlertext stehen.
        set_busy("Refreshing...");
        request_update();
      }
    }
    mark_dirty();
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  stop_busy_timer();
  // Eine zu kleine Inbox meldet sich genau hier - die Konfiguration von
  // Clay passt dann nicht in einem Stueck herein.
  set_status_kind((reason == APP_MSG_BUFFER_OVERFLOW)
                    ? "Data too large" : "Message lost",
                  STATUS_ERROR);
}

// ------------------------------------------------------------
//  ZEIT
// ------------------------------------------------------------
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_clock();
  mark_dirty();
}

// ------------------------------------------------------------
//  INIT / DEINIT
// ------------------------------------------------------------
// Bildschirm 1 traegt die Schluessel ohne Suffix aus der
// Zwei-Aktionen-Fassung, die weiteren _2 bis _5
#define SCREEN_KEYS(SFX) (ScreenKeys) {                                   \
  .status  = MESSAGE_KEY_STATUS_ENTITY##SFX,                              \
  .label   = { MESSAGE_KEY_LABEL_TOP##SFX,   MESSAGE_KEY_LABEL_BOTTOM##SFX },   \
  .service = { MESSAGE_KEY_SERVICE_TOP##SFX, MESSAGE_KEY_SERVICE_BOTTOM##SFX }, \
  .entity  = { MESSAGE_KEY_ENTITY_TOP##SFX,  MESSAGE_KEY_ENTITY_BOTTOM##SFX },  \
  .confirm = { MESSAGE_KEY_CONFIRM_TOP##SFX, MESSAGE_KEY_CONFIRM_BOTTOM##SFX }  \
}

static void init_keys(void) {
  s_keys[0] = SCREEN_KEYS();
  s_keys[1] = SCREEN_KEYS(_2);
  s_keys[2] = SCREEN_KEYS(_3);
  s_keys[3] = SCREEN_KEYS(_4);
  s_keys[4] = SCREEN_KEYS(_5);
}

static void initial_sync(void *ctx) {
  if (config_complete()) {
    set_busy("Connecting...");
    request_update();
  } else {
    set_status_kind("Not configured", STATUS_ERROR);
  }
  restart_poll_timer();
}

static void init(void) {
  init_keys();
  load_config();
  update_clock();

  s_tri_up     = gpath_create(&TRI_UP_INFO);
  s_tri_down   = gpath_create(&TRI_DOWN_INFO);

  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, main_click_config);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load   = main_window_load,
    .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);

  // Die Firmware liefert nicht zwingend die gewuenschte Groesse; der
  // kleinere der beiden Werte ist der, den es tatsaechlich gibt.
  uint32_t inbox  = app_message_inbox_size_maximum();
  uint32_t outbox = app_message_outbox_size_maximum();
  if (inbox  > INBOX_WANTED)  inbox  = INBOX_WANTED;
  if (outbox > OUTBOX_WANTED) outbox = OUTBOX_WANTED;
  app_message_open(inbox, outbox);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  app_timer_register(INITIAL_SYNC_MS, initial_sync, NULL);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();

  if (s_confirm_timer) app_timer_cancel(s_confirm_timer);
  if (s_busy_timer)    app_timer_cancel(s_busy_timer);
  if (s_anim_timer)    app_timer_cancel(s_anim_timer);
  if (s_poll_timer)    app_timer_cancel(s_poll_timer);

  if (s_confirm_window) window_destroy(s_confirm_window);
  window_destroy(s_main_window);

  gpath_destroy(s_tri_up);
  gpath_destroy(s_tri_down);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}

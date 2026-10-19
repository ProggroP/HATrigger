import dbm.dumb, json, os, sys
platform, theme = sys.argv[1], sys.argv[2]
p = os.path.expanduser(f"~/.pebble-sdk/4.33.1/{platform}/localstorage/6f4bbfbd-c1e8-468c-b0af-92cfa2461df5")
os.makedirs(os.path.dirname(p), exist_ok=True)
db = dbm.dumb.open(p, "c")
db["clay-settings"] = json.dumps({
    "HA_URL": "http://127.0.0.1:8123",
    "HA_TOKEN": "screenshot-stub-token",
    "THEME": theme,
    # Bildschirm 1: Haustuer
    "STATUS_ENTITY": "lock.front_door",
    "LABEL_TOP": "Unlock door",
    "SERVICE_TOP": "lock.open",
    "ENTITY_TOP": "lock.front_door",
    "CONFIRM_TOP": True,
    "LABEL_BOTTOM": "Lock up",
    "SERVICE_BOTTOM": "lock.lock",
    "ENTITY_BOTTOM": "lock.front_door",
    "CONFIRM_BOTTOM": False,
    "COLOR": "0",       # Blau
    # Bildschirm 2: Garage
    "STATUS_ENTITY_2": "cover.garage",
    "LABEL_TOP_2": "Open garage",
    "SERVICE_TOP_2": "cover.open_cover",
    "ENTITY_TOP_2": "cover.garage",
    "CONFIRM_TOP_2": True,
    "LABEL_BOTTOM_2": "Close garage",
    "SERVICE_BOTTOM_2": "cover.close_cover",
    "ENTITY_BOTTOM_2": "cover.garage",
    "CONFIRM_BOTTOM_2": False,
    "COLOR_2": "4",     # Lila, bewusst nicht die Vorgabe
    # Bildschirm 3: Kaffeemaschine
    "STATUS_ENTITY_3": "switch.coffee_maker",
    "LABEL_TOP_3": "Brew coffee",
    "SERVICE_TOP_3": "switch.turn_on",
    "ENTITY_TOP_3": "switch.coffee_maker",
    "CONFIRM_TOP_3": False,
    "LABEL_BOTTOM_3": "Switch off",
    "SERVICE_BOTTOM_3": "switch.turn_off",
    "ENTITY_BOTTOM_3": "switch.coffee_maker",
    "CONFIRM_BOTTOM_3": False,
    "COLOR_3": "2",     # Gruen
    # Bildschirm 4: Licht, nur oben belegt, ohne Label
    "SERVICE_TOP_4": "light.toggle",
    "ENTITY_TOP_4": "light.living_room",
    "CONFIRM_TOP_4": False,
    "SERVICE_BOTTOM_4": "",
    "ENTITY_BOTTOM_4": "",
    "COLOR_4": "5",     # Petrol
    # Bildschirm 5: Hoftor
    "STATUS_ENTITY_5": "cover.gate",
    "LABEL_TOP_5": "Open gate",
    "SERVICE_TOP_5": "cover.open_cover",
    "ENTITY_TOP_5": "cover.gate",
    "CONFIRM_TOP_5": True,
    "LABEL_BOTTOM_5": "Close gate",
    "SERVICE_BOTTOM_5": "cover.close_cover",
    "ENTITY_BOTTOM_5": "cover.gate",
    "CONFIRM_BOTTOM_5": False,
    "COLOR_5": "1",     # Rot
}).encode()
db.close()
print(f"{platform} {theme} geseedet")

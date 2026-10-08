// ============================================================
//  HA Trigger - PebbleKit JS
//
//  Die Uhr haelt die vollstaendige Konfiguration persistent.
//  Diese Seite fuehrt die HTTP-Aufrufe gegen Home Assistant aus und
//  holt sich die Konfiguration von der Uhr zurueck, falls der eigene
//  localStorage leer ist (etwa nach Neuinstallation der Telefon-App).
// ============================================================

var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var SETTINGS_KEY    = 'clay-settings';
var HTTP_TIMEOUT_MS = 8000;
var POLL_DELAYS_MS  = [900, 2500, 5000];   // Nachfassen nach einer Aktion
var SCREEN_COUNT    = 5;                   // muss zu src/c/main.c passen

// Bildschirm 1 nutzt die Schluessel ohne Suffix (Zwei-Aktionen-Fassung),
// die weiteren _2 bis _5
function sfx(screen) {
  return screen > 0 ? '_' + (screen + 1) : '';
}

function screenIndex(v) {
  var n = parseInt(v, 10);
  return (n >= 0 && n < SCREEN_COUNT) ? n : 0;
}

// Muss zu StatusKind in src/c/main.c passen
var KIND_NEUTRAL = 0;
var KIND_BUSY    = 1;
var KIND_OK      = 2;
var KIND_ERROR   = 3;

// ------------------------------------------------------------
//  Einstellungen lesen und schreiben
//  clay.getSettings() ohne Argument wirft, daher direkt localStorage.
//  Clay legt dort flache Werte ab (index.js: settingsStorage[key] =
//  settings[key].value), die Felder sind also direkt lesbar.
// ------------------------------------------------------------
function getSettings() {
  try {
    return JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}');
  } catch (e) {
    return {};
  }
}

function saveSettings(obj) {
  try {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify(obj));
  } catch (e) {
    console.log('localStorage schreiben fehlgeschlagen: ' + e);
  }
}

function str(v, fallback) {
  if (v === undefined || v === null) return fallback;
  return ('' + v).trim();
}

// ------------------------------------------------------------
//  URL und Token aufbereiten
//
//  Beides kommt per Copy-Paste ins Konfigurationsfeld und bringt
//  regelmaessig Muell mit: fehlendes Schema, ein Pfad hinter dem Host,
//  Zeilenumbrueche im Token. Ohne Schema haengt XMLHttpRequest die
//  Eingabe an die Seiten-URL an und meldet nur "HA unreachable";
//  ein Zeilenumbruch im Token zerlegt den Authorization-Header.
// ------------------------------------------------------------
function baseUrl(s) {
  var u = str(s.HA_URL, '');
  if (!u) return '';

  u = u.replace(/\s+/g, '');
  if (!/^https?:\/\//i.test(u)) u = 'http://' + u;

  // Nachgestellte Schraegstriche und ein versehentlich mitkopiertes
  // /lovelace oder /api abschneiden
  u = u.replace(/\/+$/, '');
  u = u.replace(/\/api$/i, '');
  return u;
}

// Long-Lived Tokens sind base64url und enthalten nie Leerraum
function token(s) {
  return str(s.HA_TOKEN, '').replace(/\s+/g, '');
}

function isConfigured(s) {
  return baseUrl(s).length > 0 && token(s).length > 0;
}

// ------------------------------------------------------------
//  Zustandstexte
// ------------------------------------------------------------
var STATE_TEXT = {
  'locked':      'Locked',
  'unlocked':    'Unlocked',
  'locking':     'Locking...',
  'unlocking':   'Unlocking...',
  'open':        'Open',
  'opening':     'Opening...',
  'closed':      'Closed',
  'closing':     'Closing...',
  'jammed':      'Jammed!',
  'on':          'On',
  'off':         'Off',
  'home':        'Home',
  'not_home':    'Away',
  'idle':        'Idle',
  'unavailable': 'Unavailable',
  'unknown':     'Unknown',
  // Kein HA-Zustand, sondern eine Konvention fuer Status-Template-Sensoren,
  // die eine Aktion an Anwesenheit knuepfen: "du bist nicht da, das
  // Aufschliessen wuerde jetzt abgelehnt".
  'away':        'Away'
};

// Zustaende, die kein normaler Betrieb sind, kommen rot heraus
var STATE_BAD  = { 'jammed': 1, 'unavailable': 1, 'unknown': 1, 'away': 1 };
var STATE_WARN = { 'locking': 1, 'unlocking': 1, 'opening': 1, 'closing': 1 };

function prettyState(raw) {
  if (raw === undefined || raw === null || raw === '') return 'Unknown';
  var key = ('' + raw).toLowerCase();
  if (STATE_TEXT.hasOwnProperty(key)) return STATE_TEXT[key];
  // Template-Sensoren liefern meist schon fertigen Text
  return ('' + raw).substring(0, 40);
}

function stateKind(raw) {
  var key = ('' + raw).toLowerCase();
  if (STATE_BAD[key])  return KIND_ERROR;
  if (STATE_WARN[key]) return KIND_BUSY;
  return KIND_OK;
}

// ------------------------------------------------------------
//  An die Uhr senden
// ------------------------------------------------------------
function sendToWatch(dict) {
  Pebble.sendAppMessage(dict, function () {}, function (e) {
    console.log('AppMessage NACK: ' + JSON.stringify(e));
  });
}

// Mit screen markiert verwirft die Uhr die Antwort, wenn inzwischen
// ein anderer Bildschirm zu sehen ist
function sendStatus(text, kind, screen) {
  var msg = {
    'STATUS_TEXT': text,
    'STATUS_KIND': (kind === undefined) ? KIND_NEUTRAL : kind
  };
  if (screen !== undefined) msg.SCREEN = screen;
  sendToWatch(msg);
}

function sendError(text, screen) {
  sendStatus(text, KIND_ERROR, screen);
}

// ------------------------------------------------------------
//  HTTP gegen Home Assistant
// ------------------------------------------------------------
function haRequest(method, path, body, onOk, onErr) {
  var s   = getSettings();
  var url = baseUrl(s);
  var tok = token(s);

  if (!url) { onErr('No HA URL'); return; }
  if (!tok) { onErr('No token'); return; }

  var xhr = new XMLHttpRequest();
  xhr.open(method, url + path, true);
  xhr.setRequestHeader('Authorization', 'Bearer ' + tok);
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.timeout = HTTP_TIMEOUT_MS;

  xhr.onload = function () {
    if (xhr.status >= 200 && xhr.status < 300) {
      onOk(xhr.responseText);
    } else if (xhr.status === 401) {
      onErr('Invalid token');
    } else if (xhr.status === 403) {
      onErr('Access denied');
    } else if (xhr.status === 404) {
      onErr('Not found');
    } else if (xhr.status === 400) {
      onErr('Action rejected');
    } else if (xhr.status === 0) {
      // Kein HTTP-Status: DNS, TLS oder Reverse Proxy, nicht HA selbst
      onErr('HA unreachable');
    } else {
      onErr('HTTP ' + xhr.status);
    }
  };
  xhr.ontimeout = function () { onErr('Timeout'); };
  xhr.onerror   = function () { onErr('HA unreachable'); };

  try {
    xhr.send(body ? JSON.stringify(body) : null);
  } catch (e) {
    onErr('HA unreachable');
  }
}

// ------------------------------------------------------------
//  Status abfragen
// ------------------------------------------------------------
// Eigene Status-Entity des Bildschirms, sonst die der oberen, sonst
// die der unteren Aktion
function statusEntityFor(s, screen) {
  var x = sfx(screen);
  return str(s['STATUS_ENTITY' + x], '') ||
         str(s['ENTITY_TOP' + x], '') ||
         str(s['ENTITY_BOTTOM' + x], '');
}

function fetchStatus(screen) {
  var s = getSettings();

  if (!isConfigured(s)) {
    sendError('Not configured');
    return;
  }

  var entity = statusEntityFor(s, screen);
  if (!entity) {
    sendStatus('No status entity', KIND_NEUTRAL, screen);
    return;
  }

  haRequest('GET', '/api/states/' + encodeURIComponent(entity), null,
    function (text) {
      var state = 'unknown';
      try { state = JSON.parse(text).state; } catch (e) {}
      sendStatus(prettyState(state), stateKind(state), screen);
    },
    function (err) { sendError(err, screen); });
}

function pollAfterAction(screen) {
  POLL_DELAYS_MS.forEach(function (delay) {
    setTimeout(function () { fetchStatus(screen); }, delay);
  });
}

// ------------------------------------------------------------
//  Aktion ausfuehren
// ------------------------------------------------------------
// Entity-Domains, bei denen sich die Aktion von selbst versteht
var SELF_TRIGGERING = {
  'script':       'script.turn_on',
  'scene':        'scene.turn_on',
  'automation':   'automation.trigger',
  'button':       'button.press',
  'input_button': 'input_button.press'
};

function runAction(index, screen) {
  var s    = getSettings();
  var pos  = (index === 1) ? 'TOP' : 'BOTTOM';
  var x    = sfx(screen);
  var service = str(s['SERVICE_' + pos + x], '');
  var entity  = str(s['ENTITY_' + pos + x], '');
  var fail    = function (err) { sendError(err, screen); };

  // Steht nur eine Ziel-Entity da, die sich selbst ausloest, ergibt sich
  // die Aktion aus ihrer Domain. Sonst muesste derselbe Name zweimal
  // eingetragen werden - einmal als Aktion, einmal als Ziel.
  if (!service && entity) {
    service = SELF_TRIGGERING[entity.split('.')[0]] || '';
  }

  if (!service) {
    fail('Action missing');
    return;
  }

  // Ein gefuelltes Feld ohne Punkt ist der haeufigste Tippfehler
  // (script_mein_skript statt script.mein_skript). "Action missing"
  // waere hier irrefuehrend - das Feld ist ja ausgefuellt.
  if (service.indexOf('.') < 1) {
    fail('Use domain.service');
    return;
  }

  var parts   = service.split('.');
  var domain  = parts[0];
  var srvName = parts.slice(1).join('.');
  var body    = entity ? { entity_id: entity } : {};

  haRequest('POST', '/api/services/' + domain + '/' + srvName, body,
    function () {
      sendStatus('OK', KIND_OK, screen);
      pollAfterAction(screen);
    },
    fail);
}

// ------------------------------------------------------------
//  Konfiguration von der Uhr zurueckholen
//  Die Uhr schickt sie in Teilen (erst URL/Token/Theme, dann einer je
//  Bildschirm); CONFIG_PUSH=2 ist der letzte.
// ------------------------------------------------------------
var SCREEN_FIELDS = [
  'STATUS_ENTITY',
  'LABEL_TOP', 'SERVICE_TOP', 'ENTITY_TOP', 'CONFIRM_TOP',
  'LABEL_BOTTOM', 'SERVICE_BOTTOM', 'ENTITY_BOTTOM', 'CONFIRM_BOTTOM'
];

var CONFIG_KEYS = ['HA_URL', 'HA_TOKEN', 'THEME'];
var BOOL_KEYS   = {};

(function () {
  for (var i = 0; i < SCREEN_COUNT; i++) {
    SCREEN_FIELDS.forEach(function (f) {
      var k = f + sfx(i);
      CONFIG_KEYS.push(k);
      if (f.indexOf('CONFIRM_') === 0) BOOL_KEYS[k] = true;
    });
  }
})();

function requestConfigFromWatch() {
  sendToWatch({ 'REQUEST_CONFIG': 1 });
}

// Gegenrichtung: die Uhr hat ihren persist-Speicher verloren (App neu
// installiert), das Telefon kennt die Einstellungen aber noch. Ohne
// diesen Push bliebe die Uhr auf "Nicht konfiguriert" stehen, bis der
// Nutzer die Clay-Seite von Hand oeffnet und speichert.
function pushConfigToWatch() {
  var s = getSettings();
  var msg = {};

  CONFIG_KEYS.forEach(function (k) {
    if (s[k] === undefined || s[k] === null) return;
    if (BOOL_KEYS[k]) {
      msg[k] = s[k] ? 1 : 0;          // uint8 auf der Uhr, kein Boolean
    } else {
      msg[k] = '' + s[k];
    }
  });

  // URL und Token in der bereinigten Form, mit der auch gesendet wird
  msg.HA_URL   = baseUrl(s);
  msg.HA_TOKEN = token(s);

  sendToWatch(msg);
}

function absorbConfigFromWatch(payload) {
  var s = getSettings();
  var gotSomething = false;

  CONFIG_KEYS.forEach(function (k) {
    if (payload[k] === undefined) return;

    if (BOOL_KEYS[k]) {
      s[k] = (payload[k] ? true : false);
      gotSomething = true;
      return;
    }

    // Ein leeres Feld von der Uhr heisst "kenne ich nicht" und ist kein
    // Loeschbefehl. Sonst raeumt eine frisch installierte, noch leere
    // Uhr die Einstellungen des Telefons ab - Token inklusive.
    var v = str(payload[k], '');
    if (v === '' && str(s[k], '') !== '') return;

    s[k] = v;
    gotSomething = true;
  });

  if (gotSomething) {
    saveSettings(s);
    console.log('Konfiguration von der Uhr uebernommen (Teil ' +
                payload.CONFIG_PUSH + ')');
  }

  // Erst nach dem letzten Teil ist die Konfiguration vollstaendig
  if (payload.CONFIG_PUSH !== 2) return;

  if (isConfigured(s)) {
    fetchStatus(screenIndex(payload.SCREEN));
  } else {
    sendError('Not configured');
  }
}

// ------------------------------------------------------------
//  Events
// ------------------------------------------------------------
Pebble.addEventListener('ready', function () {
  var s = getSettings();
  if (isConfigured(s)) {
    // Erst abgleichen, dann fragen - die Uhr kann ihren Speicher
    // verloren haben, waehrend das Telefon noch alles kennt.
    // Die Uhr fragt nach dem Empfang selbst den Status ihres
    // aktuellen Bildschirms ab - nur sie weiss, welcher das ist.
    setTimeout(pushConfigToWatch, 250);
  } else {
    // Telefonseite leer: Konfiguration von der Uhr anfordern
    setTimeout(requestConfigFromWatch, 250);
  }
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};

  if (p.CONFIG_PUSH) {
    absorbConfigFromWatch(p);
    return;
  }

  var screen = screenIndex(p.SCREEN);

  if (p.REQUEST_UPDATE) {
    fetchStatus(screen);
  }

  if (p.ACTION_TRIGGER) {
    runAction(p.ACTION_TRIGGER, screen);
  }
});

// Nach dem Speichern auf der Clay-Seite braucht es hier nichts weiter:
// Clay schickt die Konfiguration an die Uhr, und die fragt daraufhin
// selbst den Status ihres aktuellen Bildschirms ab.

# Screenshots

Sechs Aufnahmen, je Plattform derselbe Satz:

| Datei | Inhalt |
|---|---|
| `*-1-dark.png` | Bildschirm 1 (Haustür), Status „Locked", fünf Seitenpunkte |
| `*-2-garage.png` | Bildschirm 2 (Garage), Status „Closed", fünf Seitenpunkte |
| `*-3-confirm.png` | Sicherheitsabfrage mit ablaufendem Countdown-Ring |

Emery 200 × 228, Gabbro 260 × 260, jeweils native Auflösung ohne Rahmen.

## Wofür sie sind — und wofür nicht

**`pebble publish` verwendet diese Dateien nicht.** Es nimmt die Screenshots
beim Veröffentlichen live aus dem laufenden Emulator auf. Die Uhr muss zu
diesem Zeitpunkt also selbst einen vorzeigbaren Zustand zeigen — nicht
„Not configured" oder „No response".

Die Dateien hier sind für alles andere: Store-Seite von Hand befüllen,
README, Vorschau, Vergleich zwischen zwei Releases.

## Zustand herstellen (vor dem Publish oder für neue Aufnahmen)

Die App braucht dafür eine erreichbare Home-Assistant-Instanz. Statt echter
Zugangsdaten tut es ein Stub auf `127.0.0.1:8123`, der je Entity einen
festen Zustand meldet (Schloss verriegelt, Garage zu, Kaffeemaschine aus,
Licht an, Tor offen) und jede Anfrage protokolliert — der Emulator läuft in
WSL und erreicht ihn direkt. `seed.py` belegt alle fünf Bildschirme, damit
die Rail den vollen Satz Seitenpunkte zeigt; auf Bildschirm 4 ist nur die
obere Aktion belegt, und zwar ohne Label:

```bash
(setsid python3 fake_ha.py > /tmp/fake_ha.log 2>&1 &)
```

Dann die Einstellungen der Telefonseite setzen (Emulator vorher stoppen,
sonst überschreibt pypkjs den localStorage beim Beenden):

```bash
python3 seed.py emery DARK      # oder: gabbro LIGHT
```

Danach `pebble install --emulator emery`. Beim Start schiebt die JS-Seite
die Konfiguration auf die Uhr, die Statuszeile zeigt „Locked" in Grün.

Für einen Theme-Wechsel muss der Emulator nicht neu booten: nur
`pkill -f pypkjs`, neu seeden, nochmal installieren — das spart auf Gabbro
die knappe Minute Bootzeit. Achtung: `pkill -f pypkjs` trennt *beide*
laufenden Emulatoren vom Telefon, danach die jeweils andere Plattform
ebenfalls neu installieren.

Die App startet auf dem zuletzt gezeigten Bildschirm. Vor einer Aufnahme
also per `pebble emu-button click select` auf den gewünschten blättern.

Zum Schluss den Stub beenden. Das Muster in eckigen Klammern verhindert,
dass `pkill -f` die eigene `bash -lc`-Kommandozeile mit erwischt:

```bash
pkill -f '[f]ake_ha.py'
```

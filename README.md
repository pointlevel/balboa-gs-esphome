# balboa-gs-esphome

ESPHome-komponent som läser och styr ett Balboa GS-system (DZ, t.ex. GS510DZ/GS523DZ)
med panel VL801D via panelens RJ45-kabel. Hårdvara: Wemos D1 mini på kretskortet från
[MagnusPer/Balboa-GS510SZ](https://github.com/MagnusPer/Balboa-GS510SZ). Avkodningen bygger på
[Shuraxxx-forken för VL801D](https://github.com/Shuraxxx/-Balboa-GS523DZ-with-panel-VL801D-DeluxeSerie--MQTT).

## Innehåll
- `components/balboa_gs/` – komponenten
- `example/spabad.yaml` – komplett konfiguration
- `example/secrets.yaml.example` – vilka hemligheter som behövs

## Installation
1. Skapa en ny enhet i ESPHome och klistra in `example/spabad.yaml`.
   Komponenten hämtas automatiskt härifrån via `external_components`.
2. Lägg till `api_key`, `ota_password` och `fallback_password` under **Secrets**.

## Första flashningen (USB)
1. I ESPHome Device Builder: **spabad → Install → Manual download → Factory format**.
2. Öppna https://web.esphome.io i Chrome eller Edge, anslut D1 mini med USB,
   välj **Connect** och sedan **Install** med filen du laddade ner.
3. Efter det uppdateras den trådlöst (OTA) direkt från Device Builder.

Flasha innan kortet sitter i spabadet, och koppla inte USB och RJ45 samtidigt.

## Uppstart mot spabadet – gör i den här ordningen
1. **Bryt strömmen** till spabadet innan du kopplar.
2. Sätt `probe_setpoint: false` till att börja med så att inga knapptryck skickas.
3. Slå på och kontrollera i Home Assistant:
   - *Panelkontakt* = på
   - *Vattentemperatur* och *Display* stämmer med panelen
4. Testa knapptryck: *Belysning (tryck)*. Tänds ljuset fungerar skrivningen.
5. Sätt `probe_setpoint: true` igen och installera.

## Om något inte stämmer
- **Vattentemperaturen hoppar till måltemperaturen när du trycker Temp på panelen:**
  biten för inställningsläge stämmer inte för din panel. Standard är bit 40
  ("TempMenu") enligt micke85:s kartläggning. Titta på *Råbitar 29-71* medan
  panelen blinkar med måltemperaturen och se vilken bit som ändras. Som extra
  skydd räknas ett värde som syns före och efter en blinkning alltid som
  inställd temperatur, och vattentemperaturen godtas först efter 3 s utan blink.
- **Knapptryck missas ibland:** sätt `button_repeat: 2` eller `3`.
- **Panelkontakt växlar av och på:** troligen svag 5 V eller dålig kontakt i RJ45.

## Värmeläge
Väljaren *Värmeläge* (Standard, Economy, Sleep) trycker Temp upp (väcker panelen,
ändrar inte temperaturen) och sedan Mode/Prog, väntar tills panelen lämnat menyn
och kontrollerar läget mot bit 59/60. Det upprepas tills läget stämmer, högst 4 gånger.
Sleep tolkas som att varken standard- eller ekobiten är satt.

## Bitkarta (VL801D)
Enligt [micke85/Balboa-GS510DZ-with-panel-VL801D](https://github.com/micke85/Balboa-GS510DZ-with-panel-VL801D):
30 tidsmeny · 36/37 filter 1/2 · 39 värme · 40 inställd temp visas · 42 blåsmaskin ·
43 filtrering · 47 ljus · 48/49 pump 1/2 · 59 standardläge · 60 ekoläge · 65 Mode/Prog (1 = ej i menyn)

## Knappkoder (bit 72-75)
Mode 1000 · Pump 1 1001 · Pump 2 1010 · Ljus 1011 · Time 1100 · Blåsmaskin 1101 · Temp upp 1110 · Temp ner 1111

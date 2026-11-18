# Zadání pro Codex: společná revize a opravy ScratchMonkey / HVProg2 / AVRDUDE

**Datum zadání:** 18. 11. 2026  
**Jazyk komunikace a výstupů:** čeština  
**Režim práce:** nejprve pouze analýza; veškeré změny až po **výslovném souhlasu uživatele**.

## 0. Nejdůležitější pravidlo – VŽDY VYŽÁDAT SOUHLAS

Toto zadání je určeno pro Codex běžící lokálně ve VS Code / v prostředí s přístupem k místním repozitářům. **Zadání samo o sobě neopravňuje k žádné změně zdrojových souborů.**

1. Nejprve v **read-only režimu** zjisti aktuální stav místních repozitářů, porovnej jej se zde popsanými nálezy, prohlédni dostupná schémata a sepiš konkrétní plán oprav s názvy souborů a očekávanými dopady.
2. **Před každou samostatnou etapou úprav** (ScratchMonkey – kritické chyby, ScratchMonkey – další robustnost, AVRDUDE – XPROG, integrační testy / build) napiš uživateli, co přesně chceš změnit, v jakých souborech, jaký bude dopad a případná rizika, a polož přímou otázku **„Souhlasíš s provedením této etapy?“**. Bez jednoznačného „ano/souhlasím“ **nic neměň**. Souhlas s jednou etapou neplatí pro ostatní etapy.
3. **Před jakýmkoliv buildem či testem, který zapisuje do pracovního adresáře, vytváří artefakty nebo používá fyzický hardware**, rovněž vyžádej souhlas s konkrétní činností; neprováděj ji v rámci implicitního souhlasu s analýzou. Pasivní prohlížení souborů a `git status` je možné v analytické etapě.
4. **Nikdy automaticky** neprováděj `git commit`, `push`, `pull`, `merge`, `rebase`, `reset`, `clean`, změnu větví, `stash`, aktualizaci závislostí ani přepis uživatelských změn. Každou takovou operaci navrhni zvlášť a vyžádej výslovný souhlas. Existující rozpracované změny ponech beze změny.
5. **Nikdy bez samostatného explicitního souhlasu** nepřipojuj programátor k hardwaru, neměň napájení, nepouštěj chip erase, nezapisuj Flash/EEPROM, fuse ani lock bity. Pro vysokonapěťové režimy (HVSP/HVPP/TPI-HV) upozorni na riziko 12 V a nejprve požaduj kontrolu skutečného zapojení i napětí.
6. Po každé schválené etapě předlož diff, výsledky a zbývající rizika, **zastav se a požádej o souhlas před další etapou**. Pokud se ukáže nová chyba nebo bude třeba rozšířit schválený rozsah, opět se zeptej.

**Požadované chování v první odpovědi Codexu:** jen inventura, ověření zjištění a návrh nejbližší konkrétní etapy. Neprováděj opravy „rovnou“ ani na základě tohoto dokumentu.

## 1. Repozitáře a rozsah

Zpracuj dvě **místní** pracovní kopie (najdi je v aktuálním workspace; obvyklé umístění jako sousední adresáře):

- **Firmware:** `bales0/ScratchMonkey-orangkucing` – výchozí větev `master`, GitHub `https://github.com/bales0/ScratchMonkey-orangkucing`.
- **Host:** `bales0/avrdude` – výchozí větev `main`, GitHub `https://github.com/bales0/avrdude`.
- **Referenční upstream – pouze pro porovnání:** `https://github.com/microtherion/ScratchMonkey` (zejména ScratchMonkey 2.0, architektura HWIF, TPI). Dále `orangkucing/ScratchMonkey` a `avrdudes/avrdude`.

**Revize použitá při předchozí statické analýze:** firmware `84245cd01753…`, AVRDUDE `3c90247d55f6…`, Microtherion `6507105b8bec…`. Toto **nejsou předpoklady o aktuálním lokálním stavu**: nejprve zjisti místní `HEAD`, větve, `git status`, lokální diff a zda již některé nálezy nejsou opraveny. Pokud chybí lokální kopie či potřebný soubor, neprováděj automatický clone; upozorni uživatele.

**Cíl:** fungující kombinace AVRDUDE ↔ STK500v2/XPROG ↔ ScratchMonkey ↔ **HVProg2, řídicí ATmega32 / 7,3728 MHz** pro ISP, HVSP, HVPP, TPI (včetně HV) a PDI; bezpečné chování při odpojeném cíli; zachování stávajících režimů, srozumitelná hlášení chyb a důsledné regresní testy.

**Mimo rozsah bez dalšího souhlasu:** UPDI/JTAG, nové HW funkce, nové PCB, zásadní rewrite architektury, přepínání napájení hardwarem, zásahy do fuse řídicího ATmega32, automatický update upstreamu.

## 2. Než začneš měnit firmware – skutečné HW podklady

Ve firmware repozitáři jsou `specification/HVprog2.jpg` (HVProg2 rev. 1.4), `specification/PDI2.jpg` a `specification/TPI.jpg`. Přečti je a porovnej s `SMoConfig.h`, `platformio.ini` a implementací GPIO.

Z předběžné revize:

- Řídicí MCU je osazen **ATmega32** s externím **7,3728MHz** krystalem; staré označení `MEGA8535-P` ve schématu je generický symbol, nikoli doložení použitého MCU.
- V MightyCore standard pinoutu: `PB0–7=0–7`, `PD0–7=8–15`, `PC0–7=16–23`, `PA0–7=24–31`.
- Profil HVProg2 používá přibližně `SMO_HVRESET=10`, `SMO_HVENABLE=11`, `SMO_SVCC=13`, `SMO_AVCC=31`; ověř mapování a skutečné polarity **pro všechny** režimy, nikoli jen kompilaci.
- PDI z originálního návodu používá **3,3V provoz** a 510Ω rezistory. HVSP/HVPP používají podle schématu 4,5–5,5V režim. `SMoGeneral::gVoltage=33/50` je **hlášená hodnota**, nikoli důkaz, že HW fyzicky přepíná napájecí napětí.
- Zkontroluj generování 12V v MAX662A části, řízení přes `SMO_HVENABLE`, bezpečný stav RESETu, PWM/ADC regulaci VTARGET, Timer2/PDI heartbeat, konflikt SPI/port B při HVPP, směr PDI_DATA při přepínání MOSI/MISO, a používání ISP konektoru + JP2 (SELF/OUT).
- Neodvozuj elektrickou bezpečnost jen z komentářů. Pokud něco nelze jednoznačně ověřit bez měření, označ to jako **HW TEST REQUIRED**, nikoli jako ověřené.

Zkontroluj, že všechny změny jdou překládat pro konkrétní ATmega32; nepřenášej kód Microtherion pro Arduino Uno/Leonardo beze změn portů a časovačů.

## 3. ScratchMonkey – kritické chyby (samostatná schvalovaná etapa FW-1)

### FW-1A: HVPP čtení signatury / kalibrace – chybějící fyzický řídicí impuls

Soubor `SMoHVPP.cpp`, funkce `ReadSignatureCal()`; v prohlížené revizi se vykonával tento **neúčinný výraz**:

```cpp
HVPPControlPattern(kDone, byteSel);
```

To pouze vrací hodnotu tabulky; neprovede nastavení portu. Ověř správný vzor v ostatních čtecích operacích a Microtherion 2.0, proveď bezpečné dokončení cyklu (pravděpodobně `HVPPControls(HVPPControlPattern(kDone, byteSel));`). Především **ověř pořadí** nastavení směru dat, dokončovacího řídicího impulsu, odečtu a odeslání odpovědi, aby se skutečně korektně uvolnily řídicí linky. Přidej mock/regresní test aspoň pro sekvenci očekávaných stavů.

### FW-1B: PDI neomezené čekání na start bit a únik z chybového stavu

Soubor `SMoPDI.cpp`, `PDILoad()` obsahoval:

```cpp
while (data.c[1] == 0xFF)
    data.c[1] = SPI.transfer(0xFF);
```

Při odpojeném XMEGA smyčka může běžet navždy. Doplň **pevný deadline i ochranu proti chybnému/nekonečnému dekódování rámce**, korektní návrat chyby přes `PDILoad`, `PDILoadS`, `WaitUntilNVMActive`, `WaitWhileNVMControllerBusy`, `ReadMem`, `WriteMem`, `CRC`, `EnterProgmode`. Žádná vyšší funkce nesmí vrátit `XPRG_ERR_OK` po neúspěšné komunikaci.

U `SMoPDI::EnterProgmode()` při selhání zajisti **skutečný cleanup**: vypnutí PDI heartbeat a přerušení, návrat z dočasně vypnutého SPI do bezpečného stavu, uvolnění MOSI/PDI_DATA bez konfliktu výstupů, bezpečná poloha RESET a 12V generátoru (bez domněnek o jeho zapojení). Ověř bezchybné znovuotevření po timeoutu bez hard resetu zařízení.

Původní chyba `SMoPDI::WriteMem()` (`XPRG_Body[2]` místo `[1]` při timeoutu) je podle předchozí kontroly **již opravena**; ověř, nevracej tuto chybu zpět.

### FW-1C: ISP zápis – nekonečný value polling

`SMoISP.cpp`, `ProgramMemory()`, větev value polling:

```cpp
while (SPITransaction(...) == pollVal)
    ;
```

Přidej rozumnou, pro operaci a cílový čip odvozenou mez čekání s výstupem `STATUS_RDY_BSY_TOUT`, nikoli tiché předstírání úspěchu. Zkontroluj existující chování timed delay, RDY/BSY, page a byte mode. Nerozbíjej pomalé SPI / „limp mode“ a přepínání adres nad 64 kB.

### FW-1D: Validace STK500v2 požadavků i mimo XPROG

`SMoXPROG.cpp` již obsahuje `ValidRequest()` a omezení na 256B přenosy; zachovej. Doplň **před přístupem do bufferu** kontrolu délek `CMD_PROGRAM_*`, `CMD_READ_*`, fuse, signature, `CMD_SET_CONTROL_STACK`, `CMD_SPI_MULTI`, address a ostatních příkazů ISP/HVSP/HVPP. Vycházej ze skutečné STK500v2 specifikace a AVRDUDE formátu; buffer `SMoCommand::gBody` má 276 bajtů, příjem max. 275. Eliminuj 0-length/do-while přetečení, nadměrné čtení, zápis a přenos odpovědí větších než buffer. **Nekompatibilní/poškozené příkazy pouze odmítni, nikdy nesmějí způsobit napájecí nebo 12V akci.**

Zkontroluj `SMoCommand.cpp`: ochrana framování, timeout na částečný paket, kontrolní součet, velikost odpovědí a reset parseru. Část testů již existuje; rozšiř je.

## 4. ScratchMonkey – další robustnost (samostatná schvalovaná etapa FW-2)

### FW-2A: HVPP/HVSP completion a timeout

`SMoHVPP.cpp` a `SMoHVSP.cpp` na některých místech ignorují výsledek `HVPPPollWait()` / `HVSPPollWait()` u chip erase / zápisu fuse / lock. Komentáře správně upozorňují, že **některé starší AVR RDY/BSY nepodporují**. Neopravuj to slepým globálním timeoutem. Rozliš:

- čipy/operace používající RDY/BSY – správně reportuj timeout;
- čipy, pro které AVRDUDE posílá časové čekání bez spolehlivého RDY/BSY – dodrž datasheetem požadovanou prodlevu;
- při chybě bezpečně opustit HV režim podle sekvence předepsané cílovým čipem.

Prověř „stale success“ po ADC timeoutu (napětí nedosaženo → kód přesto pokračuje a vrátí OK). Změň to na **fail-safe** a před tím ověř polaritu, ADC škálování, PWM výstup, analogové usazení a 12V sekvenci podle schématu. Protože může jít o rizikové změny HV, nejdříve je popiš a žádej zvláštní schválení.

### FW-2B: TPI validace příjmu a časování

`SMoTPI.cpp` má již timeout v `TPILoad()` a čekání na `NVMEN`. Ověř jejich správnost při přetečení `millis()`, chybných start/stop bitech, paritě a rozpojeném cíli. Porovnej s Microtherion 2.0 (`SMoTPI.cpp` + `SMoHWIF_TPI.h`), který ověřuje identifikační registr `0x80`, paritu a bounded retry. Přenes **vhodné kontroly**, nikoli automaticky software-bitbang (u HVProg2 zachovej HW SPI, pokud je správné). Zkontroluj výpočet `sPageMask` (aktuálně `int8_t`), rozsahy zápisů, převod rámce 12 bitů, zápis/čtení fuse, TPI-HV 12V i low-voltage TPI. Testy musí kontrolovat špatnou paritu a opakované pokusy bez zatuhnutí.

### FW-2C: Režimy a sdílené periferie

Proveď audit přechodů `ISP↔TPI↔PDI↔HVSP↔HVPP` včetně chyby uprostřed příkazu. Zkontroluj uvolnění SPI/Timer2, přerušení, směrování MOSI, vyřazení 12V a bezpečný stav pinů před a po `LEAVE_PROGMODE`. Přednostně řeš snadno reprodukovatelné konflikty a přidej mock testy.

## 5. AVRDUDE – hostitelská kompatibilita (samostatná schvalovaná etapa HOST-1)

### Host – již přidané změny; nepřepisovat bez důkazu

V místní větvi `bales0/avrdude` pravděpodobně již jsou:

- `src/avrdude.conf.in`: `PM_TPI | PM_PDI | PM_ISP` pro `stk500v2`/`scratchmonkey`;
- `src/stk500v2.c`: `PGMTYPE_STK500` ve výběru XPROG (`initialize`, `enable`);
- `XPRG_PARAM_FLASHPAGESIZE = 0x05` (v `src/stk500v2_private.h`) a odeslání stránky Flash pro PDI;
- úprava TPI fuse/stránkových operací včetně `XPRG_ERASE_CONFIG`;
- obecná úprava seznamu programátorů `src/main.c`.

**Nezačínej znovu implementovat již hotové opravy.** Zjisti, zda jsou v lokální pracovní kopii, zda kompilují a odpovídají aktuálnímu firmware.

### HOST-1A: 512B stránky PDI – interakce s firmwarem

`stk600_xprog_paged_write()` posílá stránky nad 256 B po 256B blocích. V nalezené revizi přitom **v obou blocích** nastavoval `b[2] = writemode`, obsahující `XPRG_MEM_WRITE_WRITE`. Firmware `SMoPDI::WriteMem()` prováděl commit stránky při `numBytes == 0` **nebo** při hranici stránky. To naznačuje předčasný commit po prvních 256 B, zatímco druhá polovina teprve následuje.

Je nutné nejprve zrekonstruovat **přesnou semantiku** XPROG `WRITE_MEM` a XMEGA NVM page bufferu (příkazy erase/load/write). Poté zvol jednu koordinovanou opravu host + firmware, aby **512B stránka byla potvrzena přesně ve správný okamžik** (typicky až po jejím úplném sestavení), bez poškození dříve načtených dat. Prověř vedle 512 B také stránky 32/64/128/256 B, data různé délky, poslední částečnou stránku, přesah boot/application hranice a zápis bez chip erase. Pozor: současný simulátor paměti je jen byte-array a tuto chybu nenasimuluje; nutno přidat věrnější model stranového bufferu / commitů.

### HOST-1B: Nezasahovat do jiných STK500v2

Povolení XPROG pro **všechna** zařízení `PGMTYPE_STK500` může změnit chování běžných ne-HVProg2 STK500v2. Navrhni oddělený programátorský profil (např. `hvprog2`) nebo interní explicitní příznak XPROG podle identifikátoru programátoru, nikoli bezpodmínečný zásah do všech STK500. Zvaž zpětnou kompatibilitu s autorovým původním příkazem `-c stk500v2` a uživatelovými skripty: **před volbou API/CLI řešení se zeptej**, zda má zůstat alias `stk500v2` pro PDI, nebo bude preferován samostatný `-c hvprog2`.

**Nezaměň** `scratchmonkey` identifikátor v `avrdude.conf` za skutečný řetězec z `CMD_SIGN_ON`; firmware se historicky hlásí jako `STK500_2`. Ověř, jak lze dané zařízení bezpečně rozlišit. Nesmí vzniknout regrese originálního STK500v2, STK600 ani AVRISP mkII.

### HOST-1C: Chyby XPROG transportu a kompatibilita odpovědí

Porovnej konkrétní vysílané bajty/odpovědi:

- `CMD_XPROG_SETMODE`, `XPRG_CMD_ENTER/LEAVE_PROGMODE`;
- `XPRG_CMD_SET_PARAM` (`NVMBASE`, `EEPPAGESIZE`, `FLASHPAGESIZE`, TPI NVM registry);
- PDI signature/silicon revision, Flash/EEPROM/fuse/lock, chip/page erase, user signature, calibration, CRC;
- TPI fuse erase+write+verify, timeout návratové kódy;
- 256B limity XPROG a 275B STK500v2 body, page/writemode rozdělení, TPI 1B oblasti;
- rozdíly ve word/byte adresování a nad 64 kB, application/boot mapování.

Pokud host špatně dekóduje chyby firmware, oprav; při selhání nikdy neprezentuj úspěšný verify. **Neodstraň** obecné úpravy třetího projektu (např. `main.c`) bez věcného zdůvodnění a souhlasu.

## 6. Testy (samostatně schvalované etapy TEST-FW, TEST-HOST, TEST-HW)

### Existující testy – nejprve zkontrolovat, až pak rozšířit

ScratchMonkey obsahuje:

- `Tests/host/firmware_tests.cpp`, `Tests/host/run_tests.py` (mockované Arduino/SPI: parser timeout/CRC, XPROG validace, TPI/PDI timeout);
- `Tests/host/avrdude_tests.py` (reálný AVRDUDE proti síťovému STK500v2 simulátoru pro ISP/HVSP/HVPP, PDI flash write/verify, TPI fuse write);
- `platformio.ini`, `Tools/BuildAvrdude.ps1`, `Tools/Avrdude.ps1`.

**Testy popsané v dokumentaci nejsou důkaz, že nyní prošly.** Po svolení je skutečně spusť, uveď přesný příkaz, návratový kód, relevantní log a lokální commit.

### Rozšíření testů – minimum

1. **FW parser:** nulové/krátké/oversize pakety, poškozený checksum, výpadek během rámce, wrap `millis()`, zneužité délky `CMD_SPI_MULTI`, dlouhé čtení/zápis a kontrola bezpečného odmítnutí před GPIO/HV akcí.
2. **ISP:** signature, Flash/EEPROM read/write (model), fuse/lock, pomalé ISP, value/RDY polling timeout, ext. adresy, power/reset cleanup.
3. **HVSP/HVPP:** signatura a kalibrace včetně sequence kontrol řídicích portů, Flash/EEPROM/fuse/lock, chip erase, RDY/BSY i čipy bez něj, timeouty a cleanup.
4. **TPI:** 4/5/9/10 rodina, identity, parita, neplatná odpověď, 12V a LV vstup, fuse erase/read/write/verify, timeouty a obnova spojení.
5. **PDI:** neodpovídající zařízení, parita/stop bit, `ENTER/LEAVE`, všechny druhy pamětí, CRC, chip/page erase, ztracený ACK, různá page_size, **věrná simulace 512B bufferu včetně commit count**, zkrácený poslední blok, opakovaný zápis, data `0xFF` a bez chip erase.
6. **AVRDUDE:** integrační testy pro každý programátorský profil; regresi běžného STK500v2 (ISP), STK500 HVPP/HVSP, STK600 PDI, výběr TPI/PDI XPROG a rozlišení případného `hvprog2` aliasu.
7. **Statika/kompilace:** firmware build ATmega32 7,3728MHz s reálným MightyCore a kontrolou RAM/Flash, AVRDUDE build, varování kompilátoru; nic neschovávej `#pragma` jen kvůli zelenému buildu.

### Fyzické HW testy – pouze po zvláštním povolení

Bez fyzického souhlasu nedělat **vůbec nic** na reálném programátoru. Navrhni testovací matici pro: ATmega328P ISP/HVPP, ATtiny85 ISP/HVSP, ATtiny10 TPI, ATxmega128A1 PDI. Postupuj nejdříve neinvazivně: multimetr/osciloskop (VCC, 12V, SCK, RESET), pak identifikace a čtení signatur, dále backup pamětí/fuse, teprve schválené zápisy a verifikace. PDI test pouze se správně nastavenými 3,3 V a PDI adaptérem; HV režimy až po ověření bezpečného 5V napájení. Nikdy automaticky neprogramuj řídicí ATmega32.

## 7. Priorita a hranice dokončení

| Priorita | Co | Kdy považovat za vyřešené |
| --- | --- | --- |
| **P0** | HVPP chybějící commit řídicích signálů | Správný kód + unit test stavu portů |
| **P0** | PDI nekonečné čekání / cleanup | Všechna čekání omezená + odpojený target vrací chybu, režim lze znovu otevřít |
| **P0** | ISP value polling bez timeoutu | Omezený deadline, korektní STK500v2 status, regresní test |
| **P0** | PDI 512B stránky / host↔FW | Model stránkového bufferu + skutečný průběh host/firma + schválený fyzický test |
| **P1** | HVSP/HVPP timeouty / napětí | Bez regresí starých čipů, fail-safe, pravidla z datasheetů |
| **P1** | Validace všech rámců | Žádný OOB nebo 12V změna při invalid command |
| **P1** | TPI identita/parita | Chybné rámce hlásí chybu, žádné zatuhnutí |
| **P1** | AVRDUDE profil HVProg2 | Vlastní kompatibilita bez rozbití ostatních STK500v2 |
| **P2** | Čistá integrace Microtherion | Jen doložené opravy, žádný plošný rewrite |

## 8. Požadované výstupy od Codexu

Před každou etapou:

- Identifikace lokálního `HEAD`, pracovních změn a přesné zdrojové příčiny.
- Krátký seznam souborů a **konkrétní patch plan**.
- Rizika a kompatibilita: ISP/HVPP/HVSP/TPI/PDI, 3,3/5V, ATmega32.
- **Výslovná otázka na souhlas** a čekání na odpověď.

Po každé schválené etapě:

- Souhrn přesných změn s `git diff --stat` a odkazem na dotčené funkce.
- Co bylo ověřeno (kompilace/test), co nebylo a proč; bez předstírání hardwarových testů.
- Případné nové regresní testy a výsledek.
- Úpravy zůstávají **lokální, bez commitu a bez push**, dokud je uživatel zvlášť neschválí.
- Nejdříve požádej o souhlas, teprve pak pokračuj na další etapu.

## 9. První odpověď, kterou má Codex nyní poskytnout

> Provedu pouze čtení místních repozitářů ScratchMonkey a AVRDUDE, zjistím stav větví, nepotvrzené chyby a dostupná schémata HVProg2. Bez tvého souhlasu nebudu měnit žádný soubor, spouštět build, programovat čipy ani vytvářet commity. Potom ti předložím konkrétní návrh první etapy FW-1 a výslovně požádám o souhlas.

---

**Poznámka:** Nálezy uvedené výše vycházejí ze statické analýzy GitHub revizí dne 18. 11. 2026. Jsou výchozími hypotézami ke kontrole aktuální lokální kopie, nikoli tvrzením, že dané chyby stále existují nebo že některý režim prošel fyzickým testem.

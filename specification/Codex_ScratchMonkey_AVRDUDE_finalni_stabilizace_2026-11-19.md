# Zadání pro Codex – finální stabilizace ScratchMonkey / AVRDUDE

**Datum:** 19. 11. 2026  
**Stav:** Rozsah výslovně schválen uživatelem  
**Režim práce:** cílená implementace a verifikace; žádné nevyžádané změny funkčních programovacích sekvencí

## 0. Hlavní cíl

Dokončit stabilizaci programátoru HVProg2 se ScratchMonkey a odpovídajícího forku AVRDUDE. Zachovat již funkční ISP, HVSP a HVPP (včetně **reálně ověřeného HVSP ATtiny13**), ověřit a případně opravit TPI/PDI, zlepšit bezpečné ukončování aktivní HV relace a zajistit **zpětnou kompatibilitu standardních režimů s veřejným AVRDUDE**. Vše doložit testy a dokumentací.

**Zásadní rozhodnutí:** Předchozí nefunkčnost HVSP na ATtiny13 způsobovalo **chybné fyzické připojení**, nikoli prokázaná chyba časování firmware. Po opravě zapojení HVSP funguje. **NEMĚNIT vstupní/výstupní sekvenci HVSP, její časování, mapování GPIO, hodnoty resetDelay/stabDelay ani 32B control stack**, pokud se nově neprokáže konkrétní reprodukovatelná chyba. Datasheetová hypotéza bez závady není důvod k opravě.

## 1. Dotčené projekty a odpovědnost

### Projekt A – firmware (primární)
- **Repozitář:** https://github.com/bales0/ScratchMonkey-orangkucing
- **Výchozí ověřovaná revize:** při poslední analýze `c04c7e463645c6db5fc309ec5296c125aed2dd49` (18. 11. 2026). **Před úpravami načti aktuální HEAD a diff proti tomuto základu; pracuj s aktuálním stavem**, ne s automatickým předpokladem, že HEAD je stále stejný.
- Rozsah: ISP/HVSP/HVPP regresní ochrana, bezpečné ukončení aktivního HV režimu, TPI/PDI validace a opravy jen dle důkazů, testy, dokumentace.

### Projekt B – hostitel AVRDUDE (navazující)
- **Repozitář:** https://github.com/bales0/avrdude
- **Výchozí ověřovaná revize:** při poslední analýze `cd794b01819b3f3b77614ff824a08dd2f7e884c7` (18. 11. 2026). Načti aktuální HEAD.
- Rozsah: detekce XPROG, správnost odpovědí a zápisu PDI/TPI, zpětná kompatibilita standardních profilů, regresní testy, dokumentace.

### Referenční projekty – pouze pro čtení, **neměnit**
- Upstream AVRDUDE: https://github.com/avrdudes/avrdude
- Původní ScratchMonkey 2.x: https://github.com/microtherion/ScratchMonkey
- Původní HVProg2 fork: https://github.com/orangkucing/ScratchMonkey
- Microtherionův historický patch: https://github.com/microtherion/ScratchMonkey/blob/master/avrdude.patch
- Orangkucingův historický patch AVRDUDE: https://gist.github.com/orangkucing/fe633b955be9306859b7

Všechny změny připrav **odděleně pro projekt A a B**, s jasným popisem závislostí. Nenahrávej commity a neprováděj zápis na reálné cílové čipy bez samostatného výslovného pokynu, pokud práce probíhá v prostředí, které takové operace dovoluje.

## 2. Výslovně schválený rozsah (a nic navíc)

1. **Zachování funkčních ISP, HVSP a HVPP.**
2. **Bezpečnostní ukončení HV relace.**
3. **Finální ověření TPI a PDI.**
4. **Kompatibilita s veřejným AVRDUDE.**
5. **Dokumentace a předávací testy.**

### Mimo rozsah – nepřidávat

- **Univerzální build / nové PlatformIO prostředí pro ATmega16, ATmega32 a další MCU, rozšiřování podporovaných procesorů, HW abstrakce kvůli dalším MCU.** Tato oblast byla uživatelem výslovně vynechána. Existující build a testy je možné *použít pro verifikaci*, nikoli konfiguračně rozšiřovat.
- Preventivní refaktoring funkčních ISP/HVSP/HVPP sekvencí a portů.
- Změny zapojení HV_PROG, nové mapování signálů, změny fuse cílového MCU bez specifického důkazu.
- Změny napájecího návrhu, bootloaderu, přidávání nových programovacích protokolů, rozšiřování podpory čipů, diagnostických výstupů nesouvisejících s přejímkou.
- Automatické flashování firmware, zápis fuse, chip erase nebo jiné destruktivní operace v rámci běžného sestavení/testu.

## 3. Nezměnitelné podmínky kompatibility

### 3.1 Standardní veřejný AVRDUDE

Na firmwaru HVProg2 musí zůstat podporovány standardní cesty:

| Režim | Standardní AVRDUDE profil | Nutné chování |
| --- | --- | --- |
| ISP | `-c stk500v2` | Čtení signatury, erase, program/read/verify dle cíle |
| HVSP | `-c stk500hvsp` | Beze změny časování a control stack; ATtiny13 zůstává funkční |
| HVPP | `-c stk500pp` | Beze změny standardní sady příkazů |

Historické aliasy (`scratchmonkey`, `scratchmonkey_hvsp`, `scratchmonkey_pp`) nesmí začít vyžadovat nestandardní firmware handshake pro standardní funkce. Identifikační text `STK500_2` ve firmwaru HVProg2 zachovat, pokud existuje; **nepřepínat na `SCRATCHMONKEY` jen kvůli XPROG**.

Podpora TPI/PDI přes obecný profil `stk500v2` v upravené větvi AVRDUDE je rozšíření. **Nesmí ovlivnit ISP/HVSP/HVPP v upstream AVRDUDE.** Neprohlašovat TPI/PDI na standardním veřejném AVRDUDE za funkční jen proto, že existují aliasy.

### 3.2 Původní implementace a jejich rozdíl

- Microtherionův patch rozpoznával `SCRATCHMONKEY`, doplňoval stavové LED a XPROG pro speciální interní typ ScratchMonkey.
- Orangkucingův patch aktivoval XPROG také pro obecný STK500, protože HVProg2 se hlásí `STK500_2`, a předával parametry pro PDI; to ale předpokládalo podporu XPROG i u jiných STK500.
- Současný fork `bales0/avrdude` místo toho používá **podmíněnou detekci** `CMD_XPROG_SETMODE` pouze při požadavku na TPI/PDI. Toto chování **zachovat a regresně ověřit**.

Požadavek: nevracet bezpodmínečné povolení XPROG pro všechny `PGMTYPE_STK500`, nevytvářet závislost na identifikačním řetězci, pokud lze schopnost bezpečně ověřit existujícím příkazem.

## 4. Pracovní postup Codexu – povinné fáze

### Fáze 0 – nejprve audit aktuálního HEAD

1. Zaznamenej commit, pracovní větev, čistotu pracovního stromu a přesné verze použitých nástrojů pro oba repozitáře.
2. Projdi nové commity od výše uvedených revizí. Nepřepisuj již hotové a správné opravy.
3. Vypiš krátkou matici **existující implementace / existující test / chybějící test / skutečná chyba**.
4. Při neshodě mezi komentářem, dokumentací a zdrojovým kódem považuj skutečný kód za základ pro analýzu a nejasnost zaznamenej.
5. Pokud je navržen zásah do fungující časovací sekvence HVSP, **neimplementuj jej bez nového důkazu o chybě**; popiš pouze případné riziko a měření, které by ho ověřilo.

### Fáze 1 – ISP/HVSP/HVPP: regresní ochrana, ne redesign

**Firmware A**

- Přezkoumej `SMoISP.cpp`, `SMoHVSP.cpp`, `SMoHVPP.cpp`, `SMoSafety.h`, `SMoCommand.cpp`, `SMoValidation.cpp`, volání `SPI.begin/end`, timerů a řízení GPIO.
- Ověř, že opakovaný `ENTER_PROGMODE` po `CHIP_ERASE` neuvede periferie do konfliktního stavu.
- Ověř `LEAVE_PROGMODE`, uvolnění datového busu, deaktivaci 12 V a resetu, obnovu SPI, chybové návraty a opakované pokusy po chybě.
- Přidej/rozšiř **nedestruktivní host-side simulační testy**: standardní vstup/výstup ISP, HVSP ATtiny13, HVPP, čtení signatury, práce s control stack, fuse/lock read, abort/retry. Nepřidávej test, který jen uměle vynucuje alternativní vstupní časování HVSP.
- Pokud existující testy již něco pokrývají, rozšiř pouze chybějící scénáře.
- Nezměnit mapování SCI/SDI/SII/SDO a elektrické polarity pro HVProg2.

**Host B**

- Přes skutečný AVRDUDE ověř, že profily `stk500v2`, `stk500hvsp`, `stk500pp` stále generují správnou rodinu příkazů.
- Ověř i aliasy, ale **alias nepovažuj za indikátor rozšíření XPROG**.
- Porovnej chování proti veřejnému upstreamu, bez přenášení dalších nepoptaných změn.

### Fáze 2 – bezpečné ukončení HV relace

**Firmware A**

- Proveď audit skutečně dosažitelných stavů: připojení 12V generátoru, aplikace 12V RESET, target VCC, aktivní HVSP/HVPP a přechody mezi nimi. Ověř bezpečný stav po chybě ADC, timeoutu, neplatném paketu, ztrátě linky, neúspěšném pollingu a `LEAVE_PROGMODE`.
- Zvlášť prověř stav **po úspěšném ENTER, když host přestane posílat příkazy**. Pokud firmware může ponechat HV aktivní bez omezení, doplň watchdog/lease aktivní programovací relace, případně jinou minimální bezpečnou ochranu.
- Watchdog nesmí vypršet během *platné dlouhotrvající* operace. Použij měřený/odůvodněný limit, zohledni blokující operace a možnost obnovení při příchozích úplných platných příkazech. Nepoužívej agresivní timeout 100 ms pro celou programovací relaci jen proto, že `DEFAULTTIMEOUT` má tuto hodnotu.
- Při expiraci provést bezpečný **shutdown HV → bezpečný RESET → target VCC podle HW zapojení → uvolnění pinů** ve správném pořadí. Zkontroluj, že `SMoAbortHV()` nepůsobí glitch nebo nežádoucí souběh se SPI/TPI/PDI.
- Nedovolit náhodné vstupy do aktivního HV stavu přes poškozený/částečný rámec; stávající parser a `ValidRequest()` využít.
- Přidat simulovaný test „ENTER → host zmizí → vše bezpečně vypnuto → nové spojení obnoví provoz“ a test normálního dlouhého přenosu bez falešného vypnutí.
- Při nejasném bezpečném pořadí signálů se řídit schématem `specification/HVprog2.jpg`, nikoliv obecnou deskou STK500.

**Host B**

- Při chybě odeslat standardní LEAVE, pokud je komunikace ještě funkční, ale nespoléhat se na něj jako na jedinou bezpečnostní ochranu. Zamezit falešnému oznámení úspěchu po ztraceném ACK.

### Fáze 3 – TPI/PDI: finální ověření a pouze doložené opravy

**Firmware A – TPI**

- Prověř `SMoTPI.cpp`, `SMoSerialFrame.h`, směrové přepnutí datové linky, guard clocks, start/data/parity/stop bity, timeouty, převod chyb (`XPRG_ERR_*`), uvolnění periferií.
- Ověř `ENTER/LEAVE`, chip erase, konfigurační erase před fuse, fuse write/readback, Flash write/readback, přerušení během komunikace a další otevření bez restartu MCU programátoru.
- Prověř variantu low-voltage i high-voltage TPI, **pouze pokud jsou již deklarovány jako podporované**; nerozšiřuj rozsah protokolů.

**Firmware A – PDI**

- Prověř `SMoPDI.cpp`, `SMoXPROG.cpp`: 32/64/128/256/512B stránky, průchod přes hranici stránky, 256B transportní fragmenty 512B stránky, zachování NVM bufferu mezi přenosy, jediný commit v závěrečném fragmentu, EEPROM, aplikační/boot Flash, user signature a fuse/lock.
- Ověř `XPRG_PARAM_FLASHPAGESIZE`, adresy přes 64 KB, práci se 32bit adresami, partial-page data, zarovnání a práci při neúplném posledním fragmentu.
- Ověř čtení, write/verify, CRC a korektní timeout/chybový status, když cíl neodpovídá či se odpojí. Pozor na datový buffer RAM a ohraničení požadavků.
- Výslovně prokaž, že **pouhé načtení prvního 256B fragmentu 512B stránky nezapíše Flash**.

**Host B – XPROG**

- Přezkoumej změny v `src/stk500v2.c`, zejména `stk500v2_enable`, `stk500v2_initialize`, `stk500v2_command`, XPROG program-enable, paged write/read a fuse erase.
- Potvrď, že `CMD_XPROG_SETMODE` se při použití standardního sériového STK500 vyvolá jen pro TPI/PDI cíle a jeho odmítnutí vede k jasné chybě, nikoli k nechtěnému ISP fallbacku či falešnému úspěchu.
- Nedovolit replay možná dokončeného **erase/write** po ztrátě ACK. Ověřit správnou velikost odpovědí na různých transportech (serial STK500, STK600 USB, AVRISP mkII a případné JTAG wrappery).
- Zajistit přesné předání velikosti stránky do firmware a kompatibilní fragmentaci 512B zápisů.
- Ověřit vzájemnou shodu XPROG request/reply pro TPI a PDI, včetně všech statusů a limitu paketů.
- Neprovádět změny generického AVRDUDE, pokud je lze vyřešit lokálně ve větvi STK500v2/XPROG.

### Fáze 4 – kompatibilita s veřejným AVRDUDE

Testovací matice musí pokrývat alespoň:

| Host | Programátor | Cíl/režim | Očekávání |
| --- | --- | --- | --- |
| Veřejný AVRDUDE | HVProg2 firmware A | ISP | Standardní příkazy, úspěšná kompatibilita |
| Veřejný AVRDUDE | HVProg2 firmware A | HVSP (ATtiny13) | Funkční read/signature, sekvenci neměnit |
| Veřejný AVRDUDE | HVProg2 firmware A | HVPP | Standardní kompatibilita |
| Upravený AVRDUDE B | HVProg2 firmware A | ISP/HVSP/HVPP | Stejné funkční výsledky jako veřejný AVRDUDE |
| Upravený AVRDUDE B | HVProg2 firmware A | TPI | Detekce XPROG, funkční operace |
| Upravený AVRDUDE B | HVProg2 firmware A | PDI | Detekce XPROG, korektní paged write/read/verify |
| Upravený AVRDUDE B | standardní STK500v2 bez XPROG | ISP | Nesmí se změnit chování |
| Upravený AVRDUDE B | standardní STK500v2 bez XPROG | TPI/PDI | Jasné odmítnutí bez falešného úspěchu |

Kde není reálný HW, proveď **protokolovou simulaci** a ve výsledku jednoznačně uveď „SIMULOVÁNO / REÁLNĚ NEOVĚŘENO“. Nikdy nezaměňuj statickou kontrolu s fyzickým testem.

Zvláštní regresní případy:
- Chybné/opakované/krátké XPROG odpovědi a výpadek transportu.
- Standardní `-c stk500v2` pro ATtiny13 nesmí vyvolávat HVSP ani XPROG.
- Standardní `-c stk500hvsp` pro ATtiny13 musí používat správnou HVSP cestu bez XPROG.
- `scratchmonkey*` aliasy nesmějí měnit signály nebo tajně aktivovat jiný protokol.
- Žádná změna HW sign-on `STK500_2` jen kvůli navázání TPI/PDI.

### Fáze 5 – dokumentace a předávací testy

V obou repozitářích doplň nebo aktualizuj stručnou dokumentaci:

1. Co lze spustit veřejným AVRDUDE a co vyžaduje fork B.
2. Jaké profily `-c` použít pro ISP/HVSP/HVPP/TPI/PDI.
3. Které režimy jsou prokázané fyzickým testem a které jen simulací.
4. Význam historických aliasů ScratchMonkey a rozdíl proti `CMD_XPROG_SETMODE` capability probe.
5. Bezpečný postup ukončení relace a chování při odpojení hosta.
6. Přehled provedených oprav a nedořešených omezení.
7. Stručný popis správného HVSP zapojení ATtiny13 (SCI/SDI/SII/SDO/VCC/RESET/GND), včetně upozornění na zrcadlení skutečného IDC konektoru oproti historickému obrázku; čísla konektoru musí odpovídat **ověřenému schématu**, nikoli domněnce.

Předávací dokumentace nemá slibovat kompletní podporu všech MCU ani nově přidávat build profily. Formulace „ATmega16/ATmega32 a další“ v předchozích debatách není součástí tohoto schváleného zadání.

## 5. Akceptační kritéria

Úkol je hotový teprve pokud:

- [ ] Byly zkontrolovány aktuální HEAD obou projektů a změny jsou minimální, věcně zdůvodněné.
- [ ] ISP, HVSP ATtiny13 a HVPP neobsahují neodůvodněné změny sekvence, časování nebo mapování.
- [ ] Je doložena kompatibilita standardních režimů s veřejným AVRDUDE (reálná nebo simulovaná – přesně označeno).
- [ ] Aktivní HV relace se umí bezpečně ukončit také po ztrátě komunikace, bez falešných timeoutů při standardní práci.
- [ ] TPI rámcování, časování, fuse a recovery prošly odpovídajícími host-side testy; HW testy jsou explicitně odděleny.
- [ ] PDI zápisy stránek 32–512 B, zvlášť 512B fragmentace, erase/commit/read/verify prošly odpovídajícími testy.
- [ ] Upravený AVRDUDE aktivuje XPROG jen po úspěšném ověření schopnosti a jen pro TPI/PDI.
- [ ] Standardní STK500v2 bez XPROG stále funguje v ISP a TPI/PDI odmítá bezpečně.
- [ ] Neproběhl automatický hardware flash/erase/fuse write; případné HW kroky jsou pouze připravený manuální test.
- [ ] Finální report popisuje každý změněný soubor, důvod, test a známé omezení.

## 6. Povinný formát výstupu Codexu

Codex má předat:

1. **Audit:** aktuální SHA obou HEAD, stručné odchylky od předpokládaných výchozích revizí.
2. **Změny firmware A:** seznam souborů a vysvětlení s důkazem, proč byly nutné.
3. **Změny AVRDUDE B:** seznam souborů a vysvětlení dopadů na upstream kompatibilitu.
4. **Test matrix:** příkaz, režim, očekávaný výsledek, skutečný výsledek, PASS/FAIL/SKIP, `SIMULATED` vs. `HARDWARE`.
5. **Zbývající rizika:** zejména HV fallback, délka timeoutů, chování na odpojeném HW, TPI/PDI na fyzických čipech.
6. **Doporučení před vydáním:** pouze konkrétní blokující závady a manuální HW přejímka, nikoli obecné nápady na nový vývoj.

**Než Codex přistoupí k jakékoli nevyhnutelné změně fungující HVSP vstupní sekvence, ať ji výslovně označí jako blokovanou novým souhlasem uživatele, s popisem důkazu o chybě a rollback plánem.**

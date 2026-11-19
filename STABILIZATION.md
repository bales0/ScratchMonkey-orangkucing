# Finální stabilizace HVprog2 – 19. 11. 2026

## Audit a rozsah

Firmware A: větev `master`, HEAD `c04c7e463645c6db5fc309ec5296c125aed2dd49`.
AVRDUDE B: větev `main`, HEAD `cd794b01819b3f3b77614ff824a08dd2f7e884c7`.
Oba HEAD odpovídaly výchozím revizím zadání, nepřibyly žádné commity.
Na začátku byl B čistý; A obsahoval uživatelské odstranění starého zadání a
nové zadání ve `specification/`. Tyto uživatelské změny nebyly upravovány.
Změny této stabilizace jsou pracovní diff, bez commitu/push a bez nové větve.
Referenční repozitáře nebyly měněny. `platformio.ini` zůstal beze změny:
ATmega32, externí 7,3728 MHz, MightyCore standard pinout.

| Oblast | Existovalo | Chybělo / zjištěná chyba | Výsledek |
|---|---|---|---|
| ISP/HVSP/HVPP | Validace, timeouty, čtení a základní regrese | Přímá kompatibilita veřejného klienta se skutečným FW v simulaci | Doplněn SPI/GPIO model a veřejný klient |
| HV relace | Explicitní LEAVE a abort při chybě | Neomezeně aktivní HV po zmizení hosta | Přidán 30s idle lease |
| Parser po expiraci | Timeout neúplného rámce 100 ms | Čerstvá neúplná hlavička mohla pohltit první rámec nového hosta | Reset parseru při expiraci; test nejprve selhal a po opravě prošel |
| TPI/PDI | Rámcování, deadline, NVM buffer a 512B fragmentace | Erase model, přerušení neúplné stránky, průkaz nezapsané první poloviny | Rozšířeny testy; další změna TPI/PDI sekvence nebyla potřebná |
| Host transport | XPROG probe, kontrola odpovědí, zákaz replay | JTAG clamp zahazoval poslední bajt odpovědi | Dva výpočty v backendu B opraveny po selhání testu |
| Dokumentace | Historické instrukce a Arduino manuál | Zastaralé velikosti a tvrzení, že host nebyl ověřen | Aktualizováno; historický Arduino obrázek není podklad HVprog2 |

## Použité nástroje

- PlatformIO Core 6.2.0, jeho Python 3.11.7.
- Platform Atmel AVR 5.1.0, MightyCore 3.0.2, AVR GCC 7.3.0.
- Python testů 3.14.5; MSYS2 UCRT64 GCC/G++ 16.2.0, revize balíčku 3.
- Build AVRDUDE: MSYS2 CMake 4.4.3, Ninja 1.13.2, Bison 3.8.2, Flex 2.6.4.
- Veřejný klient: [AVRDUDE 8.3, oficiální Windows x64 release](https://github.com/avrdudes/avrdude/releases/tag/v8.3).
  ZIP SHA-256 `8630c7d8bb9682a1685aaa9ad42550ae1131c9d13528f0d20975368ba615e9b0`
  byl ověřen proti digestu oficiálního GitHub release. Rozbalen pouze do
  `.pio/tools/avrdude-public-8.3/release/`.
- Fork B hlásí `8.2-20261118 (cd794b01)`; nová oprava je v jeho pracovním diffu.
  Číslo verze ani protokolový údaj FW `2.10` neidentifikují všechny pracovní změny.

## Změněné soubory A

| Soubor | Důvod a ověření |
|---|---|
| `SMoSession.h`, `SMoSession.cpp` | Nový 30s idle lease; test HVSP/HVPP/TPI, wrap, pořadí shutdown, keepalive, dlouhá operace a reconnect |
| `SMoCommand.h`, `SMoCommand.cpp` | Obnova lease po úspěšné odpovědi; rozlišení statusu XPROG; reset rozpracovaného parseru při expiraci |
| `ScratchMonkey.ino` | Kontrola expirace mezi příkazy, bez změny jejich sekvencí |
| `SMoXPROG.h`, `SMoXPROG.cpp` | Zjištění aktivního TPI/HV a oddělení resetu interního stavu od manipulace s piny |
| `Tests/host/protocol_tests.cpp` | Regrese lease, realistické erase a NVM buffer modely, standardní GPIO/SPI cíle pro veřejný klient |
| `Tests/host/mocks/Arduino.h` | Sledování GPIO/ADC a modelování přenosů při testu; pouze host-side mock |
| `Tests/host/run_tests.py` | Překlad nového modulu relace do existujícího nativního testu |
| `Tests/host/avrdude_tests.py` | Ohraničený bridge, stavy FAILED/COLLISION/TIMEOUT, duplikovaný ACK, LEAVE při chybě a odmítnutí TPI bez XPROG |
| `Tests/host/acceptance_tests.py` | Veřejný AVRDUDE a fork proti skutečnému přeloženému FW; strojová matice a porovnání celých standardních paketů |
| `README.md`, `DEVELOPMENT.md`, `STABILIZATION.md` | Aktuální přejímka, oprava zastaralých údajů, profily, zapojení a omezení |

`SMoHVSP.cpp`, `SMoHVPP.cpp`, `SMoISP.cpp`, `SMoConfig.h`, `SMoSafety.h`,
`SMoGeneral.cpp` ani `platformio.ini` se v této etapě nemění. Sign-on je stále
`STK500_2`. Vstupní/výstupní HVSP sekvence, zpoždění a mapování zůstaly stejné.
Test porovnává i celé pakety ENTER/LEAVE a 32B control stack veřejného klienta a forku.

## Změny B a historické patche

Viz [samostatný report B](../avrdude/HVPROG2.md).
V `src/stk500v2.c` se mění pouze dva clampy v přijímačích JTAG:
`rv = maxsize + 1` zahrne obálku; následující kopie `rv - 1` zachová všechna data.
Nezměněná verze selhala v testu delší/padded odpovědi, opravená prošla.
Přidané `tests/stk500v2_transport.c` a `tests/run_stk500v2_tests.py` testují skutečný
backend s virtuálními IO poskytovateli. Standardní USB/JTAG hardware není tímto testován.

[Microtherionův patch](https://github.com/microtherion/ScratchMonkey/blob/master/avrdude.patch)
vázal XPROG a LED na sign-on `SCRATCHMONKEY` a interní typ programátoru.
[Orangkucingův patch](https://gist.github.com/orangkucing/fe633b955be9306859b7)
zahrnul obecný STK500 a doplnil parametr stránky Flash. Nynější B zachovává
podmíněný probe `CMD_XPROG_SETMODE` pouze pro požadavek TPI/PDI. ISP ani HVSP/HVPP
jej neposílají. Odmítnutý probe ukončí inicializaci bez ISP fallbacku.

## Profily a kompatibilita

| Režim | Profil `-c` | Veřejný AVRDUDE 8.3 | Fork B |
|---|---|---|---|
| ISP | `stk500v2` | Standardní cesta ověřena simulací | Stejná standardní cesta |
| HVSP | `stk500hvsp` | ATtiny13, signatura/fuse/lock/kalibrace ověřeny simulací | Stejné pakety a výsledky |
| HVPP | `stk500pp` | ATmega32, signatura/fuse/lock/kalibrace ověřeny simulací | Stejné pakety a výsledky |
| TPI | `stk500v2` | Toto HVprog2 rozšíření není deklarováno jako podporované | Probe + XPROG, ověřeno simulací |
| PDI | `stk500v2` | Toto HVprog2 rozšíření není deklarováno jako podporované | Probe + XPROG, ověřeno simulací |

Aliasy `scratchmonkey`, `scratchmonkey_hvsp`, `scratchmonkey_pp` označují stejné
profily. Nejsou důkazem schopnosti XPROG a nevyžadují přejmenování FW sign-on.
U TPI se LV/HV varianta volí existujícím adaptérem dle `specification/TPI.jpg`:
JP1 používá RESET5V a napájení 5V, JP2 RESET12V a VTARGET. Nejde o nový profil.
Stávající TPI firmware aktivuje generátor i při použití LV konektoru; rozhodující
je správně oddělená větev RESET adaptéru. PDI vyžaduje ověřených 3,3 V a správný
adaptér dle `specification/PDI2.jpg`; hlášené `gVoltage` není měření ani přepínač napájení.

## Chování HV lease

Po úspěšné odpovědi v aktivním HVSP/HVPP nebo TPI/HV začíná 30 000 ms nečinnosti.
Limit je šestinásobek běžného 5s receive timeoutu hosta a výrazně převyšuje dobu
přenosu maximálního 275B těla při 115200 baud (přibližně 24 ms).
Počítá se doba mezi příkazy; uvnitř blokující operace se expirace nekontroluje.
Po dokončení úspěšné operace se začne počítat znovu. Test simuluje platnou
64s operaci bez falešného vypnutí. Při chybě fungují původní abort/LEAVE cesty.

Neplatné, neúplné a checksum-error rámce lease neobnovují. Při expiraci:

1. PD3/`SMO_HVENABLE` LOW vypne generování 12 V přes T2 a SHDN MAX662.
2. PD2/`SMO_HVRESET` HIGH sepne T1 a stáhne PROGRST/RESET k zemi.
3. PWM/`SMO_SVCC` = 0 vypne VTARGET.
4. Zastaví se Timer2/SPI a uvolní datové/řídicí piny; RESET zůstává bezpečně stažen.
5. Vymaže se stav relace a neúplného parseru. Nový platný ENTER obnoví provoz.

Toto pořadí vychází z `specification/HVprog2.jpg` rev. 1.4, T1/T2, R9 a C8.
T1 zůstane sepnutý, takže zbytkový náboj C8 nemá být uvolněn na RESET.
Nejde o změnu standardního HVSP LEAVE ani o hardwarový watchdog. PDI a ISP
nemají aktivní 12V relaci a tento idle lease je neruší. Interaktivní HV práce
s pauzou delší než 30 s vyžaduje nový ENTER, obvykle znovuotevření klienta.

`SMoAbortHV()` zůstává beze změny: jeho dosažitelné cesty v HVPP/HVSP nemají
aktivní SPI/PDI heartbeat; v TPI jej ADC chyba volá před spuštěním SPI. Aktivní
PDI nelze přepnout do HV sekvence bez ukončení relace díky `CheckSession()`.
Nový expiry shutdown zastavuje timer/SPI před uvolněním sběrnice.

## Testy a výsledky

V této etapě **nebyl otevřen fyzický COM port ani proveden HW flash/erase/fuse write**.
Veškeré zápisy/erase uvedené v testech proběhly v simulačních modelech.

| Příkaz z kořene A | Režim/model | Očekáváno / skutečnost | Výsledek |
|---|---|---|---|
| `python Tests/host/run_tests.py` | Parser, ISP/HVSP/HVPP/TPI/PDI, fake millis a GPIO/SPI | Korektní data/status, timeout, shutdown a recovery | PASS, SIMULATED |
| `python Tests/host/acceptance_tests.py` | Veřejný 8.3 + B, 6 standardních profilů/aliasů proti FW | Signatura/fuse/lock/2 kalibrační bajty; bez XPROG; shodné pakety | PASS, SIMULATED |
| `python Tests/host/avrdude_tests.py` | Reálný B přes loopback, protokol/NVM i přeložený FW | PDI 32–512 B, fragmentace, app/boot, TPI fuse, chybové odpovědi | PASS, SIMULATED |
| `python ../avrdude/tests/run_stk500v2_tests.py --build-dir .pio/tools/avrdude/build --output-dir .pio/stabilization` | Skutečný backend B, virtuální serial/USB/JTAG IO | Délky, všechny XPROG chyby, nepřehrání erase/write, padded JTAG data | PASS, SIMULATED |
| `$env:PLATFORMIO_BUILD_DIR='.pio/build-stabilization'; pio run -e hvprog2 -j 1` | Stávající ATmega32 profil | Přeložit a vejít se do pamětí | PASS, BUILD |
| `.\Tools\BuildAvrdude.ps1` | Stávající lokální B, MSYS2 | Sestavit a lokálně instalovat | PASS, BUILD |
| Manuální HW přejímka nového HEX | Reálné ISP/HVSP/HVPP/TPI/PDI a napětí | Vyžaduje vlastní testy a měření | SKIP, HARDWARE / REÁLNĚ NEOVĚŘENO |

Přesné argv, profily, očekávaný a skutečný exit code jednotlivých případů jsou
v `.pio/stabilization/compatibility-matrix.json` a `xprog-matrix.json`. Logy:
`firmware-tests.log`, `compatibility-tests.log`, `xprog-tests.log`, `transport-tests.log`.
Standardní testy komunikují až se skutečným přeloženým parserem a handlery FW;
modelují fyzické GPIO/SPI, nikoli jen odpovědi STK500 serveru.

PDI test výslovně načte první 256B fragment, přečte nezměněnou Flash (samé FF),
zkontroluje zachování RAM page bufferu a nula commitů. Až poslední fragment
potvrdí celou stránku jednou. Testuje se i přerušený buffer, nové otevření,
překročení stránky, adresa nad 64 KiB, částečný konec, EEPROM erase/write,
fuse/lock, user signature, CRC formát a nedostupný cíl.
TPI model odmítá změnu bitu 0→1 bez erase a ověřuje config erase i chip erase.
CRC test ověřuje příkaz, status a pořadí 24bit výsledku; není nezávislým výpočtem
hardwarového CRC algoritmu.

Historie HW: dřívější ISP čtení ATtiny13 na COM7 načetlo `1E 90 07`, lfuse `6A`,
hfuse/lock `FF`, kalibraci `57 54`. Uživatel potvrdil fungující HVSP po opravě
zapojení. Původní chybná signatura HVSP není důkazem chyby jeho časování.
Tato historie nenahrazuje HW přejímku nového HEX s lease.

## Správné HVSP zapojení ATtiny13

Elektrická čísla konektoru **HV_PROG 2×10** dle `specification/HVprog2.jpg`:

| Signál | HV_PROG pin | Řadič ATmega32 / standard Arduino číslo | ATtiny13 DIP8 pin |
|---|---:|---|---:|
| SDI | 1, DATA0 | PB0 / 0 | 5, PB0 |
| SII | 2, DATA1 | PB1 / 1 | 6, PB1 |
| SDO | 3, DATA2 | PB2 / 2 | 7, PB2 |
| SCI | 17, XTAL1 | PD7 / 15 přes invertor IC5 | 2, PB3 |
| RESET 12V | 18, PROGRST | T1 řízený PD2 / 10 | 1, PB5/RESET |
| VCC | 19, VTG | VTARGET | 8 |
| GND | 20 | GND | 4 |

Použít označení pinu 1 skutečného IDC konektoru a ověřit propojení měřením.
Pohled na protikus, zezadu nebo na pájecí stranu je zrcadlený; historický
obrázek Arduino kabeláže se nesmí použít jako číslování HV_PROG. Čísla v tabulce
označují elektrické kontakty schématu, nikoli neověřenou orientaci fotografie.
12 V patří pouze na RESET. VCC pro HVSP musí být předem ověřené 4,5–5,5 V.

## Omezení a přejímka před vydáním

- Konkrétní nový HEX nebyl nahrán; elektrické vypnutí HV/VTARGET a transienty
  RESET při expiraci vyžadují multimetr/osciloskop. Softwarový test měří jen
  pořadí zápisů a finální logické stavy.
- Lease není nezávislý hardwarový watchdog. Chrání nečinnost mezi příkazy;
  případné nekonečné uváznutí uvnitř knihovny `SPI.transfer()` nepřeruší.
- Reálné TPI/PDI (včetně LV/HV TPI a XMEGA 3,3 V) jsou REÁLNĚ NEOVĚŘENÉ.
  Bezpečné 512B PDI zápisy vyžadují klienta se správným posledním WRITE flagem;
  starý klient potvrzující každý 256B fragment není kompatibilní opravou hosta.
- Firmware zůstává u 256B XPROG fragmentů; PDI Flash write vyžaduje sudou
  počáteční adresu, lichou délku doplní FF. Žádná podpora dalších MCU/build profilů se nepřidává.
- Test JTAG obálek nedeklaruje XPROG podporu reálného JTAGICE mkII/3. JTAGICE mkII
  nemá XPROG v tabulce ISP send obálky; toto existující omezení nebylo rozšiřováno.

Manuální přejímka: ověřit konektory a napětí; samostatně schválit/nahrát controller
HEX, potom jen číst signatury/fuse/kalibraci a ověřit shutdown po odmlčení hosta.
Pro ISP použít `stk500v2`, pro HVSP `stk500hvsp`, pro HVPP `stk500pp`.
Příklad pouze ručního čtení s nakonfigurovaným veřejným AVRDUDE:

```cmd
avrdude -c stk500v2 -p t13 -P COM7 -b 115200 -n -U signature:r:isp-signature.hex:h
avrdude -c stk500hvsp -p t13 -P COM7 -b 115200 -n -U signature:r:hvsp-signature.hex:h
```

HVSP příkaz spustit až po správném přepojení; původní poslední stav byl pouze ISP.
Pro test expirace nechat řádně navázanou HV relaci bez LEAVE a měřit vypnutí
nejpozději 30 s po poslední úspěšné odpovědi. Nové spojení musí obnovit čtení.
Write/erase/verify na fyzickém cíli je samostatná přejímka s předchozí zálohou
a výslovným souhlasem; tato etapa je neprovádí.

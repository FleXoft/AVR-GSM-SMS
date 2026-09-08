# AVR-GSM – otthonautomatizálás, 1.2.1

Az új, időzített főciklusba visszakerültek a régi alkalmazás funkciói:
SMS-olvasás és válasz, relévezérlés, mozgás- és tápfigyelés, automatikus
hívásfogadás és hangjelzés, két LM75 érzékelő, EEPROM-statisztikák,
valamint a státuszképernyők. Az öt eredeti LCD-effekt, a B1 képernyőléptetés
és a buzzer megmaradt.

Ez már **nem csak diagnosztikai firmware**: feltöltés után SMS-parancsra
kapcsolja a reléket, és engedélyezett eseményekről SMS-t küld.
A módosítás során nem történt hardverprogramozás vagy tényleges SMS-küldés.

A régi alkalmazás változatlan mentése: `legacy/`.
Az előző diagnosztikai alkalmazás és Makefile mentése: `backups/diagnostic-v1/`.
A projektben a munka kezdetén nem volt Git-tároló.

## Fordítás és programozás

Szükséges: GNU make, avr-gcc/avr-libc és AVR binutils, Python 3.
A tesztekhez AddressSanitizer/UndefinedBehaviorSanitizer-képes gazdagépes C fordító.

```sh
make -j4
make check
make clean
make -j4 check
```

**Friss firmware: `build/AVR-GSM-SMS.hex`.** Az ELF, listázás, memória-
és veremkimutatás ugyanitt található. A régi gyökérkönyvtári fordítási
kimenetek a `legacy/generated/` alatt vannak; ezeket ne keverd az új HEX-szel.

Explicit programozás a saját programozóval/porttal:

```sh
make program AVRDUDE_PROGRAMMER=jtag1 AVRDUDE_PORT=/dev/cu.usbserial-A3000SLY
```

A normál build nem programoz. A `program` cél csak Flash-írást kér,
fuse-okat nem állít; a chip erase EEPROM-ra gyakorolt hatását a meglévő
EESAVE fuse határozza meg. A runtime EEPROM-rekord nem része az EEP-képnek.

Alapbeállítás: ATmega32, 7 372 800 Hz, 57 600 baud, 8N1. A `UART_BAUD_RATE`
és `VERSION` make-változó felülírható. A fizikai órajelnek egyeznie kell
a fordításival. A Timer1 időalapja 12 800-zal osztható órajelet igényel.
A Makefile párhuzamosan is helyes függőségi sorrendet tart, változó kapcsolóknál
újrafordít, változatlan bemenetnél nem; nem növel verziószámlálót és nem
írja át a forrást minden futáskor. A fordítás `-Werror` mellett történik.

## SMS-parancsok

Egyetlen, legfeljebb 160 karakteres GSM-7 SMS használható. Az eredeti,
ékezet nélküli parancsok támogatottak; több parancs elválasztható szóközzel,
sortöréssel, vesszővel vagy pontosvesszővel. Kis-/nagybetű nem számít.

| Parancs | Működés |
|---|---|
| `HELP` | A parancslista visszaküldése a feladónak. |
| `STATUS` | Bemenetek, élesítés, relék, két hőmérséklet és számlálók a feladónak. |
| `R1ON`, `R1OFF` | 1. relé be/ki. |
| `R2ON`, `R2OFF` | 2. relé be/ki. |
| `SETRELAY1ON`, `SETRELAY1OFF` | Az 1. relé hosszú parancsnevei. |
| `SETRELAY2ON`, `SETRELAY2OFF` | A 2. relé hosszú parancsnevei. |
| `ENABLE` | Mozgásfigyelés élesítése. |
| `DISABLE` | Mozgásfigyelés kikapcsolása; a tápfigyelést nem kapcsolja ki. |
| `POWER` | Mozgásfigyelés és tápfigyelés élesítése, a régi működés szerint. |
| `SETNUMBER` | Az SMS feladóját menti riasztási címzettként EEPROM-ba. |

Példa: `R1ON R2OFF STATUS`. A státusz már az üzenetben kért reléállapotokat
mutatja. `HELP` és `STATUS` együtt esetén egy HELP-válasz készül.

A teljes üzenet ellenőrzése megelőzi a relékapcsolást. `NOTR1ON`, illetve
`R1ON hibásparancs` nem kapcsolja a relét: hibaválasz készül.
A SETNUMBER a régi kódban még nem mentett ténylegesen; most működik.

**Feladóazonosítás:** az eredeti viselkedést megtartva nincs engedélyezett
feladók listája. Bármely érvényes telefonszámról érkező támogatott parancsot
elfogad, a SETNUMBER-t is. Az EEPROM-ban tárolt riasztási címzett nem
hozzáférési jogosultság. Új EEPROM esetén a régi forrásban szereplő
`+36309791403` az alapcímzett; a kijelzőn ellenőrizhető és SETNUMBER-rel módosítható.

## SMS-kezelés és hibák

A SIM-től független helyi lekérdezések (`AT+CBC`, `AT+CCLK?`, `AT+CMTE?`)
SIM nélkül és PIN-zár esetén is futnak. A `SIM not inserted` válasz nem
modemhiba: a program helyi adatokat kér le, majd ismét ellenőrzi a SIM-et.
A SIM hiánya nem nullázza a már megkapott akkumulátor-, óra- és
hőmérsékletadatokat. Az adott lekérdezés saját hibája továbbra is `n/a`-t eredményez.
A kétmezős `+CMTE: 1,32` válaszból a második mező, 32 °C jelenik meg;
az egymezős SIM300D-válasz támogatása is megmaradt.

Sikeres SIM-ellenőrzés után PDU-mód (`CMGF=0`) és SIM-tár (`CPMS="SM","SM","SM"`)
aktív. Az üzeneteket `CNMI=2,1,0,0,0` indexértesítések jelzik.
A `CMGR=<index>,1` nem változtatja meg a rekord olvasottsági állapotát.
A nyers PDU hosszát, címét, kódolását és teljes tartalmát ellenőrzi;
a szövegben lévő `OK`, `RING`, sortörés nem modemválasz.

A támogatás egyetlen SMS-DELIVER üzenetre, numerikus telefonszámra és a régi
parancsokhoz szükséges GSM-7 ASCII karakterkészletre korlátozott.
UCS2/Unicode, bináris SMS, GSM-kiterjesztő karakterek, összefűzött SMS és UDH
nincs dekódolva. Ezeket nem hajtja végre és nem törli; a SIM-en maradnak.
Ez parancskezelő, nem általános SMS-olvasó.

Négy értesítést sorba állít, az ismétlődő indexeket összevonja. A kimaradt
értesítések és újraindításkor bent maradt SMS-ek miatt háttérben a SIM
rekordjait is végigolvassa; a teljes kör után 60 s szünet következik.
A beolvasás a státuszlekérdezésekkel felváltva fut, nem egyszerre árasztja
el a soros portot. A ME-tár értesítéseit számlálja, de a kezelt tár a SIM.

Törlés csak sikeres, teljes olvasás és alkalmazásoldali feldolgozás után,
**az adott indexre** (`CMGD=<index>`) történik. `DEL ALL` nincs.
Ha a válasz küldési helye foglalt, az üzenet feldolgozása halasztódik;
nincs félig végrehajtott reléparancs. Sikertelen törlésnél megtartja a
törlési szándékot és később újrapróbálja, ugyanabban a futásban nem hajtja
végre még egyszer a parancsot. MCU-reset és a végrehajtás/törlés közé eső
áramszünet után azonban az üzenet újra előkerülhet: nincs tartós, pontosan
egyszeri végrehajtást garantáló SMS-napló.

Egyszerre egy kimenő SMS lehet sorban. A PDU feltöltése legfeljebb 16 UART-
bájtot próbál átadni ciklusonként, figyelembe véve a TX-puffer szabad helyét.
A hálózati regisztrációig vár. A számláló csak `+CMGS` és végső `OK`
után nő: ez a modem/SMSC elfogadása, nem kézbesítési jelentés a címzettről.

A kijelzőn az utolsó SMS eredménye: 0 hiba, 1 elfogadva, 2 bizonytalan,
3 sorba állított riasztás. Ctrl-Z után bekövetkező időtúllépés/kapcsolathiba
bizonytalan eredmény: nincs automatikus ismétlés, mert az SMS esetleg már
elment. Prompt/feltöltési hiba esetén ESC-pel kilép a beviteli módból,
majd szükség esetén újrainicializál. Biztosan sikertelen riasztás legalább
60 s elteltével ismét próbálkozik; normál STATUS/HELP-választ új parancs
küldésével lehet ismét kérni.

## Riasztások, hívás és szenzorok

Induláskor a relék kikapcsoltak, a mozgásfigyelés nincs élesítve,
a tápfigyelés engedélyezett. Ezek az állapotok nem EEPROM-beállítások.

- Mozgás: OPT1 aktív alacsony, PB0. A régi főciklussal egyezően az aktív
  optocsatoló mozgást jelent; a régi STATUS-szöveg ezzel ellentétes hibája javítva.
  Riasztás után a mozgásfigyelés kikapcsol, ENABLE/POWER élesíti újra.
- Táp: OPT2 aktív alacsony, PD2; aktív optocsatoló jelenti a meglévő tápot.
  100 ms bemeneti szűrés után egyszeri áramszünet-SMS készül.
  A fizikai bemenet visszaállásakor visszatérési SMS készül és újra élesedik.
  Hiányzó OPT2-bekötés induláskor is áramszünetet jelent, ahogy a régi programban.
- A függő riasztások típusonként összevonódnak, nem korlátlan eseménynaplót alkotnak.
- `RING` hatására ATA, sikeres válasz után az eredeti CLDTMF hangsor indul;
  a hívásszámláló csak sikeres fogadáskor nő. Az ismételt RING ugyanabban
  a hívásban nem számít új fogadásnak. NO CARRIER törli a hívásállapotot.
- Két LM75: 7 bites I2C-cím 0x48 és 0x49, PC0=SCL és PC1=SDA.
  Váltott olvasás, érzékelőnként kb. 2 s frissítés; 20 ms határ minden buszlépésre.
  NACK vagy beragadt busz esetén `n/a`, a többi feladat tovább fut.
  A negatív és félfokos értékek javítva: például -0,5 °C is helyesen jelenik meg.

## Kijelző és főciklus

A végtelen főciklus ezen az AVR-en megfelelő. Timer1 ad 5 ms időalapot;
a feladatok rövid lépésekben futnak, közben az UART folyamatosan ürül.
A régi `rand_matrix1..5` táblák változatlanul a `lcd_effects.h` fájlban vannak.
Egy cella 30 ms-ig telt blokk, majd megjelenik a szöveg, és 5 ms után jön
a következő cella. A teljes effekt nagyjából 1,4 s maradt.

2×20 HD44780: DB4..7 = PB4..7, RS=PB1, RW=PA0, E=PA1.
A kijelzés nem függ OPT2-től. Az LCD busy-várakozás korlátos.
Képernyőváltás 5 másodpercenként; B1 (PD3) 30 ms pergésmentesítés után
lenyomásonként egyszer léptet, animáció közben is. Timer2 hajtja a PD4 buzzert,
kb. 2 kHz-en. A Timer1 és Timer2 ezért foglalt.

A képernyőkön modem/SIM/hálózat, szolgáltató, térerő, akkumulátor, modemhőmérséklet,
I/O, értesítések, hibaszámlálók, LM75/relék, EEPROM-statisztikák,
riasztási címzett, élesítés, SMS-eredmény, modemóra, névjegy és verzió látszik.
A modemóra csak kijelzés; a program nem állítja be és nem szinkronizálja.

GSM Ready = SIM/SMS-inicializálás kész. A `SIM/SMS waiting` felirat azt jelenti,
hogy az alap AT-kapcsolat beállítása kész, de a SIM/SMS-kezelés még várakozik. Hálózat: -1 nincs adat, 0 nincs regisztráció,
1 saját hálózat, 2 keresés, 3 elutasítva, 4 ismeretlen, 5 roaming.
SIM: -1 ismeretlen, 0 nem READY, 1 READY.
A CSQ 99 ismeretlen; más értéknél `-113 + 2*rssi` dBm jelenik meg.

## EEPROM

A korábbi 0..7 címeken lévő reset/sent/received/call számlálókat az első
indulás beolvassa, ha még nincs új érvényes rekord. Két új rekord kezdőcíme
64 és 128; rekordméret 31 bájt. Verzió, sorszám, négy számláló, riasztási
telefonszám, CRC16 és utoljára megírt érvényességi jel szerepel bennük.
Ezek a címek az új firmware számára fenntartottak.

Az írás legfeljebb egy bájtot indít ciklusonként, és nem vár foglalt EEPROM-ra.
A régebbi érvényes rekord csak az új commit után válik leválthatóvá.
A számlálók 65535-nél telítődnek. A reset-számláló minden induláskor nő.
A RAM-ban még függő változás az EEPROM-commit előtti áramvesztéskor elveszhet.

## Ellenőrzések és következő panelpróba

`make check`: figyelmeztetésmentes AVR-build, memóriahatárok és hét
memóriavédelmi ellenőrzéssel futó gazdagépes teszt:

- GSM-státusztesztek SIM nélkül, PIN-zárral, SIM-állapotváltozással és
  sikertelen SMS-inicializálással; a felhasználó tényleges CBC/CMTE válaszai;
  valamint mind az öt animáció;
- SMS/PDU, 150/160 karakter, hibás/csonka kódolás, SIM-scan, törlés;
- nem blokkoló SMS-feltöltés, visszanyomás, promptmegszakítás, bizonytalan küldés;
- pontos parancsok, STATUS, SETNUMBER, pergésmentesített riasztások;
- LM75-regisztersorrend, negatív érték, NACK és időtúllépés;
- EEPROM-migráció és megszakítás az új rekord minden bájthatárán;
- teljes modem–alkalmazás út: inicializálás → CMTI → PDU → R1ON STATUS →
  célzott törlés → visszaküldött SMS → folytatódó státuszlekérdezés.

A jelenlegi AVR-build 18 872 bájt Flash-t és 1271 bájt statikus SRAM-ot használ;
777 bájt marad a veremnek. A legnagyobb saját függvénykeret a fordító szerint
221 bájt; ehhez a hívott könyvtári rutinok és megszakítások kerete is hozzáadódik.

A tesztek nem helyettesítik a fizikai UART, a modem és a megszakítások
paneles mérését. A build `.su` fájlokat is készít a függvények veremigényéről;
a statikus RAM-kimutatás nem futásidejű stack high-water mérés.

Panelen elsőként ellenőrizd a riasztási címzettet és az OPT1/OPT2 polaritását,
majd próbáld a HELP/STATUS parancsot, a reléket, B1-et, a szenzorokat,
a tápki-/visszakapcsolást és a hívásfogadást. Modemhiba alatt is mennie kell
a kijelzőnek és a gombnak. A riasztási kimenetek tesztje már valódi SMS-eket küld.

Az AVR resetben tartja az FT232-t, ezért az USB-s modemterminál nem használható
vele egyidejűleg. Fő AT-port: MTX/MRX jumperállás. PA2 és PWRKEY összeköttetése
szükséges az automatikus bekapcsoláshoz. A tápjumpereket a saját panelváltozat
kézikönyve szerint kell beállítani.

## Dokumentáció

- [Olimex AVR-GSM kézikönyv](https://www.olimex.com/Products/AVR/Development/AVR-GSM/resources/AVR-GSM.pdf)
- [SIM300D AT-parancsok: CMGR, CMGS, CPMS, CNMI](https://www.olimex.com/Products/AVR/Development/AVR-GSM/resources/SIM300DATC.pdf)
- [3GPP/ETSI SMS-formátum, TS 23.040](https://etsi.org/deliver/etsi_ts/123000_123099/123040/16.00.00_60/ts_123040v160000p.pdf)
- [3GPP/ETSI GSM-7 ábécé, TS 23.038](https://www.etsi.org/deliver/etsi_ts/123000_123099/123038/17.00.00_60/ts_123038v170000p.pdf)
- [TI LM75 adatlap](https://www.ti.com/lit/ds/symlink/lm75b.pdf)

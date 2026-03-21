# Shareholder – NAS Drive Mapper

**Shareholder** verbindet NAS-Freigaben (SMB/CIFS) als Windows-Netzlaufwerke –
automatisch beim Programmstart, sicher verschlüsselt und ohne Administratorrechte.

---

## Inhaltsverzeichnis

1. [Überblick](#1-überblick)
2. [Voraussetzungen](#2-voraussetzungen)
3. [Installation](#3-installation)
4. [Ersteinrichtung](#4-ersteinrichtung)
5. [Laufwerke verbinden](#5-laufwerke-verbinden)
6. [Laufwerke trennen](#6-laufwerke-trennen)
7. [Passwort verwalten](#7-passwort-verwalten)
8. [Konfiguration ändern](#8-konfiguration-ändern)
9. [Befehlsübersicht](#9-befehlsübersicht)
10. [Fehlerbehebung](#10-fehlerbehebung)
11. [Sicherheitshinweise](#11-sicherheitshinweise)
12. [Technische Details (für Entwickler)](#12-technische-details-für-entwickler)

---

## 1. Überblick

Shareholder löst das Problem, dass NAS-Laufwerke nach jedem Windows-Start
manuell neu verbunden werden müssen. Das Programm:

- liest eine **verschlüsselte Konfigurationsdatei** (`shares.cfg`),
- fragt einmalig nach dem **Entschlüsselungspasswort** (oder liest es aus dem
  Windows Credential Manager),
- verbindet alle konfigurierten NAS-Shares als **nicht-persistente**
  Netzlaufwerke (sie werden beim Abmelden/Neustart automatisch getrennt).

### Typischer Ablauf

```
shareholder.exe --setup   ← einmalig: Passwort + Shares festlegen
shareholder.exe           ← täglich: alle Laufwerke auf einen Schlag verbinden
```

Autostart einrichten → Aufgabenplanung oder Verknüpfung im Autostart-Ordner
(siehe Abschnitt [3. Installation](#3-installation)).

---

## 2. Voraussetzungen

| Anforderung | Mindestversion |
|---|---|
| Windows | 10 / 11 oder Windows Server 2016+ |
| Netzwerk | SMB/CIFS-Erreichbarkeit zum NAS |
| Benutzerrechte | Keine Administratorrechte nötig |

Die EXE läuft **ohne Installation** – einfach kopieren und starten.

---

## 3. Installation

1. `shareholder.exe` in ein beliebiges Verzeichnis kopieren, z. B.
   `C:\Tools\shareholder\`.
2. Ersteinrichtung durchführen (→ Abschnitt 4).

### Autostart einrichten (optional)

**Variante A – Aufgabenplanung (empfohlen):**

```bat
schtasks /create /tn "Shareholder" /tr "\"C:\Tools\shareholder\shareholder.exe\"" /sc ONLOGON /f
```

**Variante B – Autostart-Ordner:**

```
Win+R → shell:startup → Verknüpfung zu shareholder.exe einfügen
```

> **Hinweis:** Damit der Autostart ohne Passworteingabe funktioniert, muss das
> Passwort im Windows Credential Manager gespeichert sein (wird während
> `--setup` angeboten).

---

## 4. Ersteinrichtung

Die Einrichtung läuft **einmalig** und erstellt die verschlüsselte
Konfigurationsdatei `shares.cfg` im selben Verzeichnis wie die EXE.

```bat
shareholder.exe --setup
```

### Schritt-für-Schritt

**Schritt 1 – Passwort festlegen**

```
Neues Passwort: ••••••••••••
Passwort bestätigen: ••••••••••••
```

Das Passwort wird zur Verschlüsselung der Konfigurationsdatei verwendet.
Es wird **nicht** gespeichert – nur der daraus abgeleitete Schlüssel wird
zum Ver- und Entschlüsseln der Datei benutzt.

**Schritt 2 – Shares eingeben**

Für jeden NAS-Share werden folgende Angaben abgefragt:

| Feld | Beispiel | Hinweis |
|---|---|---|
| Laufwerksbuchstabe | `Z:` | Einzelner Buchstabe + Doppelpunkt |
| UNC-Pfad | `\\nas\daten` | Doppelter Backslash, kein abschließender `\` |
| Benutzername | `nas-benutzer` | Leer lassen → aktueller Windows-Benutzer |
| Share-Passwort | `••••••••` | Leer lassen → keine separate Authentifizierung |

Geben Sie so viele Shares ein wie gewünscht. Leere Eingabe beim
Laufwerksbuchstaben beendet die Eingabe.

**Beispiel-Sitzung:**

```
=== Neue Konfiguration erstellen ===

Laufwerksbuchstabe (z.B. Z:, leer = fertig): Z:
UNC-Pfad (z.B. \\nas\share): \\192.168.1.100\daten
Benutzername (leer = aktueller Windows-Benutzer): admin
Share-Passwort: ••••••••
  --> Share hinzugefügt.

Laufwerksbuchstabe (z.B. Z:, leer = fertig): Y:
UNC-Pfad (z.B. \\nas\share): \\192.168.1.100\backup
Benutzername (leer = aktueller Windows-Benutzer):
  --> Share hinzugefügt.

Laufwerksbuchstabe (z.B. Z:, leer = fertig):

Konfiguration gespeichert: C:\Tools\shareholder\shares.cfg
Passwort im Windows Credential Manager speichern? [j/N]: j
Passwort gespeichert.
```

**Schritt 3 – Passwort speichern (optional)**

Wenn Sie `j` eingeben, wird das Passwort sicher im Windows Credential Manager
hinterlegt. Beim nächsten Start von Shareholder ist keine Eingabe mehr nötig –
ideal für den Autostart.

---

## 5. Laufwerke verbinden

```bat
shareholder.exe
```

Das Programm:

1. Sucht nach einem gespeicherten Passwort im Windows Credential Manager.
   - Gefunden → Laufwerke werden sofort verbunden.
   - Nicht gefunden → Passwortabfrage (max. 3 Versuche).
2. Entschlüsselt `shares.cfg`.
3. Verbindet jeden konfigurierten Share als Netzlaufwerk.
4. Gibt eine Erfolgs-/Fehlermeldung pro Laufwerk aus.

**Beispielausgabe:**

```
======================================
  Shareholder – NAS Drive Mapper
======================================

Gespeichertes Passwort wird verwendet...

Verbinde 2 Laufwerk(e)...
  Verbinde Z: -> \\192.168.1.100\daten  ... OK
  Verbinde Y: -> \\192.168.1.100\backup ... OK

2/2 Laufwerke erfolgreich verbunden.
```

Die verbundenen Laufwerke erscheinen im Windows Explorer unter „Dieser PC"
und sind sofort nutzbar.

> **Hinweis:** Die Verbindungen sind **nicht persistent**. Nach einem
> Windows-Abmelden oder Neustart müssen sie mit `shareholder.exe` erneut
> verbunden werden (→ Autostart, Abschnitt 3).

---

## 6. Laufwerke trennen

```bat
shareholder.exe --unmap
```

Trennt alle in der Konfiguration hinterlegten Laufwerke. Geöffnete Dateien
auf den Netzlaufwerken werden dabei zwangsweise geschlossen.

---

## 7. Passwort verwalten

### Gespeichertes Passwort löschen

```bat
shareholder.exe --forget
```

Entfernt den Eintrag aus dem Windows Credential Manager. Beim nächsten Start
wird das Passwort wieder manuell abgefragt.

### Passwort ändern

Da das Passwort den Verschlüsselungsschlüssel der Konfigurationsdatei
ableitet, muss bei einem Passwortwechsel die gesamte Konfiguration neu
erstellt werden:

1. Altes `shares.cfg` sichern oder löschen.
2. `shareholder.exe --setup` erneut ausführen.
3. Gleiches Passwort eingeben, falls der Credential-Manager-Eintrag
   aktualisiert werden soll, oder `--forget` vorher ausführen.

---

## 8. Konfiguration ändern

Um Shares hinzuzufügen, zu entfernen oder zu ändern:

1. `shareholder.exe --setup` ausführen.
2. Neues Passwort eingeben (kann dasselbe wie bisher sein).
3. Alle Shares neu eingeben.

Die alte `shares.cfg` wird dabei überschrieben.

---

## 9. Befehlsübersicht

| Befehl | Beschreibung |
|---|---|
| `shareholder.exe` | Alle Laufwerke verbinden |
| `shareholder.exe --setup` | Neue Konfiguration erstellen (Ersteinrichtung / Änderung) |
| `shareholder.exe --unmap` | Alle Laufwerke trennen |
| `shareholder.exe --forget` | Gespeichertes Passwort aus dem Credential Manager löschen |
| `shareholder.exe --help` | Kurzübersicht der Befehle anzeigen |

---

## 10. Fehlerbehebung

### „Keine Konfigurationsdatei gefunden"

`shares.cfg` liegt nicht neben der EXE. Lösung: `shareholder.exe --setup`
ausführen, um die Konfiguration zu erstellen.

### „Decryption failed: wrong password or corrupted file"

Das eingegebene Passwort stimmt nicht mit dem beim Setup gewählten Passwort
überein, oder die `shares.cfg` ist beschädigt. Lösung:
- Richtiges Passwort eingeben (3 Versuche).
- Falls das Passwort verloren ist: `shares.cfg` löschen und `--setup` erneut
  ausführen.

### „FEHLER: Zu viele Fehlversuche"

Nach 3 falschen Passworteingaben beendet sich das Programm. Neu starten und
korrektes Passwort eingeben.

### Ein Laufwerk meldet „FEHLER: …"

Mögliche Ursachen:

| Fehlermeldung (Windows) | Ursache | Lösung |
|---|---|---|
| `Zugriff verweigert` | Falscher Benutzername / Share-Passwort | Config mit `--setup` aktualisieren |
| `Netzwerkpfad nicht gefunden` | NAS nicht erreichbar / UNC-Pfad falsch | Netzwerk prüfen, UNC-Pfad prüfen |
| `Lokaler Gerätename wird bereits verwendet` | Laufwerksbuchstabe belegt | Anderen Buchstaben in der Config wählen |
| `Mehrfache Verbindungen...nicht unterstützt` | NAS erlaubt pro Benutzer nur eine Verbindung | Gleichen Benutzer für alle Shares eines NAS verwenden |

### Laufwerk erscheint nicht im Explorer

Windows-Sitzung neu starten oder `shareholder.exe --unmap` gefolgt von
`shareholder.exe` ausführen.

---

## 11. Sicherheitshinweise

- Das **Hauptpasswort** wird niemals im Klartext auf der Festplatte gespeichert.
- Der **Windows Credential Manager** schützt das gespeicherte Passwort
  zusätzlich mit DPAPI (an das Windows-Benutzerkonto gebunden).
- Die Konfigurationsdatei ist mit **AES-256-GCM** verschlüsselt. Ein
  manipuliertes oder kopierten `shares.cfg` kann ohne das korrekte Passwort
  nicht entschlüsselt werden.
- Share-Passwörter in der Config sind Bestandteil des verschlüsselten Inhalts
  und damit genauso gut geschützt wie das Hauptpasswort es erlaubt – wählen
  Sie ein **starkes Hauptpasswort**.
- Die Netzlaufwerke sind `CONNECT_TEMPORARY` und werden beim Abmelden oder
  Neustart **automatisch getrennt**.

---

## 12. Technische Details (für Entwickler)

### Bauen

```bat
:: Visual Studio 2022 Developer Command Prompt
cd C:\Development\shareholder

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

:: EXE: build\Release\shareholder.exe
```

Für 32-Bit: `-A Win32`

### Architektur

| Modul | Datei | Aufgabe |
|---|---|---|
| Einstiegspunkt | `main.cpp` | CLI-Parsing, Ablaufsteuerung |
| Verschlüsselung | `crypto.cpp` | AES-256-GCM, PBKDF2-SHA256 (Windows CNG) |
| Konfiguration | `config_manager.cpp` | JSON-Serialisierung, Datei lesen/schreiben |
| Credentials | `credential_store.cpp` | Windows Credential Manager |
| Laufwerke | `drive_mapper.cpp` | `WNetAddConnection2` (MPR) |

### Verschlüsseltes Dateiformat (`shares.cfg`)

```
Offset  Länge   Inhalt
0       4       Magic "SHLR"
4       1       Version (0x01)
5       16      PBKDF2-Salt (zufällig, pro Verschlüsselung neu)
21      12      AES-GCM Nonce (zufällig, pro Verschlüsselung neu)
33      16      AES-GCM Authentication Tag
49      N       Ciphertext (JSON-Konfiguration)
```

### Konfigurationsformat (intern, nach Entschlüsselung)

```json
{
  "shares": [
    { "drive": "Z:", "unc": "\\\\nas\\daten",  "user": "nas-user", "password": "geheim" },
    { "drive": "Y:", "unc": "\\\\nas\\backup", "user": "",         "password": "" }
  ]
}
```

### Verwendete Windows-APIs

| API | Bibliothek | Zweck |
|---|---|---|
| `BCryptDeriveKeyPBKDF2` | `bcrypt.dll` | Schlüsselableitung |
| `BCryptEncrypt/Decrypt` | `bcrypt.dll` | AES-256-GCM |
| `BCryptGenRandom` | `bcrypt.dll` | Zufallszahlen |
| `CredWrite/CredRead` | `advapi32.dll` | Credential Manager |
| `WNetAddConnection2` | `mpr.dll` | Netzlaufwerk verbinden |
| `WNetCancelConnection2` | `mpr.dll` | Netzlaufwerk trennen |

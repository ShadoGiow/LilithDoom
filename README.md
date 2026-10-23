# BladeFireMode — progetto PERSONALE

> **Questo e' un progetto per uso personale.** E' un **mashup di funzioni di varie mod che mi piacciono**,
> messo insieme per il *mio* gioco e il *mio* elenco di mod. **Non e' una mod rilasciata per tutti**:
> non e' pensato per essere scaricato, installato o usato da altre persone, non offro supporto.

Plugin SKSE (CommonLibSSE-NG) per Skyrim SE/AE: accende la lama di una spada dopo una combinazione di tasti,
in sincronia con un'animazione, con danni di fuoco extra e consumo di mana.

## Cosa fa

- Combinazione di tasti (pad o tastiera, registrabile dal menu F1) -> invia un evento al grafo d'animazione del
  personaggio; una clip sostituita con Open Animation Replacer (condizione su una variabile globale) fa partire
  l'animazione di accensione.
- Si aggancia a un evento della clip (evento `Pie` con un testo preciso, configurabile) per sapere quando e' partita; quando la mano tocca la lama avvia
  l'accensione graduale (sequenze `mBegin` -> `mLoop`, `mEnd` per lo spegnimento) del gruppo di effetti dentro la
  mesh dell'arma, pilotando direttamente il `NiControllerManager`.
- A lama accesa: danni di fuoco extra (moltiplicatore sul danno base + valore fisso, bonus attacchi potenti,
  resistenza al fuoco, bruciatura nel tempo) e consumo di mana (costo iniziale + al secondo, soglia di spegnimento).
- Suoni: all'accensione/spegnimento suona un suono di Skyrim dato come *editor ID* (configurabile, con pulsante di prova).
- Menu F1 (SKSE Menu Framework): stato, tasti, animazione (tempi, evento, installazione di un file `.hkx`), suoni, danni,
  mana, diagnostica. Le impostazioni stanno in `Data/SKSE/Plugins/BladeFireMode.ini`.

## Struttura

| File | Ruolo |
| --- | --- |
| `src/main.cpp` | avvio plugin, log, messaggi SKSE, thread che chiede il tick al thread di gioco |
| `src/Settings.*` | impostazioni, file INI, nomi dei tasti |
| `src/Input.*` | lettura tasti (XInput + tastiera/mouse), combinazioni, registrazione dal menu |
| `src/Fx.*` | ricerca del gruppo effetti sulla spada e avvio delle sequenze |
| `src/Mode.*` | macchina a stati, sink animazione e colpi, mana, danni, suoni, blocco comandi (opzionale: movimento; gli attacchi NON si bloccano, farebbe rinfoderare l'arma) |
| `src/Anim.*` | installazione di un'animazione nella cartella standalone BladeFireMode |
| `src/Menu.*` | pagine del menu F1 (funzioni cimgui collegate a runtime) |

## Compilazione

Il workflow GitHub (`.github/workflows/build.yml`) clona CommonLibSSE-NG (versione fissata), configura xmake e
produce `SKSE/Plugins/BladeFireMode.dll` nell'artefatto `BladeFireMode-dll`.

Localmente: `xmake f -y -p windows -a x64 -m releasedbg --skyrim_vr=n && xmake build -y -v`
(dopo aver clonato la libreria in `lib/commonlibsse-ng`).

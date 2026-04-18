# Guida alla Replicazione Logica Elegoo Centauri Carbon (CC) per ESP32

Questa guida descrive come implementare da zero la comunicazione con una stampante Elegoo Centauri Carbon (protocollo CC) senza utilizzare la libreria C++ originale, ma replicandone il comportamento.

## 1. Architettura della Comunicazione
La stampante Centauri Carbon comunica via rete locale (LAN) utilizzando due canali:
1.  **UDP (Discovery)**: Per trovare l'IP della stampante automaticamente.
2.  **WebSocket (Controllo/Stato)**: Per inviare comandi e ricevere lo stato in tempo reale.

## 2. Passo 1: Individuazione (Discovery)
La stampante "ascolta" pacchetti UDP sulla porta **3000**.
- **Cosa inviare**: Un pacchetto UDP Broadcast (all'indirizzo 255.255.255.255) contenente un JSON vuoto o un comando di ricerca generico.
- **Cosa aspettarsi**: La stampante risponderà con un JSON che include il suo `Id`, `MainboardIP` e `MachineName`. 
- **Esempio Risposta**: `{"Id":"...","Data":{"Name":"Centauri Carbon","MainboardIP":"192.168.86.52",...}}`

## 3. Passo 2: Connessione WebSocket
Una volta ottenuto l'IP, bisogna aprire una connessione WebSocket.
- **URL**: `ws://<IP_STAMPANTE>:3030/websocket`
- **Protocollo**: Standard WebSocket.

## 4. Passo 3: Il Protocollo dei Messaggi (SDCP)
Ogni messaggio scambiato deve seguire questa struttura JSON fissa:
```json
{
  "Id": "opzionale",
  "Topic": "sdcp/...",
  "Data": {
    "Cmd": <CODICE_COMANDO>,
    "RequestID": "<STRINGA_UNIVOCA>",
    "MainboardID": "<ID_STAMPANTE>",
    "TimeStamp": <UNIX_TIMESTAMP_MS>,
    "From": 1,
    "Data": { ... parametri specifici ... }
  }
}
```

### Comandi Principali (Cmd):
- **Cmd 0**: Richiesta Stato (Get Status).
- **Cmd 128**: Controllo Assi (Move).
- **Cmd 130**: Controllo Temperatura (Set Temp).

## 5. Passo 4: Mantenere la Connessione (Heartbeat)
La stampante chiude la connessione se non riceve attività.
- **Logica**: Inviare un comando di "Get Status" (Cmd 0) ogni 2-3 secondi.
- **Risposta**: La stampante invierà un oggetto `Status` contenente `TempOfNozzle`, `TempOfHotbed`, `PrintInfo` (progresso, layer), e `CurrentStatus` (0=IDLE, 1=PRINTING, etc.).

## 6. Passo 5: Parsing dei Dati (Esempio)
Quando ricevi un messaggio WebSocket, devi estrarre:
- `Status.TempOfNozzle`: Temperatura attuale estrusore.
- `Status.PrintInfo.Progress`: Percentuale di stampa.
- `Status.PrintInfo.Filename`: Nome del file in stampa.

---
*Documento generato per Antigravity - Progetto Elegoo-Link Replicator.*

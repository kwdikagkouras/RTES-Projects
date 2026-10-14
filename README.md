# RTES-Projects

Αποθετήριο εργασιών για το μάθημα **«Ενσωματωμένα Συστήματα Πραγματικού Χρόνου» (ΕΣΠΧ)**.

## `ergasia_0`: K-Nearest Neighbors (KNN) με pthreads

Ατομική Εργασία 0. Υλοποίηση του αλγορίθμου K-Nearest Neighbors στη γλώσσα C με χρήση POSIX threads.

**Περιεχόμενα φακέλου:**

- `knn_pthreads.c`: Ο πηγαίος κώδικας του αλγορίθμου. Παράγει και αρχείο `output.mat` για σύγκριση των αποτελεσμάτων.
- `input.mat`: Αρχείο δεδομένων για γρήγορη δοκιμή του προγράμματος.
- `knn.m`: MATLAB script για την επεξεργασία/οπτικοποίηση των αποτελεσμάτων.
- `Makefile`: Αρχείο αυτοματοποίησης της μεταγλώττισης. Παράγει το εκτελέσιμο `knn_pthreads`.

**Οδηγίες εκτέλεσης (Linux/Ubuntu):**

```bash
cd ergasia_0
make
./knn_pthreads input.mat 5
```

Η γενική μορφή εκτέλεσης είναι:

```bash
./knn_pthreads <αρχείο.mat> <k>
```

---

## `ergasia_1`: Producer–Consumer

Ατομική Εργασία 1. Υλοποίηση και πειραματική αξιολόγηση του μοντέλου Producer–Consumer με χρήση pthreads και συγχρονισμού μέσω mutexes/condition variables.

**Περιεχόμενα φακέλου:**

- `pc_E1.c`: Ο κώδικας της υλοποίησης Producer–Consumer.
- `Makefile`: Αρχείο αυτοματοποίησης της μεταγλώττισης. Παράγει το εκτελέσιμο `pc`.
- `run_experiments.sh`: Bash script για την αυτοματοποιημένη εκτέλεση πολλαπλών πειραμάτων και τη συλλογή των αποτελεσμάτων στο `results.csv`.

**Οδηγίες εκτέλεσης (Linux/Ubuntu):**

```bash
cd ergasia_1
make
./pc
```

Για την εκτέλεση όλων των πειραμάτων:

```bash
chmod +x run_experiments.sh
./run_experiments.sh
```

Για διαγραφή του εκτελέσιμου αρχείου:

```bash
make clean
```

---

## `final_project`: Telemetry Firehose

Τελική εργασία. Υλοποίηση εφαρμογής τηλεμετρίας σε C, η οποία λαμβάνει μηνύματα μέσω WebSocket, τα επεξεργάζεται με τη χρήση νημάτων και καταγράφει μετρικές απόδοσης.

Η εφαρμογή αποτελείται από:

- **Producer thread**: Συνδέεται στο Bluesky Firehose μέσω WebSocket και εισάγει τα εισερχόμενα JSON μηνύματα σε bounded circular queue.
- **Consumer thread**: Επεξεργάζεται τα μηνύματα από την ουρά και μετρά τα events τύπου `commit`, `identity`, `account` και `info`.
- **Logger thread**: Καταγράφει ανά δευτερόλεπτο τις μετρικές στο `metrics_log.txt`, όπως τους μετρητές μηνυμάτων, την πληρότητα του buffer και τη χρήση CPU.
- **Μηχανισμό επανασύνδεσης**: Ανιχνεύει διακοπές δικτύου και καταγράφει κατάλληλες τιμές `NaN` στο αρχείο μετρήσεων.

**Περιεχόμενα φακέλου:**

- `firehose.c`: Ο πηγαίος κώδικας της εφαρμογής τηλεμετρίας.
- `Makefile`: Κανόνες μεταγλώττισης για:
  - εκτέλεση σε Linux PC (`telemetry_host`),
  - cross-compilation για Raspberry Pi (`telemetry`).
- `metrics_log.txt`: Αρχείο καταγραφής των μετρικών εκτέλεσης.

**Απαιτούμενες βιβλιοθήκες:**

- POSIX Threads (`pthread`)
- `libwebsockets`
- `cJSON`

**Μεταγλώττιση σε Linux PC:**

```bash
cd final_project
make pc
```

Το εκτελέσιμο αρχείο θα δημιουργηθεί ως `telemetry_host`.

**Cross-compilation για Raspberry Pi:**

Για τη μεταγλώττιση για Raspberry Pi απαιτούνται ο cross-compiler `arm-linux-gnueabihf-gcc` και ένα κατάλληλο sysroot. Η προεπιλεγμένη τοποθεσία του sysroot είναι:

```text
$HOME/pi_sysroot
```

Η μεταγλώττιση πραγματοποιείται με:

```bash
cd final_project
make pi
```

Για τη μεταγλώττιση και των δύο εκδόσεων:

```bash
make all
```

Για διαγραφή των εκτελέσιμων αρχείων και του αρχείου μετρήσεων:

```bash
make clean
```

---

## Δομή αποθετηρίου

```text
RTES-Projects/
├── ergasia_0/
│   ├── Makefile
│   ├── input.mat
│   ├── knn.m
│   └── knn_pthreads.c
├── ergasia_1/
│   ├── Makefile
│   ├── pc_E1.c
│   └── run_experiments.sh
├── final_project/
│   ├── Makefile
│   ├── firehose.c
│   └── metrics_log.txt
└── README.md
```

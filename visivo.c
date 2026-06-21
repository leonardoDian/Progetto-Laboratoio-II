#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>

/*
 * ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================
 * NOTA: getline() è già definita in glibc con _GNU_SOURCE.
 * La ridefinizione è stata rimossa per evitare conflitti.
 * ============================================================================
 */

/*
 * ============================================================================
 * DATA STRUCTURES
 * ============================================================================
 */

/**
 * arco - Struttura per rappresentare un arco del grafo
 * 
 * u, v: estremi dell'arco (u < v per normalizzazione)
 * weight: peso dell'arco
 * msf: flag che indica se l'arco fa parte della Minimum Spanning Forest
 * next: puntatore per la lista concatenata (gestione collisioni hash)
 */
typedef struct arco {
    int u, v;
    int weight;
    bool msf;
    struct arco *next;
} arco;

/**
 * elemento - Struttura per la lista di adiacenza
 * 
 * id: identificatore del nodo adiacente
 * w: peso dell'arco che collega i due nodi
 * msf: flag per indicare se l'arco è nella MSF
 * next: puntatore al prossimo elemento della lista
 */
typedef struct elemento {
    int id;
    int w;
    bool msf;
    struct elemento *next;
} elemento;

/**
 * unionFind - Struttura per Union-Find (Disjoint Set Union)
 * 
 * parent: array dei padri per il path compression
 * rank: array dei rank per union by rank
 */
typedef struct {
    int *parent;
    int *rank;
} unionFind;

/**
 * grafo - Struttura principale che contiene lo stato del grafo
 * 
 * gHash: tabella hash per l'accesso rapido agli archi
 * vicini: liste di adiacenza per ogni nodo
 * cCon: array delle componenti connesse (nodo minimo come identificatore)
 * numCoCo: numero di componenti connesse (escludendo nodo 0)
 * costoMSF: costo totale della Minimum Spanning Forest
 * numNodi: numero totale di nodi (+1 per l'indice 0 inutilizzato)
 * numArchi: numero totale di archi nel grafo
 * hashSize: dimensione della tabella hash
 * nmutex: numero di mutex per la hash table (riduce la contenzione)
 * mutHash: array di mutex per proteggere singole bucket della hash table
 * mutCCon: mutex per proteggere le componenti connesse (busy flag)
 * mutMSF: mutex per proteggere costoMSF, numCoCo, cCon
 * mutPrint: mutex per proteggere le operazioni di stampa
 * condCCon: condition variables per la sincronizzazione delle componenti
 * compBusy: flag per indicare se una componente è in uso
 * terminato: flag per terminare i thread worker
 * uf: struttura Union-Find per Kruskal iniziale
 */
typedef struct {
    arco **gHash;
    elemento **vicini;
    int *cCon;
    int numCoCo;
    long long costoMSF;
    int numNodi;
    int numArchi;
    int hashSize;
    int nmutex;
    pthread_mutex_t *mutHash;
    pthread_mutex_t mutCCon;
    pthread_mutex_t mutMSF;
    pthread_mutex_t mutPrint;
    pthread_cond_t *condCCon;
    bool *compBusy;
    bool terminato;
    unionFind *uf;
} grafo;

/**
 * operazione - Struttura per rappresentare un'operazione dal file
 * 
 * tipo: '+' per aggiunta, '-' per rimozione
 * u, v: estremi dell'arco
 * w: peso dell'arco (usato solo per l'aggiunta)
 */
typedef struct {
    char tipo;
    int u, v, w;
} operazione;

/**
 * buffer_t - Buffer circolare thread-safe per le operazioni
 * 
 * buffer: array circolare di operazioni
 * dimensione: dimensione del buffer
 * testa: indice del primo elemento da consumare
 * coda: indice dove inserire il prossimo elemento
 * conteggio: numero di elementi presenti nel buffer
 * mutex: mutex per proteggere l'accesso al buffer
 * nonPieno: condition variable per segnalare che il buffer non è pieno
 * nonVuoto: condition variable per segnalare che il buffer non è vuoto
 * finito: flag per indicare che non ci saranno più operazioni
 */
typedef struct {
    operazione *buffer;
    int dimensione;
    int testa;
    int coda;
    int conteggio;
    pthread_mutex_t mutex;
    pthread_cond_t nonPieno;
    pthread_cond_t nonVuoto;
    bool finito;
} buffer_t;

/*
 * ============================================================================
 * LIST UTILITY FUNCTIONS
 * ============================================================================
 */

/**
 * inserisci_ordinato - Inserisce un elemento in una lista ordinata per id
 * 
 * Complessità: O(n) dove n è la lunghezza della lista
 * 
 * @param lista: puntatore alla testa della lista (modificabile)
 * @param id: id del nodo da inserire
 * @param w: peso dell'arco
 * @param msf: flag MSF
 */
void inserisci_ordinato(elemento **lista, int id, int w, bool msf) {
    // Alloca memoria per il nuovo elemento
    elemento *nuovo = malloc(sizeof(elemento));
    if (!nuovo) { 
        perror("malloc"); 
        exit(1); 
    }
    // Inizializza i campi del nuovo elemento
    nuovo->id = id;
    nuovo->w = w;
    nuovo->msf = msf;
    nuovo->next = NULL;

    // Caso: lista vuota o primo elemento ha id maggiore
    if (!*lista || (*lista)->id > id) {
        nuovo->next = *lista;
        *lista = nuovo;
        return;
    }
    
    // Cerca la posizione di inserimento (ordinamento crescente per id)
    elemento *cur = *lista;
    while (cur->next && cur->next->id < id)
        cur = cur->next;
    
    // Inserisce dopo cur
    nuovo->next = cur->next;
    cur->next = nuovo;
}

/**
 * rimuovi_da_lista - Rimuove un elemento dalla lista per id
 * 
 * Complessità: O(n) dove n è la lunghezza della lista
 * 
 * @param lista: puntatore alla testa della lista (modificabile)
 * @param id: id del nodo da rimuovere
 */
void rimuovi_da_lista(elemento **lista, int id) {
    elemento *cur = *lista, *prev = NULL;
    while (cur) {
        if (cur->id == id) {
            // Rimuovi cur dalla lista
            if (prev) 
                prev->next = cur->next;
            else 
                *lista = cur->next;
            free(cur);
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

/*
 * ============================================================================
 * HASH TABLE FUNCTIONS
 * ============================================================================
 */

/**
 * hash_function - Funzione hash per archi non orientati
 * 
 * Utilizza XOR e moltiplicazioni per distribuire uniformemente gli archi.
 * L'hash è simmetrico (u,v) e (v,u) producono lo stesso hash.
 * 
 * @param u: primo estremo dell'arco
 * @param v: secondo estremo dell'arco
 * @param hashSize: dimensione della tabella hash
 * @return: indice della bucket nell'intervallo [0, hashSize-1]
 */
unsigned int hash_function(int u, int v, int hashSize) {
    // NOTA: l'operazione XOR è simmetrica, quindi hash(u,v) == hash(v,u)
    return ((unsigned)u * 73856093u ^ (unsigned)v * 19349663u) % hashSize;
}

/**
 * trova_arco - Cerca un arco nella tabella hash
 * 
 * L'arco è non orientato, quindi cerca sia (u,v) che (v,u).
 * 
 * @param g: puntatore al grafo
 * @param u: primo estremo
 * @param v: secondo estremo
 * @return: puntatore all'arco se trovato, NULL altrimenti
 */
arco* trova_arco(grafo *g, int u, int v) {
    // Calcola l'hash per la coppia (u,v)
    int h = hash_function(u, v, g->hashSize);
    arco *a = g->gHash[h];
    
    // Scansiona la bucket alla ricerca dell'arco
    while (a) {
        // L'arco è non orientato, quindi controlla entrambe le direzioni
        if ((a->u == u && a->v == v) || (a->u == v && a->v == u))
            return a;
        a = a->next;
    }
    return NULL;
}

/*
 * ============================================================================
 * UNION-FIND (DISJOINT SET UNION) FUNCTIONS
 * ============================================================================
 */

/**
 * uf_init - Inizializza una struttura Union-Find
 * 
 * @param n: numero di elementi
 * @return: puntatore alla struttura inizializzata
 */
unionFind* uf_init(int n) {
    unionFind *uf = malloc(sizeof(unionFind));
    if (!uf) { perror("malloc"); exit(1); }
    
    uf->parent = malloc(n * sizeof(int));
    if (!uf->parent) { perror("malloc"); exit(1); }
    
    uf->rank = malloc(n * sizeof(int));
    if (!uf->rank) { perror("malloc"); exit(1); }
    
    // Inizializza: ogni elemento è il padre di se stesso, rank = 0
    for (int i = 0; i < n; i++) {
        uf->parent[i] = i;
        uf->rank[i] = 0;
    }
    return uf;
}

/**
 * uf_free - Dealloca una struttura Union-Find
 * 
 * @param uf: puntatore alla struttura da deallocare
 */
void uf_free(unionFind *uf) {
    free(uf->parent);
    free(uf->rank);
    free(uf);
}

/**
 * uf_find - Trova il rappresentante di un elemento con path compression
 * 
 * Algoritmo iterativo con path compression parziale (path halving).
 * 
 * @param uf: puntatore alla struttura Union-Find
 * @param x: elemento di cui trovare il rappresentante
 * @return: rappresentante dell'insieme
 */
int uf_find(unionFind *uf, int x) {
    // Path compression: mentre x non è radice, comprimi il percorso
    while (uf->parent[x] != x) {
        // Path halving: salta un livello
        uf->parent[x] = uf->parent[uf->parent[x]];
        x = uf->parent[x];
    }
    return x;
}

/**
 * uf_union - Unisce due insiemi usando union by rank
 * 
 * @param uf: puntatore alla struttura Union-Find
 * @param x: primo elemento
 * @param y: secondo elemento
 */
void uf_union(unionFind *uf, int x, int y) {
    // Trova i rappresentanti
    int rx = uf_find(uf, x);
    int ry = uf_find(uf, y);
    
    // Se sono già nello stesso insieme, non fare nulla
    if (rx == ry) return;
    
    // Union by rank: attacca l'albero più piccolo al più grande
    if (uf->rank[rx] < uf->rank[ry])
        uf->parent[rx] = ry;
    else if (uf->rank[rx] > uf->rank[ry])
        uf->parent[ry] = rx;
    else {
        // Se i rank sono uguali, uno diventa padre dell'altro e incrementa il rank
        uf->parent[ry] = rx;
        uf->rank[rx]++;
    }
}

/*
 * ============================================================================
 * GRAPH READING FUNCTIONS
 * ============================================================================
 */

/**
 * cmp_archi - Funzione di confronto per qsort degli archi per peso
 * 
 * @param a: primo arco (come void*)
 * @param b: secondo arco (come void*)
 * @return: negativo se a < b, 0 se uguali, positivo se a > b
 */
int cmp_archi(const void *a, const void *b) {
    arco *aa = *(arco**)a;
    arco *bb = *(arco**)b;
    if (aa->weight < bb->weight) return -1;
    if (aa->weight > bb->weight) return 1;
    return 0;
}

/**
 * leggi_grafo - Legge un grafo da file in formato DIMACS
 * 
 * Formato:
 *   p sp N M         (N nodi, M archi)
 *   a u v w          (arco da u a v con peso w)
 * 
 * @param f: file pointer aperto in lettura
 * @param g: struttura grafo da popolare
 */
void leggi_grafo(FILE *f, grafo *g) {
    char *linea = NULL;
    size_t len = 0;
    ssize_t nread;
    bool has_p = false;

    // Legge il file riga per riga
    while ((nread = getline(&linea, &len, f)) != -1) {
        // Riga 'p': definisce il numero di nodi e archi
        if (linea[0] == 'p') {
            int nodi, archi;
            sscanf(linea, "p sp %d %d", &nodi, &archi);
            
            // +1 per l'indice 0 inutilizzato (i nodi sono 1..N)
            g->numNodi = nodi + 1;
            g->numArchi = 0;
            
            // Usa hashSize fornito o default 100000
            g->hashSize = (g->hashSize > 0) ? g->hashSize : 100000;
            
            // Alloca tabella hash
            g->gHash = calloc(g->hashSize, sizeof(arco*));
            if (!g->gHash) { perror("calloc"); exit(1); }
            
            // Alloca liste di adiacenza
            g->vicini = calloc(g->numNodi, sizeof(elemento*));
            if (!g->vicini) { perror("calloc"); exit(1); }
            
            // Alloca array componenti connesse
            g->cCon = malloc(g->numNodi * sizeof(int));
            if (!g->cCon) { perror("malloc"); exit(1); }
            
            // Inizializza Union-Find per Kruskal
            g->uf = uf_init(g->numNodi);
            
            // Inizializza componenti connesse (ogni nodo è una componente)
            for (int i = 0; i < g->numNodi; i++)
                g->cCon[i] = i;
            
            g->costoMSF = 0;
            g->numCoCo = 0;
            g->terminato = false;
            
            // Usa nmutex fornito o default 1000
            g->nmutex = (g->nmutex > 0) ? g->nmutex : 1000;
            
            // Alloca mutex per hash table
            g->mutHash = malloc(g->nmutex * sizeof(pthread_mutex_t));
            if (!g->mutHash) { perror("malloc"); exit(1); }
            for (int i = 0; i < g->nmutex; i++)
                pthread_mutex_init(&g->mutHash[i], NULL);
            
            // Inizializza mutex per sincronizzazione
            pthread_mutex_init(&g->mutCCon, NULL);
            pthread_mutex_init(&g->mutMSF, NULL);
            pthread_mutex_init(&g->mutPrint, NULL);
            
            // Alloca condition variables per componenti
            g->condCCon = malloc(g->numNodi * sizeof(pthread_cond_t));
            if (!g->condCCon) { perror("malloc"); exit(1); }
            
            // Alloca flag per componenti busy
            g->compBusy = malloc(g->numNodi * sizeof(bool));
            if (!g->compBusy) { perror("malloc"); exit(1); }
            
            for (int i = 0; i < g->numNodi; i++) {
                pthread_cond_init(&g->condCCon[i], NULL);
                g->compBusy[i] = false;
            }
            has_p = true;
        } 
        // Riga 'a': definisce un arco
        else if (linea[0] == 'a') {
            if (!has_p) {
                fprintf(stderr, "Errore: arco prima di p\n");
                exit(1);
            }
            int u, v, w;
            sscanf(linea, "a %d %d %d", &u, &v, &w);
            
            // Controllo validità nodi
            if (u < 0 || v < 0 || u >= g->numNodi || v >= g->numNodi) {
                fprintf(stderr, "Attenzione: arco fuori range ignorato\n");
                continue;
            }
            
            // Ignora self-loop
            if (u == v) continue;
            
            // Normalizza per grafo non orientato (u < v)
            if (u > v) { int tmp = u; u = v; v = tmp; }
            
            // Controlla duplicati
            if (trova_arco(g, u, v) != NULL) {
                continue;  // Arco già presente, salta
            }
            
            // Crea nuovo arco
            arco *a = malloc(sizeof(arco));
            if (!a) { perror("malloc"); exit(1); }
            a->u = u;
            a->v = v;
            a->weight = w;
            a->msf = false;
            
            // Inserisce nella hash table (in testa alla bucket)
            int h = hash_function(u, v, g->hashSize);
            a->next = g->gHash[h];
            g->gHash[h] = a;
            
            // Aggiorna liste di adiacenza
            inserisci_ordinato(&g->vicini[u], v, w, false);
            inserisci_ordinato(&g->vicini[v], u, w, false);
            
            g->numArchi++;
        }
    }
    free(linea);
    
    if (!has_p) {
        fprintf(stderr, "Errore: file grafo non ha p\n");
        exit(1);
    }
}

/*
 * ============================================================================
 * KRUSKAL ALGORITHM FOR INITIAL MSF
 * ============================================================================
 */

/**
 * kruskal - Calcola la Minimum Spanning Forest iniziale con Kruskal
 * 
 * L'algoritmo:
 * 1. Ordina tutti gli archi per peso
 * 2. Aggiunge archi in ordine di peso se non creano cicli
 * 3. Calcola le componenti connesse risultanti
 * 
 * @param g: puntatore al grafo
 */
void kruskal(grafo *g) {
    // Crea array di puntatori agli archi
    arco **archi = malloc(g->numArchi * sizeof(arco*));
    if (!archi) { perror("malloc"); exit(1); }
    
    // Copia tutti gli archi dalla hash table all'array
    int idx = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            archi[idx++] = a;
            a = a->next;
        }
    }
    
    // Ordina gli archi per peso
    qsort(archi, g->numArchi, sizeof(arco*), cmp_archi);

    // Resetta Union-Find
    unionFind *uf = g->uf;
    for (int i = 0; i < g->numNodi; i++) {
        uf->parent[i] = i;
        uf->rank[i] = 0;
    }

    // Resetta costo MSF
    pthread_mutex_lock(&g->mutMSF);
    g->costoMSF = 0;
    pthread_mutex_unlock(&g->mutMSF);
    
    // Scansiona archi in ordine di peso
    for (int i = 0; i < g->numArchi; i++) {
        arco *a = archi[i];
        int ru = uf_find(uf, a->u);
        int rv = uf_find(uf, a->v);
        
        // Se i nodi sono in componenti diverse, aggiungi l'arco alla MSF
        if (ru != rv) {
            uf_union(uf, a->u, a->v);
            a->msf = true;
            
            // Aggiorna costo MSF
            pthread_mutex_lock(&g->mutMSF);
            g->costoMSF += a->weight;
            pthread_mutex_unlock(&g->mutMSF);
            
            // Aggiorna flag MSF nelle liste di adiacenza
            elemento *e = g->vicini[a->u];
            while (e && e->id != a->v) e = e->next;
            if (e) e->msf = true;
            
            e = g->vicini[a->v];
            while (e && e->id != a->u) e = e->next;
            if (e) e->msf = true;
        }
    }
    free(archi);

    /*
     * Calcola componenti connesse basate sulla MSF
     * Ogni componente è identificata dal nodo minimo in essa contenuto
     * Il nodo 0 è escluso (non fa parte del grafo reale)
     */
    pthread_mutex_lock(&g->mutMSF);
    
    // Prima passata: trova il rappresentante di ogni nodo
    for (int i = 0; i < g->numNodi; i++)
        g->cCon[i] = uf_find(uf, i);
    
    // Trova il nodo minimo in ogni componente
    int *min_node = malloc(g->numNodi * sizeof(int));
    if (!min_node) { perror("malloc"); exit(1); }
    for (int i = 0; i < g->numNodi; i++) min_node[i] = -1;
    
    for (int i = 0; i < g->numNodi; i++) {
        int root = g->cCon[i];
        if (min_node[root] == -1 || i < min_node[root])
            min_node[root] = i;
    }
    
    // Aggiorna cCon con il nodo minimo
    for (int i = 0; i < g->numNodi; i++)
        g->cCon[i] = min_node[g->cCon[i]];
    free(min_node);

    // Conta le componenti (escludendo il nodo 0)
    bool *visti = calloc(g->numNodi, sizeof(bool));
    if (!visti) { perror("calloc"); exit(1); }
    g->numCoCo = 0;
    for (int i = 1; i < g->numNodi; i++) {
        if (!visti[g->cCon[i]]) {
            visti[g->cCon[i]] = true;
            g->numCoCo++;
        }
    }
    free(visti);
    pthread_mutex_unlock(&g->mutMSF);
}

/*
 * ============================================================================
 * SYNCHRONIZATION FUNCTIONS
 * ============================================================================
 */

/**
 * lock_componente - Acquisisce il lock su una componente connessa
 * 
 * Utilizza una condizione per attendere se la componente è busy.
 * 
 * @param g: puntatore al grafo
 * @param comp: id della componente da lockare
 */
void lock_componente(grafo *g, int comp) {
    if (comp < 0 || comp >= g->numNodi) return;
    
    pthread_mutex_lock(&g->mutCCon);
    // Attendi finché la componente non è libera o il grafo non è terminato
    while (g->compBusy[comp] && !g->terminato)
        pthread_cond_wait(&g->condCCon[comp], &g->mutCCon);
    if (!g->terminato)
        g->compBusy[comp] = true;
    pthread_mutex_unlock(&g->mutCCon);
}

/**
 * unlock_componente - Rilascia il lock su una componente connessa
 * 
 * @param g: puntatore al grafo
 * @param comp: id della componente da sbloccare
 */
void unlock_componente(grafo *g, int comp) {
    if (comp < 0 || comp >= g->numNodi) return;
    
    pthread_mutex_lock(&g->mutCCon);
    g->compBusy[comp] = false;
    // Sveglia tutti i thread in attesa su questa componente
    pthread_cond_broadcast(&g->condCCon[comp]);
    pthread_mutex_unlock(&g->mutCCon);
}

/**
 * lock_componenti - Acquisisce i lock su due componenti con ordine
 * 
 * Per evitare deadlock, i lock vengono acquisiti in ordine crescente.
 * 
 * @param g: puntatore al grafo
 * @param u: primo nodo
 * @param v: secondo nodo
 */
void lock_componenti(grafo *g, int u, int v) {
    // Ottieni gli id delle componenti
    int cu, cv;
    pthread_mutex_lock(&g->mutMSF);
    cu = g->cCon[u];
    cv = g->cCon[v];
    pthread_mutex_unlock(&g->mutMSF);
    
    // Acquisisci lock in ordine per evitare deadlock
    if (cu == cv) {
        lock_componente(g, cu);
    } else {
        if (cu < cv) {
            lock_componente(g, cu);
            lock_componente(g, cv);
        } else {
            lock_componente(g, cv);
            lock_componente(g, cu);
        }
    }
}

/**
 * unlock_componenti - Rilascia i lock su due componenti
 * 
 * @param g: puntatore al grafo
 * @param u: primo nodo
 * @param v: secondo nodo
 */
void unlock_componenti(grafo *g, int u, int v) {
    int cu, cv;
    pthread_mutex_lock(&g->mutMSF);
    cu = g->cCon[u];
    cv = g->cCon[v];
    pthread_mutex_unlock(&g->mutMSF);
    
    if (cu == cv) {
        unlock_componente(g, cu);
    } else {
        unlock_componente(g, cu);
        unlock_componente(g, cv);
    }
}

/**
 * lock_hash - Acquisisce il lock sulla bucket hash di un arco
 * 
 * @param g: puntatore al grafo
 * @param u: primo estremo
 * @param v: secondo estremo
 */
void lock_hash(grafo *g, int u, int v) {
    int h = hash_function(u, v, g->hashSize);
    int m = h % g->nmutex;
    pthread_mutex_lock(&g->mutHash[m]);
}

/**
 * unlock_hash - Rilascia il lock sulla bucket hash di un arco
 * 
 * @param g: puntatore al grafo
 * @param u: primo estremo
 * @param v: secondo estremo
 */
void unlock_hash(grafo *g, int u, int v) {
    int h = hash_function(u, v, g->hashSize);
    int m = h % g->nmutex;
    pthread_mutex_unlock(&g->mutHash[m]);
}

/*
 * ============================================================================
 * DFS FOR MSF PATH SEARCH
 * ============================================================================
 */

/**
 * dfs_trova_max - DFS per trovare l'arco di peso massimo nel percorso MSF
 * 
 * Cerca un percorso tra curr e target utilizzando solo archi MSF.
 * Durante la ricerca, tiene traccia dell'arco di peso massimo incontrato.
 * 
 * @param g: puntatore al grafo
 * @param curr: nodo corrente
 * @param target: nodo target
 * @param visited: array di flag per nodi visitati
 * @param max_arco: puntatore per memorizzare l'arco di peso massimo
 * @param max_peso: puntatore per memorizzare il peso massimo
 * @return: true se esiste un percorso, false altrimenti
 */
bool dfs_trova_max(grafo *g, int curr, int target, bool *visited, arco **max_arco, int *max_peso) {
    visited[curr] = true;
    if (curr == target) return true;
    
    // Esplora tutti i vicini del nodo corrente
    elemento *e = g->vicini[curr];
    while (e) {
        // Considera solo archi MSF e nodi non visitati
        if (e->msf && !visited[e->id]) {
            arco *a = trova_arco(g, curr, e->id);
            if (a && dfs_trova_max(g, e->id, target, visited, max_arco, max_peso)) {
                // Se il peso dell'arco corrente è maggiore del massimo trovato, aggiorna
                if (a->weight > *max_peso) {
                    *max_peso = a->weight;
                    *max_arco = a;
                }
                return true;
            }
        }
        e = e->next;
    }
    return false;
}

/*
 * ============================================================================
 * OPERATION FUNCTIONS (ADD/DELETE EDGE)
 * ============================================================================
 */

/**
 * ricalcola_numCoCo - Ricalcola il numero di componenti connesse
 * 
 * @param g: puntatore al grafo
 */
void ricalcola_numCoCo(grafo *g) {
    pthread_mutex_lock(&g->mutMSF);
    bool *visti = calloc(g->numNodi, sizeof(bool));
    if (!visti) { perror("calloc"); exit(1); }
    
    g->numCoCo = 0;
    // Conta le componenti escludendo il nodo 0
    for (int i = 1; i < g->numNodi; i++) {
        if (!visti[g->cCon[i]]) {
            visti[g->cCon[i]] = true;
            g->numCoCo++;
        }
    }
    free(visti);
    pthread_mutex_unlock(&g->mutMSF);
}

/**
 * add_arco - Aggiunge un arco al grafo e aggiorna la MSF
 * 
 * Algoritmo:
 * 1. Se l'arco connette due componenti diverse: aggiungi alla MSF
 * 2. Se l'arco è in una componente: aggiungi e rimuovi l'arco di peso massimo nel ciclo
 * 
 * @param g: puntatore al grafo
 * @param u: primo estremo
 * @param v: secondo estremo
 * @param w: peso dell'arco
 * @param valido: puntatore a bool che indica se l'operazione è valida
 */
void add_arco(grafo *g, int u, int v, int w, bool *valido) {
    // Validazione input
    if (u < 0 || v < 0 || u >= g->numNodi || v >= g->numNodi || u == v) {
        *valido = false;
        return;
    }
    if (u > v) { int tmp = u; u = v; v = tmp; }

    // Acquisisci lock sulle componenti
    lock_componenti(g, u, v);
    if (g->terminato) { unlock_componenti(g, u, v); *valido = false; return; }

    // Acquisisci lock sulla hash table
    lock_hash(g, u, v);
    // Controlla se l'arco esiste già
    if (trova_arco(g, u, v) != NULL) {
        unlock_hash(g, u, v);
        unlock_componenti(g, u, v);
        *valido = false;
        return;
    }
    
    // Crea nuovo arco
    arco *nuovo = malloc(sizeof(arco));
    if (!nuovo) { perror("malloc"); exit(1); }
    nuovo->u = u;
    nuovo->v = v;
    nuovo->weight = w;
    nuovo->msf = false;
    
    // Inserisce nella hash table
    int h = hash_function(u, v, g->hashSize);
    nuovo->next = g->gHash[h];
    g->gHash[h] = nuovo;
    
    // Aggiorna liste di adiacenza
    inserisci_ordinato(&g->vicini[u], v, w, false);
    inserisci_ordinato(&g->vicini[v], u, w, false);
    g->numArchi++;
    unlock_hash(g, u, v);

    // Ottieni le componenti dei nodi
    int cu, cv;
    pthread_mutex_lock(&g->mutMSF);
    cu = g->cCon[u];
    cv = g->cCon[v];
    pthread_mutex_unlock(&g->mutMSF);
    
    if (cu == cv) {
        /*
         * Caso 1: u e v sono nella stessa componente
         * L'aggiunta dell'arco crea un ciclo.
         * Se il nuovo arco ha peso minore dell'arco di peso massimo nel ciclo,
         * sostituisci l'arco di peso massimo con il nuovo.
         */
        bool *visited = calloc(g->numNodi, sizeof(bool));
        if (!visited) { perror("calloc"); exit(1); }
        
        arco *max_arco = NULL;
        int max_peso = -1;
        
        // Trova il percorso MSF tra u e v e l'arco di peso massimo
        if (dfs_trova_max(g, u, v, visited, &max_arco, &max_peso)) {
            // Se il nuovo arco è più leggero, sostituisci
            if (max_arco && w < max_peso) {
                // Rimuovi l'arco di peso massimo dalla MSF
                lock_hash(g, max_arco->u, max_arco->v);
                max_arco->msf = false;
                
                elemento *e = g->vicini[max_arco->u];
                while (e && e->id != max_arco->v) e = e->next;
                if (e) e->msf = false;
                
                e = g->vicini[max_arco->v];
                while (e && e->id != max_arco->u) e = e->next;
                if (e) e->msf = false;
                
                pthread_mutex_lock(&g->mutMSF);
                g->costoMSF -= max_arco->weight;
                pthread_mutex_unlock(&g->mutMSF);
                unlock_hash(g, max_arco->u, max_arco->v);

                // Aggiungi il nuovo arco alla MSF
                lock_hash(g, u, v);
                nuovo->msf = true;
                
                elemento *e1 = g->vicini[u];
                while (e1 && e1->id != v) e1 = e1->next;
                if (e1) e1->msf = true;
                
                e1 = g->vicini[v];
                while (e1 && e1->id != u) e1 = e1->next;
                if (e1) e1->msf = true;
                
                pthread_mutex_lock(&g->mutMSF);
                g->costoMSF += w;
                pthread_mutex_unlock(&g->mutMSF);
                unlock_hash(g, u, v);
            }
        }
        free(visited);
    } else {
        /*
         * Caso 2: u e v sono in componenti diverse
         * L'arco viene aggiunto alla MSF (collega due componenti)
         */
        lock_hash(g, u, v);
        nuovo->msf = true;
        
        elemento *e = g->vicini[u];
        while (e && e->id != v) e = e->next;
        if (e) e->msf = true;
        
        e = g->vicini[v];
        while (e && e->id != u) e = e->next;
        if (e) e->msf = true;
        
        pthread_mutex_lock(&g->mutMSF);
        g->costoMSF += w;
        pthread_mutex_unlock(&g->mutMSF);
        unlock_hash(g, u, v);

        // Unisci le due componenti (usa il nodo minimo come identificatore)
        pthread_mutex_lock(&g->mutMSF);
        int newRoot = (cu < cv) ? cu : cv;
        int oldRoot = (cu < cv) ? cv : cu;
        for (int i = 0; i < g->numNodi; i++) {
            if (g->cCon[i] == oldRoot)
                g->cCon[i] = newRoot;
        }
        pthread_mutex_unlock(&g->mutMSF);
        ricalcola_numCoCo(g);
    }
    unlock_componenti(g, u, v);
    *valido = true;
}

/**
 * canc_arco - Rimuove un arco dal grafo e aggiorna la MSF
 * 
 * Algoritmo:
 * 1. Rimuovi l'arco
 * 2. Se l'arco era nella MSF, trova il minimo arco per riconnettere
 * 
 * @param g: puntatore al grafo
 * @param u: primo estremo
 * @param v: secondo estremo
 * @param valido: puntatore a bool che indica se l'operazione è valida
 */
void canc_arco(grafo *g, int u, int v, bool *valido) {
    // Validazione input
    if (u < 0 || v < 0 || u >= g->numNodi || v >= g->numNodi || u == v) {
        *valido = false;
        return;
    }
    if (u > v) { int tmp = u; u = v; v = tmp; }

    lock_componenti(g, u, v);
    if (g->terminato) { unlock_componenti(g, u, v); *valido = false; return; }

    lock_hash(g, u, v);
    // Cerca l'arco da rimuovere
    arco *a = trova_arco(g, u, v);
    if (!a) {
        unlock_hash(g, u, v);
        unlock_componenti(g, u, v);
        *valido = false;
        return;
    }
    
    bool era_msf = a->msf;
    int peso = a->weight;

    // Rimuovi l'arco dalla hash table
    int h = hash_function(u, v, g->hashSize);
    arco *cur = g->gHash[h], *prev = NULL;
    while (cur) {
        if (cur == a) {
            if (prev) prev->next = cur->next;
            else g->gHash[h] = cur->next;
            free(cur);
            break;
        }
        prev = cur;
        cur = cur->next;
    }
    
    // Rimuovi dalle liste di adiacenza
    rimuovi_da_lista(&g->vicini[u], v);
    rimuovi_da_lista(&g->vicini[v], u);
    g->numArchi--;
    unlock_hash(g, u, v);

    if (era_msf) {
        /*
         * L'arco rimosso era nella MSF.
         * Dobbiamo trovare il minimo arco che riconnette le due componenti.
         */
        pthread_mutex_lock(&g->mutMSF);
        g->costoMSF -= peso;
        pthread_mutex_unlock(&g->mutMSF);
        
        // Ricalcola le componenti connesse basate sulla MSF
        bool *visited = calloc(g->numNodi, sizeof(bool));
        if (!visited) { perror("calloc"); exit(1); }
        
        int *comp = malloc(g->numNodi * sizeof(int));
        if (!comp) { perror("malloc"); exit(1); }
        
        int num_comp = 0;
        
        // DFS per trovare le componenti
        for (int i = 1; i < g->numNodi; i++) {
            if (!visited[i]) {
                int *stack = malloc(g->numNodi * sizeof(int));
                if (!stack) { perror("malloc"); exit(1); }
                int top = 0;
                stack[top++] = i;
                visited[i] = true;
                comp[i] = i;
                
                while (top) {
                    int nodo = stack[--top];
                    comp[nodo] = i;
                    elemento *e = g->vicini[nodo];
                    while (e) {
                        if (e->msf && !visited[e->id] && e->id != 0) {
                            visited[e->id] = true;
                            stack[top++] = e->id;
                        }
                        e = e->next;
                    }
                }
                
                // Trova il nodo minimo nella componente
                int min = i;
                for (int j = 1; j < g->numNodi; j++) {
                    if (comp[j] == i && j < min) min = j;
                }
                for (int j = 1; j < g->numNodi; j++) {
                    if (comp[j] == i) comp[j] = min;
                }
                num_comp++;
                free(stack);
            }
        }
        comp[0] = 0;
        
        pthread_mutex_lock(&g->mutMSF);
        for (int i = 0; i < g->numNodi; i++)
            g->cCon[i] = comp[i];
        g->numCoCo = num_comp;
        pthread_mutex_unlock(&g->mutMSF);
        free(comp);
        free(visited);

        // Cerca l'arco minimo che riconnette due componenti diverse
        arco *min_arco = NULL;
        int min_peso = INT_MAX;
        for (int i = 0; i < g->hashSize; i++) {
            arco *a2 = g->gHash[i];
            while (a2) {
                // Escludi archi già nella MSF e quelli che coinvolgono il nodo 0
                if (!a2->msf && a2->u != 0 && a2->v != 0) {
                    int cu, cv;
                    pthread_mutex_lock(&g->mutMSF);
                    cu = g->cCon[a2->u];
                    cv = g->cCon[a2->v];
                    pthread_mutex_unlock(&g->mutMSF);
                    
                    if (cu != cv && a2->weight < min_peso) {
                        min_peso = a2->weight;
                        min_arco = a2;
                    }
                }
                a2 = a2->next;
            }
        }
        
        // Se trovato un arco, aggiungilo alla MSF
        if (min_arco) {
            lock_hash(g, min_arco->u, min_arco->v);
            min_arco->msf = true;
            
            elemento *e = g->vicini[min_arco->u];
            while (e && e->id != min_arco->v) e = e->next;
            if (e) e->msf = true;
            
            e = g->vicini[min_arco->v];
            while (e && e->id != min_arco->u) e = e->next;
            if (e) e->msf = true;
            
            pthread_mutex_lock(&g->mutMSF);
            g->costoMSF += min_arco->weight;
            pthread_mutex_unlock(&g->mutMSF);
            unlock_hash(g, min_arco->u, min_arco->v);

            // Unisci le due componenti
            int cu, cv;
            pthread_mutex_lock(&g->mutMSF);
            cu = g->cCon[min_arco->u];
            cv = g->cCon[min_arco->v];
            int newRoot = (cu < cv) ? cu : cv;
            int oldRoot = (cu < cv) ? cv : cu;
            for (int i = 1; i < g->numNodi; i++) {
                if (g->cCon[i] == oldRoot)
                    g->cCon[i] = newRoot;
            }
            pthread_mutex_unlock(&g->mutMSF);
            ricalcola_numCoCo(g);
        }
    }
    unlock_componenti(g, u, v);
    *valido = true;
}

/*
 * ============================================================================
 * CIRCULAR BUFFER FUNCTIONS
 * ============================================================================
 */

/**
 * buffer_init - Inizializza il buffer circolare
 * 
 * @param buf: puntatore al buffer
 * @param dim: dimensione del buffer
 */
void buffer_init(buffer_t *buf, int dim) {
    buf->buffer = malloc(dim * sizeof(operazione));
    if (!buf->buffer) { perror("malloc"); exit(1); }
    buf->dimensione = dim;
    buf->testa = 0;
    buf->coda = 0;
    buf->conteggio = 0;
    buf->finito = false;
    pthread_mutex_init(&buf->mutex, NULL);
    pthread_cond_init(&buf->nonPieno, NULL);
    pthread_cond_init(&buf->nonVuoto, NULL);
}

/**
 * buffer_destroy - Dealloca il buffer
 * 
 * @param buf: puntatore al buffer
 */
void buffer_destroy(buffer_t *buf) {
    free(buf->buffer);
    pthread_mutex_destroy(&buf->mutex);
    pthread_cond_destroy(&buf->nonPieno);
    pthread_cond_destroy(&buf->nonVuoto);
}

/**
 * buffer_inserisci - Inserisce un'operazione nel buffer
 * 
 * Blocca se il buffer è pieno.
 * 
 * @param buf: puntatore al buffer
 * @param op: operazione da inserire
 * @param g: puntatore al grafo (per controllare terminato)
 */
void buffer_inserisci(buffer_t *buf, operazione op, grafo *g) {
    pthread_mutex_lock(&buf->mutex);
    // Attendi se il buffer è pieno e il grafo non è terminato
    while (buf->conteggio == buf->dimensione && !g->terminato)
        pthread_cond_wait(&buf->nonPieno, &buf->mutex);
    if (g->terminato) {
        pthread_mutex_unlock(&buf->mutex);
        return;
    }
    // Inserisci l'operazione
    buf->buffer[buf->coda] = op;
    buf->coda = (buf->coda + 1) % buf->dimensione;
    buf->conteggio++;
    pthread_cond_signal(&buf->nonVuoto);
    pthread_mutex_unlock(&buf->mutex);
}

/**
 * buffer_estrai - Estrae un'operazione dal buffer
 * 
 * Blocca se il buffer è vuoto.
 * 
 * @param buf: puntatore al buffer
 * @param op: puntatore dove memorizzare l'operazione estratta
 * @param g: puntatore al grafo
 * @return: true se estratta, false se buffer vuoto o terminato
 */
bool buffer_estrai(buffer_t *buf, operazione *op, grafo *g) {
    pthread_mutex_lock(&buf->mutex);
    // Attendi se il buffer è vuoto, non finito e il grafo non è terminato
    while (buf->conteggio == 0 && !buf->finito && !g->terminato)
        pthread_cond_wait(&buf->nonVuoto, &buf->mutex);
    if (buf->conteggio == 0 || g->terminato) {
        pthread_mutex_unlock(&buf->mutex);
        return false;
    }
    // Estrai l'operazione
    *op = buf->buffer[buf->testa];
    buf->testa = (buf->testa + 1) % buf->dimensione;
    buf->conteggio--;
    pthread_cond_signal(&buf->nonPieno);
    pthread_mutex_unlock(&buf->mutex);
    return true;
}

/*
 * ============================================================================
 * WORKER THREAD
 * ============================================================================
 */

/**
 * thread_arg_t - Argomenti per il thread worker
 * 
 * g: puntatore al grafo
 * buf: puntatore al buffer delle operazioni
 */
typedef struct {
    grafo *g;
    buffer_t *buf;
} thread_arg_t;

/**
 * worker_thread - Funzione eseguita dai thread worker
 * 
 * Estrae operazioni dal buffer e le esegue.
 * 
 * @param arg: puntatore a thread_arg_t
 * @return: NULL
 */
void *worker_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    grafo *g = targ->g;
    buffer_t *buf = targ->buf;
    operazione op;
    bool valido;
    
    while (buffer_estrai(buf, &op, g)) {
        if (g->terminato) break;
        
        if (op.tipo == '+') {
            add_arco(g, op.u, op.v, op.w, &valido);
            
            pthread_mutex_lock(&g->mutPrint);
            if (valido) {
                printf("+ %d %d %d %d %lld\n", op.u, op.v, op.w, g->numCoCo, g->costoMSF);
            } else {
                printf("+ %d %d %d 0\n", op.u, op.v, op.w);
            }
            fflush(stdout);
            pthread_mutex_unlock(&g->mutPrint);
        } else if (op.tipo == '-') {
            canc_arco(g, op.u, op.v, &valido);
            
            pthread_mutex_lock(&g->mutPrint);
            if (valido) {
                printf("- %d %d %d %lld\n", op.u, op.v, g->numCoCo, g->costoMSF);
            } else {
                printf("- %d %d 0\n", op.u, op.v);
            }
            fflush(stdout);
            pthread_mutex_unlock(&g->mutPrint);
        }
    }
    return NULL;
}

/*
 * ============================================================================
 * STATISTICS FUNCTIONS
 * ============================================================================
 */

/**
 * calcola_statistiche - Calcola statistiche sulla tabella hash
 * 
 * @param g: puntatore al grafo
 * @param non_vuote: puntatore per memorizzare il numero di bucket non vuote
 * @param media: puntatore per memorizzare la lunghezza media delle liste
 * @param max_len: puntatore per memorizzare la lunghezza massima delle liste
 */
void calcola_statistiche(grafo *g, int *non_vuote, double *media, int *max_len) {
    *non_vuote = 0;
    *max_len = 0;
    int totale = 0;
    
    for (int i = 0; i < g->hashSize; i++) {
        int len = 0;
        arco *a = g->gHash[i];
        while (a) { len++; a = a->next; }
        if (len > 0) {
            (*non_vuote)++;
            totale += len;
            if (len > *max_len) *max_len = len;
        }
    }
    *media = (*non_vuote > 0) ? (double)totale / (*non_vuote) : 0.0;
}

/**
 * ricalcola_finale - Ricalcola tutti i valori finali del grafo
 * 
 * @param g: puntatore al grafo
 */
void ricalcola_finale(grafo *g) {
    pthread_mutex_lock(&g->mutMSF);
    
    // Ricalcola costo MSF
    g->costoMSF = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            if (a->msf) g->costoMSF += a->weight;
            a = a->next;
        }
    }
    
    // Ricalcola componenti connesse
    bool *visited = calloc(g->numNodi, sizeof(bool));
    if (!visited) { perror("calloc"); exit(1); }
    
    int *comp = malloc(g->numNodi * sizeof(int));
    if (!comp) { perror("malloc"); exit(1); }
    
    int num_comp = 0;
    for (int i = 1; i < g->numNodi; i++) {
        if (!visited[i]) {
            int *stack = malloc(g->numNodi * sizeof(int));
            if (!stack) { perror("malloc"); exit(1); }
            int top = 0;
            stack[top++] = i;
            visited[i] = true;
            comp[i] = i;
            
            while (top) {
                int nodo = stack[--top];
                comp[nodo] = i;
                elemento *e = g->vicini[nodo];
                while (e) {
                    if (e->msf && !visited[e->id] && e->id != 0) {
                        visited[e->id] = true;
                        stack[top++] = e->id;
                    }
                    e = e->next;
                }
            }
            
            int min = i;
            for (int j = 1; j < g->numNodi; j++) {
                if (comp[j] == i && j < min) min = j;
            }
            for (int j = 1; j < g->numNodi; j++) {
                if (comp[j] == i) comp[j] = min;
            }
            num_comp++;
            free(stack);
        }
    }
    comp[0] = 0;
    for (int i = 0; i < g->numNodi; i++)
        g->cCon[i] = comp[i];
    g->numCoCo = num_comp;
    free(comp);
    free(visited);
    pthread_mutex_unlock(&g->mutMSF);

    // Ricalcola numero di archi
    g->numArchi = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) { g->numArchi++; a = a->next; }
    }
}

/*
 * ============================================================================
 * MAIN FUNCTION
 * ============================================================================
 */

int main(int argc, char *argv[]) {
    int opt;
    int threads = 3;           // Numero di thread worker
    int hashSize = 100000;     // Dimensione della tabella hash
    int nmutex = 1000;         // Numero di mutex per la hash table
    char *file_grafo = NULL;
    char *file_archi = NULL;

    // Parsing degli argomenti da riga di comando
    while ((opt = getopt(argc, argv, "t:H:M:")) != -1) {
        switch (opt) {
            case 't': 
                threads = atoi(optarg); 
                if (threads < 1) threads = 1; 
                break;
            case 'H': 
                hashSize = atoi(optarg); 
                if (hashSize < 1) hashSize = 1; 
                break;
            case 'M': 
                nmutex = atoi(optarg); 
                if (nmutex < 1) nmutex = 1; 
                break;
            default:
                fprintf(stderr, "Uso: %s file_grafo file_archi [-t threads] [-H hashsize] [-M nmutex]\n", argv[0]);
                exit(1);
        }
    }
    
    // Verifica che siano stati specificati i file
    if (optind + 2 > argc) {
        fprintf(stderr, "Uso: %s file_grafo file_archi [-t threads] [-H hashsize] [-M nmutex]\n", argv[0]);
        exit(1);
    }
    file_grafo = argv[optind];
    file_archi = argv[optind+1];

    // Inizializza struttura grafo
    grafo g = {0};
    g.hashSize = hashSize;
    g.nmutex = nmutex;

    // Leggi il grafo dal file
    FILE *fg = fopen(file_grafo, "r");
    if (!fg) { perror("fopen grafo"); exit(1); }
    leggi_grafo(fg, &g);
    fclose(fg);

    // Calcola MSF iniziale con Kruskal
    kruskal(&g);

    // Stampa stato iniziale
    pthread_mutex_lock(&g.mutPrint);
    printf("%d %d %lld\n", g.numArchi, g.numCoCo, g.costoMSF);
    pthread_mutex_unlock(&g.mutPrint);

    // Inizializza buffer per le operazioni
    buffer_t buffer;
    buffer_init(&buffer, 10000);

    // Crea thread worker
    thread_arg_t targ = { &g, &buffer };
    pthread_t *tid = malloc(threads * sizeof(pthread_t));
    if (!tid) { perror("malloc"); exit(1); }
    for (int i = 0; i < threads; i++)
        pthread_create(&tid[i], NULL, worker_thread, &targ);

    // Leggi le operazioni dal file
    FILE *fo = fopen(file_archi, "r");
    if (!fo) {
        fprintf(stderr, "Attenzione: non riesco ad aprire %s\n", file_archi);
    } else {
        char *linea = NULL;
        size_t len = 0;
        ssize_t nread;
        while ((nread = getline(&linea, &len, fo)) != -1) {
            // Salta commenti e linee vuote
            if (linea[0] == '#' || linea[0] == 'c' || linea[0] == '\n') continue;
            
            operazione op;
            if (linea[0] == '+') {
                if (sscanf(linea, "+ %d %d %d", &op.u, &op.v, &op.w) == 3) {
                    op.tipo = '+';
                    buffer_inserisci(&buffer, op, &g);
                }
            } else if (linea[0] == '-') {
                if (sscanf(linea, "- %d %d", &op.u, &op.v) == 2) {
                    op.tipo = '-';
                    op.w = 0;
                    buffer_inserisci(&buffer, op, &g);
                }
            }
        }
        free(linea);
        fclose(fo);
    }

    // Segnala che non ci saranno più operazioni
    pthread_mutex_lock(&buffer.mutex);
    buffer.finito = true;
    pthread_cond_broadcast(&buffer.nonVuoto);
    pthread_mutex_unlock(&buffer.mutex);

    // Attendi la terminazione dei thread
    for (int i = 0; i < threads; i++)
        pthread_join(tid[i], NULL);
    free(tid);

    pthread_mutex_lock(&g.mutPrint);
    printf("Operazioni terminate\n");
    pthread_mutex_unlock(&g.mutPrint);

    // Ricalcola i valori finali
    ricalcola_finale(&g);

    // Calcola e stampa statistiche sulla hash table
    int non_vuote, max_len;
    double media;
    calcola_statistiche(&g, &non_vuote, &media, &max_len);
    
    pthread_mutex_lock(&g.mutPrint);
    printf("Numero posizioni non vuote: %d\n", non_vuote);
    printf("Lunghezza media liste: %.7f\n", media);
    printf("Lunghezza massima liste: %d\n", max_len);
    printf("%d %d %lld\n", g.numArchi, g.numCoCo, g.costoMSF);
    pthread_mutex_unlock(&g.mutPrint);

    /*
     * ========================================================================
     * CLEANUP
     * ========================================================================
     */
    
    // Dealloca buffer
    buffer_destroy(&buffer);
    
    // Dealloca componenti
    for (int i = 0; i < g.numNodi; i++) {
        pthread_cond_destroy(&g.condCCon[i]);
        elemento *e = g.vicini[i];
        while (e) {
            elemento *next = e->next;
            free(e);
            e = next;
        }
    }
    free(g.vicini);
    free(g.cCon);
    free(g.condCCon);
    free(g.compBusy);
    
    // Dealloca mutex
    for (int i = 0; i < g.nmutex; i++)
        pthread_mutex_destroy(&g.mutHash[i]);
    free(g.mutHash);
    pthread_mutex_destroy(&g.mutCCon);
    pthread_mutex_destroy(&g.mutMSF);
    pthread_mutex_destroy(&g.mutPrint);
    
    // Dealloca hash table
    for (int i = 0; i < g.hashSize; i++) {
        arco *a = g.gHash[i];
        while (a) {
            arco *next = a->next;
            free(a);
            a = next;
        }
    }
    free(g.gHash);
    
    // Dealloca Union-Find
    uf_free(g.uf);

    return 0;
}
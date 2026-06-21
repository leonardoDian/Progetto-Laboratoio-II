#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <math.h>

/* ========== STRUTTURE DATI ========== */

// Struttura per gli archi
typedef struct arco {
    int u, v;
    int weight;
    bool msf;
    struct arco *next;
} arco;

// Struttura per gli elementi delle liste di adiacenza
typedef struct elemento {
    int id;
    int w;
    bool msf;
    struct elemento *next;
} elemento;

// Struttura principale del grafo
typedef struct {
    arco **gHash;       // tabella hash
    elemento **vicini;  // liste di adiacenza
    int *cCon;          // componenti connesse
    int numCoCo;        // numero componenti connesse
    long costoMSF;      // costo della MSF
    int numNodi;        // numero di nodi
    int numArchi;       // numero di archi
    int hashSize;       // dimensione tabella hash
    int nMutex;         // numero mutex
    pthread_mutex_t *mutexArray;  // array di mutex
    pthread_mutex_t compMutex;    // mutex per componenti
    pthread_cond_t compCond;      // condition variable per componenti
    bool *compBusy;     // componenti busy
} grafo;

// Struttura per le operazioni da eseguire
typedef struct {
    char op;        // '+' o '-'
    int u, v, w;    // parametri
} operazione;

// Struttura per il buffer produttori/consumatori
typedef struct {
    operazione *buffer;
    int size;
    int head;
    int tail;
    int count;
    bool done;
    pthread_mutex_t mutex;
    pthread_cond_t notEmpty;
    pthread_cond_t notFull;
} buffer_t;

// Struttura per i thread consumer
typedef struct {
    grafo *g;
    buffer_t *buffer;
    int threadId;
} thread_data_t;

/* ========== UNION-FIND ========== */

typedef struct {
    int *parent;
    int *rank;
} union_find;

union_find* uf_create(int n) {
    union_find *uf = malloc(sizeof(union_find));
    uf->parent = malloc(n * sizeof(int));
    uf->rank = malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) {
        uf->parent[i] = i;
        uf->rank[i] = 0;
    }
    return uf;
}

void uf_destroy(union_find *uf) {
    free(uf->parent);
    free(uf->rank);
    free(uf);
}

int uf_find(union_find *uf, int x) {
    if (uf->parent[x] != x) {
        uf->parent[x] = uf_find(uf, uf->parent[x]);
    }
    return uf->parent[x];
}

void uf_union(union_find *uf, int x, int y) {
    int rx = uf_find(uf, x);
    int ry = uf_find(uf, y);
    if (rx == ry) return;
    if (uf->rank[rx] < uf->rank[ry]) {
        uf->parent[rx] = ry;
    } else if (uf->rank[rx] > uf->rank[ry]) {
        uf->parent[ry] = rx;
    } else {
        uf->parent[ry] = rx;
        uf->rank[rx]++;
    }
}

/* ========== FUNZIONI DEL GRAFO ========== */

// Funzione hash
int hash_function(int u, int v, int size) {
    return (u * 31 + v * 17) % size;
}

// Crea un nuovo arco
arco* crea_arco(int u, int v, int w, bool msf) {
    arco *a = malloc(sizeof(arco));
    a->u = u;
    a->v = v;
    a->weight = w;
    a->msf = msf;
    a->next = NULL;
    return a;
}

// Crea un nuovo elemento per lista adiacenza
elemento* crea_elemento(int id, int w, bool msf) {
    elemento *e = malloc(sizeof(elemento));
    e->id = id;
    e->w = w;
    e->msf = msf;
    e->next = NULL;
    return e;
}

// Inizializza il grafo
grafo* grafo_init(int numNodi, int hashSize, int nMutex) {
    grafo *g = malloc(sizeof(grafo));
    g->numNodi = numNodi;
    g->hashSize = hashSize;
    g->nMutex = nMutex;
    g->numArchi = 0;
    g->numCoCo = 0;
    g->costoMSF = 0;
    
    g->gHash = calloc(hashSize, sizeof(arco*));
    g->vicini = calloc(numNodi, sizeof(elemento*));
    g->cCon = malloc(numNodi * sizeof(int));
    g->compBusy = calloc(numNodi, sizeof(bool));
    
    g->mutexArray = malloc(nMutex * sizeof(pthread_mutex_t));
    for (int i = 0; i < nMutex; i++) {
        pthread_mutex_init(&g->mutexArray[i], NULL);
    }
    
    pthread_mutex_init(&g->compMutex, NULL);
    pthread_cond_init(&g->compCond, NULL);
    
    return g;
}

// Dealloca il grafo
void grafo_destroy(grafo *g) {
    // Dealloca tabella hash
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            arco *tmp = a;
            a = a->next;
            free(tmp);
        }
    }
    free(g->gHash);
    
    // Dealloca liste adiacenza
    for (int i = 0; i < g->numNodi; i++) {
        elemento *e = g->vicini[i];
        while (e) {
            elemento *tmp = e;
            e = e->next;
            free(tmp);
        }
    }
    free(g->vicini);
    
    free(g->cCon);
    free(g->compBusy);
    
    for (int i = 0; i < g->nMutex; i++) {
        pthread_mutex_destroy(&g->mutexArray[i]);
    }
    free(g->mutexArray);
    
    pthread_mutex_destroy(&g->compMutex);
    pthread_cond_destroy(&g->compCond);
    
    free(g);
}

// Cerca un arco nella tabella hash
arco* trova_arco(grafo *g, int u, int v) {
    int hash = hash_function(u, v, g->hashSize);
    arco *a = g->gHash[hash];
    while (a) {
        if ((a->u == u && a->v == v) || (a->u == v && a->v == u)) {
            return a;
        }
        a = a->next;
    }
    return NULL;
}

// Aggiunge arco alla tabella hash
void aggiungi_hash(grafo *g, arco *a) {
    int hash = hash_function(a->u, a->v, g->hashSize);
    a->next = g->gHash[hash];
    g->gHash[hash] = a;
}

// Rimuove arco dalla tabella hash
bool rimuovi_hash(grafo *g, int u, int v) {
    int hash = hash_function(u, v, g->hashSize);
    arco **ptr = &g->gHash[hash];
    while (*ptr) {
        arco *a = *ptr;
        if ((a->u == u && a->v == v) || (a->u == v && a->v == u)) {
            *ptr = a->next;
            free(a);
            return true;
        }
        ptr = &a->next;
    }
    return false;
}

// Aggiunge nodo alla lista di adiacenza in ordine
void aggiungi_adiacenza(grafo *g, int idx, int id, int w, bool msf) {
    elemento *nuovo = crea_elemento(id, w, msf);
    elemento **ptr = &g->vicini[idx];
    while (*ptr && (*ptr)->id < id) {
        ptr = &(*ptr)->next;
    }
    nuovo->next = *ptr;
    *ptr = nuovo;
}

// Rimuove nodo dalla lista di adiacenza
bool rimuovi_adiacenza(grafo *g, int idx, int id) {
    elemento **ptr = &g->vicini[idx];
    while (*ptr) {
        if ((*ptr)->id == id) {
            elemento *tmp = *ptr;
            *ptr = (*ptr)->next;
            free(tmp);
            return true;
        }
        ptr = &(*ptr)->next;
    }
    return false;
}

// Trova componente connessa del nodo
int trova_comp(grafo *g, int nodo) {
    return g->cCon[nodo];
}

// Costruisce la MSF con Kruskal
void kruskal(grafo *g) {
    // Raccogli tutti gli archi in un array
    int maxArchi = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            maxArchi++;
            a = a->next;
        }
    }
    
    arco **archi = malloc(maxArchi * sizeof(arco*));
    int count = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            archi[count++] = a;
            a = a->next;
        }
    }
    
    // Ordina archi per peso (bubble sort semplice)
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (archi[j]->weight > archi[j+1]->weight) {
                arco *tmp = archi[j];
                archi[j] = archi[j+1];
                archi[j+1] = tmp;
            }
        }
    }
    
    // Kruskal
    union_find *uf = uf_create(g->numNodi);
    for (int i = 0; i < count; i++) {
        int u = archi[i]->u;
        int v = archi[i]->v;
        if (uf_find(uf, u) != uf_find(uf, v)) {
            archi[i]->msf = true;
            g->costoMSF += archi[i]->weight;
            
            // Aggiorna le liste di adiacenza
            // Cerchiamo gli elementi e impostiamo msf a true
            elemento *e;
            for (e = g->vicini[u]; e; e = e->next) {
                if (e->id == v) { e->msf = true; break; }
            }
            for (e = g->vicini[v]; e; e = e->next) {
                if (e->id == u) { e->msf = true; break; }
            }
            
            uf_union(uf, u, v);
        } else {
            archi[i]->msf = false;
        }
    }
    
    // Aggiorna cCon
    for (int i = 0; i < g->numNodi; i++) {
        int root = uf_find(uf, i);
        // Trova il nodo più piccolo nella componente
        int min = i;
        for (int j = 0; j < g->numNodi; j++) {
            if (uf_find(uf, j) == root && j < min) {
                min = j;
            }
        }
        g->cCon[i] = min;
    }
    
    // Calcola numero componenti connesse
    bool *visti = calloc(g->numNodi, sizeof(bool));
    g->numCoCo = 0;
    for (int i = 0; i < g->numNodi; i++) {
        int root = uf_find(uf, i);
        if (!visti[root]) {
            visti[root] = true;
            g->numCoCo++;
        }
    }
    
    free(visti);
    uf_destroy(uf);
    free(archi);
}

// Trova il cammino tra due nodi nella MSF
void trova_cammino(grafo *g, int u, int v, int *path, int *pathLen, bool *visited) {
    if (u == v) {
        path[*pathLen] = u;
        (*pathLen)++;
        return;
    }
    
    visited[u] = true;
    path[*pathLen] = u;
    (*pathLen)++;
    
    elemento *e = g->vicini[u];
    while (e) {
        if (e->msf && !visited[e->id]) {
            trova_cammino(g, e->id, v, path, pathLen, visited);
            if (path[*pathLen - 1] == v) {
                return;
            }
        }
        e = e->next;
    }
    
    // Backtrack
    (*pathLen)--;
    visited[u] = false;
}

// Trova arco di peso massimo nel cammino
bool trova_arco_max(grafo *g, int *path, int pathLen, int *maxU, int *maxV, int *maxW) {
    if (pathLen < 2) return false;
    
    *maxW = -2147483648;  // INT_MIN
    for (int i = 0; i < pathLen - 1; i++) {
        int u = path[i];
        int v = path[i+1];
        arco *a = trova_arco(g, u, v);
        if (a && a->weight > *maxW) {
            *maxW = a->weight;
            *maxU = u;
            *maxV = v;
        }
    }
    return true;
}

/* ========== OPERAZIONI SUL GRAFO ========== */

// Aggiorna cCon dopo un'unione di componenti
void aggiorna_cCon_unione(grafo *g, int comp1, int comp2) {
    int newComp = comp1 < comp2 ? comp1 : comp2;
    for (int i = 0; i < g->numNodi; i++) {
        if (g->cCon[i] == comp1 || g->cCon[i] == comp2) {
            g->cCon[i] = newComp;
        }
    }
    g->numCoCo--;
}

// Aggiorna cCon dopo una divisione di componenti
void aggiorna_cCon_divisione(grafo *g, int u, int v) {
    bool *visited = calloc(g->numNodi, sizeof(bool));
    int *queue = malloc(g->numNodi * sizeof(int));
    int head = 0, tail = 0;
    
    // Visita da u
    queue[tail++] = u;
    visited[u] = true;
    while (head < tail) {
        int curr = queue[head++];
        elemento *e = g->vicini[curr];
        while (e) {
            if (e->msf && !visited[e->id]) {
                visited[e->id] = true;
                queue[tail++] = e->id;
            }
            e = e->next;
        }
    }
    
    // Determina nuova componente per i nodi visitati
    int minVisited = u;
    for (int i = 0; i < g->numNodi; i++) {
        if (visited[i] && i < minVisited) {
            minVisited = i;
        }
    }
    
    // Aggiorna cCon per i nodi visitati
    for (int i = 0; i < g->numNodi; i++) {
        if (visited[i]) {
            g->cCon[i] = minVisited;
        }
    }
    
    // Trova nodo più piccolo della componente non visitata
    int minNotVisited = -1;
    for (int i = 0; i < g->numNodi; i++) {
        if (!visited[i] && g->cCon[i] != -1) {
            if (minNotVisited == -1 || i < minNotVisited) {
                minNotVisited = i;
            }
        }
    }
    
    if (minNotVisited != -1) {
        for (int i = 0; i < g->numNodi; i++) {
            if (!visited[i] && g->cCon[i] != -1) {
                g->cCon[i] = minNotVisited;
            }
        }
        g->numCoCo++;
    }
    
    free(visited);
    free(queue);
}

// Operazione di aggiunta arco
bool aggiungi_arco(grafo *g, int u, int v, int w) {
    // Verifica nodi validi
    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi || u >= v) {
        return false;
    }
    
    // Verifica arco già presente
    if (trova_arco(g, u, v) != NULL) {
        return false;
    }
    
    // Blocca le componenti
    int compU = trova_comp(g, u);
    int compV = trova_comp(g, v);
    
    pthread_mutex_lock(&g->compMutex);
    while (g->compBusy[compU] || g->compBusy[compV]) {
        pthread_cond_wait(&g->compCond, &g->compMutex);
    }
    g->compBusy[compU] = true;
    if (compV != compU) g->compBusy[compV] = true;
    pthread_mutex_unlock(&g->compMutex);
    
    // Blocca la posizione hash
    int hash = hash_function(u, v, g->hashSize);
    pthread_mutex_lock(&g->mutexArray[hash % g->nMutex]);
    
    // Crea nuovo arco
    arco *nuovo = crea_arco(u, v, w, false);
    aggiungi_hash(g, nuovo);
    g->numArchi++;
    
    // Aggiungi alle liste di adiacenza
    aggiungi_adiacenza(g, u, v, w, false);
    aggiungi_adiacenza(g, v, u, w, false);
    
    // Se sono in componenti diverse
    if (compU != compV) {
        nuovo->msf = true;
        // Aggiorna la MSF nelle liste di adiacenza
        elemento *e;
        for (e = g->vicini[u]; e; e = e->next) {
            if (e->id == v) { e->msf = true; break; }
        }
        for (e = g->vicini[v]; e; e = e->next) {
            if (e->id == u) { e->msf = true; break; }
        }
        g->costoMSF += w;
        aggiorna_cCon_unione(g, compU, compV);
    } else {
        // Stessa componente: trova cammino e arco di peso massimo
        int *path = malloc(g->numNodi * sizeof(int));
        bool *visited = calloc(g->numNodi, sizeof(bool));
        int pathLen = 0;
        
        trova_cammino(g, u, v, path, &pathLen, visited);
        
        int maxU, maxV, maxW;
        if (trova_arco_max(g, path, pathLen, &maxU, &maxV, &maxW)) {
            if (maxW > w) {
                // Sostituisci arco
                arco *old = trova_arco(g, maxU, maxV);
                if (old) {
                    old->msf = false;
                    nuovo->msf = true;
                    elemento *e;
                    
                    // Aggiorna liste adiacenza
                    for (e = g->vicini[maxU]; e; e = e->next) {
                        if (e->id == maxV) { e->msf = false; break; }
                    }
                    for (e = g->vicini[maxV]; e; e = e->next) {
                        if (e->id == maxU) { e->msf = false; break; }
                    }
                    for (e = g->vicini[u]; e; e = e->next) {
                        if (e->id == v) { e->msf = true; break; }
                    }
                    for (e = g->vicini[v]; e; e = e->next) {
                        if (e->id == u) { e->msf = true; break; }
                    }
                    
                    g->costoMSF = g->costoMSF - maxW + w;
                }
            }
        }
        
        free(path);
        free(visited);
    }
    
    pthread_mutex_unlock(&g->mutexArray[hash % g->nMutex]);
    
    // Sblocca le componenti
    pthread_mutex_lock(&g->compMutex);
    g->compBusy[compU] = false;
    if (compV != compU) g->compBusy[compV] = false;
    pthread_cond_broadcast(&g->compCond);
    pthread_mutex_unlock(&g->compMutex);
    
    return true;
}

// Operazione di cancellazione arco
bool cancella_arco(grafo *g, int u, int v) {
    // Verifica nodi validi
    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi || u >= v) {
        return false;
    }
    
    // Trova l'arco
    arco *a = trova_arco(g, u, v);
    if (!a) {
        return false;
    }
    
    // Blocca le componenti
    int compU = trova_comp(g, u);
    int compV = trova_comp(g, v);
    
    pthread_mutex_lock(&g->compMutex);
    while (g->compBusy[compU] || g->compBusy[compV]) {
        pthread_cond_wait(&g->compCond, &g->compMutex);
    }
    g->compBusy[compU] = true;
    if (compV != compU) g->compBusy[compV] = true;
    pthread_mutex_unlock(&g->compMutex);
    
    // Blocca la posizione hash
    int hash = hash_function(u, v, g->hashSize);
    pthread_mutex_lock(&g->mutexArray[hash % g->nMutex]);
    
    bool eraMSF = a->msf;
    
    // Rimuovi dalle liste di adiacenza
    rimuovi_adiacenza(g, u, v);
    rimuovi_adiacenza(g, v, u);
    
    // Rimuovi dalla tabella hash
    rimuovi_hash(g, u, v);
    g->numArchi--;
    
    if (eraMSF) {
        // Era nella MSF, dobbiamo riparare
        // Prima togli il costo dalla MSF
        g->costoMSF -= a->weight;
        
        // Trova la componente di u e v dopo la rimozione
        bool *visited = calloc(g->numNodi, sizeof(bool));
        int *queue = malloc(g->numNodi * sizeof(int));
        int head = 0, tail = 0;
        
        // Visita da u (senza passare da v)
        queue[tail++] = u;
        visited[u] = true;
        while (head < tail) {
            int curr = queue[head++];
            elemento *e = g->vicini[curr];
            while (e) {
                if (e->msf && !visited[e->id] && !(curr == u && e->id == v)) {
                    visited[e->id] = true;
                    queue[tail++] = e->id;
                }
                e = e->next;
            }
        }
        
        // Trova l'arco di peso minimo tra le due componenti
        int minW = 2147483647;  // INT_MAX
        int bestU = -1, bestV = -1;
        
        for (int i = 0; i < g->numNodi; i++) {
            if (visited[i]) {
                elemento *e = g->vicini[i];
                while (e) {
                    if (!visited[e->id]) {
                        // Arco che collega le due componenti
                        if (e->w < minW) {
                            minW = e->w;
                            bestU = i < e->id ? i : e->id;
                            bestV = i < e->id ? e->id : i;
                        }
                    }
                    e = e->next;
                }
            }
        }
        
        if (bestU != -1 && bestV != -1) {
            // Aggiungi l'arco alla MSF
            arco *newMSF = trova_arco(g, bestU, bestV);
            if (newMSF) {
                newMSF->msf = true;
                g->costoMSF += newMSF->weight;
                
                // Aggiorna liste adiacenza
                elemento *e;
                for (e = g->vicini[bestU]; e; e = e->next) {
                    if (e->id == bestV) { e->msf = true; break; }
                }
                for (e = g->vicini[bestV]; e; e = e->next) {
                    if (e->id == bestU) { e->msf = true; break; }
                }
            }
        } else {
            // Due componenti separate
            aggiorna_cCon_divisione(g, u, v);
        }
        
        free(visited);
        free(queue);
    }
    
    pthread_mutex_unlock(&g->mutexArray[hash % g->nMutex]);
    
    // Sblocca le componenti
    pthread_mutex_lock(&g->compMutex);
    g->compBusy[compU] = false;
    if (compV != compU) g->compBusy[compV] = false;
    pthread_cond_broadcast(&g->compCond);
    pthread_mutex_unlock(&g->compMutex);
    
    return true;
}

/* ========== FUNZIONI PER IL BUFFER ========== */

buffer_t* buffer_init(int size) {
    buffer_t *b = malloc(sizeof(buffer_t));
    b->buffer = malloc(size * sizeof(operazione));
    b->size = size;
    b->head = 0;
    b->tail = 0;
    b->count = 0;
    b->done = false;
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->notEmpty, NULL);
    pthread_cond_init(&b->notFull, NULL);
    return b;
}

void buffer_destroy(buffer_t *b) {
    free(b->buffer);
    pthread_mutex_destroy(&b->mutex);
    pthread_cond_destroy(&b->notEmpty);
    pthread_cond_destroy(&b->notFull);
    free(b);
}

void buffer_insert(buffer_t *b, operazione op) {
    pthread_mutex_lock(&b->mutex);
    while (b->count == b->size) {
        pthread_cond_wait(&b->notFull, &b->mutex);
    }
    b->buffer[b->tail] = op;
    b->tail = (b->tail + 1) % b->size;
    b->count++;
    pthread_cond_signal(&b->notEmpty);
    pthread_mutex_unlock(&b->mutex);
}

bool buffer_remove(buffer_t *b, operazione *op) {
    pthread_mutex_lock(&b->mutex);
    while (b->count == 0 && !b->done) {
        pthread_cond_wait(&b->notEmpty, &b->mutex);
    }
    if (b->count == 0 && b->done) {
        pthread_mutex_unlock(&b->mutex);
        return false;
    }
    *op = b->buffer[b->head];
    b->head = (b->head + 1) % b->size;
    b->count--;
    pthread_cond_signal(&b->notFull);
    pthread_mutex_unlock(&b->mutex);
    return true;
}

void buffer_set_done(buffer_t *b) {
    pthread_mutex_lock(&b->mutex);
    b->done = true;
    pthread_cond_broadcast(&b->notEmpty);
    pthread_mutex_unlock(&b->mutex);
}

/* ========== THREAD CONSUMER ========== */

void* consumer_thread(void *arg) {
    thread_data_t *data = (thread_data_t*)arg;
    grafo *g = data->g;
    buffer_t *buffer = data->buffer;
    
    operazione op;
    while (buffer_remove(buffer, &op)) {
        bool result;
        
        if (op.op == '+') {
            result = aggiungi_arco(g, op.u, op.v, op.w);
            if (result) {
                printf("+ %d %d %d %d %d %ld\n", op.u, op.v, op.w, g->numArchi, g->numCoCo, g->costoMSF);
            } else {
                printf("+ %d %d %d 0\n", op.u, op.v, op.w);
            }
        } else if (op.op == '-') {
            result = cancella_arco(g, op.u, op.v);
            if (result) {
                printf("- %d %d %d %d %ld\n", op.u, op.v, g->numArchi, g->numCoCo, g->costoMSF);
            } else {
                printf("- %d %d 0\n", op.u, op.v);
            }
        }
        fflush(stdout);
    }
    
    return NULL;
}

/* ========== FUNZIONI PER LA LETTURA DEI FILE ========== */

// Legge il file .gr
bool leggi_grafo(const char *filename, grafo *g) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Errore: impossibile aprire %s\n", filename);
        return false;
    }
    
    char line[256];
    int numNodi = 0, numArchi = 0;
    bool primaLinea = true;
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'c') continue;
        if (line[0] == 'p') {
            if (primaLinea) {
                sscanf(line, "p sp %d %d", &numNodi, &numArchi);
                // I nodi sono numerati da 0 a numNodi (incluso)
                g->numNodi = numNodi + 1;
                primaLinea = false;
            }
        } else if (line[0] == 'a') {
            int u, v, w;
            sscanf(line, "a %d %d %d", &u, &v, &w);
            // Converti da 1-based a 0-based
            u--;
            v--;
            // Crea arco
            arco *a = crea_arco(u, v, w, false);
            aggiungi_hash(g, a);
            g->numArchi++;
            // Aggiungi alle liste di adiacenza
            aggiungi_adiacenza(g, u, v, w, false);
            aggiungi_adiacenza(g, v, u, w, false);
        }
    }
    
    fclose(f);
    return true;
}

// Legge il file delle operazioni
bool leggi_operazioni(const char *filename, buffer_t *buffer) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Errore: impossibile aprire %s\n", filename);
        return false;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        operazione op;
        if (line[0] == '+') {
            if (sscanf(line, "+ %d %d %d", &op.u, &op.v, &op.w) == 3) {
                op.op = '+';
                buffer_insert(buffer, op);
            }
        } else if (line[0] == '-') {
            if (sscanf(line, "- %d %d", &op.u, &op.v) == 2) {
                op.op = '-';
                op.w = 0;
                buffer_insert(buffer, op);
            }
        }
    }
    
    fclose(f);
    return true;
}

/* ========== FUNZIONI PER LE STATISTICHE ========== */

void calcola_statistiche(grafo *g) {
    int posizioniNonVuote = 0;
    double sommaLunghezze = 0;
    int maxLunghezza = 0;
    
    for (int i = 0; i < g->hashSize; i++) {
        int len = 0;
        arco *a = g->gHash[i];
        while (a) {
            len++;
            a = a->next;
        }
        if (len > 0) {
            posizioniNonVuote++;
            sommaLunghezze += len;
            if (len > maxLunghezza) maxLunghezza = len;
        }
    }
    
    double media = posizioniNonVuote > 0 ? sommaLunghezze / posizioniNonVuote : 0;
    
    printf("Numero posizioni non vuote: %d\n", posizioniNonVuote);
    printf("Lunghezza media liste: %.7f\n", media);
    printf("Lunghezza massima liste: %d\n", maxLunghezza);
}

/* ========== MAIN ========== */

int main(int argc, char *argv[]) {
    int opt;
    int numThreads = 3;
    int hashSize = 100000;
    int nMutex = 1000;
    char *fileGrafo = NULL;
    char *fileOperazioni = NULL;
    
    // Parsing argomenti
    while ((opt = getopt(argc, argv, "t:H:M:")) != -1) {
        switch (opt) {
            case 't':
                numThreads = atoi(optarg);
                break;
            case 'H':
                hashSize = atoi(optarg);
                break;
            case 'M':
                nMutex = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Uso: %s file_grafo file_archi [-t threads] [-H hashsize] [-M nmutex]\n", argv[0]);
                return 1;
        }
    }
    
    if (optind + 1 >= argc) {
        fprintf(stderr, "Errore: specificare file_grafo e file_archi\n");
        return 1;
    }
    
    fileGrafo = argv[optind];
    fileOperazioni = argv[optind + 1];
    
    // Inizializza grafo
    grafo *g = grafo_init(0, hashSize, nMutex);
    
    // Leggi grafo
    if (!leggi_grafo(fileGrafo, g)) {
        grafo_destroy(g);
        return 1;
    }
    
    // Calcola MSF con Kruskal
    kruskal(g);
    
    // Stampa stato iniziale
    printf("%d %d %ld\n", g->numArchi, g->numCoCo, g->costoMSF);
    fflush(stdout);
    
    // Inizializza buffer
    buffer_t *buffer = buffer_init(1024);
    
    // Leggi operazioni
    if (!leggi_operazioni(fileOperazioni, buffer)) {
        buffer_destroy(buffer);
        grafo_destroy(g);
        return 1;
    }
    buffer_set_done(buffer);
    
    // Crea thread consumatori
    pthread_t *threads = malloc(numThreads * sizeof(pthread_t));
    thread_data_t *threadData = malloc(numThreads * sizeof(thread_data_t));
    
    for (int i = 0; i < numThreads; i++) {
        threadData[i].g = g;
        threadData[i].buffer = buffer;
        threadData[i].threadId = i;
        pthread_create(&threads[i], NULL, consumer_thread, &threadData[i]);
    }
    
    // Attendi i thread
    for (int i = 0; i < numThreads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Operazioni terminate\n");
    fflush(stdout);
    
    // Calcola statistiche finali
    calcola_statistiche(g);
    
    // Ricalcola archi, componenti e costo MSF
    int numArchi = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            numArchi++;
            a = a->next;
        }
    }
    
    // Ricalcola componenti connesse
    bool *visti = calloc(g->numNodi, sizeof(bool));
    int numCoCo = 0;
    for (int i = 0; i < g->numNodi; i++) {
        if (!visti[g->cCon[i]]) {
            visti[g->cCon[i]] = true;
            numCoCo++;
        }
    }
    free(visti);
    
    // Ricalcola costo MSF
    long costoMSF = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            if (a->msf) costoMSF += a->weight;
            a = a->next;
        }
    }
    
    printf("%d %d %ld\n", numArchi, numCoCo, costoMSF);
    fflush(stdout);
    
    // Cleanup
    free(threads);
    free(threadData);
    buffer_destroy(buffer);
    grafo_destroy(g);
    
    return 0;
}
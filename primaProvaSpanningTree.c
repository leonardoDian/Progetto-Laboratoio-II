#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>

// -------------------- STRUTTURE DATI GRAFO --------------------
typedef struct arco {
    int u, v;
    int weight;
    bool msf;
    struct arco *next;
} arco;

typedef struct elemento {
    int id;
    int w;
    bool msf;
    struct elemento *next;
} elemento;

typedef struct {
    int *parent;
    int *rank;
} unionFind;

typedef struct {
    arco **gHash;
    elemento **vicini;
    int *cCon;
    int numCoCo;
    long costoMSF;
    int numNodi;
    int numArchi;
    unionFind *uf;
} grafo;

// -------------------- UTILITY getline (portabile) --------------------
ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    size_t pos = 0;
    int c;
    if (!lineptr || !stream || !n) return -1;
    if (!*lineptr) { *n = 128; *lineptr = malloc(*n); if (!*lineptr) return -1; }
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t new_size = *n * 2;
            char *new_ptr = realloc(*lineptr, new_size);
            if (!new_ptr) return -1;
            *lineptr = new_ptr;
            *n = new_size;
        }
        (*lineptr)[pos++] = c;
        if (c == '\n') break;
    }
    if (pos == 0) return -1;
    (*lineptr)[pos] = '\0';
    return pos;
}

// -------------------- GRAFO (registrazione) --------------------
void registraGrafo(FILE *fGrafo, grafo *g) {
    char *linea = NULL;
    size_t lunghezza = 0;
    ssize_t nLetti;
    g->numNodi = 0;
    g->numArchi = 0;
    g->costoMSF = 0;

    while ((nLetti = getline(&linea, &lunghezza, fGrafo)) != -1) {
        if (linea[0] == 'p') {
            int nodiFile, archiFile;
            sscanf(linea, "p sp %d %d", &nodiFile, &archiFile);
            g->numNodi = nodiFile + 1;
            g->numArchi = archiFile;

            int hashSize = (g->numNodi / 4);
            if (hashSize < 1) hashSize = 1;
            g->gHash = calloc(hashSize, sizeof(arco*));
            g->cCon = malloc(g->numNodi * sizeof(int));
            g->vicini = malloc(g->numNodi * sizeof(elemento*));
            g->uf = malloc(sizeof(unionFind));
            g->uf->parent = malloc(g->numNodi * sizeof(int));
            g->uf->rank = malloc(g->numNodi * sizeof(int));

            for (int i = 0; i < hashSize; i++) g->gHash[i] = NULL;
            for (int i = 0; i < g->numNodi; i++) {
                g->vicini[i] = NULL;
                g->cCon[i] = i;
                g->uf->parent[i] = i;
                g->uf->rank[i] = 0;
            }
        } else if (linea[0] == 'a') {
            int u, v, w;
            sscanf(linea, "a %d %d %d", &u, &v, &w);
            if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
                fprintf(stderr, "ERRORE: nodo fuori range (%d,%d)\n", u, v);
                continue;
            }

            int hashSize = (g->numNodi / 4);
            if (hashSize < 1) hashSize = 1;
            int hash = (u + v) % hashSize;

            arco *a = malloc(sizeof(arco));
            a->u = u; a->v = v; a->weight = w; a->msf = false; a->next = g->gHash[hash];
            g->gHash[hash] = a;

            elemento *e1 = malloc(sizeof(elemento));
            e1->id = v; e1->w = w; e1->msf = false; e1->next = g->vicini[u];
            g->vicini[u] = e1;

            elemento *e2 = malloc(sizeof(elemento));
            e2->id = u; e2->w = w; e2->msf = false; e2->next = g->vicini[v];
            g->vicini[v] = e2;
        }
    }
    free(linea);
}

// -------------------- UNION-FIND (per Kruskal) --------------------
int find(unionFind *uf, int x) {
    if (uf->parent[x] != x)
        uf->parent[x] = find(uf, uf->parent[x]);
    return uf->parent[x];
}

void unionSets(unionFind *uf, int x, int y) {
    int rx = find(uf, x);
    int ry = find(uf, y);
    if (rx != ry) {
        if (rx < ry) uf->parent[ry] = rx;
        else uf->parent[rx] = ry;
    }
}

int cmpArchi(const void *a, const void *b) {
    arco *aa = *(arco**)a;
    arco *bb = *(arco**)b;
    return aa->weight - bb->weight;
}

void kruskal(grafo *g) {
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    arco **arr = malloc(g->numArchi * sizeof(arco*));
    int idx = 0;
    for (int i = 0; i < hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            arr[idx++] = a;
            a = a->next;
        }
    }
    qsort(arr, g->numArchi, sizeof(arco*), cmpArchi);

    for (int i = 0; i < g->numNodi; i++) {
        g->uf->parent[i] = i;
        g->uf->rank[i] = 0;
    }

    g->costoMSF = 0;
    for (int i = 0; i < g->numArchi; i++) {
        arco *a = arr[i];
        int ru = find(g->uf, a->u);
        int rv = find(g->uf, a->v);
        if (ru != rv) {
            a->msf = true;
            g->costoMSF += a->weight;
            elemento *e = g->vicini[a->u];
            while (e) { if (e->id == a->v) { e->msf = true; break; } e = e->next; }
            e = g->vicini[a->v];
            while (e) { if (e->id == a->u) { e->msf = true; break; } e = e->next; }
            unionSets(g->uf, a->u, a->v);
        }
    }

    for (int i = 0; i < g->numNodi; i++) {
        g->cCon[i] = find(g->uf, i);
    }
    bool *visti = calloc(g->numNodi, sizeof(bool));
    g->numCoCo = 0;
    for (int i = 0; i < g->numNodi; i++) {
        int r = find(g->uf, i);
        if (!visti[r]) {
            visti[r] = true;
            g->numCoCo++;
        }
    }
    free(visti);
    free(arr);
}

// -------------------- STRUTTURA DATI CONDIVISI PER THREAD --------------------
#define NMUTEX 16
#define BUFFER_SIZE 1024

typedef struct {
    char op;        // '+' o '-'
    int u, v, w;
} operazione_t;

typedef struct {
    grafo *g;
    // Mutex per hash bucket (ne usiamo NMUTEX)
    pthread_mutex_t hash_mutexes[NMUTEX];
    // Sincronizzazione componenti connesse
    pthread_mutex_t comp_mutex;
    pthread_cond_t comp_cond;
    bool *component_busy;   // array di dimensione g->numNodi
    // Mutex per statistiche (numArchi, costoMSF, numCoCo) e stampa
    pthread_mutex_t stats_mutex;
    pthread_mutex_t print_mutex;
    // Buffer produttore/consumatore
    operazione_t buffer[BUFFER_SIZE];
    int head, tail, count;
    pthread_mutex_t buffer_mutex;
    pthread_cond_t buffer_not_full;
    pthread_cond_t buffer_not_empty;
    bool done;
} shared_data;

// -------------------- FUNZIONI AUSILIARIE CON LOCK --------------------

// Cerca un arco nell'hash (assume mutex del bucket già acquisito)
arco* trovaArcoInHash(grafo *g, int u, int v) {
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int hash = (u + v) % hashSize;
    arco *a = g->gHash[hash];
    while (a) {
        if ((a->u == u && a->v == v) || (a->u == v && a->v == u))
            return a;
        a = a->next;
    }
    return NULL;
}

// Trova l'arco di peso massimo sul percorso tra u e v nell'albero MSF (senza lock, assume che il chiamante abbia già bloccato le componenti)
bool trovaMassimoPercorso(shared_data *sd, int u, int v, arco **maxArco, int *maxPeso) {
    grafo *g = sd->g;
    bool *visited = calloc(g->numNodi, sizeof(bool));
    int *parent = malloc(g->numNodi * sizeof(int));
    arco **parentEdge = malloc(g->numNodi * sizeof(arco*));
    int *stack = malloc(g->numNodi * sizeof(int));
    int top = 0;

    visited[u] = true;
    parent[u] = -1;
    parentEdge[u] = NULL;
    stack[top++] = u;

    while (top > 0) {
        int curr = stack[--top];
        if (curr == v) break;
        elemento *e = g->vicini[curr];
        while (e) {
            if (e->msf && !visited[e->id]) {
                visited[e->id] = true;
                parent[e->id] = curr;
                // Ottieni l'arco con lock sul bucket
                int hashSize = (g->numNodi / 4);
                if (hashSize < 1) hashSize = 1;
                int hash = (curr + e->id) % hashSize;
                pthread_mutex_lock(&sd->hash_mutexes[hash % NMUTEX]);
                arco *a = trovaArcoInHash(g, curr, e->id);
                parentEdge[e->id] = a;
                pthread_mutex_unlock(&sd->hash_mutexes[hash % NMUTEX]);
                stack[top++] = e->id;
            }
            e = e->next;
        }
    }

    if (!visited[v]) {
        free(visited); free(parent); free(parentEdge); free(stack);
        return false;
    }

    *maxPeso = -1;
    *maxArco = NULL;
    int curr = v;
    while (curr != u) {
        arco *a = parentEdge[curr];
        if (a && a->weight > *maxPeso) {
            *maxPeso = a->weight;
            *maxArco = a;
        }
        curr = parent[curr];
    }
    free(visited); free(parent); free(parentEdge); free(stack);
    return true;
}

// Trova l'arco di peso minimo che collega le due componenti (senza lock sulle componenti, ma assume che siano bloccate)
arco* trovaArcoMinimoTraComponenti(shared_data *sd, int compA, int compB) {
    grafo *g = sd->g;
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    arco *best = NULL;
    int bestWeight = INT_MAX;

    for (int i = 0; i < hashSize; i++) {
        pthread_mutex_lock(&sd->hash_mutexes[i % NMUTEX]);
        arco *a = g->gHash[i];
        while (a) {
            if (!a->msf) {
                int ca = g->cCon[a->u];
                int cb = g->cCon[a->v];
                if ((ca == compA && cb == compB) || (ca == compB && cb == compA)) {
                    if (a->weight < bestWeight) {
                        bestWeight = a->weight;
                        best = a;
                    }
                }
            }
            a = a->next;
        }
        pthread_mutex_unlock(&sd->hash_mutexes[i % NMUTEX]);
    }
    return best;
}

// Rimuove un elemento dalla lista di adiacenza (nessun lock)
void rimuoviDaLista(grafo *g, int from, int id) {
    elemento *e = g->vicini[from], *prev = NULL;
    while (e) {
        if (e->id == id) {
            if (prev) prev->next = e->next;
            else g->vicini[from] = e->next;
            free(e);
            return;
        }
        prev = e;
        e = e->next;
    }
}

// -------------------- GESTIONE LOCK COMPONENTI --------------------
void lock_components(shared_data *sd, int u, int v, int *cu, int *cv) {
    grafo *g = sd->g;
    pthread_mutex_lock(&sd->comp_mutex);
    while (1) {
        *cu = g->cCon[u];
        *cv = g->cCon[v];
        int a = *cu, b = *cv;
        if (a > b) { int t = a; a = b; b = t; }
        if (!sd->component_busy[a] && (a == b || !sd->component_busy[b])) {
            sd->component_busy[a] = true;
            if (a != b) sd->component_busy[b] = true;
            break;
        }
        pthread_cond_wait(&sd->comp_cond, &sd->comp_mutex);
    }
    pthread_mutex_unlock(&sd->comp_mutex);
}

void unlock_components(shared_data *sd, int cu, int cv) {
    grafo *g = sd->g;
    pthread_mutex_lock(&sd->comp_mutex);
    int ids[4] = {cu, cv, g->cCon[cu], g->cCon[cv]};
    for (int i = 0; i < 4; i++) {
        int id = ids[i];
        if (id >= 0 && id < g->numNodi && sd->component_busy[id]) {
            sd->component_busy[id] = false;
        }
    }
    pthread_cond_broadcast(&sd->comp_cond);
    pthread_mutex_unlock(&sd->comp_mutex);
}

// -------------------- OPERAZIONI THREAD-SAFE --------------------
void addArco(shared_data *sd, int u, int v, int w) {
    grafo *g = sd->g;
    if (u > v) { int tmp = u; u = v; v = tmp; }
    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
        pthread_mutex_lock(&sd->print_mutex);
        printf("+ %d %d %d 0\n", u, v, w);
        pthread_mutex_unlock(&sd->print_mutex);
        return;
    }

    int cu, cv;
    lock_components(sd, u, v, &cu, &cv);

    // Verifica se l'arco esiste già (con lock hash)
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int hash = (u + v) % hashSize;
    pthread_mutex_lock(&sd->hash_mutexes[hash % NMUTEX]);
    if (trovaArcoInHash(g, u, v) != NULL) {
        pthread_mutex_unlock(&sd->hash_mutexes[hash % NMUTEX]);
        unlock_components(sd, cu, cv);
        pthread_mutex_lock(&sd->print_mutex);
        printf("+ %d %d %d 0\n", u, v, w);
        pthread_mutex_unlock(&sd->print_mutex);
        return;
    }

    // Crea nuovo arco e aggiunge alle strutture
    arco *nuovo = malloc(sizeof(arco));
    nuovo->u = u; nuovo->v = v; nuovo->weight = w; nuovo->msf = false;
    nuovo->next = g->gHash[hash];
    g->gHash[hash] = nuovo;

    elemento *e1 = malloc(sizeof(elemento));
    e1->id = v; e1->w = w; e1->msf = false;
    e1->next = g->vicini[u];
    g->vicini[u] = e1;

    elemento *e2 = malloc(sizeof(elemento));
    e2->id = u; e2->w = w; e2->msf = false;
    e2->next = g->vicini[v];
    g->vicini[v] = e2;

    pthread_mutex_unlock(&sd->hash_mutexes[hash % NMUTEX]);

    // Aggiorna numArchi
    pthread_mutex_lock(&sd->stats_mutex);
    g->numArchi++;
    pthread_mutex_unlock(&sd->stats_mutex);

    bool aggiunto = false;

    if (cu == cv) {
        // Stessa componente: cerca arco massimo nel percorso
        arco *maxArco = NULL;
        int maxPeso = -1;
        if (trovaMassimoPercorso(sd, u, v, &maxArco, &maxPeso)) {
            if (w < maxPeso && maxArco != NULL) {
                // Rimuovi l'arco massimo dalla MSF
                maxArco->msf = false;
                for (elemento *e = g->vicini[maxArco->u]; e; e = e->next)
                    if (e->id == maxArco->v) { e->msf = false; break; }
                for (elemento *e = g->vicini[maxArco->v]; e; e = e->next)
                    if (e->id == maxArco->u) { e->msf = false; break; }
                pthread_mutex_lock(&sd->stats_mutex);
                g->costoMSF -= maxArco->weight;
                pthread_mutex_unlock(&sd->stats_mutex);

                // Aggiungi il nuovo arco alla MSF
                nuovo->msf = true;
                e1->msf = true;
                e2->msf = true;
                pthread_mutex_lock(&sd->stats_mutex);
                g->costoMSF += w;
                pthread_mutex_unlock(&sd->stats_mutex);
                aggiunto = true;
            }
        }
    } else {
        // Componenti diverse: aggiungi alla MSF
        nuovo->msf = true;
        e1->msf = true;
        e2->msf = true;
        pthread_mutex_lock(&sd->stats_mutex);
        g->costoMSF += w;
        pthread_mutex_unlock(&sd->stats_mutex);

        // Unisci le componenti (cu in cv, ma assicuriamoci che l'ID sia il minimo)
        int oldComp = cu;
        int newComp = cv;
        if (newComp < oldComp) { // se cv è più piccolo, scambiamo per unire il più grande nel più piccolo
            int tmp = oldComp; oldComp = newComp; newComp = tmp;
        }
        for (int i = 0; i < g->numNodi; i++) {
            if (g->cCon[i] == oldComp)
                g->cCon[i] = newComp;
        }
        pthread_mutex_lock(&sd->stats_mutex);
        g->numCoCo--;
        pthread_mutex_unlock(&sd->stats_mutex);
        aggiunto = true;
    }

    // Rilascia lock componenti
    unlock_components(sd, cu, cv);

    // Stampa risultato
    pthread_mutex_lock(&sd->stats_mutex);
    int nArchi = g->numArchi;
    int nCoCo = g->numCoCo;
    long costo = g->costoMSF;
    pthread_mutex_unlock(&sd->stats_mutex);
    pthread_mutex_lock(&sd->print_mutex);
    printf("+ %d %d %d %d %d %ld\n", u, v, w, nArchi, nCoCo, costo);
    pthread_mutex_unlock(&sd->print_mutex);
}

void cancArco(shared_data *sd, int u, int v) {
    grafo *g = sd->g;
    if (u > v) { int tmp = u; u = v; v = tmp; }
    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
        pthread_mutex_lock(&sd->print_mutex);
        printf("- %d %d 0\n", u, v);
        pthread_mutex_unlock(&sd->print_mutex);
        return;
    }

    int cu, cv;
    lock_components(sd, u, v, &cu, &cv);

    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int hash = (u + v) % hashSize;
    pthread_mutex_lock(&sd->hash_mutexes[hash % NMUTEX]);

    arco *a = trovaArcoInHash(g, u, v);
    if (!a) {
        pthread_mutex_unlock(&sd->hash_mutexes[hash % NMUTEX]);
        unlock_components(sd, cu, cv);
        pthread_mutex_lock(&sd->print_mutex);
        printf("- %d %d 0\n", u, v);
        pthread_mutex_unlock(&sd->print_mutex);
        return;
    }

    int peso = a->weight;
    bool eraInMSF = a->msf;

    // Rimuovi da hash
    arco *curr = g->gHash[hash], *prev = NULL;
    while (curr) {
        if ((curr->u == u && curr->v == v) || (curr->u == v && curr->v == u)) {
            if (prev) prev->next = curr->next;
            else g->gHash[hash] = curr->next;
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    pthread_mutex_unlock(&sd->hash_mutexes[hash % NMUTEX]);

    // Rimuovi da liste di adiacenza
    rimuoviDaLista(g, u, v);
    rimuoviDaLista(g, v, u);

    pthread_mutex_lock(&sd->stats_mutex);
    g->numArchi--;
    pthread_mutex_unlock(&sd->stats_mutex);

    if (eraInMSF) {
        // Rimuovi dalla MSF
        pthread_mutex_lock(&sd->stats_mutex);
        g->costoMSF -= peso;
        pthread_mutex_unlock(&sd->stats_mutex);

        // Dividi la componente: visita da u senza attraversare l'arco rimosso
        bool *visited = calloc(g->numNodi, sizeof(bool));
        int *stack = malloc(g->numNodi * sizeof(int));
        int top = 0;
        stack[top++] = u;
        visited[u] = true;
        while (top > 0) {
            int currNode = stack[--top];
            elemento *e = g->vicini[currNode];
            while (e) {
                if (e->msf && !visited[e->id] && !(currNode == u && e->id == v)) {
                    visited[e->id] = true;
                    stack[top++] = e->id;
                }
                e = e->next;
            }
        }
        free(stack);

        // Determina nuova componente per i visitati (minimo ID)
        int compU = u;
        for (int i = 0; i < g->numNodi; i++) {
            if (visited[i] && i < compU) compU = i;
        }
        int oldComp = g->cCon[u];
        int compV = -1;
        for (int i = 0; i < g->numNodi; i++) {
            if (!visited[i] && g->cCon[i] == oldComp) {
                if (compV == -1 || i < compV) compV = i;
            }
        }
        if (compV == -1) compV = oldComp; // caso raro (non dovrebbe accadere)

        // Assegna le nuove componenti
        for (int i = 0; i < g->numNodi; i++) {
            if (visited[i]) g->cCon[i] = compU;
            else if (g->cCon[i] == oldComp) g->cCon[i] = compV;
        }
        free(visited);

        pthread_mutex_lock(&sd->stats_mutex);
        g->numCoCo++;
        pthread_mutex_unlock(&sd->stats_mutex);

        // Cerca arco alternativo tra le due componenti
        arco *alternativo = trovaArcoMinimoTraComponenti(sd, compU, compV);
        if (alternativo != NULL) {
            // Aggiungi l'arco alternativo alla MSF
            int hashAlt = (alternativo->u + alternativo->v) % hashSize;
            pthread_mutex_lock(&sd->hash_mutexes[hashAlt % NMUTEX]);
            alternativo->msf = true;
            for (elemento *e = g->vicini[alternativo->u]; e; e = e->next)
                if (e->id == alternativo->v) { e->msf = true; break; }
            for (elemento *e = g->vicini[alternativo->v]; e; e = e->next)
                if (e->id == alternativo->u) { e->msf = true; break; }
            pthread_mutex_unlock(&sd->hash_mutexes[hashAlt % NMUTEX]);

            pthread_mutex_lock(&sd->stats_mutex);
            g->costoMSF += alternativo->weight;
            pthread_mutex_unlock(&sd->stats_mutex);

            // Unisci le due componenti
            int newComp = (compU < compV) ? compU : compV;
            int oldComp2 = (compU > compV) ? compU : compV;
            for (int i = 0; i < g->numNodi; i++) {
                if (g->cCon[i] == oldComp2)
                    g->cCon[i] = newComp;
            }
            pthread_mutex_lock(&sd->stats_mutex);
            g->numCoCo--;
            pthread_mutex_unlock(&sd->stats_mutex);
        }
    }

    // Rilascia lock componenti
    unlock_components(sd, cu, cv);

    // Stampa risultato
    pthread_mutex_lock(&sd->stats_mutex);
    int nArchi = g->numArchi;
    int nCoCo = g->numCoCo;
    long costo = g->costoMSF;
    pthread_mutex_unlock(&sd->stats_mutex);
    pthread_mutex_lock(&sd->print_mutex);
    printf("- %d %d %d %d %ld\n", u, v, nArchi, nCoCo, costo);
    pthread_mutex_unlock(&sd->print_mutex);
}

// -------------------- STATISTICHE FINALI --------------------
void stampaStatistiche(shared_data *sd) {
    grafo *g = sd->g;
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int nonVuote = 0, maxLen = 0;
    double somma = 0;
    for (int i = 0; i < hashSize; i++) {
        int len = 0;
        pthread_mutex_lock(&sd->hash_mutexes[i % NMUTEX]);
        arco *a = g->gHash[i];
        while (a) { len++; a = a->next; }
        pthread_mutex_unlock(&sd->hash_mutexes[i % NMUTEX]);
        if (len > 0) {
            nonVuote++;
            somma += len;
            if (len > maxLen) maxLen = len;
        }
    }
    double media = nonVuote ? somma / nonVuote : 0;
    pthread_mutex_lock(&sd->print_mutex);
    printf("Numero posizioni non vuote: %d\n", nonVuote);
    printf("Lunghezza media liste: %.7f\n", media);
    printf("Lunghezza massima liste: %d\n", maxLen);
    pthread_mutex_unlock(&sd->print_mutex);
}

// -------------------- THREAD CONSUMATORE --------------------
void *worker_thread(void *arg) {
    shared_data *sd = (shared_data*)arg;
    while (1) {
        operazione_t op;
        pthread_mutex_lock(&sd->buffer_mutex);
        while (sd->count == 0 && !sd->done) {
            pthread_cond_wait(&sd->buffer_not_empty, &sd->buffer_mutex);
        }
        if (sd->count == 0 && sd->done) {
            pthread_mutex_unlock(&sd->buffer_mutex);
            break;
        }
        op = sd->buffer[sd->head];
        sd->head = (sd->head + 1) % BUFFER_SIZE;
        sd->count--;
        pthread_cond_signal(&sd->buffer_not_full);
        pthread_mutex_unlock(&sd->buffer_mutex);

        if (op.op == '+')
            addArco(sd, op.u, op.v, op.w);
        else if (op.op == '-')
            cancArco(sd, op.u, op.v);
    }
    return NULL;
}

// -------------------- MAIN --------------------
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s file.gr file.mp num_threads\n", argv[0]);
        return 1;
    }

    int num_threads = atoi(argv[3]);
    if (num_threads < 1) num_threads = 1;

    FILE *fGrafo = fopen(argv[1], "r");
    if (!fGrafo) { perror("Errore apertura file grafo"); return 1; }
    FILE *fMod = fopen(argv[2], "r");
    if (!fMod) { perror("Errore apertura file modifiche"); fclose(fGrafo); return 1; }

    grafo g;
    registraGrafo(fGrafo, &g);
    fclose(fGrafo);

    kruskal(&g);
    printf("%d %d %ld\n", g.numArchi, g.numCoCo, g.costoMSF);

    shared_data sd;
    sd.g = &g;
    for (int i = 0; i < NMUTEX; i++) pthread_mutex_init(&sd.hash_mutexes[i], NULL);
    pthread_mutex_init(&sd.comp_mutex, NULL);
    pthread_cond_init(&sd.comp_cond, NULL);
    sd.component_busy = calloc(g.numNodi, sizeof(bool));
    pthread_mutex_init(&sd.stats_mutex, NULL);
    pthread_mutex_init(&sd.print_mutex, NULL);
    sd.head = sd.tail = sd.count = 0;
    pthread_mutex_init(&sd.buffer_mutex, NULL);
    pthread_cond_init(&sd.buffer_not_full, NULL);
    pthread_cond_init(&sd.buffer_not_empty, NULL);
    sd.done = false;

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_thread, &sd);
    }

    char *linea = NULL;
    size_t lunghezza = 0;
    ssize_t nLetti;
    while ((nLetti = getline(&linea, &lunghezza, fMod)) != -1) {
        if (linea[0] == '\n' || linea[0] == '\0' || linea[0] == 'c') continue;
        operazione_t op;
        if (linea[0] == '+') {
            if (sscanf(linea, "+ %d %d %d", &op.u, &op.v, &op.w) == 3) {
                op.op = '+';
            } else continue;
        } else if (linea[0] == '-') {
            if (sscanf(linea, "- %d %d", &op.u, &op.v) == 2) {
                op.op = '-';
                op.w = 0;
            } else continue;
        } else continue;

        pthread_mutex_lock(&sd.buffer_mutex);
        while (sd.count == BUFFER_SIZE)
            pthread_cond_wait(&sd.buffer_not_full, &sd.buffer_mutex);
        sd.buffer[sd.tail] = op;
        sd.tail = (sd.tail + 1) % BUFFER_SIZE;
        sd.count++;
        pthread_cond_signal(&sd.buffer_not_empty);
        pthread_mutex_unlock(&sd.buffer_mutex);
    }
    free(linea);
    fclose(fMod);

    pthread_mutex_lock(&sd.buffer_mutex);
    sd.done = true;
    pthread_cond_broadcast(&sd.buffer_not_empty);
    pthread_mutex_unlock(&sd.buffer_mutex);

    for (int i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);
    free(threads);

    stampaStatistiche(&sd);

    // Cleanup
    free(sd.component_busy);
    for (int i = 0; i < NMUTEX; i++) pthread_mutex_destroy(&sd.hash_mutexes[i]);
    pthread_mutex_destroy(&sd.comp_mutex);
    pthread_cond_destroy(&sd.comp_cond);
    pthread_mutex_destroy(&sd.stats_mutex);
    pthread_mutex_destroy(&sd.print_mutex);
    pthread_mutex_destroy(&sd.buffer_mutex);
    pthread_cond_destroy(&sd.buffer_not_full);
    pthread_cond_destroy(&sd.buffer_not_empty);

    return 0;
}
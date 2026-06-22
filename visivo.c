#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>

/* ========== STRUTTURE DATI ========== */

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
    arco **gHash;
    elemento **vicini;
    int *cCon;
    int numCoCo;
    long costoMSF;
    int numNodi;
    int numArchi;
    int hashSize;
    int nMutex;
    pthread_mutex_t *mutexArray;
    pthread_mutex_t compMutex;
    pthread_cond_t compCond;
    bool *compBusy;
} grafo;

typedef struct {
    char op;
    int u, v, w;
} operazione;

typedef struct {
    operazione *buffer;
    int size;
    int head, tail, count;
    bool done;
    pthread_mutex_t mutex;
    pthread_cond_t notEmpty, notFull;
} buffer_t;

typedef struct {
    grafo *g;
    buffer_t *buffer;
    int threadId;
} thread_data_t;

/* ========== UNION-FIND ========== */

typedef struct {
    int *parent, *rank;
} union_find;

union_find* uf_create(int n) {
    union_find *uf = malloc(sizeof(union_find));
    uf->parent = malloc(n * sizeof(int));
    uf->rank = malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) { uf->parent[i] = i; uf->rank[i] = 0; }
    return uf;
}

void uf_destroy(union_find *uf) {
    free(uf->parent); free(uf->rank); free(uf);
}

int uf_find(union_find *uf, int x) {
    if (uf->parent[x] != x) uf->parent[x] = uf_find(uf, uf->parent[x]);
    return uf->parent[x];
}

void uf_union(union_find *uf, int x, int y) {
    int rx = uf_find(uf, x), ry = uf_find(uf, y);
    if (rx == ry) return;
    if (uf->rank[rx] < uf->rank[ry]) uf->parent[rx] = ry;
    else if (uf->rank[rx] > uf->rank[ry]) uf->parent[ry] = rx;
    else { uf->parent[ry] = rx; uf->rank[rx]++; }
}

/* ========== FUNZIONI DEL GRAFO ========== */

int hash_function(int u, int v, int size) {
    return (u * 31 + v * 17) % size;
}

arco* crea_arco(int u, int v, int w, bool msf) {
    arco *a = malloc(sizeof(arco));
    a->u = u; a->v = v; a->weight = w; a->msf = msf; a->next = NULL;
    return a;
}

elemento* crea_elemento(int id, int w, bool msf) {
    elemento *e = malloc(sizeof(elemento));
    e->id = id; e->w = w; e->msf = msf; e->next = NULL;
    return e;
}

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
    for (int i = 0; i < nMutex; i++) pthread_mutex_init(&g->mutexArray[i], NULL);

    pthread_mutex_init(&g->compMutex, NULL);
    pthread_cond_init(&g->compCond, NULL);
    return g;
}

void grafo_destroy(grafo *g) {
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) { arco *tmp = a; a = a->next; free(tmp); }
    }
    free(g->gHash);

    for (int i = 0; i < g->numNodi; i++) {
        elemento *e = g->vicini[i];
        while (e) { elemento *tmp = e; e = e->next; free(tmp); }
    }
    free(g->vicini);

    free(g->cCon);
    free(g->compBusy);

    for (int i = 0; i < g->nMutex; i++) pthread_mutex_destroy(&g->mutexArray[i]);
    free(g->mutexArray);

    pthread_mutex_destroy(&g->compMutex);
    pthread_cond_destroy(&g->compCond);
    free(g);
}

arco* trova_arco(grafo *g, int u, int v) {
    int hash = hash_function(u, v, g->hashSize);
    arco *a = g->gHash[hash];
    while (a) {
        if ((a->u == u && a->v == v) || (a->u == v && a->v == u)) return a;
        a = a->next;
    }
    return NULL;
}

void aggiungi_hash(grafo *g, arco *a) {
    int hash = hash_function(a->u, a->v, g->hashSize);
    a->next = g->gHash[hash];
    g->gHash[hash] = a;
}

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

void aggiungi_adiacenza(grafo *g, int idx, int id, int w, bool msf) {
    elemento *nuovo = crea_elemento(id, w, msf);
    elemento **ptr = &g->vicini[idx];
    while (*ptr && (*ptr)->id < id) ptr = &(*ptr)->next;
    nuovo->next = *ptr;
    *ptr = nuovo;
}

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

/* ========== KRUSKAL ========== */

void kruskal(grafo *g) {
    int maxArchi = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) { maxArchi++; a = a->next; }
    }
    arco **archi = malloc(maxArchi * sizeof(arco*));
    int count = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) { archi[count++] = a; a = a->next; }
    }

    for (int i = 0; i < count-1; i++)
        for (int j = 0; j < count-i-1; j++)
            if (archi[j]->weight > archi[j+1]->weight) {
                arco *tmp = archi[j];
                archi[j] = archi[j+1];
                archi[j+1] = tmp;
            }

    union_find *uf = uf_create(g->numNodi);
    for (int i = 0; i < count; i++) {
        int u = archi[i]->u, v = archi[i]->v;
        if (uf_find(uf, u) != uf_find(uf, v)) {
            archi[i]->msf = true;
            g->costoMSF += archi[i]->weight;
            for (elemento *e = g->vicini[u]; e; e = e->next)
                if (e->id == v) { e->msf = true; break; }
            for (elemento *e = g->vicini[v]; e; e = e->next)
                if (e->id == u) { e->msf = true; break; }
            uf_union(uf, u, v);
        } else {
            archi[i]->msf = false;
        }
    }

    for (int i = 0; i < g->numNodi; i++) {
        int root = uf_find(uf, i);
        int min = i;
        for (int j = 0; j < g->numNodi; j++)
            if (uf_find(uf, j) == root && j < min) min = j;
        g->cCon[i] = min;
    }

    bool *visti = calloc(g->numNodi, sizeof(bool));
    g->numCoCo = 0;
    for (int i = 0; i < g->numNodi; i++) {
        int root = uf_find(uf, i);
        if (!visti[root]) { visti[root] = true; g->numCoCo++; }
    }
    free(visti);
    uf_destroy(uf);
    free(archi);
}

/* ========== MSF DINAMICA ========== */

bool trova_cammino(grafo *g, int u, int v, int *path, int *pathLen, bool *visited) {
    visited[u] = true;
    path[(*pathLen)++] = u;
    if (u == v) return true;
    elemento *e = g->vicini[u];
    while (e) {
        if (e->msf && !visited[e->id]) {
            if (trova_cammino(g, e->id, v, path, pathLen, visited)) return true;
        }
        e = e->next;
    }
    (*pathLen)--;
    visited[u] = false;
    return false;
}

bool trova_arco_max(grafo *g, int *path, int pathLen, int *maxU, int *maxV, int *maxW) {
    if (pathLen < 2) return false;
    *maxW = INT_MIN;
    for (int i = 0; i < pathLen-1; i++) {
        int u = path[i], v = path[i+1];
        arco *a = trova_arco(g, u, v);
        if (a && a->weight > *maxW) {
            *maxW = a->weight;
            *maxU = u;
            *maxV = v;
        }
    }
    return true;
}

void aggiorna_cCon_unione(grafo *g, int comp1, int comp2) {
    int newComp = comp1 < comp2 ? comp1 : comp2;
    for (int i = 0; i < g->numNodi; i++) {
        if (g->cCon[i] == comp1 || g->cCon[i] == comp2) g->cCon[i] = newComp;
    }
    g->numCoCo--;
}

void aggiorna_cCon_divisione(grafo *g, int u, int v) {
    bool *visited = calloc(g->numNodi, sizeof(bool));
    int *stack = malloc(g->numNodi * sizeof(int));
    int top = 0;
    stack[top++] = u;
    visited[u] = true;

    while (top > 0) {
        int curr = stack[--top];
        elemento *e = g->vicini[curr];
        while (e) {
            if (e->msf && !visited[e->id]) {
                visited[e->id] = true;
                stack[top++] = e->id;
            }
            e = e->next;
        }
    }

    int minVisited = u;
    for (int i = 0; i < g->numNodi; i++)
        if (visited[i] && i < minVisited) minVisited = i;

    int minNotVisited = -1;
    for (int i = 0; i < g->numNodi; i++) {
        if (!visited[i] && g->cCon[i] != -1) {
            if (minNotVisited == -1 || i < minNotVisited) minNotVisited = i;
        }
    }

    for (int i = 0; i < g->numNodi; i++) {
        if (visited[i]) g->cCon[i] = minVisited;
        else if (g->cCon[i] != -1) g->cCon[i] = minNotVisited;
    }
    g->numCoCo++;

    free(visited);
    free(stack);
}

/* ========== OPERAZIONI CON LOCK ========== */

bool aggiungi_arco(grafo *g, int u, int v, int w) {
    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi || u >= v) return false;

    pthread_mutex_lock(&g->compMutex);
    int compU = g->cCon[u];
    int compV = g->cCon[v];
    while (g->compBusy[compU] || g->compBusy[compV]) {
        pthread_cond_wait(&g->compCond, &g->compMutex);
    }
    g->compBusy[compU] = true;
    if (compV != compU) g->compBusy[compV] = true;

    int hash = hash_function(u, v, g->hashSize);
    pthread_mutex_lock(&g->mutexArray[hash % g->nMutex]);

    if (trova_arco(g, u, v) != NULL) {
        pthread_mutex_unlock(&g->mutexArray[hash % g->nMutex]);
        g->compBusy[compU] = false;
        if (compV != compU) g->compBusy[compV] = false;
        pthread_cond_broadcast(&g->compCond);
        pthread_mutex_unlock(&g->compMutex);
        return false;
    }

    arco *nuovo = crea_arco(u, v, w, false);
    aggiungi_hash(g, nuovo);
    g->numArchi++;
    aggiungi_adiacenza(g, u, v, w, false);
    aggiungi_adiacenza(g, v, u, w, false);

    if (compU != compV) {
        nuovo->msf = true;
        for (elemento *e = g->vicini[u]; e; e = e->next)
            if (e->id == v) { e->msf = true; break; }
        for (elemento *e = g->vicini[v]; e; e = e->next)
            if (e->id == u) { e->msf = true; break; }
        g->costoMSF += w;
        aggiorna_cCon_unione(g, compU, compV);
    } else {
        int *path = malloc(g->numNodi * sizeof(int));
        bool *visited = calloc(g->numNodi, sizeof(bool));
        int pathLen = 0;
        if (trova_cammino(g, u, v, path, &pathLen, visited)) {
            int maxU = -1, maxV = -1, maxW = -1;
            if (trova_arco_max(g, path, pathLen, &maxU, &maxV, &maxW)) {
                if (maxW > w) {
                    arco *old = trova_arco(g, maxU, maxV);
                    if (old) {
                        old->msf = false;
                        nuovo->msf = true;
                        for (elemento *e = g->vicini[maxU]; e; e = e->next)
                            if (e->id == maxV) { e->msf = false; break; }
                        for (elemento *e = g->vicini[maxV]; e; e = e->next)
                            if (e->id == maxU) { e->msf = false; break; }
                        for (elemento *e = g->vicini[u]; e; e = e->next)
                            if (e->id == v) { e->msf = true; break; }
                        for (elemento *e = g->vicini[v]; e; e = e->next)
                            if (e->id == u) { e->msf = true; break; }
                        g->costoMSF = g->costoMSF - maxW + w;
                    }
                }
            }
        }
        free(path);
        free(visited);
    }

    pthread_mutex_unlock(&g->mutexArray[hash % g->nMutex]);

    g->compBusy[compU] = false;
    if (compV != compU) g->compBusy[compV] = false;
    pthread_cond_broadcast(&g->compCond);
    pthread_mutex_unlock(&g->compMutex);
    return true;
}

bool cancella_arco(grafo *g, int u, int v) {
    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi || u >= v) return false;

    pthread_mutex_lock(&g->compMutex);
    int compU = g->cCon[u];
    int compV = g->cCon[v];
    while (g->compBusy[compU] || g->compBusy[compV]) {
        pthread_cond_wait(&g->compCond, &g->compMutex);
    }
    g->compBusy[compU] = true;
    if (compV != compU) g->compBusy[compV] = true;

    int hash = hash_function(u, v, g->hashSize);
    pthread_mutex_lock(&g->mutexArray[hash % g->nMutex]);

    arco *a = trova_arco(g, u, v);
    if (!a) {
        pthread_mutex_unlock(&g->mutexArray[hash % g->nMutex]);
        g->compBusy[compU] = false;
        if (compV != compU) g->compBusy[compV] = false;
        pthread_cond_broadcast(&g->compCond);
        pthread_mutex_unlock(&g->compMutex);
        return false;
    }

    int peso = a->weight;
    bool eraMSF = a->msf;

    rimuovi_adiacenza(g, u, v);
    rimuovi_adiacenza(g, v, u);
    rimuovi_hash(g, u, v);
    g->numArchi--;

    if (eraMSF) {
        g->costoMSF -= peso;

        bool *visited = calloc(g->numNodi, sizeof(bool));
        int *stack = malloc(g->numNodi * sizeof(int));
        int top = 0;
        stack[top++] = u;
        visited[u] = true;
        while (top > 0) {
            int curr = stack[--top];
            elemento *e = g->vicini[curr];
            while (e) {
                if (e->msf && !visited[e->id] && !(curr == u && e->id == v)) {
                    visited[e->id] = true;
                    stack[top++] = e->id;
                }
                e = e->next;
            }
        }

        int minW = INT_MAX;
        int bestU = -1, bestV = -1;
        for (int i = 0; i < g->numNodi; i++) {
            if (visited[i]) {
                elemento *e = g->vicini[i];
                while (e) {
                    if (!visited[e->id]) {
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
            arco *newMSF = trova_arco(g, bestU, bestV);
            if (newMSF) {
                newMSF->msf = true;
                g->costoMSF += newMSF->weight;
                for (elemento *e = g->vicini[bestU]; e; e = e->next)
                    if (e->id == bestV) { e->msf = true; break; }
                for (elemento *e = g->vicini[bestV]; e; e = e->next)
                    if (e->id == bestU) { e->msf = true; break; }
            }
        } else {
            aggiorna_cCon_divisione(g, u, v);
        }

        free(visited);
        free(stack);
    }

    pthread_mutex_unlock(&g->mutexArray[hash % g->nMutex]);

    g->compBusy[compU] = false;
    if (compV != compU) g->compBusy[compV] = false;
    pthread_cond_broadcast(&g->compCond);
    pthread_mutex_unlock(&g->compMutex);
    return true;
}

/* ========== BUFFER ========== */

buffer_t* buffer_init(int size) {
    buffer_t *b = malloc(sizeof(buffer_t));
    b->buffer = malloc(size * sizeof(operazione));
    b->size = size;
    b->head = b->tail = b->count = 0;
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
    while (b->count == b->size) pthread_cond_wait(&b->notFull, &b->mutex);
    b->buffer[b->tail] = op;
    b->tail = (b->tail + 1) % b->size;
    b->count++;
    pthread_cond_signal(&b->notEmpty);
    pthread_mutex_unlock(&b->mutex);
}

bool buffer_remove(buffer_t *b, operazione *op) {
    pthread_mutex_lock(&b->mutex);
    while (b->count == 0 && !b->done) pthread_cond_wait(&b->notEmpty, &b->mutex);
    if (b->count == 0 && b->done) { pthread_mutex_unlock(&b->mutex); return false; }
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
    (void)data->threadId;
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

/* ========== LETTURA FILE ========== */

bool popola_grafo(const char *filename, grafo *g) {
    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr, "Errore: impossibile aprire %s\n", filename); return false; }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'c' || line[0] == 'p') continue;
        if (line[0] == 'a') {
            int u, v, w;
            if (sscanf(line, "a %d %d %d", &u, &v, &w) == 3) {
                u--; v--;
                if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
                    fprintf(stderr, "Nodo fuori range: %d %d\n", u+1, v+1);
                    fclose(f); return false;
                }
                arco *a = crea_arco(u, v, w, false);
                aggiungi_hash(g, a);
                g->numArchi++;
                aggiungi_adiacenza(g, u, v, w, false);
                aggiungi_adiacenza(g, v, u, w, false);
            }
        }
    }
    fclose(f);
    return true;
}

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
                // Non convertire: i nodi sono già 0-based
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
/* ========== STATISTICHE ========== */

void calcola_statistiche(grafo *g) {
    int posizioniNonVuote = 0;
    double sommaLunghezze = 0;
    int maxLunghezza = 0;

    for (int i = 0; i < g->hashSize; i++) {
        int len = 0;
        arco *a = g->gHash[i];
        while (a) { len++; a = a->next; }
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
    char *fileGrafo = NULL, *fileOperazioni = NULL;

    while ((opt = getopt(argc, argv, "t:H:M:")) != -1) {
        switch (opt) {
            case 't': numThreads = atoi(optarg); break;
            case 'H': hashSize = atoi(optarg); break;
            case 'M': nMutex = atoi(optarg); break;
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

    // Prima lettura per estrarre N
    int N = 0, M = 0;
    FILE *f = fopen(fileGrafo, "r");
    if (!f) { fprintf(stderr, "Errore: impossibile aprire %s\n", fileGrafo); return 1; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'p') { sscanf(line, "p sp %d %d", &N, &M); break; }
    }
    fclose(f);
    if (N == 0) { fprintf(stderr, "Formato .gr non valido\n"); return 1; }

    grafo *g = grafo_init(N + 1, hashSize, nMutex);
    if (!popola_grafo(fileGrafo, g)) { grafo_destroy(g); return 1; }

    kruskal(g);

    printf("%d %d %ld\n", g->numArchi, g->numCoCo, g->costoMSF);
    fflush(stdout);

    buffer_t *buffer = buffer_init(1024);

    pthread_t *threads = malloc(numThreads * sizeof(pthread_t));
    thread_data_t *threadData = malloc(numThreads * sizeof(thread_data_t));

    for (int i = 0; i < numThreads; i++) {
        threadData[i].g = g;
        threadData[i].buffer = buffer;
        threadData[i].threadId = i;
        pthread_create(&threads[i], NULL, consumer_thread, &threadData[i]);
    }

    leggi_operazioni(fileOperazioni, buffer);
    buffer_set_done(buffer);

    for (int i = 0; i < numThreads; i++) pthread_join(threads[i], NULL);

    printf("Operazioni terminate\n");
    fflush(stdout);

    calcola_statistiche(g);

    // Ricalcolo finale
    int numArchi = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) { numArchi++; a = a->next; }
    }
    bool *visti = calloc(g->numNodi, sizeof(bool));
    int numCoCo = 0;
    for (int i = 0; i < g->numNodi; i++) {
        if (!visti[g->cCon[i]]) { visti[g->cCon[i]] = true; numCoCo++; }
    }
    free(visti);
    long costoMSF = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) { if (a->msf) costoMSF += a->weight; a = a->next; }
    }
    printf("%d %d %ld\n", numArchi, numCoCo, costoMSF);
    fflush(stdout);

    free(threads);
    free(threadData);
    buffer_destroy(buffer);
    grafo_destroy(g);
    return 0;
}
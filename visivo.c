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
 * DATA STRUCTURES
 * ============================================================================
 */

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

typedef struct {
    char tipo;
    int u, v, w;
} operazione;

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

void inserisci_ordinato(elemento **lista, int id, int w, bool msf) {
    elemento *nuovo = malloc(sizeof(elemento));
    if (!nuovo) { 
        perror("malloc"); 
        exit(1); 
    }
    nuovo->id = id;
    nuovo->w = w;
    nuovo->msf = msf;
    nuovo->next = NULL;

    if (!*lista || (*lista)->id > id) {
        nuovo->next = *lista;
        *lista = nuovo;
        return;
    }
    
    elemento *cur = *lista;
    while (cur->next && cur->next->id < id)
        cur = cur->next;
    
    nuovo->next = cur->next;
    cur->next = nuovo;
}

void rimuovi_da_lista(elemento **lista, int id) {
    elemento *cur = *lista, *prev = NULL;
    while (cur) {
        if (cur->id == id) {
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

unsigned int hash_function(int u, int v, int hashSize) {
    return ((unsigned)u * 73856093u ^ (unsigned)v * 19349663u) % hashSize;
}

arco* trova_arco(grafo *g, int u, int v) {
    int h = hash_function(u, v, g->hashSize);
    arco *a = g->gHash[h];
    
    while (a) {
        if ((a->u == u && a->v == v) || (a->u == v && a->v == u))
            return a;
        a = a->next;
    }
    return NULL;
}

/*
 * ============================================================================
 * UNION-FIND FUNCTIONS
 * ============================================================================
 */

unionFind* uf_init(int n) {
    unionFind *uf = malloc(sizeof(unionFind));
    if (!uf) { perror("malloc"); exit(1); }
    
    uf->parent = malloc(n * sizeof(int));
    if (!uf->parent) { perror("malloc"); exit(1); }
    
    uf->rank = malloc(n * sizeof(int));
    if (!uf->rank) { perror("malloc"); exit(1); }
    
    for (int i = 0; i < n; i++) {
        uf->parent[i] = i;
        uf->rank[i] = 0;
    }
    return uf;
}

void uf_free(unionFind *uf) {
    free(uf->parent);
    free(uf->rank);
    free(uf);
}

int uf_find(unionFind *uf, int x) {
    while (uf->parent[x] != x) {
        uf->parent[x] = uf->parent[uf->parent[x]];
        x = uf->parent[x];
    }
    return x;
}

void uf_union(unionFind *uf, int x, int y) {
    int rx = uf_find(uf, x);
    int ry = uf_find(uf, y);
    
    if (rx == ry) return;
    
    if (uf->rank[rx] < uf->rank[ry])
        uf->parent[rx] = ry;
    else if (uf->rank[rx] > uf->rank[ry])
        uf->parent[ry] = rx;
    else {
        uf->parent[ry] = rx;
        uf->rank[rx]++;
    }
}

/*
 * ============================================================================
 * GRAPH READING FUNCTIONS
 * ============================================================================
 */

int cmp_archi(const void *a, const void *b) {
    arco *aa = *(arco**)a;
    arco *bb = *(arco**)b;
    if (aa->weight < bb->weight) return -1;
    if (aa->weight > bb->weight) return 1;
    return 0;
}

void leggi_grafo(FILE *f, grafo *g) {
    char *linea = NULL;
    size_t len = 0;
    ssize_t nread;
    bool has_p = false;

    while ((nread = getline(&linea, &len, f)) != -1) {
        if (linea[0] == 'p') {
            int nodi, archi;
            sscanf(linea, "p sp %d %d", &nodi, &archi);
            
            g->numNodi = nodi + 1;
            g->numArchi = 0;
            
            g->hashSize = (g->hashSize > 0) ? g->hashSize : 100000;
            
            g->gHash = calloc(g->hashSize, sizeof(arco*));
            if (!g->gHash) { perror("calloc"); exit(1); }
            
            g->vicini = calloc(g->numNodi, sizeof(elemento*));
            if (!g->vicini) { perror("calloc"); exit(1); }
            
            g->cCon = malloc(g->numNodi * sizeof(int));
            if (!g->cCon) { perror("malloc"); exit(1); }
            
            g->uf = uf_init(g->numNodi);
            
            for (int i = 0; i < g->numNodi; i++)
                g->cCon[i] = i;
            
            g->costoMSF = 0;
            g->numCoCo = 0;
            g->terminato = false;
            
            g->nmutex = (g->nmutex > 0) ? g->nmutex : 1000;
            
            g->mutHash = malloc(g->nmutex * sizeof(pthread_mutex_t));
            if (!g->mutHash) { perror("malloc"); exit(1); }
            for (int i = 0; i < g->nmutex; i++)
                pthread_mutex_init(&g->mutHash[i], NULL);
            
            pthread_mutex_init(&g->mutCCon, NULL);
            pthread_mutex_init(&g->mutMSF, NULL);
            pthread_mutex_init(&g->mutPrint, NULL);
            
            g->condCCon = malloc(g->numNodi * sizeof(pthread_cond_t));
            if (!g->condCCon) { perror("malloc"); exit(1); }
            
            g->compBusy = malloc(g->numNodi * sizeof(bool));
            if (!g->compBusy) { perror("malloc"); exit(1); }
            
            for (int i = 0; i < g->numNodi; i++) {
                pthread_cond_init(&g->condCCon[i], NULL);
                g->compBusy[i] = false;
            }
            has_p = true;
        } 
        else if (linea[0] == 'a') {
            if (!has_p) {
                fprintf(stderr, "Errore: arco prima di p\n");
                exit(1);
            }
            int u, v, w;
            sscanf(linea, "a %d %d %d", &u, &v, &w);
            
            if (u < 0 || v < 0 || u >= g->numNodi || v >= g->numNodi) {
                fprintf(stderr, "Attenzione: arco fuori range ignorato\n");
                continue;
            }
            
            if (u == v) continue;
            
            if (u > v) { int tmp = u; u = v; v = tmp; }
            
            if (trova_arco(g, u, v) != NULL) {
                continue;
            }
            
            arco *a = malloc(sizeof(arco));
            if (!a) { perror("malloc"); exit(1); }
            a->u = u;
            a->v = v;
            a->weight = w;
            a->msf = false;
            
            int h = hash_function(u, v, g->hashSize);
            a->next = g->gHash[h];
            g->gHash[h] = a;
            
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

void kruskal(grafo *g) {
    arco **archi = malloc(g->numArchi * sizeof(arco*));
    if (!archi) { perror("malloc"); exit(1); }
    
    int idx = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            archi[idx++] = a;
            a = a->next;
        }
    }
    
    qsort(archi, g->numArchi, sizeof(arco*), cmp_archi);

    unionFind *uf = g->uf;
    for (int i = 0; i < g->numNodi; i++) {
        uf->parent[i] = i;
        uf->rank[i] = 0;
    }

    pthread_mutex_lock(&g->mutMSF);
    g->costoMSF = 0;
    pthread_mutex_unlock(&g->mutMSF);
    
    for (int i = 0; i < g->numArchi; i++) {
        arco *a = archi[i];
        int ru = uf_find(uf, a->u);
        int rv = uf_find(uf, a->v);
        
        if (ru != rv) {
            uf_union(uf, a->u, a->v);
            a->msf = true;
            
            pthread_mutex_lock(&g->mutMSF);
            g->costoMSF += a->weight;
            pthread_mutex_unlock(&g->mutMSF);
            
            elemento *e = g->vicini[a->u];
            while (e && e->id != a->v) e = e->next;
            if (e) e->msf = true;
            
            e = g->vicini[a->v];
            while (e && e->id != a->u) e = e->next;
            if (e) e->msf = true;
        }
    }
    free(archi);

    pthread_mutex_lock(&g->mutMSF);
    
    for (int i = 0; i < g->numNodi; i++)
        g->cCon[i] = uf_find(uf, i);
    
    int *min_node = malloc(g->numNodi * sizeof(int));
    if (!min_node) { perror("malloc"); exit(1); }
    for (int i = 0; i < g->numNodi; i++) min_node[i] = -1;
    
    for (int i = 0; i < g->numNodi; i++) {
        int root = g->cCon[i];
        if (min_node[root] == -1 || i < min_node[root])
            min_node[root] = i;
    }
    
    for (int i = 0; i < g->numNodi; i++)
        g->cCon[i] = min_node[g->cCon[i]];
    free(min_node);

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

void lock_componente(grafo *g, int comp) {
    if (comp < 0 || comp >= g->numNodi) return;
    
    pthread_mutex_lock(&g->mutCCon);
    while (g->compBusy[comp] && !g->terminato)
        pthread_cond_wait(&g->condCCon[comp], &g->mutCCon);
    if (!g->terminato)
        g->compBusy[comp] = true;
    pthread_mutex_unlock(&g->mutCCon);
}

void unlock_componente(grafo *g, int comp) {
    if (comp < 0 || comp >= g->numNodi) return;
    
    pthread_mutex_lock(&g->mutCCon);
    g->compBusy[comp] = false;
    pthread_cond_broadcast(&g->condCCon[comp]);
    pthread_mutex_unlock(&g->mutCCon);
}

void lock_componenti(grafo *g, int u, int v) {
    int cu, cv;
    pthread_mutex_lock(&g->mutMSF);
    cu = g->cCon[u];
    cv = g->cCon[v];
    pthread_mutex_unlock(&g->mutMSF);
    
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

void unlock_componenti(grafo *g, int u, int v) {
    int cu, cv;
    pthread_mutex_lock(&g->mutMSF);
    cu = g->cCon[u];
    cv = g->cCon[v];
    pthread_mutex_unlock(&g->mutMSF);
    
    if (cu == cv) {
        unlock_componente(g, cu);
    } else {
        if (cu < cv) {
            unlock_componente(g, cv);
            unlock_componente(g, cu);
        } else {
            unlock_componente(g, cu);
            unlock_componente(g, cv);
        }
    }
}

void lock_hash(grafo *g, int u, int v) {
    int h = hash_function(u, v, g->hashSize);
    int m = h % g->nmutex;
    pthread_mutex_lock(&g->mutHash[m]);
}

void unlock_hash(grafo *g, int u, int v) {
    int h = hash_function(u, v, g->hashSize);
    int m = h % g->nmutex;
    pthread_mutex_unlock(&g->mutHash[m]);
}

/*
 * ============================================================================
 * DFS FOR MSF PATH SEARCH - Versione che non usa lock
 * ============================================================================
 */

bool dfs_trova_max(grafo *g, int curr, int target, bool *visited, arco **max_arco, int *max_peso) {
    visited[curr] = true;
    if (curr == target) return true;
    
    elemento *e = g->vicini[curr];
    while (e) {
        if (e->msf && !visited[e->id]) {
            // Cerca l'arco senza lock (già protetto dal chiamante)
            int h = hash_function(curr, e->id, g->hashSize);
            arco *a = g->gHash[h];
            while (a) {
                if ((a->u == curr && a->v == e->id) || (a->u == e->id && a->v == curr))
                    break;
                a = a->next;
            }
            
            if (a && dfs_trova_max(g, e->id, target, visited, max_arco, max_peso)) {
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

void ricalcola_numCoCo(grafo *g) {
    pthread_mutex_lock(&g->mutMSF);
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

void ricalcola_finale(grafo *g) {
    pthread_mutex_lock(&g->mutMSF);
    
    g->costoMSF = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) {
            if (a->msf) g->costoMSF += a->weight;
            a = a->next;
        }
    }
    
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

    g->numArchi = 0;
    for (int i = 0; i < g->hashSize; i++) {
        arco *a = g->gHash[i];
        while (a) { g->numArchi++; a = a->next; }
    }
}

void add_arco(grafo *g, int u, int v, int w, bool *valido) {
    if (u < 0 || v < 0 || u >= g->numNodi || v >= g->numNodi || u == v) {
        *valido = false;
        return;
    }
    if (u > v) { int tmp = u; u = v; v = tmp; }

    // Acquisisci lock sulle componenti
    lock_componenti(g, u, v);
    if (g->terminato) { 
        unlock_componenti(g, u, v); 
        *valido = false; 
        return; 
    }

    // Acquisisci lock sulla hash table
    lock_hash(g, u, v);
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
    
    int h = hash_function(u, v, g->hashSize);
    nuovo->next = g->gHash[h];
    g->gHash[h] = nuovo;
    
    inserisci_ordinato(&g->vicini[u], v, w, false);
    inserisci_ordinato(&g->vicini[v], u, w, false);
    g->numArchi++;
    unlock_hash(g, u, v);

    int cu, cv;
    pthread_mutex_lock(&g->mutMSF);
    cu = g->cCon[u];
    cv = g->cCon[v];
    pthread_mutex_unlock(&g->mutMSF);
    
    if (cu == cv) {
        // Stessa componente: trova arco max nel ciclo
        bool *visited = calloc(g->numNodi, sizeof(bool));
        if (!visited) { perror("calloc"); exit(1); }
        
        arco *max_arco = NULL;
        int max_peso = -1;
        
        // DFS per trovare l'arco di peso massimo nel percorso
        if (dfs_trova_max(g, u, v, visited, &max_arco, &max_peso)) {
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
        // Componenti diverse: aggiungi alla MSF
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
        int newRoot = (cu < cv) ? cu : cv;
        int oldRoot = (cu < cv) ? cv : cu;
        for (int i = 0; i < g->numNodi; i++) {
            if (g->cCon[i] == oldRoot)
                g->cCon[i] = newRoot;
        }
        pthread_mutex_unlock(&g->mutMSF);
        unlock_hash(g, u, v);
        ricalcola_numCoCo(g);
    }
    unlock_componenti(g, u, v);
    *valido = true;
}

void canc_arco(grafo *g, int u, int v, bool *valido) {
    if (u < 0 || v < 0 || u >= g->numNodi || v >= g->numNodi || u == v) {
        *valido = false;
        return;
    }
    if (u > v) { int tmp = u; u = v; v = tmp; }

    lock_componenti(g, u, v);
    if (g->terminato) { 
        unlock_componenti(g, u, v); 
        *valido = false; 
        return; 
    }

    lock_hash(g, u, v);
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
    
    rimuovi_da_lista(&g->vicini[u], v);
    rimuovi_da_lista(&g->vicini[v], u);
    g->numArchi--;
    unlock_hash(g, u, v);

    if (era_msf) {
        pthread_mutex_lock(&g->mutMSF);
        g->costoMSF -= peso;
        pthread_mutex_unlock(&g->mutMSF);
        
        // Ricalcola le componenti
        ricalcola_finale(g);
        
        // Cerca il minimo arco per riconnettere le componenti
        arco *min_arco = NULL;
        int min_peso = INT_MAX;
        
        // Salva le componenti correnti per evitare lock ricorsivi
        int *comp_temp = malloc(g->numNodi * sizeof(int));
        if (!comp_temp) { perror("malloc"); exit(1); }
        pthread_mutex_lock(&g->mutMSF);
        for (int i = 0; i < g->numNodi; i++)
            comp_temp[i] = g->cCon[i];
        pthread_mutex_unlock(&g->mutMSF);
        
        for (int i = 0; i < g->hashSize; i++) {
            pthread_mutex_lock(&g->mutHash[i % g->nmutex]);
            arco *a2 = g->gHash[i];
            while (a2) {
                if (!a2->msf && a2->u != 0 && a2->v != 0) {
                    int cu = comp_temp[a2->u];
                    int cv = comp_temp[a2->v];
                    
                    if (cu != cv && a2->weight < min_peso) {
                        min_peso = a2->weight;
                        min_arco = a2;
                    }
                }
                a2 = a2->next;
            }
            pthread_mutex_unlock(&g->mutHash[i % g->nmutex]);
        }
        free(comp_temp);
        
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
            int cu = g->cCon[min_arco->u];
            int cv = g->cCon[min_arco->v];
            int newRoot = (cu < cv) ? cu : cv;
            int oldRoot = (cu < cv) ? cv : cu;
            for (int i = 1; i < g->numNodi; i++) {
                if (g->cCon[i] == oldRoot)
                    g->cCon[i] = newRoot;
            }
            pthread_mutex_unlock(&g->mutMSF);
            unlock_hash(g, min_arco->u, min_arco->v);
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

void buffer_destroy(buffer_t *buf) {
    free(buf->buffer);
    pthread_mutex_destroy(&buf->mutex);
    pthread_cond_destroy(&buf->nonPieno);
    pthread_cond_destroy(&buf->nonVuoto);
}

void buffer_inserisci(buffer_t *buf, operazione op, grafo *g) {
    pthread_mutex_lock(&buf->mutex);
    while (buf->conteggio == buf->dimensione && !g->terminato)
        pthread_cond_wait(&buf->nonPieno, &buf->mutex);
    if (g->terminato) {
        pthread_mutex_unlock(&buf->mutex);
        return;
    }
    buf->buffer[buf->coda] = op;
    buf->coda = (buf->coda + 1) % buf->dimensione;
    buf->conteggio++;
    pthread_cond_signal(&buf->nonVuoto);
    pthread_mutex_unlock(&buf->mutex);
}

bool buffer_estrai(buffer_t *buf, operazione *op, grafo *g) {
    pthread_mutex_lock(&buf->mutex);
    while (buf->conteggio == 0 && !buf->finito && !g->terminato)
        pthread_cond_wait(&buf->nonVuoto, &buf->mutex);
    if (buf->conteggio == 0 || g->terminato) {
        pthread_mutex_unlock(&buf->mutex);
        return false;
    }
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

typedef struct {
    grafo *g;
    buffer_t *buf;
} thread_arg_t;

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

/*
 * ============================================================================
 * MAIN FUNCTION
 * ============================================================================
 */

int main(int argc, char *argv[]) {
    int opt;
    int threads = 3;
    int hashSize = 100000;
    int nmutex = 1000;
    char *file_grafo = NULL;
    char *file_archi = NULL;

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
    
    if (optind + 2 > argc) {
        fprintf(stderr, "Uso: %s file_grafo file_archi [-t threads] [-H hashsize] [-M nmutex]\n", argv[0]);
        exit(1);
    }
    file_grafo = argv[optind];
    file_archi = argv[optind+1];

    grafo g = {0};
    g.hashSize = hashSize;
    g.nmutex = nmutex;

    FILE *fg = fopen(file_grafo, "r");
    if (!fg) { perror("fopen grafo"); exit(1); }
    leggi_grafo(fg, &g);
    fclose(fg);

    kruskal(&g);

    pthread_mutex_lock(&g.mutPrint);
    printf("%d %d %lld\n", g.numArchi, g.numCoCo, g.costoMSF);
    pthread_mutex_unlock(&g.mutPrint);

    buffer_t buffer;
    buffer_init(&buffer, 10000);

    thread_arg_t targ = { &g, &buffer };
    pthread_t *tid = malloc(threads * sizeof(pthread_t));
    if (!tid) { perror("malloc"); exit(1); }
    for (int i = 0; i < threads; i++)
        pthread_create(&tid[i], NULL, worker_thread, &targ);

    FILE *fo = fopen(file_archi, "r");
    if (!fo) {
        fprintf(stderr, "Attenzione: non riesco ad aprire %s\n", file_archi);
    } else {
        char *linea = NULL;
        size_t len = 0;
        ssize_t nread;
        while ((nread = getline(&linea, &len, fo)) != -1) {
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

    pthread_mutex_lock(&buffer.mutex);
    buffer.finito = true;
    pthread_cond_broadcast(&buffer.nonVuoto);
    pthread_mutex_unlock(&buffer.mutex);

    for (int i = 0; i < threads; i++)
        pthread_join(tid[i], NULL);
    free(tid);

    pthread_mutex_lock(&g.mutPrint);
    printf("Operazioni terminate\n");
    pthread_mutex_unlock(&g.mutPrint);

    ricalcola_finale(&g);

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
    
    buffer_destroy(&buffer);
    
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
    
    for (int i = 0; i < g.nmutex; i++)
        pthread_mutex_destroy(&g.mutHash[i]);
    free(g.mutHash);
    pthread_mutex_destroy(&g.mutCCon);
    pthread_mutex_destroy(&g.mutMSF);
    pthread_mutex_destroy(&g.mutPrint);
    
    for (int i = 0; i < g.hashSize; i++) {
        arco *a = g.gHash[i];
        while (a) {
            arco *next = a->next;
            free(a);
            a = next;
        }
    }
    free(g.gHash);
    
    uf_free(g.uf);

    return 0;
}
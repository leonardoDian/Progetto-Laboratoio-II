#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>

// -------------------- STRUTTURE DATI --------------------
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

// -------------------- UTILITY --------------------
ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    // Implementazione portabile (già presente nel codice originale)
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

// -------------------- GRAFO --------------------
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
            g->numNodi = nodiFile + 1;   // nodi da 0 a nodiFile
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
            // NON convertiamo: i nodi nel file sono 1..N e li usiamo così
            // (il nodo 0 è extra e non compare)
            if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
                fprintf(stderr, "ERRORE: nodo fuori range (%d,%d)\n", u, v);
                continue;
            }

            int hashSize = (g->numNodi / 4);
            if (hashSize < 1) hashSize = 1;
            int hash = (u + v) % hashSize;

            arco *a = malloc(sizeof(arco));
            a->u = u;
            a->v = v;
            a->weight = w;
            a->msf = false;
            a->next = g->gHash[hash];
            g->gHash[hash] = a;

            elemento *e1 = malloc(sizeof(elemento));
            e1->id = v;
            e1->w = w;
            e1->msf = false;
            e1->next = g->vicini[u];
            g->vicini[u] = e1;

            elemento *e2 = malloc(sizeof(elemento));
            e2->id = u;
            e2->w = w;
            e2->msf = false;
            e2->next = g->vicini[v];
            g->vicini[v] = e2;
        }
    }
    free(linea);
}

// -------------------- UNION-FIND --------------------
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

// -------------------- KRUSKAL --------------------
int cmpArchi(const void *a, const void *b) {
    arco *aa = *(arco**)a;
    arco *bb = *(arco**)b;
    return aa->weight - bb->weight;
}

void kruskal(grafo *g) {
    // Raccogli tutti gli archi
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

    // Reinizializza union-find
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
            // Aggiorna flag nelle liste di adiacenza
            elemento *e = g->vicini[a->u];
            while (e) { if (e->id == a->v) { e->msf = true; break; } e = e->next; }
            e = g->vicini[a->v];
            while (e) { if (e->id == a->u) { e->msf = true; break; } e = e->next; }
            unionSets(g->uf, a->u, a->v);
        }
    }

    // Calcola cCon e numCoCo
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

// -------------------- FUNZIONI DI RICERCA --------------------
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

// DFS per trovare l'arco massimo nel percorso nella MSF
bool dfsTrovaMax(grafo *g, int curr, int target, bool *visited, arco **arcoMax, int *pesoMax) {
    visited[curr] = true;
    if (curr == target) return true;
    elemento *e = g->vicini[curr];
    while (e) {
        if (e->msf && !visited[e->id]) {
            arco *a = trovaArcoInHash(g, curr, e->id);
            if (dfsTrovaMax(g, e->id, target, visited, arcoMax, pesoMax)) {
                if (a && a->weight > *pesoMax) {
                    *pesoMax = a->weight;
                    *arcoMax = a;
                }
                return true;
            }
        }
        e = e->next;
    }
    return false;
}

// Rimuove un elemento dalla lista di adiacenza (usa g->vicini[from])
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

// -------------------- OPERAZIONI --------------------
void addArco(grafo *g, int u, int v, int w) {
    // Normalizza: u < v
    if (u > v) { int tmp = u; u = v; v = tmp; }

    // Controlli
    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
        printf("+ %d %d %d 0\n", u, v, w);
        return;
    }
    if (trovaArcoInHash(g, u, v) != NULL) {
        printf("+ %d %d %d 0\n", u, v, w);
        return;
    }

    // Creazione arco
    arco *nuovo = malloc(sizeof(arco));
    nuovo->u = u;
    nuovo->v = v;
    nuovo->weight = w;
    nuovo->msf = false;
    nuovo->next = NULL;

    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int hash = (u + v) % hashSize;
    nuovo->next = g->gHash[hash];
    g->gHash[hash] = nuovo;

    // Aggiunta alle liste di adiacenza
    elemento *e1 = malloc(sizeof(elemento));
    e1->id = v; e1->w = w; e1->msf = false;
    e1->next = g->vicini[u];
    g->vicini[u] = e1;

    elemento *e2 = malloc(sizeof(elemento));
    e2->id = u; e2->w = w; e2->msf = false;
    e2->next = g->vicini[v];
    g->vicini[v] = e2;

    g->numArchi++;

    bool aggiuntoMSF = false;

    if (g->cCon[u] == g->cCon[v]) {
        // Stessa componente: cerca arco massimo nel percorso
        bool *visited = calloc(g->numNodi, sizeof(bool));
        arco *arcoMax = NULL;
        int pesoMax = -1;
        if (dfsTrovaMax(g, u, v, visited, &arcoMax, &pesoMax)) {
            if (w < pesoMax && arcoMax != NULL) {
                // Sostituisci
                arcoMax->msf = false;
                for (elemento *e = g->vicini[arcoMax->u]; e; e = e->next)
                    if (e->id == arcoMax->v) { e->msf = false; break; }
                for (elemento *e = g->vicini[arcoMax->v]; e; e = e->next)
                    if (e->id == arcoMax->u) { e->msf = false; break; }
                g->costoMSF -= arcoMax->weight;

                nuovo->msf = true;
                e1->msf = true;
                e2->msf = true;
                g->costoMSF += w;
                aggiuntoMSF = true;
            }
        }
        free(visited);
    } else {
        // Componenti diverse: aggiungi alla MSF
        nuovo->msf = true;
        e1->msf = true;
        e2->msf = true;
        g->costoMSF += w;

        // Unisci componenti
        int oldComp = g->cCon[v];
        int newComp = g->cCon[u];
        for (int i = 0; i < g->numNodi; i++)
            if (g->cCon[i] == oldComp)
                g->cCon[i] = newComp;
        g->numCoCo--;
        aggiuntoMSF = true;
    }

    // Se non è stato aggiunto alla MSF, non modifichiamo numCoCo
    // Ricalcola numCoCo? Non serve, è già corretto
    printf("+ %d %d %d %d %d %ld\n", u, v, w, g->numArchi, g->numCoCo, g->costoMSF);
}

void cancArco(grafo *g, int u, int v) {
    // Normalizza
    if (u > v) { int tmp = u; u = v; v = tmp; }

    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
        printf("- %d %d 0\n", u, v);
        return;
    }

    arco *a = trovaArcoInHash(g, u, v);
    if (!a) {
        printf("- %d %d 0\n", u, v);
        return;
    }

    int peso = a->weight;
    bool eraInMSF = a->msf;

    // Rimuovi da hash
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int hash = (u + v) % hashSize;
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

    // Rimuovi da liste di adiacenza
    rimuoviDaLista(g, u, v);
    rimuoviDaLista(g, v, u);

    g->numArchi--;

    if (eraInMSF) {
        g->costoMSF -= peso;
        // --- Dividi la componente ---
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

        // Determina nuova componente per i visitati (minimo)
        int minVisited = u;
        for (int i = 0; i < g->numNodi; i++)
            if (visited[i] && i < minVisited) minVisited = i;

        // Componente di u (visitati) e di v (non visitati, ma che erano nella vecchia comp)
        int oldComp = g->cCon[u];
        int compU = minVisited;
        int compV = -1;
        // Prima assegna i visitati a compU
        for (int i = 0; i < g->numNodi; i++)
            if (visited[i]) g->cCon[i] = compU;

        // I non visitati che erano in oldComp formeranno l'altra componente
        for (int i = 0; i < g->numNodi; i++) {
            if (!visited[i] && g->cCon[i] == oldComp) {
                if (compV == -1 || i < compV) compV = i;
            }
        }
        if (compV == -1) compV = oldComp;  // caso raro
        for (int i = 0; i < g->numNodi; i++) {
            if (!visited[i] && g->cCon[i] == oldComp)
                g->cCon[i] = compV;
        }

        free(visited);
        g->numCoCo++;   // la componente si è divisa

        // --- Cerca arco alternativo ---
        arco *arcoMin = NULL;
        int pesoMin = INT_MAX;
        for (int i = 0; i < hashSize; i++) {
            arco *a2 = g->gHash[i];
            while (a2) {
                if (!a2->msf &&
                    ((g->cCon[a2->u] == compU && g->cCon[a2->v] == compV) ||
                     (g->cCon[a2->u] == compV && g->cCon[a2->v] == compU))) {
                    if (a2->weight < pesoMin) {
                        pesoMin = a2->weight;
                        arcoMin = a2;
                    }
                }
                a2 = a2->next;
            }
        }

        if (arcoMin != NULL) {
            arcoMin->msf = true;
            for (elemento *e = g->vicini[arcoMin->u]; e; e = e->next)
                if (e->id == arcoMin->v) { e->msf = true; break; }
            for (elemento *e = g->vicini[arcoMin->v]; e; e = e->next)
                if (e->id == arcoMin->u) { e->msf = true; break; }
            g->costoMSF += arcoMin->weight;

            // Unisci le due componenti
            int newComp = compU < compV ? compU : compV;
            int oldComp2 = compU > compV ? compU : compV;
            for (int i = 0; i < g->numNodi; i++)
                if (g->cCon[i] == oldComp2)
                    g->cCon[i] = newComp;
            g->numCoCo--;
        }
    }

    printf("- %d %d %d %d %ld\n", u, v, g->numArchi, g->numCoCo, g->costoMSF);
}

// -------------------- LETTURA OPERAZIONI --------------------
void updateGrafo(grafo *g, FILE *fModifiche) {
    char *linea = NULL;
    size_t lunghezza = 0;
    ssize_t nLetti;

    while ((nLetti = getline(&linea, &lunghezza, fModifiche)) != -1) {
        if (linea[0] == '\n' || linea[0] == '\0' || linea[0] == 'c')
            continue;

        if (linea[0] == '+') {
            int u, v, w;
            if (sscanf(linea, "+ %d %d %d", &u, &v, &w) == 3) {
                addArco(g, u, v, w);
            }
        } else if (linea[0] == '-') {
            int u, v;
            if (sscanf(linea, "- %d %d", &u, &v) == 2) {
                cancArco(g, u, v);
            }
        }
    }
    free(linea);
}

// -------------------- STATISTICHE FINALI (opzionali) --------------------
void stampaStatistiche(grafo *g) {
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int nonVuote = 0, maxLen = 0;
    double somma = 0;
    for (int i = 0; i < hashSize; i++) {
        int len = 0;
        arco *a = g->gHash[i];
        while (a) { len++; a = a->next; }
        if (len > 0) {
            nonVuote++;
            somma += len;
            if (len > maxLen) maxLen = len;
        }
    }
    double media = nonVuote ? somma / nonVuote : 0;
    printf("Numero posizioni non vuote: %d\n", nonVuote);
    printf("Lunghezza media liste: %.7f\n", media);
    printf("Lunghezza massima liste: %d\n", maxLen);
}

// -------------------- MAIN --------------------
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s file.gr file.mp\n", argv[0]);
        return 1;
    }

    FILE *fGrafo = fopen(argv[1], "r");
    if (!fGrafo) {
        perror("Errore apertura file grafo");
        return 1;
    }
    FILE *fMod = fopen(argv[2], "r");
    if (!fMod) {
        perror("Errore apertura file modifiche");
        fclose(fGrafo);
        return 1;
    }

    grafo g;
    registraGrafo(fGrafo, &g);
    fclose(fGrafo);

    kruskal(&g);

    // Stampa stato iniziale
    printf("%d %d %ld\n", g.numArchi, g.numCoCo, g.costoMSF);

    // Esegui le operazioni
    updateGrafo(&g, fMod);
    fclose(fMod);

    // (Opzionale) statistiche finali
    // stampaStatistiche(&g);

    // Non è richiesta stampa finale, ma se volessimo ristampare lo stato:
    // printf("%d %d %ld\n", g.numArchi, g.numCoCo, g.costoMSF);

    // Cleanup (non necessario ma buona pratica)
    // ... (si può fare con una funzione di distruzione)

    return 0;
}
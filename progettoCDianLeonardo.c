// ------ LIBRERIE ----------
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <pthread.h>

// ----- DEFINES -----
#define MAX_THREADS 16

// ----- STRUTTURE DATI ------

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
    int hashSize;
    elemento **vicini;
    int *cCon;
    int numCoCo;
    long costoMSF;
    int numNodi;
    int numArchi;
    unionFind *uf;
} grafo;


// ----- FUNZIONI -------

// Funzione getline per poter prendere la rifa sigolarmente  epoterla elaborare più facilmente
ssize_t getline(char **lineptr, size_t *n, FILE *stream) 
{
    size_t pos = 0;

    int c;

    if (!lineptr || !stream || !n) return -1;

    if (!*lineptr) { *n = 128; *lineptr = malloc(*n); if (!*lineptr) return -1; }

    while ((c = fgetc(stream)) != EOF) 
    {
        if (pos + 1 >= *n) 
        {
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

// Funzione di registrazione del grafo
// Funzione sigle-thread
void registraGrafo(FILE *f, grafo *g)
{
    // Inizializzo i valori per la lettura dell'arco 
    char *stringa = NULL;
    size_t lenStringa;
    ssize_t charLetti;
    g->numArchi = 0;
    g->numArchi = 0;
    g->numCoCo = 0;

    // Ciclo di registrazione del grafo
    // Prendo l'arco e aggiorno tutte le strutture all'interno della struttura dati
    while((charLetti = getline(&stringa, &lenStringa, f)) != -1)
    {
        switch(stringa[0])
        {
            case 'p':
                {
                    int nodiG, archiG;
                    sscanf(stringa ,"p sp %d %d", &nodiG, &archiG);
                    g->numNodi = nodiG + 1; // +1 perche lo 0 non viene conteggiato nel conto totale dei nodi nel file
                    g->numArchi = archiG;

                    g->hashSize = (g->numNodi/4);

                    // Nel caso si abbaino meno di 4 nodi allora comunque imposto l dimensione dell'hash a 1
                    if(g->hashSize < 1)
                        g->hashSize = 1;
                    
                    // Allocazione array 
                    g->gHash = malloc(g->hashSize *sizeof(arco*));
                    g->cCon = malloc(g->numNodi * sizeof(int));
                    g->vicini = malloc(g->numNodi * sizeof(elemento*));
                    g->uf = malloc(sizeof(unionFind));
                    g->uf->parent = malloc(g->numNodi * sizeof(int));
                    g->uf->rank = malloc(g->numNodi * sizeof(int));

                    // Inizializzaione dehgli array 
                    for(int i = 0; g->hashSize > i; i++) g->gHash[i] = NULL;

                    for(int i = 0; g->numNodi > i; i++)
                    {
                        g->cCon[i] = i;
                        g->vicini[i] = NULL;
                        g->uf->parent[i] = i;
                        g->uf->rank[i] = 0;
                    }
                }
                break;

            case 'a':
                {
                    int u, v, w;
                    sscanf(stringa, "a %d %d %d",&u,&v,&w);
                    
                    // Controllo se l'arco è valido 
                    if(u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi)
                    {
                        fprintf(stderr, "Arco non valido: nodo inesistente");
                        break;
                    }

                    // Se l'arco è valido allora inserisco nel garfo
                    int hashFun = (u+v) % g->hashSize;

                    // Creo l'arco per inerirlo nella tabella di hashing
                    arco *a = malloc(sizeof (arco));
                    a->msf = false;
                    a->next = g->gHash[hashFun];
                    a->u = u;
                    a->v = v;
                    a->weight = w;
                    g->gHash[hashFun] = a;

                    // Creo e inserisco gli elemnti per le liste di adiacenza
                    elemento *e1= malloc(sizeof (elemento));
                    e1->id = v;
                    e1->msf = false;
                    e1->w = w;
                    e1->next = g->vicini[u];
                    g->vicini[u] = e1;

                    elemento *e2 = malloc(sizeof (elemento));
                    e2->id = u;
                    e2->msf = false;
                    e2->w = w;
                    e2->next = g->vicini[v];
                    g->vicini[v] = e2;
                    break;
                }
            
            case 'c':
                // Commento nel file, non faccio niente
                break;
            default:
                // Carattere non riconosciuto
                fprintf(stderr, "Riga non conforme alla struttura del file .gr");
                break;
        }

    }
    // Libero la stringa per la lettura delle righe 
    free(stringa);
}

// Funzioni union-rank per la struttura union find per la ricerca delle componenti connesse e l'algoritmo di kruskal
int find(unionFind *uf, int x)
{
    if(uf->parent[x] != x)
        uf->parent[x] = find(uf, uf->parent[x]);
    return uf->parent[x];
}

void unionSets(unionFind *uf, int x, int y) 
{
    int rx = find(uf, x);
    int ry = find(uf, y);
    if (rx != ry) {
        if (rx < ry) uf->parent[ry] = rx;
        else uf->parent[rx] = ry;
    }
}

// Funzione per il sorting degli archi che migliora la velocità del kruskal
int cmpArchi(const void *a, const void *b) 
{
    arco *aa = *(arco**)a;
    arco *bb = *(arco**)b;
    return aa->weight - bb->weight;
}

void kruskal(grafo *g) {
    if (g->hashSize < 1) g->hashSize = 1;
    arco **arr = malloc(g->numArchi * sizeof(arco*));
    int idx = 0;
    for (int i = 0; i < g->hashSize; i++) 
    {
        arco *a = g->gHash[i];
        while (a) {
            arr[idx++] = a;
            a = a->next;
        }
    }
    qsort(arr, g->numArchi, sizeof(arco*), cmpArchi);

    for (int i = 0; i < g->numNodi; i++) 
    {
        g->uf->parent[i] = i;
        g->uf->rank[i] = 0;
    }

    g->costoMSF = 0;
    for (int i = 0; i < g->numArchi; i++) 
    {
        arco *a = arr[i];
        int ru = find(g->uf, a->u);
        int rv = find(g->uf, a->v);
        if (ru != rv) {
            a->msf = true;
            g->costoMSF += a->weight;
            elemento *e = g->vicini[a->u];
            while (e) 
            { 
                if (e->id == a->v) 
                { 
                    e->msf = true; 
                    break; 
                } 
                e = e->next; 
            }
            e = g->vicini[a->v];
            while (e) 
            { 
                if (e->id == a->u) 
                { 
                    e->msf = true; 
                    break; 
                } 
                e = e->next; 
            }
            unionSets(g->uf, a->u, a->v);
        }
    }

    for (int i = 0; i < g->numNodi; i++) 
    {
        g->cCon[i] = find(g->uf, i);
    }

    bool *visti = calloc(g->numNodi, sizeof(bool));

    g->numCoCo = 0;

    for (int i = 0; i < g->numNodi; i++) 
    {
        int r = find(g->uf, i);
        if (!visti[r]) 
        {
            visti[r] = true;
            g->numCoCo++;
        }
    }
    free(visti);
    free(arr);
}

// ---- MAIN ----
int main(int argc, char *argv[])
{
    // Controllo che vengano passati tutti i file richiesti
    if(argc != 4)
    {
        fprintf(stderr, "Input errato\n Esempio: ./msf.out esempio.gr esempio.mp [NUM_THREADS (MAX 16)]\n");
        return 1;
    }

    // Estraggo dal i valori dalla riga di input e li controllo
    int nThraeds = atoi(argv[3]);
    if(nThraeds < 1 || nThraeds > MAX_THREADS)
    {
        fprintf(stderr, "Input errato\n Numero threads fuori range\n");
        return 1;
    }

    // Apro i file
    FILE *fGrafo = fopen(argv[1], "r");
    if (!fGrafo) 
    { 
        fprintf(stderr, "Errore apertura file grafo"); 
        return 1; 
    }

    FILE *fMod = fopen(argv[2], "r");
    if (!fMod) 
    { 
        fprintf(stderr, "Errore apertura file modifiche"); 
        fclose(fGrafo); 
        return 1; 
    }

    grafo g;
    registraGrafo(fGrafo, &g);
    fclose(fGrafo);

    kruskal(&g);
    printf("%d %d %ld\n", g.numArchi, g.numCoCo, g.costoMSF);

    fclose(fMod);


    return 0;
}
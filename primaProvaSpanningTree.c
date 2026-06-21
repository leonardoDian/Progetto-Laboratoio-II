#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>



// Struttura degli archi del grafo
typedef struct arco {
    int u, v;
    int weight;
    bool msf;
    struct arco *next;
} arco;


// Struttura dei nodi per le liste di adiacenza
typedef struct elemento {
  int id;   // indice del nodo       
  int w;    // peso 
  bool msf; // true se questo arco appartiene alla MSF
  struct elemento *next;
} elemento;

// Struttura per union-find
typedef struct {
  int *parent;  // array dei genitori
  int *rank;    // array dei ranghi
} unionFind;

// Struttura del grafo
typedef struct {
  arco **gHash;       // tabella hash (array di liste di archi)
  elemento **vicini;  // array di liste di adiacenza
  int *cCon;          // array delle componenti connesse
  int numCoCo;        // numero di componente connesse
  long costoMSF;      // costo della MSF 
  int numNodi;        // numero di nodi
  int numArchi;       // numero di archi
  unionFind *uf;      // struttura union-find
} grafo;


// Ai thread ci penso dopo

// Funzione per la lettura delle righe
// Implementazione personalizzata di getline per Windows/MinGW
ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    size_t pos;
    int c;

    if (lineptr == NULL || stream == NULL || n == NULL) {
        return -1;
    }

    if (*lineptr == NULL) {
        *n = 128; // Dimensione iniziale minima
        if ((*lineptr = malloc(*n)) == NULL) {
            return -1;
        }
    }

    pos = 0;
    while ((c = fgetc(stream)) != EOF) {
        if (pos + 1 >= *n) {
            size_t new_size = *n * 2;
            char *new_ptr = realloc(*lineptr, new_size);
            if (new_ptr == NULL) {
                return -1;
            }
            *lineptr = new_ptr;
            *n = new_size;
        }

        (*lineptr)[pos++] = c;
        if (c == '\n') {
            break;
        }
    }

    if (pos == 0) {
        return -1;
    }

    (*lineptr)[pos] = '\0';
    return pos;
}

// Funzione per la registrazione del grafo all'interno del programma.
// Si utilizza la struttura grafo per memorizzare le informazioni del grafo e la struttura elemento per rappresentare gli archi
void registraGrafo(FILE *fGrafo, grafo *g)
{
  char* linea = NULL;
  size_t lunghezza = 0;
  ssize_t nLetti;
  
  g->numNodi = 0;
  g->numArchi = 0;
  g->costoMSF = 0;

  while ((nLetti = getline(&linea, &lunghezza, fGrafo)) != -1)
  {
    if(linea[0] == 'p')
    {
      int nodiFile, archiFile;
      sscanf(linea, "p sp %d %d", &nodiFile, &archiFile);
      
      // Il numero di nodi è nodiFile + 1 (perché i nodi vanno da 0 a nodiFile)
      g->numNodi = nodiFile + 1;
      g->numArchi = archiFile;
      
      printf("DEBUG: File dice %d nodi, ma alloco %d nodi (0..%d)\n", 
             nodiFile, g->numNodi, nodiFile);
      
      // Allocazione strutture
      int hashSize = (g->numNodi / 4);
      if (hashSize < 1) hashSize = 1;
      
      g->gHash = calloc(hashSize, sizeof(arco*));
      g->cCon = malloc((g->numNodi) * sizeof(int));
      g->vicini = malloc((g->numNodi) * sizeof(elemento*));
      g->uf = malloc(sizeof(unionFind));
      g->uf->parent = malloc((g->numNodi) * sizeof(int));
      g->uf->rank = malloc((g->numNodi) * sizeof(int));
      
      for(int i = 0; i < hashSize; i++)
        g->gHash[i] = NULL;
      
      for(int i = 0; i < g->numNodi; i++)
      {
        g->vicini[i] = NULL;
        g->cCon[i] = i;
        g->uf->parent[i] = i;
        g->uf->rank[i] = 0;
      }
    }
    else if(linea[0] == 'a')
    {
      int u, v, w;
      sscanf(linea, "a %d %d %d", &u, &v, &w);
      
      // NON convertire! Usa i numeri così come sono dal file
      // Perché il file ha nodi 1..6 e il programma vuole nodi 0..6
      // Quindi 1→1, 2→2, ..., 6→6
      
      printf("DEBUG: Aggiungo arco (%d,%d) peso=%d\n", u, v, w);
      
      // Verifica che i nodi siano nel range 0..g->numNodi-1
      if(u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi)
      {
        printf("ERRORE: nodo fuori range! u=%d, v=%d, range=0..%d\n", 
               u, v, g->numNodi-1);
        continue;
      }
      
      // Aggiungi alla hash table
      arco *a = malloc(sizeof(arco));
      a->u = u;
      a->v = v;
      a->weight = w;
      a->msf = false;
      
      int hashSize = (g->numNodi / 4);
      if (hashSize < 1) hashSize = 1;
      int hashFunction = (u + v) % hashSize;
      a->next = g->gHash[hashFunction];
      g->gHash[hashFunction] = a;
      
      // Aggiungi alla lista di adiacenza (entrambe le direzioni)
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
// Funzione per l'ordinamento degli archi in base al peso, da utilizzare per l'algoritmo di Kruskal
int cmpArchi(const void *a, const void *b)
{
  arco *arcoA = *(arco**)a;
  arco *arcoB = *(arco**)b;

  if (arcoA->weight < arcoB->weight) return -1;
  if (arcoA->weight > arcoB->weight) return 1;
  return 0;
}

// Funzione per trovare la radice di un nodo nella struttura union-find
int find(unionFind *uf, int x)
{
  if (uf->parent[x] != x) {
    uf->parent[x] = find(uf, uf->parent[x]); // Path compression
  }
  return uf->parent[x];
}

// Funzione per unire due componenti connesse nella struttura union-find
void unionSets(unionFind *uf, int x, int y)
{
  int rootX = find(uf, x);
  int rootY = find(uf, y);

  if (rootX != rootY) {
    // Favorisci il nodo più piccolo come radice
    if (rootX < rootY) {
      uf->parent[rootY] = rootX;
    } else {
      uf->parent[rootX] = rootY;
    }
  }
}
// Funzione per trovare la foresta di copertura del grafo con l'algoritmo di Kruskal implementando 
// anche la struttura union find per gestire le comopnenti connesse
void kruskal(grafo *g)
{
  // Raccogli tutti gli archi in un array
  arco **arrArchi = malloc(sizeof(arco*) * g->numArchi);
  int numArchiInseriti = 0;
  
  int hashSize = (g->numNodi / 4);
  if (hashSize < 1) hashSize = 1;
  
  for(int i = 0; i < hashSize; i++)
  {
    arco *a = g->gHash[i];
    while(a != NULL)
    {
      arrArchi[numArchiInseriti++] = a;
      a = a->next;
    }
  }
  
  // Ordina gli archi per peso
  qsort(arrArchi, g->numArchi, sizeof(arco*), cmpArchi);
  
  // Reinizializza union-find
  for(int i = 0; i < g->numNodi; i++)
  {
    g->uf->parent[i] = i;
    g->uf->rank[i] = 0;
  }
  
  printf("\n=== ESECUZIONE KRUSKAL ===\n");
  
  // Kruskal
  g->costoMSF = 0;
  for(int i = 0; i < g->numArchi; i++)
  {
    arco *a = arrArchi[i];
    
    int rootU = find(g->uf, a->u);
    int rootV = find(g->uf, a->v);
    
    printf("Arco (%d,%d) peso=%d: rootU=%d, rootV=%d", 
           a->u, a->v, a->weight, rootU, rootV);
    
    if(rootU != rootV)
    {
      // Unisci favorendo il nodo più piccolo come radice
      if(rootU < rootV) {
        g->uf->parent[rootV] = rootU;
        printf(" -> unisco %d sotto %d", rootV, rootU);
      } else {
        g->uf->parent[rootU] = rootV;
        printf(" -> unisco %d sotto %d", rootU, rootV);
      }
      
      a->msf = true;
      g->costoMSF += a->weight;
      
      // Aggiorna i flag nelle liste di adiacenza
      elemento *e = g->vicini[a->u];
      while(e != NULL)
      {
        if(e->id == a->v)
        {
          e->msf = true;
          break;
        }
        e = e->next;
      }
      
      e = g->vicini[a->v];
      while(e != NULL)
      {
        if(e->id == a->u)
        {
          e->msf = true;
          break;
        }
        e = e->next;
      }
      printf(" -> AGGIUNTO alla MSF\n");
    }
    else
    {
      a->msf = false;
      printf(" -> SCARTATO (creerebbe ciclo)\n");
    }
  }
  
  // Calcola le radici finali
  printf("\n=== RADICI FINALI ===\n");
  for(int i = 0; i < g->numNodi; i++)
  {
    g->cCon[i] = find(g->uf, i);
    printf("Nodo %d -> radice %d\n", i, g->cCon[i]);
  }
  
  // Le radici sono già i nodi minimi perché abbiamo favorito i numeri più piccoli
  printf("\n=== COMPONENTI FINALI ===\n");
  for(int i = 0; i < g->numNodi; i++)
  {
    printf("%d ", g->cCon[i]);
  }
  printf("\n");
  
  printf("Costo MSF: %ld\n", g->costoMSF);
  
  free(arrArchi);
}// Trova l'arco reale dentro la tabella hash partendo dai due nodi (Serve per prenderne il puntatore)
arco* trovaArcoInHash(grafo *g, int u, int v) 
{
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int hashFunction = (u + v) % hashSize;
    
    arco *a = g->gHash[hashFunction];
    while (a != NULL) {
        if ((a->u == u && a->v == v) || (a->u == v && a->v == u)) {
            return a;
        }
        a = a->next;
    }
    return NULL;
}
// Dfs che ritorna il peso del cammino tra u e v (so che esiste questo percorso perchè i due nodi hanno la stessa componente connessa)
bool dfs(grafo *g, int u, int v, bool *visited, arco **arcoMax, int *pesoCammino)
{
    visited[u] = true;

    if (u == v)
        return true; // Abbiamo raggiunto la destinazione!

    elemento *e = g->vicini[u];
    while (e != NULL)
    {
        // Camminiamo SOLO se l'arco fa parte della MSF e il nodo non è visitato
        if (e->msf && !visited[e->id])
        {
            if (dfs(g, e->id, v, visited, arcoMax, pesoCammino)) 
            {
                (*pesoCammino) += e->w; // Aggiorno il peso del cammino

                // Se la ricorsione ha trovato 'v', significa che questo arco (u, e->id) 
                // fa parte del cammino unico nella MSF.
                arco *arcoCorrente = trovaArcoInHash(g, u, e->id);

                // Se è il primo arco che analizziamo, o se è più pesante del massimo precedente, lo salviamo
                if (*arcoMax == NULL || (arcoCorrente != NULL && arcoCorrente->weight > (*arcoMax)->weight)) 
                {
                    *arcoMax = arcoCorrente;
                }
                return true; 
            }
        }
        e = e->next;
    }

    return false; // Questo ramo non porta a v
}

void updateVicini(grafo *g, int newCCon, int nodo, bool *visited)
{
  visited[nodo] = true;
  g->cCon[nodo] = newCCon; // Aggiorno il nodo corrente

  elemento *e = g->vicini[nodo];
  while (e != NULL)
  {
    // Cammino SOLO se l'arco fa parte della MSF e se il vicino non è ancora stato visitato
    if(e->msf && !visited[e->id])
    {
      updateVicini(g, newCCon, e->id, visited);
    }
    e = e->next;
  }
}

// DFS modificata per trovare l'arco massimo nel percorso
bool dfsTrovaMax(grafo *g, int current, int target, bool *visited, arco **arcoMax, int *pesoMax)
{
    visited[current] = true;
    
    if (current == target)
        return true;
    
    elemento *e = g->vicini[current];
    while (e != NULL)
    {
        // Cammino SOLO sugli archi della MSF
        if (e->msf && !visited[e->id])
        {
            // Salvo l'arco corrente prima della ricorsione
            arco *arcoCorrente = trovaArcoInHash(g, current, e->id);
            
            if (dfsTrovaMax(g, e->id, target, visited, arcoMax, pesoMax))
            {
                // Se questo arco è più pesante del massimo trovato finora
                if (arcoCorrente != NULL && arcoCorrente->weight > *pesoMax)
                {
                    *pesoMax = arcoCorrente->weight;
                    *arcoMax = arcoCorrente;
                }
                return true;
            }
        }
        e = e->next;
    }
    
    return false;
}

// Funzione per l'aggiunta di archi
void addArco(grafo *g, int u, int v, int w)
{
    printf("\n=== Aggiungo arco (%d,%d) peso=%d ===\n", u, v, w);
    
    // Verifica che i nodi siano validi
    if (u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
        printf("ERRORE: Nodi fuori range [0, %d]\n", g->numNodi-1);
        return;
    }
    
    // Controllo se l'arco esiste già
    if(trovaArcoInHash(g, u, v) != NULL)
    {
        printf("ERRORE: L'arco tra %d e %d esiste già\n", u, v);
        return;
    }
    
    // Creo il nuovo arco
    arco *nuovoArco = malloc(sizeof(arco));
    nuovoArco->u = u;
    nuovoArco->v = v;
    nuovoArco->weight = w;
    nuovoArco->msf = false;
    nuovoArco->next = NULL;
    
    // Aggiungo alla tabella hash
    int hashSize = (g->numNodi / 4);
    if (hashSize < 1) hashSize = 1;
    int hashFunction = (u + v) % hashSize;
    nuovoArco->next = g->gHash[hashFunction];
    g->gHash[hashFunction] = nuovoArco;
    
    // Aggiungo alle liste di adiacenza
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
    
    g->numArchi++;

        // Controllo se i vertici sono nella stessa componente connessa
    if(g->cCon[u] == g->cCon[v])
    {
        printf("Stessa componente connessa: %d\n", g->cCon[u]);
        
        // Cerco il percorso nella MSF corrente
        bool *visited = calloc(g->numNodi, sizeof(bool));
        arco *arcoMax = NULL;
        int pesoMax = -1;
        
        bool percorsoEsiste = dfsTrovaMax(g, u, v, visited, &arcoMax, &pesoMax);
        free(visited);
        
        if(percorsoEsiste && arcoMax != NULL)
        {
            printf("Percorso trovato! Arco massimo: (%d,%d) peso=%d\n", 
                   arcoMax->u, arcoMax->v, arcoMax->weight);
            
            // Se il nuovo arco è più leggero dell'arco massimo, sostituisco
            if(w < arcoMax->weight)
            {
                printf("Sostituisco l'arco massimo con il nuovo arco\n");
                
                // Rimuovo l'arco massimo dalla MSF
                arcoMax->msf = false;
                
                // Rimuovo dalle liste di adiacenza (aggiorno il flag)
                elemento *e = g->vicini[arcoMax->u];
                while(e != NULL)
                {
                    if(e->id == arcoMax->v)
                    {
                        e->msf = false;
                        break;
                    }
                    e = e->next;
                }
                
                e = g->vicini[arcoMax->v];
                while(e != NULL)
                {
                    if(e->id == arcoMax->u)
                    {
                        e->msf = false;
                        break;
                    }
                    e = e->next;
                }
                
                g->costoMSF -= arcoMax->weight;
                
                // Aggiungo il nuovo arco alla MSF
                nuovoArco->msf = true;
                e1->msf = true;
                e2->msf = true;
                
                g->costoMSF += w;
                printf("Nuovo costo MSF: %ld\n", g->costoMSF);
            }
            else
            {
                printf("Il nuovo arco non migliora la MSF (peso %d >= %d)\n", w, arcoMax->weight);
            }
        }
        else
        {
            printf("Nessun percorso trovato nella MSF\n");
        }
    }
    else
    {
        printf("Componenti diverse: u=%d (comp=%d), v=%d (comp=%d)\n", 
               u, g->cCon[u], v, g->cCon[v]);
        
        // Collego due alberi diversi - aggiungo l'arco alla MSF
        nuovoArco->msf = true;
        e1->msf = true;
        e2->msf = true;
        
        g->costoMSF += w;
        
        // Aggiorno le componenti connesse (unione)
        int oldComp = g->cCon[v];
        int newComp = g->cCon[u];
        
        for(int i = 0; i < g->numNodi; i++)
        {
            if(g->cCon[i] == oldComp)
                g->cCon[i] = newComp;
        }
        
        printf("Unite componenti. Nuovo costo MSF: %ld\n", g->costoMSF);
    }
    
    printf("=============================\n");
}

// Funzione per la cancellazione di un arco
// Funzione per la cancellazione di un arco
void cancArco(grafo *g, int u, int v)
{
    printf("\n=== Cancello arco (%d,%d) ===\n", u, v);
    
    // Verifica che i nodi siano validi
    if(u < 0 || u >= g->numNodi || v < 0 || v >= g->numNodi) {
        printf("ERRORE: Nodi fuori range [0, %d]\n", g->numNodi-1);
        return;
    }
    
    // Cerco l'arco nella tabella hash
    arco *arcoDaCancellare = trovaArcoInHash(g, u, v);
    
    if(arcoDaCancellare != NULL)
    {
        // Salvo informazioni sull'arco prima di cancellarlo
        int pesoArco = arcoDaCancellare->weight;
        bool eraInMSF = arcoDaCancellare->msf;
        
        // 1. Rimuovo l'arco dalla tabella hash
        int hashSize = (g->numNodi / 4);
        if(hashSize < 1) hashSize = 1;
        int hashFunction = (u + v) % hashSize;
        
        arco *curr = g->gHash[hashFunction];
        arco *prev = NULL;
        
        while(curr != NULL) {
            if((curr->u == u && curr->v == v) || (curr->u == v && curr->v == u)) {
                if(prev == NULL) {
                    g->gHash[hashFunction] = curr->next;
                } else {
                    prev->next = curr->next;
                }
                free(curr);
                break;
            }
            prev = curr;
            curr = curr->next;
        }
        
        // 2. Rimuovo l'arco dalle liste di adiacenza (da u a v)
        elemento *currE = g->vicini[u];
        elemento *prevE = NULL;
        while(currE != NULL) {
            if(currE->id == v) {
                if(prevE == NULL) {
                    g->vicini[u] = currE->next;
                } else {
                    prevE->next = currE->next;
                }
                free(currE);
                break;
            }
            prevE = currE;
            currE = currE->next;
        }
        
        // 3. Rimuovo l'arco dalle liste di adiacenza (da v a u)
        currE = g->vicini[v];
        prevE = NULL;
        while(currE != NULL) {
            if(currE->id == u) {
                if(prevE == NULL) {
                    g->vicini[v] = currE->next;
                } else {
                    prevE->next = currE->next;
                }
                free(currE);
                break;
            }
            prevE = currE;
            currE = currE->next;
        }
        
        g->numArchi--;
        
        // 4. Se l'arco era nella MSF, devo riparare la MSF
        if(eraInMSF) {
            g->costoMSF -= pesoArco;
            printf("Arco rimosso dalla MSF. Costo MSF: %ld\n", g->costoMSF);
            
            // Cerco di riconnettere le componenti usando un arco alternativo
            // I due vertici ora sono in componenti diverse
            int compU = g->cCon[u];
            int compV = g->cCon[v];
            
            if(compU != compV) {
                // Devo trovare l'arco di peso minimo che collega le due componenti
                arco *arcoMin = NULL;
                int pesoMin = INT_MAX;
                
                // Cerco in tutti gli archi del grafo
                for(int i = 0; i < hashSize; i++) {
                    arco *a = g->gHash[i];
                    while(a != NULL) {
                        if(!a->msf && g->cCon[a->u] == compU && g->cCon[a->v] == compV) {
                            if(a->weight < pesoMin) {
                                pesoMin = a->weight;
                                arcoMin = a;
                            }
                        }
                        a = a->next;
                    }
                }
                
                if(arcoMin != NULL) {
                    printf("Trovato arco alternativo: (%d,%d) peso=%d\n", 
                           arcoMin->u, arcoMin->v, arcoMin->weight);
                    
                    // Aggiungo l'arco alternativo alla MSF
                    arcoMin->msf = true;
                    
                    // Aggiorno i flag nelle liste di adiacenza
                    elemento *e = g->vicini[arcoMin->u];
                    while(e != NULL) {
                        if(e->id == arcoMin->v) {
                            e->msf = true;
                            break;
                        }
                        e = e->next;
                    }
                    
                    e = g->vicini[arcoMin->v];
                    while(e != NULL) {
                        if(e->id == arcoMin->u) {
                            e->msf = true;
                            break;
                        }
                        e = e->next;
                    }
                    
                    g->costoMSF += arcoMin->weight;
                    printf("MSF riparata. Nuovo costo MSF: %ld\n", g->costoMSF);
                    
                    // Unisco le componenti
                    int oldComp = g->cCon[arcoMin->v];
                    for(int i = 0; i < g->numNodi; i++) {
                        if(g->cCon[i] == oldComp)
                            g->cCon[i] = compU;
                    }
                } else {
                    printf("Nessun arco alternativo trovato per connettere le componenti\n");
                    printf("Il grafo MSF è ora disconnesso!\n");
                }
            }
        }
        
        printf("Arco (%d,%d) cancellato con successo\n", u, v);
    }
    else
    {
        printf("ERRORE: L'arco (%d,%d) non esiste nel grafo\n", u, v);
    }
    
    printf("=============================\n");
}

// Funzione per la lettura del file di modifiche e la selezione dell'operzaione da effettuare
void updateGrafo(grafo *g, FILE *fModifiche)
{
    char *linea = NULL;
    size_t lunghezza = 0;
    ssize_t nLetti;

    while ((nLetti = getline(&linea, &lunghezza, fModifiche)) != -1)
    {
        // Ignora righe vuote
        if (linea[0] == '\n' || linea[0] == '\0')
            continue;

        // Commenti
        if (linea[0] == 'c')
            continue;

        // Aggiunta arco
        if (linea[0] == '+')
        {
            int u, v, w;

            if (sscanf(linea, "+ %d %d %d", &u, &v, &w) == 3)
            {
                addArco(g, u, v, w);
            }
            else
            {
                printf("Formato non valido: %s", linea);
            }
        }

        // Eliminazione arco
        else if (linea[0] == '-')
        {
            int u, v;

            if (sscanf(linea, "- %d %d", &u, &v) == 2)
            {
                cancArco(g, u, v);
            }
            else
            {
                printf("Formato non valido: %s", linea);
            }
        }
    }

    free(linea);
}

// Funzione per stampa di archi
void stampaArchi(grafo *g)
{
    int hashSize = g->numNodi / 4;

    for(int i = 0; i < hashSize; i++)
    {
        arco *a = g->gHash[i];

        while(a != NULL)
        {
            printf("(%d, %d) peso=%d msf=%s\n",
                   a->u,
                   a->v,
                   a->weight,
                   a->msf ? "true" : "false");

            a = a->next;
        }
    }
}

int main(int argc, char *argv[])
{
  // Apro il file .gr
  FILE *fGrafo = fopen("minimo.gr", "r");
  if(fGrafo == NULL)
  {
    perror("Errore nell'apertura del file");
    return 1;
  }

  // Apro il file di modifiche
  FILE *fMod = fopen("minimo.mp", "r");
  if(fMod == NULL)
  {
    perror("Errore nell'apertura del file di modifiche");
    return 1;
  }

  // Creo la struttura del grafo
  grafo g;

  // Registro il grafo all'interno del programma
  registraGrafo(fGrafo, &g);

  // Applico l'algoritmo di Kruskal
  kruskal(&g);

  // Funzione per la modifica del grafo/albero
  updateGrafo(&g, fMod);

  // Chiudo il file
  fclose(fGrafo);
  fclose(fMod);


  // Per adesso stampo solo il numero di nodi e di archi per verificare che sia tutto corretto
  /*
  printf("Numero di nodi: %d\n", g.numNodi);
  printf("Numero di archi: %d\n", g.numArchi);
  printf("Costo post modifiche: %d\n", g.costoMSF);
  */

  //stampaArchi(&g);

  // Stampo l'array degli delle componenti connesse
  /*
  for(int i = 0; i < g.numNodi; i++)
  {
    printf("%d ",g.cCon[i]);
  }
  */

  printf("\n");

  return 0;
}




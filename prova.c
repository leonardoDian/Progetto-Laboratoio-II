#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

typedef struct
{
    int nInNodes; // Numero nodi entranti
    int *inNodes; // Nodi entranti
    
} inmap;

typedef struct 
{

    int nodes; // numero dei nodi del grafo
    int *out; // array con il numero di archi uscenti da ogni nodo
    inmap *in; // array con gli insiemi di archi entranti in ogni nodo

} grafo;


// Struttura dati che mi servira a mettere nel buffer degli archi letti
typedef struct 
{

    int in;
    int out;

} arco;

typedef struct
{
    int n;
    double v;
}nodo;
nodo top[5];


// Semafori e mutex per la gestione dei threds
sem_t vuoto;
sem_t pieno;
pthread_mutex_t mBuffer = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mGrafo = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mArchi = PTHREAD_MUTEX_INITIALIZER;

// Creo barrire e mutex per il risultato finale
pthread_barrier_t barrPG;
pthread_mutex_t mErrore = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mTop = PTHREAD_MUTEX_INITIALIZER;

// Flag per segnare che i consumatori devono fermarsi
int termineLavoro = 0;  

// Creo il buffer per il produttore consimatore e lo faccio di 100 archi 
arco buffer[100];

// Indici buffer
int inBuffer = 0;
int outBuffer = 0;


// Creo il grafo 
grafo *g;

// Dichiarazioni di funzioni
void *letturaMMFile(void *args);
void *creaGrafo(void * an);
double *pagerank(grafo *g, double d, double eps, int maxiter, int taux, int *numiter);
void *thraedPG(void *args);

// variabili globali per i nodi dead end e gli archi validi 
int dEndN;
int nAV = 0;

// struttura dati per il calcolo del pagerank
typedef struct 
{
    int nInizio;
    int nFine;
    double dump;
    int iterazioniMax;
    int *lastIndex;
    int *numiter;
    double eps;
    int *terminate;
    double *erroreTot;
    double *pRVett;
    double *pRVettNext;
    double *temp;
    double *top;
    pthread_mutex_t *mIndex;
} pRankData;

int main(int argc, char **argv) 
{

    if (argc != 2)
    {
        fprintf(stderr,"Errore nell'inserimento degli argomenti");
        exit(EXIT_FAILURE);
    }

    // Inizizliazzo top
    for(int i = 0; i < 5;i++)
    {
        top[i].v = 0.0;
        top[i].n = -1;
    }

    // Prendo il numero dei thread che mi è stato passato 
    int nT = atoi(argv[1]);

    // Prova di stampa dell'argomento
    printf("Numero thread: %d\n", nT);

    // Creo i thread
    pthread_t lettura;
    pthread_t crazioneGrafo[nT];

    // inizializzazione semaforo vuoto
    sem_init(&vuoto, 0, 100);  

    // Inizializzazione semaforo pieno
    sem_init(&pieno, 0, 0);    

    // inizializzo il mutex per l'esclusivita del buffer
    pthread_mutex_init(&mBuffer, NULL);

    // Inizializzo il mutex per l'esclusività del grafo
    pthread_mutex_init(&mGrafo, NULL);

    // Inizializzo il mutex per l'esclusività delle variabili per la stampa di nodi dead end e archi validi
    pthread_mutex_init(&mArchi, NULL);


    // Creo il thread per la lettura del file
    pthread_create(&lettura, NULL, letturaMMFile, (void *)"9nodi.mtx");

    // Creo i thread per la creazione del grafo
    for (int i = 0; i < nT; i++) {
        pthread_create(&crazioneGrafo[i], NULL, creaGrafo, (void*)(intptr_t) i);
    }

    // ASpetto che il lettore finisca
    pthread_join(lettura, NULL);

    // Invio i segnali al buffer che la lettura è finita
    pthread_mutex_lock(&mBuffer);

    // Imposta il flag di terminazione
    termineLavoro = 1; 

    // Invio dei segnali a tutti 
    for (int i = 0; i < nT; i++) 
    {
        // Metto a -1 per insicare che non ho più niente da inserire
        buffer[inBuffer].in = -1;
        buffer[inBuffer].out = -1;
        inBuffer = (inBuffer + 1) % 100;

        // Invio un segnale ad ogni consumatorre
        sem_post(&pieno);  
    }

    // Rilascio il buffer
    pthread_mutex_unlock(&mBuffer);

    // Aspetto che tutti i consumatori finiscano
    for (int i = 0; i < nT; i++) {
        pthread_join(crazioneGrafo[i], NULL); 
    }

    // Libero della memoria
    sem_destroy(&vuoto);
    sem_destroy(&pieno);
    pthread_mutex_destroy(&mBuffer);
    pthread_mutex_destroy(&mGrafo);

    // Stampa di prova
    /*
    for (int i = 0; i < g->nodes; i++) 
    {
        printf("Nodo %d:\n", i);
        printf("  Archi uscenti: %d\n", g->out[i]);
        printf("  Archi entranti (%d):", g->in[i].nInNodes);
        for (int j = 0; j < g->in[i].nInNodes; j++) {
            printf(" %d", g->in[i].inNodes[j]);
        }
        
        printf("\n");
    }
    */

    double *prova = pagerank(g, 0.85, 1e-6, 100, nT, 0);

    for(int i = 0; i < nAV; i++)
    {
        printf("s%f\n", prova[i]);
    }

    free(prova);

    // Libero lo spaio occupato dal grafo
    for (int i = 0; i < g->nodes; i++) 
    {
        free(g->in[i].inNodes);
    }
    free(g->in);
    free(g->out);
    free(g);
    
}

// Lettura del file Matrix Market // Produttore
void *letturaMMFile(void *args) 
{   
    // Creazione del file su cui dovrò lavorare
    FILE *file;
    char *filename;
    filename = (char *)args;

    // Apertura file 
    file = fopen(filename, "r");

   // Controllo di aver aperto correttamente il file
    if (file == NULL) 
    {
        // Esco dal programma per errore
        fprintf(stderr, "Errore durante l'apertura del file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // Creo il grafo
    g = (grafo *)calloc(1, sizeof(grafo));

    // Creo il vettore per leggere la stringa 
    char c[1024];

    // Ciclo che consente di saltare le righe commento (%)
    // Leggo il carattere
    while (fgets(c, sizeof(c), file)) 
    {
        // Controllo il primo carattere della riga 
        if (c[0] != '%') 
        {
            // Esco dal ciclo quando non necessito più di saltare i commenti
            break;
        }
    }

    // Creo le variabili che mi leggono il numero dei nodi all'interno del grafo
    int nNodi, r;

    // Leggo il numero dei nodi
    sscanf(c, "%d %d", &nNodi, &r);

    // Strampa di prova
    printf("%d %d\n", nNodi, r);

    // Imposto il numero dei nodi e istanzio e inizializzo (a 0) il grafo 
    g->nodes = nNodi;

    // Inzializzo la variabile per i nodi dead end sul grafo
    pthread_mutex_lock(&mArchi);
    dEndN = nNodi;
    pthread_mutex_unlock(&mArchi);

    g->out = (int *)calloc(nNodi, sizeof(int));
    g->in = (inmap *)calloc(nNodi, sizeof(inmap));

    // Variabili per la lettura degli archi dal file 
    int in, out;

    // Leggo gli archi
    while (fscanf(file, "%d %d", &in, &out) == 2) 
    {
        // Aggiorno il semaforo
        sem_wait(&vuoto);

        // Prendo l'esclusività del buffer
        pthread_mutex_lock(&mBuffer);

        // Stampa di prova archi
        //printf("%d %d\n", in, out);

        // Controllo se i nodi sono validi
        if (in > nNodi || in < 1 || out > nNodi || out < 1) 
        {
            // Rilascio il buffer 
            pthread_mutex_unlock(&mBuffer);

            // Aggiorno il semaforo 
            sem_post(&vuoto);

            // Passo al prossimo arco
            continue;
        }

        // Faccio si che come richiesto gli archi corrisponano alla posizione sull'array del grafo
        in -= 1;
        out -= 1;

        // Controllo se l'arco punta a se stesso
        if (in == out) 
        {
            // Rilascio il buffer
            pthread_mutex_unlock(&mBuffer);

            // Aggiorno il semaforo 
            sem_post(&vuoto);

            // Continuo con il prossimo arco
            continue;
        }

        // Inserisco nel buffer i valori e mando avanti il puntatore di ingresso nel buffer
        buffer[inBuffer].in = in;
        buffer[inBuffer].out = out;
        inBuffer = (inBuffer + 1) % 100;

        // Rilascio il buffer
        pthread_mutex_unlock(&mBuffer);

        // Aggiorno il semaforo pieno dollo in caso di su ccesso 
        sem_post(&pieno);
    }

    // Chiudo e controllo la chiususra  del filer
    if (fclose(file)) 
    {   
        // Mando un messaggio per l'essrore di chiusura
        fprintf(stderr, "Errore nella chiusura del file %s", filename);
    }

    // Esco dal thread
    pthread_exit(NULL);
}

// Consumatore
void *creaGrafo(void* an) {
    while (1) 
    {
        // Aggiorno il semaforo che sto prendendo dal buffer
        sem_wait(&pieno);

        // Prendo l'esclusività del buffer
        pthread_mutex_lock(&mBuffer);

        // leggo l'arco e aggiorno il punatore sul bufer
        arco a = buffer[outBuffer];
        outBuffer = (outBuffer + 1) % 100;

        // Rilascio il buffer
        pthread_mutex_unlock(&mBuffer);

        // Aggiorno il semaforo 
        sem_post(&vuoto);

        // Controllo se il produttore ha finito
        if (a.in == -1 && a.out == -1) 
        {
            // Sempre controllo di terminazione 
            if (termineLavoro)
            {
                // Esco dal thred
                pthread_exit(NULL);  
            }
        }

        // Prendo l'esclusiovita del grafo
        pthread_mutex_lock(&mGrafo);

        // Controllo se l'arco già esiste
        int esiste = 0;

        // Ciclo di controllo
        for (int i = 0; i < g->in[a.out].nInNodes; i++)
        {
            // Controllo se l'arco è stato già inserito
            if (g->in[a.out].inNodes[i] == a.in)
            {
                // nel caso dovesse esistere aggiorno la variabile 
                esiste = 1;
            }
        }

        // Se non esiste inzio con l'inserimento nel grafo
        if (esiste != 1) 
        {
            pthread_mutex_lock(&mArchi);

            // Aumento il numero di archi valdi
            nAV++;

            // Diminuisco il numero dei nodi che non sono dead end
            if(g->out[a.in] == 0)
            {
                dEndN--;
            }

            pthread_mutex_unlock(&mArchi);


            // Aumnto il numereo dei nodi uscenti
            g->out[a.in] += 1;

            // Stampa di prova
            int n = (int)(intptr_t) an;
            //printf("swag, sono thread %d");

            // Inserimentonel grafo 
            g->in[a.out].nInNodes += 1;
            g->in[a.out].inNodes = (int *)realloc(g->in[a.out].inNodes, sizeof(int) * (g->in[a.out].nInNodes));
            g->in[a.out].inNodes[g->in[a.out].nInNodes - 1] = a.in;
        }
        
        // Rilascio il grafo
        pthread_mutex_unlock(&mGrafo);
    }
}

double *pagerank(grafo *g, double d, double eps, int maxiter, int taux, int *numiter) {
    pthread_t tPG[taux];
    pRankData pr[taux];
    double *pRVett = malloc(g->nodes * sizeof(double));
    double *pRVettNext = malloc(g->nodes * sizeof(double));
    double deadEndSum = 0.0, erroreTotale = 0.0;
    int lastIndex = 0, terminate = 0;

    // Inizializzazione
    pthread_barrier_init(&barrPG, NULL, taux + 1);
    pthread_mutex_init(&mErrore, NULL);
    pthread_mutex_init(&mTop, NULL);

    for (int i = 0; i < g->nodes; i++) {
        pRVett[i] = 1.0 / g->nodes;
        pRVettNext[i] = 0.0;
    }

    // Configurazione thread
    int nodesPerThread = (g->nodes + taux - 1) / taux; // Divisione bilanciata
    for (int i = 0; i < taux; i++) {
        pr[i].nInizio = i * nodesPerThread;
        pr[i].nFine = (i + 1) * nodesPerThread;
        if (pr[i].nFine > g->nodes) pr[i].nFine = g->nodes;

        pr[i].dump = d;
        pr[i].eps = eps;
        pr[i].iterazioniMax = maxiter;
        pr[i].erroreTot = &erroreTotale;
        pr[i].pRVett = pRVett;
        pr[i].pRVettNext = pRVettNext;
        pr[i].numiter = numiter;
        pr[i].temp = &deadEndSum;
        pr[i].lastIndex = &lastIndex;
        pr[i].mIndex = &mBuffer; // Mutex dedicato all'indice
        pr[i].terminate = &terminate;

        pthread_create(&tPG[i], NULL, thraedPG, &pr[i]);
    }

    // Iterazioni del PageRank
    for (int iter = 0; iter < maxiter; iter++) {
        deadEndSum = 0.0;
        erroreTotale = 0.0;
        lastIndex = 0;

        // Calcolo della somma per i nodi dead-end
        for (int i = 0; i < g->nodes; i++) {
            if (g->out[i] == 0) {
                deadEndSum += pRVett[i];
            }
        }

        pthread_barrier_wait(&barrPG); // Sincronizza i thread per ogni iterazione

        if (erroreTotale < eps) {
            terminate = 1;
            printf("[Pagerank] Convergenza raggiunta dopo %d iterazioni, errore totale: %f\n", iter + 1, erroreTotale);
            *numiter = iter + 1;
            break;
        }

        // Scambia i vettori
        double *temp = pRVett;
        pRVett = pRVettNext;
        pRVettNext = temp;

        pthread_barrier_wait(&barrPG); // Sincronizza i thread
    }

    terminate = 1; // Termina i thread
    pthread_barrier_wait(&barrPG);

    // Attendi la terminazione dei thread
    for (int i = 0; i < taux; i++) {
        pthread_join(tPG[i], NULL);
    }

    pthread_barrier_destroy(&barrPG);
    pthread_mutex_destroy(&mErrore);
    pthread_mutex_destroy(&mTop);

    free(pRVettNext);
    return pRVett;
}

void *thraedPG(void *args) {
    pRankData *data = (pRankData *)args;

    while (1) {
        printf("Thread %ld: Prima della barriera\n", pthread_self());
        pthread_barrier_wait(&barrPG); // Sincronizzazione tra thread

        // Se il flag di terminazione è attivo, esci dal ciclo
        if (*(data->terminate)) {
            printf("Thread %ld: Terminazione attivata\n", pthread_self());
            break;
        }

        printf("Thread %ld: Inizio calcolo PageRank\n", pthread_self());

        // Calcolo PageRank
        for (int node = data->nInizio; node < data->nFine; node++) {
            double valoreNode = 0.0;

            // Calcolo dei contributi
            for (int i = 0; i < g->in[node].nInNodes; i++) {
                int incomingNode = g->in[node].inNodes[i];
                valoreNode += data->pRVett[incomingNode] / g->out[incomingNode];
            }

            valoreNode = data->dump * valoreNode + (1.0 - data->dump) / g->nodes;
            valoreNode += (*(data->temp)) * data->dump / g->nodes;

            double erroreLocale = fabs(valoreNode - data->pRVett[node]);
            pthread_mutex_lock(&mErrore);
            *(data->erroreTot) += erroreLocale;
            pthread_mutex_unlock(&mErrore);

            data->pRVettNext[node] = valoreNode;
        }

        pthread_barrier_wait(&barrPG); // Sincronizzazione fine iterazione
        printf("Thread %ld: Iterazione completata\n", pthread_self());

        // Verifica la terminazione
        if (*(data->terminate)) {
            printf("Thread %ld: Terminazione attivata dopo iterazione\n", pthread_self());
            break;
        }
    }

    return NULL;
}




void top5(double v, int n) {
    pthread_mutex_lock(&mTop);
    if (top[0].v <= v) {
        top[4].v = top[3].v;
        top[4].n = top[3].n;
        top[3].v = top[2].v;
        top[3].n = top[2].n;
        top[2].v = top[1].v;
        top[2].n = top[1].n;
        top[1].v = top[0].v;
        top[1].n = top[0].n;
        top[0].v = v;
        top[0].n = n;
    } else if (top[1].v <= v) {
        top[4].v = top[3].v;
        top[4].n = top[3].n;
        top[3].v = top[2].v;
        top[3].n = top[2].n;
        top[2].v = v;
        top[2].n = n;
    } else if (top[2].v <= v) {
        top[4].v = top[3].v;
        top[4].n = top[3].n;
        top[3].v = v;
        top[3].n = n;
    } else if (top[3].v <= v) {
        top[4].v = v;
        top[4].n = n;
    }
    pthread_mutex_unlock(&mTop);
}

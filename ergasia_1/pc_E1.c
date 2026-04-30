#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#define QUEUESIZE 10
#define LOOP 100

struct JobData {
    double angles[10];
    double results[10];
    struct timeval start_time;
};

struct workFunction {
  void * (*work)(void *); 
  void * arg;             
};

void *calculate_sin(void *arg) {

  struct JobData *job_data = (struct JobData *)arg;
  double *angles = job_data->angles;
  double *results = job_data->results;

  for (int i = 0; i < 10; i++) {
    results[i] = sin(angles[i]);
    //printf("Υπολογίστηκε: sin(%f) = %f\n", angles[i], results[i]);
  }

  return NULL;
}


void *producer (void *args);
void *consumer (void *args);


typedef struct {
  struct workFunction buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
} queue;

struct ConsumerData {
    queue *fifo;
    double local_wait_time;
    int local_jobs_done;
};

queue *queueInit (void);
void queueDelete (queue *q);
void queueAdd (queue *q, struct workFunction in);
void queueDel (queue *q, struct workFunction *out);

int main ()
{
  queue *fifo;
  pthread_t *pro, *con;
  int p, q;
  struct ConsumerData *con_data;

  do {
      printf("Εισάγετε τον αριθμό των producers (p > 0): ");
      // Αν η scanf δεν επιστρέψει 1 (δηλαδή δεν διάβασε 1 σωστό αριθμό)
      if (scanf("%d", &p) != 1) {
          p = 0;
          while(getchar() != '\n'); 
      }
      
      printf("Εισάγετε τον αριθμό των consumers (q > 0): ");
      if (scanf("%d", &q) != 1) {
          q = 0;
          while(getchar() != '\n'); 
      }

      if (p <= 0 || q <= 0) {
          printf("Σφάλμα: Πρέπει να δώσετε έγκυρους, θετικούς αριθμούς! Παρακαλώ δοκιμάστε ξανά.\n\n");
      }
      
  } while (p <= 0 || q <= 0);

  fifo = queueInit ();
  if (fifo ==  NULL) {
    fprintf (stderr, "main: Queue Init failed.\n");
    exit (1);
  }

  pro = (pthread_t *)malloc(p * sizeof(pthread_t));
  con = (pthread_t *)malloc(q * sizeof(pthread_t));

  con_data = malloc(q * sizeof(struct ConsumerData));

  // Δημιουργία Producers (όπως ήταν)
  for (int i = 0; i < p; i++) {
      pthread_create(&pro[i], NULL, producer, fifo);
  }

  // Δημιουργία Consumers με το νέο struct
  for (int i = 0; i < q; i++) {
      con_data[i].fifo = fifo;
      con_data[i].local_wait_time = 0.0;
      con_data[i].local_jobs_done = 0;
      pthread_create(&con[i], NULL, consumer, &con_data[i]); // Περνάμε το con_data[i]
  }

  for (int i = 0; i < p; i++) {
      pthread_join(pro[i], NULL);
  }

  while (fifo->empty == 0) {
      usleep(10000); // Περιμένει 10ms και ξαναελέγχει
  }

   
  for (int i = 0; i < q; i++) {
      pthread_cancel(con[i]);
  }

  double total_wait_time = 0.0;
  int total_jobs_done = 0;

  for (int i = 0; i < q; i++) {
      total_wait_time += con_data[i].local_wait_time;
      total_jobs_done += con_data[i].local_jobs_done;
  }

  printf("\n--- ΤΕΛΙΚΑ ΣΤΑΤΙΣΤΙΚΑ ---\n");
  printf("Producers: %d | Consumers: %d\n", p, q);
  printf("Συνολικά πακέτα: %d\n", total_jobs_done);

  double avg_wait_time = 0.0;
  if (total_jobs_done > 0) {
      avg_wait_time = total_wait_time / total_jobs_done;
      printf("Μέσος χρόνος αναμονής: %.2f μs\n", avg_wait_time);
  }

  FILE *fp = fopen("results.csv", "a"); 
  
  if (fp != NULL) {
      // Αν το αρχείο είναι άδειο (νέο), γράφουμε πρώτα τις επικεφαλίδες
      fseek(fp, 0, SEEK_END);
      if (ftell(fp) == 0) {
          fprintf(fp, "Producers,Consumers,Total_Jobs,Avg_Wait_Time_us\n");
      }
      
      // Γράφουμε τα αποτελέσματα του τρέχοντος πειράματος
      fprintf(fp, "%d,%d,%d,%.2f\n", p, q, total_jobs_done, avg_wait_time);
      fclose(fp);
      printf("-> Τα αποτελέσματα αποθηκεύτηκαν επιτυχώς στο 'results.csv'\n");
  } else {
      printf("-> Σφάλμα: Δεν μπορέσαμε να ανοίξουμε το αρχείο!\n");
  }

  queueDelete (fifo);
  free(pro);
  free(con);
  free(con_data);

  return 0;
}

void *producer (void *q)
{
  queue *fifo;
  struct workFunction job;
  int i;

  fifo = (queue *)q;

  for (i = 0; i < LOOP; i++) {
    // produce something ...

    struct JobData *data = (struct JobData *)malloc(sizeof(struct JobData));
    for (int j = 0; j < 10; j++) {
      data->angles[j] = (double)(rand() % 360); // Τυχαίες γωνίες μεταξύ 0 και 359 μοιρών
    }

    pthread_mutex_lock (fifo->mut);
    while (fifo->full) {
      //printf ("producer: queue FULL.\n");
      pthread_cond_wait (fifo->notFull, fifo->mut);
    }

    job.work = calculate_sin;
    job.arg = data;

    gettimeofday(&(data->start_time), NULL);
    queueAdd (fifo, job);
    pthread_mutex_unlock (fifo->mut);
    pthread_cond_signal (fifo->notEmpty);
  }
  return (NULL);
}

void *consumer (void *q)
{
  struct ConsumerData *my_data;
  queue *fifo;
  struct workFunction d;
  
  my_data = (struct ConsumerData *)q;
  fifo = my_data->fifo;

  while (1) {
    pthread_mutex_lock (fifo->mut);
    while (fifo->empty) {
      //printf ("consumer: queue EMPTY.\n");
      pthread_cond_wait (fifo->notEmpty, fifo->mut);
    }
    queueDel (fifo, &d);
    pthread_mutex_unlock (fifo->mut);
    pthread_cond_signal (fifo->notFull);

    struct timeval end_time;
    gettimeofday(&end_time, NULL);
    
    struct JobData *data = (struct JobData *)d.arg;
    
    double elapsed_time = (end_time.tv_sec - data->start_time.tv_sec) * 1000000.0 + 
                          (end_time.tv_usec - data->start_time.tv_usec);
                          
    //printf("Χρόνος αναμονής στην ουρά: %f microsecs\n", elapsed_time);
    //printf ("consumer: received %p.\n", d.arg);

    my_data->local_wait_time += elapsed_time;
    my_data->local_jobs_done++;

    d.work(d.arg);
    free(d.arg);
  }
  return (NULL);
}

/*
  typedef struct {
  int buf[QUEUESIZE];
  long head, tail;
  int full, empty;
  pthread_mutex_t *mut;
  pthread_cond_t *notFull, *notEmpty;
  } queue;
*/

queue *queueInit (void)
{
  queue *q;

  q = (queue *)malloc (sizeof (queue));
  if (q == NULL) return (NULL);

  q->empty = 1;
  q->full = 0;
  q->head = 0;
  q->tail = 0;
  q->mut = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
  pthread_mutex_init (q->mut, NULL);
  q->notFull = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (q->notFull, NULL);
  q->notEmpty = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
  pthread_cond_init (q->notEmpty, NULL);
	
  return (q);
}

void queueDelete (queue *q)
{
  pthread_mutex_destroy (q->mut);
  free (q->mut);	
  pthread_cond_destroy (q->notFull);
  free (q->notFull);
  pthread_cond_destroy (q->notEmpty);
  free (q->notEmpty);
  free (q);
}

void queueAdd (queue *q, struct workFunction in)
{
  q->buf[q->tail] = in;
  q->tail++;

  if (q->tail == QUEUESIZE)
    q->tail = 0;

  if (q->tail == q->head)
    q->full = 1;

  q->empty = 0;

  return;
}

void queueDel (queue *q, struct workFunction *out)
{
  *out = q->buf[q->head];

  q->head++;

  if (q->head == QUEUESIZE)
    q->head = 0;

  if (q->head == q->tail)
    q->empty = 1;

  q->full = 0;

  return;
}

#define _POSIX_C_SOURCE 200112L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h> 
#include <cjson/cJSON.h>
#include <libwebsockets.h>

#define QUEUESIZE 512
#define MAX_MSG_SIZE 4096

// --- Global Variables & Synchronization ---

// Flag to control the running state of threads (1 = Running, 0 = Terminate)
volatile sig_atomic_t keep_running = 1;

// Global counters for the 4 specific message types required by the telemetry assignment
int count_commit;
int count_identity;
int count_account;
int count_info;

// Mutex to protect the global counters from simultaneous access (Thread 2 & Thread 3)
pthread_mutex_t counters_mut;

// Flag indicating network disconnection state (0 = Connected, 1 = Disconnected)
// Used to trigger network resilience logging behavior (NaN values)
int needs_reconnect;

// --- Data Structures ---

// Network payload structure encompassing the raw JSON string
struct NetworkPacket {
    char raw_json[MAX_MSG_SIZE]; 
};

// Thread-safe Bounded Circular Queue for inter-thread communication
typedef struct {
    struct NetworkPacket buf[QUEUESIZE];
    long head, tail;
    int full, empty;
    pthread_mutex_t *mut;
    pthread_cond_t *notFull, *notEmpty;
} queue;

// --- Queue Function Prototypes ---
queue *queueInit(void);
void queueAdd(queue *q, struct NetworkPacket in);
void queueDel(queue *q, struct NetworkPacket *out);
void queueDelete(queue *q);
int getQueueOccupancy(queue *q);
queue *global_fifo = NULL;

// --- Helper Functions ---

// Reads the cumulative CPU time to calculate CPU utilization.
int get_cpu_times(unsigned long long *idle, unsigned long long *total) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;
    
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp)) {
        unsigned long long user, nice, system, idl, iowait, irq, softirq, steal;
        if (sscanf(buffer, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idl, &iowait, &irq, &softirq, &steal) >= 4) {
            *idle = idl + iowait;
            *total = user + nice + system + idl + iowait + irq + softirq + steal;
        }
    }
    fclose(fp);
    return 0;
}

// Signal handler to gracefully terminate threads upon receiving SIGINT or SIGTERM
void sig_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        keep_running = 0;

        if (global_fifo != NULL) {
            pthread_mutex_lock(global_fifo->mut);
            pthread_cond_broadcast(global_fifo->notEmpty);
            pthread_cond_broadcast(global_fifo->notFull);
            pthread_mutex_unlock(global_fifo->mut);
        }
    }
}

// --- Thread 1: The Producer (Asynchronous / Network) ---

// WebSocket event callback handler
static int firehose_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {

    (void)wsi;
    queue *fifo = (queue *)user;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            if (fifo == NULL) return -1; 
            
            // Safely copy the incoming JSON payload into a NetworkPacket structure
            // Ensure length does not exceed MAX_MSG_SIZE to prevent buffer overflow
            struct NetworkPacket packet;
            size_t copy_len = len < (MAX_MSG_SIZE - 1) ? len : (MAX_MSG_SIZE - 1);
            strncpy(packet.raw_json, (char *)in, copy_len);
            packet.raw_json[copy_len] = '\0'; 

            pthread_mutex_lock(fifo->mut);
            while (fifo->full) {
                pthread_cond_wait(fifo->notFull, fifo->mut);
            }
            queueAdd(fifo, packet);
            pthread_mutex_unlock(fifo->mut);
            
            // Signal the Consumer thread that new data is available
            pthread_cond_signal(fifo->notEmpty);
            break;
        }
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        case LWS_CALLBACK_CLIENT_CLOSED:
            // Network fault detected; signal the reconnection loop and Logger thread
            needs_reconnect = 1;
            break;
        default:
            break;
    }
    return 0; 
}

// WebSocket Protocol Definition
static struct lws_protocols protocols[] = {
    {
        .name = "firehose-protocol",
        .callback = firehose_callback,
        .per_session_data_size = 0,
        .rx_buffer_size = 0
    },
    {
        .name = NULL,
        .callback = NULL,
        .per_session_data_size = 0,
        .rx_buffer_size = 0
    }
};

void *producer(void *arg) {
    queue *fifo = (queue *)arg;
    struct lws_context_creation_info info;
    struct lws_client_connect_info ccinfo;
    struct lws_context *context;

    // Persistent connection loop ensuring network resilience
    while (keep_running) { 
        needs_reconnect = 0; 
        memset(&info, 0, sizeof(info));
        memset(&ccinfo, 0, sizeof(ccinfo));

        info.port = CONTEXT_PORT_NO_LISTEN; 
        info.protocols = protocols;
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT; 

        context = lws_create_context(&info);
        if (!context) {
            sleep(2); // if it fails to create context, wait before retrying
            continue;
        }

        // Configure WebSocket connection parameters
        ccinfo.context = context;
        ccinfo.address = "jetstream1.us-east.bsky.network";
        ccinfo.port = 443; 
        ccinfo.path = "/subscribe?wantedCollections=app.bsky.feed.post";
        ccinfo.host = ccinfo.address;
        ccinfo.origin = ccinfo.address;
        ccinfo.ssl_connection = LCCSCF_USE_SSL; 
        ccinfo.protocol = protocols[0].name;
        ccinfo.userdata = fifo; 

        lws_client_connect_via_info(&ccinfo);

        // Active event loop; breaks upon network disconnection
        while (lws_service(context, 0) >= 0 && needs_reconnect == 0 && keep_running) {
            // Actively polling for incoming packets
        }

        lws_context_destroy(context);
        if (keep_running) {
            sleep(3); // Backoff before reconnection attempt
        }
    }
    return NULL;
}

// --- Thread 2: The Consumer (Event-Driven) ---

void *consumer(void *arg) {
    queue *fifo = (queue *)arg;
    struct NetworkPacket packet;

    while (keep_running) {
        // Extract packet from the bounded circular queue
        pthread_mutex_lock(fifo->mut);
        while (fifo->empty) {
            pthread_cond_wait(fifo->notEmpty, fifo->mut);
        }
        queueDel(fifo, &packet);
        pthread_mutex_unlock(fifo->mut);
        pthread_cond_signal(fifo->notFull);
        
        // Parse JSON payload
        cJSON *root = cJSON_Parse(packet.raw_json);
        if (root != NULL) {
            cJSON *kind_item = cJSON_GetObjectItemCaseSensitive(root, "kind");

            if (cJSON_IsString(kind_item) && (kind_item->valuestring != NULL)) {
                
                pthread_mutex_lock(&counters_mut);
                
                if (strcmp(kind_item->valuestring, "commit") == 0) count_commit++;
                else if (strcmp(kind_item->valuestring, "identity") == 0) count_identity++;
                else if (strcmp(kind_item->valuestring, "account") == 0) count_account++;
                else if (strcmp(kind_item->valuestring, "info") == 0) count_info++;
                
                pthread_mutex_unlock(&counters_mut);
            }
            cJSON_Delete(root); // Prevent memory leaks
        }
    }
    return NULL;
}

// --- Thread 3: The Logger ---

void* logger_thread(void* arg) {
    queue *fifo = (queue *)arg; 
    
    // Initialize Log File with "a" (APPEND) to preserve 24-hour data safely
    FILE *csv_file = fopen("metrics_log.txt", "a");
    if (!csv_file) {
        perror("Failed to open metrics_log.txt for appending");
        exit(EXIT_FAILURE);
    }

    // Initialize CPU baseline values for Delta calculation
    unsigned long long prev_idle = 0, prev_total = 0;
    get_cpu_times(&prev_idle, &prev_total);

    // Ιnitialize monotonic clock for strict 1Hz periodic execution
    // This strictly prevents clock drift over long execution periods (e.g., 24 hours)
    struct timespec next_time;
    clock_gettime(CLOCK_MONOTONIC, &next_time);

    while (keep_running) {
        // Advance deadline by exactly 1.000000000 seconds
        next_time.tv_sec += 1;

        // Sleep strictly until the absolute deadline
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_time, NULL);

        // Calculate CPU Utilization (Delta)
        unsigned long long curr_idle = 0, curr_total = 0;
        double cpu_usage_pct = 0.0;
        
        if (get_cpu_times(&curr_idle, &curr_total) == 0) {
            unsigned long long total_diff = curr_total - prev_total;
            unsigned long long idle_diff = curr_idle - prev_idle;
            if (total_diff > 0) {
                cpu_usage_pct = 100.0 * (double)(total_diff - idle_diff) / (double)total_diff;
            }
            prev_total = curr_total;
            prev_idle = curr_idle;
        }

        // Safely acquire counters & Reset
        int current_commit, current_identity, current_account, current_info;
        
        pthread_mutex_lock(&counters_mut);
        current_commit   = count_commit;
        current_identity = count_identity;
        current_account  = count_account;
        current_info     = count_info;
        
        // Reset counters for the next 1-second window
        count_commit = 0;
        count_identity = 0;
        count_account = 0;
        count_info = 0;
        pthread_mutex_unlock(&counters_mut);

        // Calculate Buffer Occupancy
        pthread_mutex_lock(fifo->mut);
        int elements_in_queue = getQueueOccupancy(fifo);
        pthread_mutex_unlock(fifo->mut);
        
        double buffer_occupancy_pct = ((double)elements_in_queue / QUEUESIZE) * 100.0;

        // Log results to CSV
        if (csv_file) {
            struct timespec log_time;
            clock_gettime(CLOCK_REALTIME, &log_time);

            if (needs_reconnect == 0) {
                // Normal operation logging
                fprintf(csv_file, "%ld,%ld,%d,%d,%d,%d,%.2f,%.2f\n", 
                        (long)log_time.tv_sec, (long)log_time.tv_nsec,
                        current_commit, current_identity, current_account, current_info, 
                        buffer_occupancy_pct, cpu_usage_pct);
            } else {
                // Network fault logging: Insert 'NaN' to demonstrate network resilience
                if (keep_running) {
                    fprintf(csv_file, "%ld,%ld,NaN,NaN,NaN,NaN,NaN,%.2f\n", 
                            (long)log_time.tv_sec, (long)log_time.tv_nsec, cpu_usage_pct);     
                }
            }
            
            // Force write to SD card immediately to prevent data loss 
            fflush(csv_file);
        }
    }

    if (csv_file) fclose(csv_file);
    return NULL;
}

// --- Queue Implementation Functions ---

queue *queueInit(void) {
    queue *q = (queue *)malloc(sizeof(queue));
    if (q == NULL) return NULL;
    q->empty = 1;
    q->full = 0;
    q->head = 0;
    q->tail = 0;
    q->mut = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(q->mut, NULL);
    q->notFull = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
    pthread_cond_init(q->notFull, NULL);
    q->notEmpty = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
    pthread_cond_init(q->notEmpty, NULL);
    return q;
}

void queueAdd(queue *q, struct NetworkPacket in) {
    q->buf[q->tail] = in;
    q->tail++;
    if (q->tail == QUEUESIZE) q->tail = 0; 
    if (q->tail == q->head) q->full = 1; 
    q->empty = 0; 
}

void queueDel(queue *q, struct NetworkPacket *out) {
    *out = q->buf[q->head];
    q->head++;
    if (q->head == QUEUESIZE) q->head = 0; 
    if (q->head == q->tail) q->empty = 1; 
    q->full = 0; 
}

void queueDelete(queue *q) {
    pthread_mutex_destroy(q->mut);
    free(q->mut);    
    pthread_cond_destroy(q->notFull);
    free(q->notFull);
    pthread_cond_destroy(q->notEmpty);
    free(q->notEmpty);
    free(q);
}

// Calculates the current number of elements in the bounded buffer
int getQueueOccupancy(queue *q) {
    if (q->full) return QUEUESIZE;
    if (q->empty) return 0;
    if (q->tail > q->head) return q->tail - q->head;
    return QUEUESIZE - q->head + q->tail;
}

// --- Main execution block ---

int main() {
    // Initialize Global Variables
    count_commit = 0;
    count_identity = 0;
    count_account = 0;
    count_info = 0;
    needs_reconnect = 0;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Initialize Mutexes
    if (pthread_mutex_init(&counters_mut, NULL) != 0) {
        perror("Failed to initialize counters mutex");
        exit(EXIT_FAILURE);
    }

    // Initialize the Queue
    queue *fifo = queueInit();
    if (fifo == NULL) {
        fprintf(stderr, "Failed to initialize queue.\n");
        exit(EXIT_FAILURE);
    }

    global_fifo = fifo;

    // Create Threads
    pthread_t prod_thread, cons_thread, log_thread;
    
    pthread_create(&prod_thread, NULL, producer, fifo);
    pthread_create(&cons_thread, NULL, consumer, fifo);
    pthread_create(&log_thread, NULL, logger_thread, fifo);

    // Wait for Threads (Join)
    pthread_join(prod_thread, NULL);
    pthread_join(cons_thread, NULL);
    pthread_join(log_thread, NULL);

    // Cleanup
    queueDelete(fifo);
    pthread_mutex_destroy(&counters_mut);

    printf("Telemetry application terminated gracefully.\n");
    
    return 0;
}
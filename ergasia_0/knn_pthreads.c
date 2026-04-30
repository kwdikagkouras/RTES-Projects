#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cblas.h>   // Απαιτείται για τις συναρτήσεις cblas_ddot και cblas_dgemm
#include <pthread.h> 
#include <matio.h>

// Δομή δεδομένων για την επικοινωνία με τα νήματα (pthreads)
typedef struct {
    int thread_id;      // Αναγνωριστικό νήματος
    double *C_chunk;    // Δείκτης στο τμήμα του πίνακα C που ανατίθεται στο νήμα
    int nC_chunk;       // Αριθμός σημείων (γραμμών) του τμήματος
    int d;              // Αριθμός διαστάσεων των δεδομένων
    int k;              // Πλήθος κοντινότερων γειτόνων προς αναζήτηση
    int *idx_out;       // Δείκτης στον πίνακα αποθήκευσης των δεικτών (indices) των γειτόνων
    double *dst_out;    // Δείκτης στον πίνακα αποθήκευσης των αποστάσεων
    int global_offset;  // Μετατόπιση (offset) για τη σωστή αρίθμηση των δεικτών στο συνολικό πίνακα
} ThreadData;

// Υλοποίηση αλγορίθμου Quick-Select

// Αντιμετάθεση δύο ακεραίων
void swap(int *a, int *b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

// Διαχωρισμός (partition) του πίνακα για τον αλγόριθμο Quick-Select
int partition(int *arr, double *distances, int left, int right, int pivot_idx) {
    double pivot_val = distances[arr[pivot_idx]];
    swap(&arr[pivot_idx], &arr[right]);
    
    int store_idx = left;
    for (int i = left; i < right; i++) {
        if (distances[arr[i]] < pivot_val) {
            swap(&arr[i], &arr[store_idx]);
            store_idx++;
        }
    }
    swap(&arr[store_idx], &arr[right]);
    return store_idx;
}

// Εύρεση των k μικρότερων στοιχείων
void quick_select(int *arr, double *distances, int left, int right, int k) {
    if (left >= right) return;
    
    int pivot_idx = left + (right - left) / 2;
    pivot_idx = partition(arr, distances, left, right, pivot_idx);
    
    if (k == pivot_idx) {
        return;
    } else if (k < pivot_idx) {
        quick_select(arr, distances, left, pivot_idx - 1, k);
    } else {
        quick_select(arr, distances, pivot_idx + 1, right, k);
    }
}

// Κύρια συνάρτηση αναζήτησης k-NN (K-Nearest Neighbors)
// Υπολογίζει τις αποστάσεις βάσει του τύπου: D^2 = Q^2 + C^2 - 2 * Q * C^T
void knnsearch_V0(const double *C, int nC, const double *Q, int nQ, int d, int k, int *idx, double *dst) {
    
    // Δέσμευση μνήμης για τα προϋπολογισμένα τετράγωνα (L2 norms) των διανυσμάτων
    double *C_sq = malloc(nC * sizeof(double));
    double *Q_sq = malloc(nQ * sizeof(double));

    // Υπολογισμός του εσωτερικού γινομένου (dot product) κάθε διανύσματος με τον εαυτό του μέσω OpenBLAS
    for (int i = 0; i < nC; i++) {
        C_sq[i] = cblas_ddot(d, &C[i * d], 1, &C[i * d], 1);
    }
    for (int i = 0; i < nQ; i++) {
        Q_sq[i] = cblas_ddot(d, &Q[i * d], 1, &Q[i * d], 1);
    }

    // Προετοιμασία για τον τμηματικό (blocked) υπολογισμό του πίνακα αποστάσεων D
    const int BLOCK_SIZE = 256; 
    double *D_block = malloc(BLOCK_SIZE * nC * sizeof(double));
    int *current_indices = malloc(nC * sizeof(int));

    for (int q_start = 0; q_start < nQ; q_start += BLOCK_SIZE) {
        // Υπολογισμός εναπομεινάντων queries για το τρέχον μπλοκ
        int current_B = (nQ - q_start < BLOCK_SIZE) ? (nQ - q_start) : BLOCK_SIZE;

        // Αρχικοποίηση του πίνακα D_block με τα αθροίσματα Q^2 + C^2
        for (int b = 0; b < current_B; b++) {
            for (int i = 0; i < nC; i++) {
                D_block[b * nC + i] = Q_sq[q_start + b] + C_sq[i]; 
            }
        }

        // Αφαίρεση του 2 * Q * C^T κάνοντας χρήση του πολλαπλασιασμού πινάκων (GEMM) της BLAS
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasTrans, 
                    current_B, nC, d, 
                    -2.0, &Q[q_start * d], d, C, d, 
                    1.0, D_block, nC);

        // Εξαγωγή τετραγωνικής ρίζας, επιλογή k-κοντινότερων (Quick-select) και ταξινόμηση
        for (int b = 0; b < current_B; b++) {
            int q_idx = q_start + b;
            double *current_D_row = &D_block[b * nC];

            for (int i = 0; i < nC; i++) {
                // Υπολογισμός τετραγωνικής ρίζας, διασφαλίζοντας την αποφυγή αρνητικών τιμών λόγω σφαλμάτων κινητής υποδιαστολής
                current_D_row[i] = sqrt(fmax(0.0, current_D_row[i])); 
                current_indices[i] = i; // Αρχικοποίηση δεικτών
            }

            // Εφαρμογή Quick-select για ταχεία εύρεση των k γειτόνων
            quick_select(current_indices, current_D_row, 0, nC - 1, k - 1);

            // Ταξινόμηση (Selection Sort) στους k επιλεγμένους γείτονες σε αύξουσα σειρά απόστασης
            for (int i = 0; i < k - 1; i++) {
                for (int j = i + 1; j < k; j++) {
                    if (current_D_row[current_indices[j]] < current_D_row[current_indices[i]]) {
                        swap(&current_indices[i], &current_indices[j]);
                    }
                }
            }

            // Αποθήκευση αποτελεσμάτων στους τελικούς πίνακες
            for (int j = 0; j < k; j++) {
                idx[q_idx * k + j] = current_indices[j];
                dst[q_idx * k + j] = current_D_row[current_indices[j]];
            }
        }
    }

    // Απελευθέρωση δυναμικής μνήμης της συνάρτησης
    free(C_sq);
    free(Q_sq);
    free(D_block);
    free(current_indices);
}

// Συνάρτηση εκτέλεσης Νημάτων (Thread Worker)
void *worker_function(void *arg) {
    // Ανάκτηση δεδομένων νήματος από το όρισμα
    ThreadData *data = (ThreadData *)arg;
    
    printf("[Thread %d] Ξεκίνησε η επεξεργασία %d σημείων.\n", data->thread_id, data->nC_chunk);

    // Εκτέλεση αναζήτησης k-NN για το καθορισμένο τμήμα του πίνακα δεδομένων
    knnsearch_V0(data->C_chunk, data->nC_chunk, data->C_chunk, data->nC_chunk, data->d, data->k, data->idx_out, data->dst_out);

    // Προσαρμογή των δεικτών (indices) βάσει του global offset, ώστε να αντιστοιχούν στο συνολικό πίνακα
    if (data->global_offset > 0) {
        int total_results = data->nC_chunk * data->k;
        for (int i = 0; i < total_results; i++) {
            data->idx_out[i] += data->global_offset; 
        }
    }

    printf("[Thread %d] Η επεξεργασία ολοκληρώθηκε.\n", data->thread_id);
    pthread_exit(NULL);
}

// Κύρια Συνάρτηση (Main)
int main (int argc, char *argv[]) {
    
    // Έλεγχος ορισμάτων γραμμής εντολών
    if (argc != 3) {
        printf("Χρήση: %s <αρχείο.mat> <k>\n", argv[0]);
        return 1;
    }

    const char *mat_file = argv[1];
    int k = atoi(argv[2]);

    // Ανάγνωση δεδομένων από αρχείο MATLAB (.mat)
    mat_t *matfp = Mat_Open(mat_file, MAT_ACC_RDONLY);
    if (matfp == NULL) {
        printf("Σφάλμα: Αδυναμία ανάγνωσης του αρχείου %s\n", mat_file);
        return 1;
    }

    matvar_t *matvar = Mat_VarRead(matfp, "C");
    if (matvar == NULL) {
        printf("Σφάλμα: Δεν βρέθηκε η μεταβλητή 'C' στο αρχείο\n");
        Mat_Close(matfp); 
        return 1;
    }

    int nC = matvar->dims[0]; // Αριθμός σημείων (Γραμμές)
    int d = matvar->dims[1];  // Διαστάσεις (Στήλες)
    printf("Φορτώθηκαν %d σημεία (Διαστάσεις: %d). Αναζήτηση %d-κοντινότερων γειτόνων.\n", nC, d, k);

    // Μετατροπή διάταξης μνήμης από Column-Major (πρότυπο MATLAB) σε Row-Major (πρότυπο C)
    double *C_matlab = (double*)matvar->data;
    double *C_row_major = malloc(nC * d * sizeof(double));
    for (int i = 0; i < nC; i++) {
        for (int j = 0; j < d; j++) {
            C_row_major[i * d + j] = C_matlab[j * nC + i];
        }
    }
    Mat_VarFree(matvar);
    Mat_Close(matfp);

    // Δέσμευση μνήμης για τα τελικά αποτελέσματα
    int *idx = malloc(nC * k * sizeof(int));
    double *dst = malloc(nC * k * sizeof(double));

    // Διαχωρισμός του φόρτου εργασίας σε 2 νήματα (pthreads)
    int num_threads = 2;
    pthread_t threads[num_threads];
    ThreadData thread_data[num_threads];

    int half_nC = nC / 2;

    // Αρχικοποίηση παραμέτρων για το 1ο νήμα (Πρώτο μισό δεδομένων)
    thread_data[0].thread_id = 0;
    thread_data[0].C_chunk = C_row_major;
    thread_data[0].nC_chunk = half_nC;
    thread_data[0].d = d;
    thread_data[0].k = k;
    thread_data[0].idx_out = idx; 
    thread_data[0].dst_out = dst; 
    thread_data[0].global_offset = 0;

    // Αρχικοποίηση παραμέτρων για το 2ο νήμα (Δεύτερο μισό δεδομένων)
    thread_data[1].thread_id = 1;
    thread_data[1].C_chunk = C_row_major + half_nC * d; 
    thread_data[1].nC_chunk = nC - half_nC; 
    thread_data[1].d = d;
    thread_data[1].k = k;
    thread_data[1].idx_out = idx + half_nC * k; 
    thread_data[1].dst_out = dst + half_nC * k; 
    thread_data[1].global_offset = half_nC; 

    // Περιορισμός του OpenBLAS σε 1 νήμα για αποφυγή conflict με τα pthreads
    openblas_set_num_threads(1); 

    // Δημιουργία και εκτέλεση νημάτων
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_function, (void *)&thread_data[i]);
    }

    // Συγχρονισμός: Αναμονή ολοκλήρωσης (Join) όλων των νημάτων
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // Εξαγωγή αποτελεσμάτων σε νέο αρχείο MATLAB (output.mat)
    printf("Δημιουργία και αποθήκευση στο αρχείο 'output.mat'...\n");
    
    // Μετατροπή της διάταξης ξανά σε Column-Major και προσαρμογή δεικτών (1-based indexing για MATLAB)
    double *idx_out_matlab = malloc(nC * k * sizeof(double));
    double *dst_out_matlab = malloc(nC * k * sizeof(double));
    for (int i = 0; i < nC; i++) {
        for (int j = 0; j < k; j++) {
            idx_out_matlab[j * nC + i] = (double)(idx[i * k + j] + 1); 
            dst_out_matlab[j * nC + i] = dst[i * k + j];
        }
    }

    size_t dims_out[2] = {nC, k};
    mat_t *matfp_out = Mat_CreateVer("output.mat", NULL, MAT_FT_MAT5);
    
    matvar_t *var_idx = Mat_VarCreate("idx", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims_out, idx_out_matlab, 0);
    matvar_t *var_dst = Mat_VarCreate("dst", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims_out, dst_out_matlab, 0);
    
    Mat_VarWrite(matfp_out, var_idx, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp_out, var_dst, MAT_COMPRESSION_NONE);
    
    Mat_VarFree(var_idx); 
    Mat_VarFree(var_dst); 
    Mat_Close(matfp_out);

    // Καθαρισμός μνήμης και τερματισμός
    free(C_row_major);
    C_row_major = NULL; 
    free(idx);
    idx = NULL;
    free(dst); 
    dst = NULL;
    free(idx_out_matlab); 
    idx_out_matlab = NULL;
    free(dst_out_matlab);
    dst_out_matlab = NULL;

    printf("Η διαδικασία ολοκληρώθηκε με επιτυχία!\n");

    return 0;
}

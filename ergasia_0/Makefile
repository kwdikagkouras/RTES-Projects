# Ορίζουμε τον compiler και τα flags
CC = gcc
CFLAGS = -O3 -Wall

# Οι βιβλιοθήκες μας
LIBS = -lopenblas -lmatio -lpthread -lm

# Το 'all' λέει στο make τι να φτιάξει από προεπιλογή.
# Του λέμε να φτιάξει ΚΑΙ τα δύο προγράμματα!
all: knn_pthreads


# Οδηγίες για το V1 (Pthreads)
knn_pthreads: knn_pthreads.c
	$(CC) $(CFLAGS) knn_pthreads.c -o knn_pthreads $(LIBS)

# Καθαρισμός
clean:
	rm -f knn_pthreads output.mat

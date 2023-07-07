CC=mpicc
LD=mpicc

MPIDIR=${HOME}/opt/mpi
MPIINC=-I$(MPIDIR)/include
MPILIB=-lpthread -L$(MPIDIR)/lib -lmpi

LIBDIR     = -L/var/nfs_dir/scr/scr-v3.0/install/lib64 -Wl,-rpath,/var/nfs_dir/scr/scr-v3.0/install/lib64 -lscr
INCLUDES   = -I/var/nfs_dir/scr/scr-v3.0/install/include



CFLAGS=-g -Wall
LDFLAGS= $(MPILIB) -g

LINK=$(LD)

APPS=jacobi_noft jacobi_ulfm jacobi_scr

all: $(APPS)

jacobi_noft: jacobi_noft.o main.o
	$(LINK) -o $@ $^ -lm

jacobi_ulfm: jacobi_ulfm.o main.o
	$(LINK) -o $@ $^ -lm

jacobi_scr: jacobi_scr.o main.o
	$(LINK) $(INCLUDES) -o $@ $^ -lm \
		$(LDFLAGS) $(LIBDIR)

%.o: %.c jacobi.h
	$(CC) -c $(INCLUDES) $(CFLAGS) -o $@ $< \
		$(LDFLAGS) $(LIBDIR)


clean:
	rm -f *.o $(APPS) *~ 
	rm -rf iter*/


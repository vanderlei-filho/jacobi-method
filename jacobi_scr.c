#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include "jacobi.h"
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <scr.h>
#include <sys/stat.h>
#include <time.h>



/**
 * write the matrix into a byte file
 *
 * @param file_name name of the file to write in
 * @param MB  Number of rows in the input matrix.
 * @param NB   Number of columns in the input matrix.
 * @param iter    Number of iteration donne
 * @param om    Pointer to the input matrix.
 * @param nm    Pointer to the input matrix.
 */
int write_data(char *file_name, int MB, int NB, int iter, TYPE* om, TYPE* nm){
    FILE* fs = fopen(file_name, "wb");
    if (fs == NULL) {
        printf("failed to open file \n");
        return 0;
    }

    //each rank write size, iteration and matrix in separate file checkpoint
    int check = fwrite(&NB, sizeof(int), 1, fs);
    int check2 = fwrite(&MB, sizeof(int), 1, fs);
    int check3 = fwrite(&iter, sizeof(int), 1, fs);
    int check4 = (int)fwrite(om, sizeof(TYPE), (NB+2)*(MB+2), fs);
    int check5 = (int)fwrite(nm, sizeof(TYPE), (NB+2)*(MB+2), fs);
    fclose(fs);
    if (!check || !check2 || !check3 || check4 != (NB+2)*(MB+2) || check5 != (NB+2)*(MB+2)){
      return 0;
    }
    return 1;
}

/**
 * read the matrix from a byte file
 *
 * @param file_name name of the file to write in
 * @param MB  Number of rows in the input matrix.
 * @param NB   Number of columns in the input matrix.
 * @param iter    Number of iteration donne
 * @param om    Pointer to the input matrix.
 * @param nm    Pointer to the input matrix.
 */
int read_data(char *filename, int *MB, int *NB, int *iter, TYPE** om, TYPE** nm){
    FILE* file = fopen(filename, "rb"); 
    if (file == NULL) {
        printf("Failed to open the file.\n");
        return 0;
    }
    // Read the size of the matrix
    fread(NB, sizeof(int), 1, file);
    fread(MB, sizeof(int), 1, file);
    fread(iter, sizeof(int), 1, file);


    // Allocate memory for the matrix
    *om = (TYPE*)calloc(sizeof(TYPE), (*NB+2) * (*MB+2));
    *nm = (TYPE*)calloc(sizeof(TYPE), (*NB+2) * (*MB+2));


    // Read the matrix data
    int elementsRead_om = (int)fread(*om, sizeof(TYPE), (*NB + 2) * (*MB +2), file);
    int elementsRead_nm = (int)fread(*nm, sizeof(TYPE), (*NB + 2) * (*MB +2), file);

    // Check that the matrix have been correctly read
    if (elementsRead_nm!= (*NB +2) * (*MB +2) || elementsRead_om != (*NB +2) * (*MB +2)) {
        printf("Failed to read the data from the file.\n");
        free(om);
        free(nm);
        return 0;
    }

    fclose(file); 

    return 1;

}

/**
 * Create a checkpoint folder with a checkpoint file for each rank.
 *
 * @param MB  Number of rows in the input matrix.
 * @param NB   Number of columns in the input matrix.
 * @param iter    Number of iteration donne
 * @param om    Pointer to the input matrix.
 * @param nm    Pointer to the input matrix.
 */
void checkpoint(int MB, int NB, int iter, TYPE* om, TYPE* nm) {
  /*save each process stats into a file*/

    /* get rank of this process */
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    /* define checkpoint directory for the iter */
    char checkpoint_dir[256];
    sprintf(checkpoint_dir, "iter.%d", iter);

    /* build file name of checkpoint file for this rank */
    char ckpt_name[SCR_MAX_FILENAME];
    snprintf(ckpt_name, sizeof(ckpt_name), "iter.%d", iter );

    /* Step 1 : start boundary of new checkpoint*/
    SCR_Start_output(ckpt_name, SCR_FLAG_CHECKPOINT);


    char checkpoint_file[256];
    snprintf(checkpoint_file, sizeof(checkpoint_file), "%s/rank_%d.ckpt",
        checkpoint_dir, rank
    );
    int scr_retval;
    char scr_file[SCR_MAX_FILENAME];

    /* Step 2 : register the file with SCR*/
    scr_retval = SCR_Route_file(checkpoint_file, scr_file);
    if (scr_retval != SCR_SUCCESS) {
          printf("%d: failed calling SCR_Route_file(): %d: @%s:%d\n",
                 rank, scr_retval, __FILE__, __LINE__
          );
    }

    /* each rank opens, writes, and closes its file */
    int valid = write_data(checkpoint_file, MB, NB, iter, om, nm);

    /*Step 3 : define boundary of checkpoint */
    SCR_Complete_output(valid);
}

/**
 * Restart using SCR from the last checkpoint in memory.
 *
 * @param MB  Number of rows in the input matrix.
 * @param NB   Number of columns in the input matrix.
 * @param iter    Number of iteration donne
 * @param om    Pointer to the input matrix.
 * @param nm    Pointer to the input matrix.
 * @param matrix    Pointer to the input matrix.
 */
void restart(int *MB, int *NB, int *iter, TYPE** om, TYPE** nm, TYPE* matrix) {
  /*read each process stat from a file*/

  /* get rank of this process */
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  int restarted = 0;
  int have_restart = 0;
  char ckpt_name[SCR_MAX_FILENAME];
  
  do{ /*look for a valid restart point*/

    /*Step 1 :detremine wether SCR has a previous checkpoint*/
    int scr_retval = SCR_Have_restart(&have_restart, ckpt_name);
    if (scr_retval != SCR_SUCCESS) {
            printf("%d: failed calling SCR_Have_restart: %d: @%s:%d\n",
                rank, scr_retval, __FILE__, __LINE__
            );
        }
    if (have_restart) {
      if (rank == 0) {
                printf("Restarting from checkpoint named %s\n", ckpt_name);
        }
      /*Step 2 tell SCR that we are initiating a restart operation*/
      SCR_Start_restart(ckpt_name);
  



      /* build path of checkpoint file for this rank given the checkpoint name */
      char checkpoint_file[256];
      snprintf(checkpoint_file, sizeof(checkpoint_file), "%s/rank_%d.ckpt",
        ckpt_name, rank
      );

      char scr_file[SCR_MAX_FILENAME];

      /*Step 3 aquire full path*/
      SCR_Route_file(checkpoint_file, scr_file);


    /* each rank opens, reads, and closes its file */

    int valid = read_data(checkpoint_file, MB, NB, iter, om, nm);
    TYPE* verif = *om;


    /*Step 4 : tell SCR we have completed reading checkpoint file*/
    int rc = SCR_Complete_restart(valid);

    restarted = (rc == SCR_SUCCESS);
    }
  } while (have_restart && !restarted);
  
  
  /*if valid checkpoint found, return the value read, else return 0*/
  if (!restarted) {
    if(rank ==0) printf("failed reading checkpoint, restarting from begining\n");
    *om = matrix;
    *nm = (TYPE*)calloc(sizeof(TYPE), (*NB+2) * (*MB+2));
  } 
}


/**
 * Parameter SCR
*/
void scr_conf(){
    SCR_Config("STORE=/dev/shm GROUP=NODE COUNT=1");
    SCR_Config("SCR_COPY_TYPE=FILE");
    SCR_Config("CKPT=0 INTERVAL=1 GROUP=NODE STORE=/dev/shm TYPE=XOR SET_SIZE=16");
    SCR_Config("SCR_DEBUG=1");
    SCR_Config("SCR_CHECKPOINT_INTERVAL=5");
}



/**
 * Prints the minimum and maximum timings of a specific loop in the program.
 *
 * @param scomm  MPI communicator for the processes involved in the timings.
 * @param rank   Rank of the current MPI process.
 * @param twf    Time (in seconds) taken for the specific loop in the current MPI process.
 */
void print_timings(MPI_Comm scomm, int rank, double twf)
{
    // Storage for min and max times
    double mtwf, Mtwf;

    // Perform reduction to find the minimum time across all MPI processes
    MPI_Reduce(&twf, &mtwf, 1, MPI_DOUBLE, MPI_MIN, 0, scomm);

    // Perform reduction to find the maximum time across all MPI processes
    MPI_Reduce(&twf, &Mtwf, 1, MPI_DOUBLE, MPI_MAX, 0, scomm);

    // If the current process is rank 0, print the min and max timings
    if (0 == rank) {
        printf(
            "##### Timings #####\n"
            "# MIN: %13.5e \t MAX: %13.5e\n",
            mtwf, Mtwf
        );
    }
}


/**
 * Performs one iteration of the Successive Over-Relaxation (SOR) method
 * on the input matrix and computes the squared L2-norm of the difference
 * between the input and output matrices.
 *
 * @param nm   Pointer to the output matrix after one iteration of the SOR method.
 * @param om   Pointer to the input matrix.
 * @param nb   Number of columns in the input matrix.
 * @param mb   Number of rows in the input matrix.
 * @return     The squared L2-norm of the difference between the input and output matrices.
 */
TYPE SOR1(TYPE* nm, TYPE* om, int nb, int mb)
{
    TYPE norm = 0.0;
    TYPE _W = 2.0 / (1.0 + M_PI / (TYPE)nb);
    int i, j, pos;

    // Iterate through each element of the matrix
    for (j = 0; j < mb; j++) {
        for (i = 0; i < nb; i++) {
            // Compute the position of the current element
            pos = 1 + i + (j + 1) * (nb + 2);

            // Update the current element using the SOR method
            nm[pos] = (1 - _W) * om[pos] +
                      _W / 4.0 * (nm[pos - 1] +
                                  om[pos + 1] +
                                  nm[pos - (nb + 2)] +
                                  om[pos + (nb + 2)]);

            // Accumulate the squared L2-norm of the difference
            norm += (nm[pos] - om[pos]) * (nm[pos] - om[pos]);
        }
    }

    return norm;
}


/**
 * Performs any required pre-initialization steps for the Jacobi method.
 * This function is a placeholder that can be extended if needed.
 *
 * @return     0 on successful completion.
 */
int preinit_jacobi_cpu(void)
{
    // Currently, there are no pre-initialization steps required for the
    // Jacobi method on the CPU. This function serves as a placeholder and
    // can be extended if necessary.

    return 0;
}

int jacobi_cpu(TYPE* matrix, int NB, int MB, int P, int Q, MPI_Comm comm, TYPE epsilon)
{
    //scr_conf();

    /*Start SCR*/
    if (SCR_Init() != SCR_SUCCESS){
        printf("Failed initializing SCR\n");
        return 1;
    }


    int i, iter = 0;
    int rank, size, ew_rank, ew_size, ns_rank, ns_size;
    TYPE *om, *nm, *tmpm, *send_east, *send_west, *recv_east, *recv_west, diff_norm;
    double start, start_restart, start_checkpoint, time_restart, time_checkpoint, twf=0; /* timings */

    if(rank ==0) printf("begining restart scr\n");

    start_restart = MPI_Wtime();
    restart(&MB, &NB, &iter, &om, &nm, matrix);
    time_restart = MPI_Wtime()- start_restart;
    printf("restart time : %f on rank %d\n", time_restart, rank);


    MPI_Comm ns, ew;
    MPI_Request req[8] = {MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL,
                          MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL, MPI_REQUEST_NULL};

    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    
    send_east = (TYPE*)malloc(sizeof(TYPE) * MB);
    send_west = (TYPE*)malloc(sizeof(TYPE) * MB);
    recv_east = (TYPE*)malloc(sizeof(TYPE) * MB);
    recv_west = (TYPE*)malloc(sizeof(TYPE) * MB);

    /* create the north-south and east-west communicator */
    MPI_Comm_split(comm, rank % P, rank, &ns);
    MPI_Comm_size(ns, &ns_size);
    MPI_Comm_rank(ns, &ns_rank);
    MPI_Comm_split(comm, rank / P, rank, &ew);
    MPI_Comm_size(ew, &ew_size);
    MPI_Comm_rank(ew, &ew_rank);

    start = MPI_Wtime();
    do {
        /* post receives from the neighbors */
        if( 0 != ns_rank )
            MPI_Irecv( RECV_NORTH(om), NB, MPI_TYPE, ns_rank - 1, 0, ns, &req[0]);
        if( (ns_size-1) != ns_rank )
            MPI_Irecv( RECV_SOUTH(om), NB, MPI_TYPE, ns_rank + 1, 0, ns, &req[1]);
        if( (ew_size-1) != ew_rank )
            MPI_Irecv( recv_east,      MB, MPI_TYPE, ew_rank + 1, 0, ew, &req[2]);
        if( 0 != ew_rank )
            MPI_Irecv( recv_west,      MB, MPI_TYPE, ew_rank - 1, 0, ew, &req[3]);

        /* post the sends */
        if( 0 != ns_rank )
            MPI_Isend( SEND_NORTH(om), NB, MPI_TYPE, ns_rank - 1, 0, ns, &req[4]);
        if( (ns_size-1) != ns_rank )
            MPI_Isend( SEND_SOUTH(om), NB, MPI_TYPE, ns_rank + 1, 0, ns, &req[5]);
        for(i = 0; i < MB; i++) {
            send_west[i] = om[(i+1)*(NB+2)      + 1];  /* the real local data */
            send_east[i] = om[(i+1)*(NB+2) + NB + 0];  /* not the ghost region */
        }
        if( (ew_size-1) != ew_rank)
            MPI_Isend( send_east,      MB, MPI_TYPE, ew_rank + 1, 0, ew, &req[6]);
        if( 0 != ew_rank )
            MPI_Isend( send_west,      MB, MPI_TYPE, ew_rank - 1, 0, ew, &req[7]);
        /* wait until they all complete */
        MPI_Waitall(8, req, MPI_STATUSES_IGNORE);

        /* unpack the east-west newly received data */
        for(i = 0; i < MB; i++) {
            om[(i+1)*(NB+2)         ] = recv_west[i];
            om[(i+1)*(NB+2) + NB + 1] = recv_east[i];
        }

        /**
         * Call the Successive Over Relaxation (SOR) method
         */
        diff_norm = SOR1(nm, om, NB, MB);

        MPI_Allreduce(MPI_IN_PLACE, &diff_norm, 1, MPI_TYPE, MPI_SUM,
                      comm);
        if(0 == rank) {
            printf("Iteration %4d norm %f\n", iter, sqrtf(diff_norm));
        }
        tmpm = om; om = nm; nm = tmpm;  /* swap the 2 matrices */
        iter++;
        /*Do a checkpoint if needed */
        int need_ckpt;
        SCR_Need_checkpoint(&need_ckpt);
        if (need_ckpt){
            start_checkpoint = MPI_Wtime();
            checkpoint(MB, NB, iter, om, nm);
            time_checkpoint = MPI_Wtime()- start_checkpoint;
            if(rank ==0) printf("checkpoint %d \n", iter);
            print_timings( comm, rank, time_checkpoint );
        }

    } while((iter < MAX_ITER) && (sqrt(diff_norm) > epsilon));


    twf = MPI_Wtime() - start;
    print_timings( comm, rank, twf );

    if(matrix != om) free(om);
    else free(nm);
    free(send_west);
    free(send_east);
    free(recv_west);
    free(recv_east);

    MPI_Comm_free(&ns);
    MPI_Comm_free(&ew);

    /*close SCR*/
    
    SCR_Finalize();

    return iter;
}


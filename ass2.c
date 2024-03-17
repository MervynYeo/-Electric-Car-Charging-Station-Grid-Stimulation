#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <mpi.h>
#include <unistd.h>
#include <string.h>
#include <omp.h>
#define MSG_EXIT 1
#define MSG_FULL 2

#define MSG_PRINT_UNORDERED 3
#define MSG_SUGGEST 5
#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1

int master_io(MPI_Comm world_comm, MPI_Comm comm,int ncols,int nrows);
int slave_io(MPI_Comm world_comm, MPI_Comm comm,int ncol,int nrow);
int getNeighbours(int nrows,int ncols,int rank,int *suggestion);


int main(int argc, char **argv)
{
    int rank, size,nrows,ncols; 
    MPI_Comm new_comm;
    int provided;
    
    MPI_Init_thread(&argc, &argv,MPI_THREAD_MULTIPLE,&provided);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    MPI_Comm_split( MPI_COMM_WORLD,rank == size-1, 0, &new_comm);
    if(argc == 3){
        //get the rows and columns
        nrows=atoi(argv[1]);
        ncols=atoi(argv[2]);
        if(nrows*ncols!=size-1){
            printf("ERROR: nrows*ncols!= size-1");
            //MPI_Finalize();
            return 0;
        }
    }
   
   

    // color will either be 0 or 1
    if (rank == size-1)
        master_io( MPI_COMM_WORLD, new_comm,ncols,nrows );
    else
        slave_io( MPI_COMM_WORLD, new_comm,ncols,nrows );
    
    MPI_Finalize();
    return 0;
}
/* This is the master */
int master_io(MPI_Comm world_comm, MPI_Comm comm,int ncols,int nrows)
{
    int i, size, nslaves, firstmsg,slave_available;
    int niterations=0;
    MPI_Status status;
    MPI_Comm_size(world_comm, &size );
    nslaves = size - 1;
    slave_available= size - 1;
    
    int pack_size,int_size,char_size;
    int iterations= 0;
    int current_iter= 0;
    int* availability = (int*)calloc(nslaves, sizeof(int));
    bool quit=false;
    double start;
    int num_reports= 0;
    MPI_Request send_request[nslaves];
    MPI_Request receive_request[nslaves];
    MPI_Status send_status[nslaves];
    MPI_Status receive_status[nslaves];
    int num_buffer=80*nslaves;
    //index indicating current buffer index and index to write to log file
    int buffer_index=0;
    int write_index=0;
    int** node_index = (int**)malloc(num_buffer * sizeof(int*));
    int** node_availability = (int**)malloc(num_buffer * sizeof(int*));
    char** reporting_time = (char**)malloc(num_buffer * sizeof(char*));
    int ** nearby_nodes=(int**)malloc(num_buffer * sizeof(int*));
    int* iterations_arr=(int*)malloc(num_buffer * sizeof(int));
    double* communication_time = (double*)malloc(num_buffer * sizeof(double));
    
    
    MPI_Pack_size(101, MPI_CHAR, world_comm, &char_size);

    MPI_Pack_size(5, MPI_INT, world_comm, &pack_size);
    MPI_Pack_size(1, MPI_INT, world_comm, &int_size);
    MPI_Barrier(world_comm);

    #pragma omp parallel num_threads(2)
    {
        #pragma omp sections
		{
            #pragma omp section
            {
                while(true){
                    sleep(3);
                    //quit after iteration 80
                    if( current_iter>80){
                        int send=0;
                        int msg;
                        printf("Sending quit message to all the stations\n");
                        fflush(stdout);
                        quit=true;
                        MPI_Request request;
                        //send termination message to all stations and receive quit message from them
                        for(int i=0;i<nslaves;i++){
                            MPI_Isend(&send, 1, MPI_INT, i, MSG_EXIT, world_comm,&send_request[i]);
                            MPI_Irecv(&msg, 1, MPI_INT, i, MSG_EXIT,world_comm,&receive_request[i]);
                            
                        }
                        MPI_Barrier(world_comm);
                        MPI_Waitall(nslaves, send_request, send_status);
                        MPI_Waitall(nslaves, receive_request, receive_status);  
                        break;
                    }
                    else{
                        MPI_Barrier(world_comm);
                    }
                    
                    int flag;
                    MPI_Barrier(world_comm);
                    for(int i=0;i<nslaves;i++){
                        //check for any incoming alert from all stations
                        MPI_Iprobe(i, MPI_ANY_TAG, world_comm, &flag, MPI_STATUS_IGNORE);
                        if(flag){
                            
                            current_iter++;
                            
                            int position = 0;
                            char* buffer = (char*)malloc(pack_size*2+int_size);
                            
                            int* nodebuf=(int*)malloc(5*sizeof(int));
                            /
                            int* valbuf=(int*)malloc(5*sizeof(int));
                            


                            double comm_time;
                            //start time
                            double start_time=MPI_Wtime();
                            //receive message
                            MPI_Recv(buffer, pack_size*2+int_size, MPI_PACKED, i,MPI_ANY_TAG, world_comm, &status);
                            
                            //unpack information
                            MPI_Unpack(buffer, pack_size*2+int_size, &position, valbuf, 5, MPI_INT, world_comm);
                            MPI_Unpack(buffer, pack_size*2+int_size, &position, nodebuf, 5, MPI_INT, world_comm);
                            MPI_Unpack(buffer, pack_size*2+int_size, &position, &iterations, 1, MPI_INT, world_comm);

                            iterations_arr[buffer_index]=iterations;
                            node_index[buffer_index]=nodebuf;
                            node_availability[buffer_index]=valbuf;                                                  
                            
                            
                            num_reports++;
                            printf("number of reports:%d\n",num_reports);                          
                                                     
                            int* suggestion=(int*)calloc(nslaves, sizeof(int));
                            //update availability of stations
                            for(int i=1;i<5;i++){
                                if(valbuf[i]!=-1){                                                                      
                                    availability[nodebuf[i]]=current_iter;
                                    getNeighbours(nrows,ncols,nodebuf[i],suggestion);
                                }
                                
                            }


                            int*nearbynodes =(int*)malloc(8*sizeof(int));
                            memset(nearbynodes,-1,8*sizeof(int));
                            int num_nearbynodes=0;
                            char availableNodes[200]="";
                            //get nearby charging stations that are available
                            for(int i=0;i<nslaves;i++){
                                if(suggestion[i]!=0 && i!=nodebuf[0] && availability[i]<current_iter-1){
                                    char station[20];
                                    sprintf(station,"Station %d\n",i);
                                    strcat(availableNodes,station);
                                    nearbynodes[num_nearbynodes]=i;
                                    num_nearbynodes++;
                                }
                            }
                            nearby_nodes[buffer_index]=nearbynodes;


                            int send_position=0;
                            char *sendBuffer=(char*)malloc(8*int_size);
                            if(num_nearbynodes==0){
                                sprintf(availableNodes,"No nearby charging stations available.\n");
                                
                            }

                            int buffersize=201;
                            //Suggest nearby charging stations  
                            MPI_Send(&buffersize, 1, MPI_INT,  status.MPI_SOURCE,MSG_SUGGEST, world_comm);
                            MPI_Send(availableNodes, strlen(availableNodes)+1, MPI_CHAR,  status.MPI_SOURCE,MSG_SUGGEST, world_comm);
                            double end_time=MPI_Wtime();
                            //get total communication time between base station and alerted charging station
                            comm_time=end_time-start_time;
                            communication_time[buffer_index]=comm_time;

                            buffer_index++;

                         
                            
                            
                        }


                    }
                    current_iter++;
                    printf("Current iteration:%d\n",current_iter);
                    
                }

            }
            //thread for writing log file
            #pragma omp section
            {
                FILE *file;
                time_t t;
                struct tm *current_time;
                file = fopen("log.txt", "w");
                while(true){
                    //break when base station quit
                    if(write_index>=buffer_index && quit){
                        break;
                    }
                    //if there is new item to write
                    if(write_index<buffer_index){
                       
                         // Get log time
                        t = time(NULL);
                        current_time = localtime(&t);
                       
                        char log_time[100];
                        strftime(log_time, sizeof(log_time), "%Y-%m-%d %H:%M:%S", current_time);
                        //get information to write
                        int* nodebuf=node_index[write_index];
                        int* valbuf=node_availability[write_index];
                        int* nearbynodes=nearby_nodes[write_index];
                        double comm_time=communication_time[write_index];
                        int iteration=iterations_arr[write_index];
                        write_index++;
                        //Writing to log file
                        fprintf(file, "Iteration :%d\n",iteration);
                        fprintf(file,"Logged time:%s\n",log_time);
                        fprintf(file, "Availability to be considered full: 1\n");
                        fprintf(file,"Reporting node        Port value    Available Port\n");
                        fprintf(file,"%-3d                      %d              %d\n",nodebuf[0],5,valbuf[0]);
                        fprintf(file,"\n");
                        fprintf(file,"Adjacent  node        Port value    Available Port\n");
                        for(int i=1;i<5;i++){
                            int node=nodebuf[i];
                            int val=valbuf[i];
                            if(node>=0 && val>=0){
                                fprintf(file,"%-3d                      %d              %d\n",node,5,val);
                                
                            }

                        }
                        fprintf(file,"\n");
                        fprintf(file,"Available station nearby : ");
                        int nearby_count=0;
                        for(int i=0;i<8;i++){
                            if(nearbynodes[i]>=0){
                                if(nearby_count>0){
                                    fprintf(file,",%d ",nearbynodes[i]);
                                }
                                else{
                                   fprintf(file,"%d ",nearbynodes[i]); 
                                }
                                nearby_count++;
                                
                            }
                        }
                        fprintf(file,"\n");
                        fprintf(file,"Total communication time(in seconds): %f\n",comm_time);
                        fprintf(file,"Total Messages send between reporting node and base station: 2\n");
                        fprintf(file,"\n");
                        fprintf(file,"-------------------------------------------------------------------------\n");

                    }
                    

                }
                fprintf(file,"All stations have closed down for maintenance .\n");
                fclose(file);
            }
        }
    }
    //free heap array
    free(node_index);
    free(node_availability);
    free(reporting_time);
    free(nearby_nodes);
    free(iterations_arr);
    free(communication_time);
    
    
    
    return 0;
}
/* This is the slave */
int slave_io(MPI_Comm world_comm, MPI_Comm comm,int ncol,int nrow)
{
     
    int ndims=2, size, my_rank, reorder, my_cart_rank, ierr, worldSize,world_rank;
    MPI_Request send_request[4];
    MPI_Request receive_request[4];
    MPI_Status send_status[4];
    MPI_Status receive_status[4];
    MPI_Status status;

    MPI_Comm comm2D;
    int dims[ndims],coord[ndims];
    int wrap_around[ndims];
    char buf[256];
    int nbr_i_lo, nbr_i_hi;
    int nbr_j_lo, nbr_j_hi;
    MPI_Comm_size(world_comm, &worldSize); // size of the world communicator
    MPI_Comm_size(comm, &size); // size of the slave communicator
    MPI_Comm_rank(comm, &my_rank); // rank of the slave communicator
    MPI_Comm_rank(world_comm,&world_rank);
    unsigned int randomSeed;
    randomSeed = my_rank * time(NULL);
    dims[0]=nrow;
    dims[1]=ncol;    
    if(my_rank==0)
        printf("Slave Rank: %d. Comm Size: %d: Grid Dimension = [%d x %d]\n",my_rank,size,dims[0],dims[1]);
    /* create cartesian mapping */
    MPI_Dims_create(size, ndims, dims);
    wrap_around[0] = 0;
    wrap_around[1] = 0; /* periodic shift is .false. */
    reorder = 0;
    ierr =0;
    ierr = MPI_Cart_create(comm, ndims, dims, wrap_around, reorder,&comm2D);
    if(ierr != 0)
        printf("ERROR[%d] creating CART\n",ierr);
    /* find my coordinates in the cartesian communicator group */
    MPI_Cart_coords(comm2D, my_rank, ndims, coord); //coordinated is returned into the coord array
    /* use my cartesian coordinates to find my rank in cartesian
    group*/
    MPI_Cart_rank(comm2D, coord, &my_cart_rank);
    MPI_Cart_shift(comm2D, SHIFT_ROW, DISP, &nbr_i_lo, &nbr_i_hi);
    MPI_Cart_shift(comm2D, SHIFT_COL, DISP, &nbr_j_lo, &nbr_j_hi);
    int pack_size;
    int iterations=1;
    int int_size;
    int flag;
    int char_size;
    MPI_Barrier(world_comm);
    omp_set_num_threads(5);

    while(true){
        //sleep for 3 seconds
        sleep(3);
        //sync with base station
        MPI_Barrier(world_comm);
        //check for termination message
        MPI_Iprobe(worldSize-1, MPI_ANY_TAG , world_comm, &flag, MPI_STATUS_IGNORE);
        //quit
        if(flag){
            int send=0;
            int msg;
            printf("Station %d is quitting\n",my_rank);
            fflush(stdout);
            MPI_Request request;
            MPI_Recv(&msg, 1, MPI_INT, worldSize-1, MSG_EXIT,world_comm,MPI_STATUS_IGNORE);
           
            MPI_Send(&send, 1, MPI_INT,  worldSize-1, MSG_EXIT, world_comm);
            break;
            
        }
        //get number of port available
        int *port_values=(int*)malloc(5);
        int num_port=0;
        #pragma omp parallel for num_threads(5)
        for(int i=0;i<5;i++){
            int rand_val=rand_r(&randomSeed)%2;
            port_values[i]=rand_val;
            
        }

        for(int i=0;i<5;i++){
            num_port+=port_values[i];
        }
        
        int recvValues[5] = {num_port,-1, -1, -1, -1};
        int neighbors[4] = {nbr_j_lo, nbr_j_hi, nbr_i_lo, nbr_i_hi};
        bool Full=true;
        
        int request_flag;
        //if full
        if(num_port<=1){
            //get port values from neighbours
            for (int i = 1; i < 5; i++) {
                MPI_Isend(&num_port, 1, MPI_INT, neighbors[i-1], 123, comm2D,&send_request[i-1]);
                MPI_Irecv(&recvValues[i], 1, MPI_INT, neighbors[i-1], 321,comm2D,&receive_request[i-1]);
            
            }
            MPI_Barrier(comm2D);
            //listen to neighbour for any request and reply if any 
            for (int i = 1; i < 5; i++) {
                MPI_Iprobe(neighbors[i-1], 123 , comm2D, &request_flag, MPI_STATUS_IGNORE);
                if(request_flag){
                    int recv;
                    MPI_Request send_reply_request;
                    MPI_Request receive_reply_request;
                    MPI_Irecv(&recv, 1, MPI_INT, neighbors[i-1], 123,comm2D,&receive_reply_request);
                    MPI_Isend(&num_port, 1, MPI_INT, neighbors[i-1], 321, comm2D,&send_reply_request);

                }    
                
            
            }
            MPI_Waitall(4, send_request, send_status);
            MPI_Waitall(4, receive_request, receive_status);
        }
        else{
            MPI_Barrier(comm2D);
            for (int i = 1; i < 5; i++) {
                //listen to neighbour for any request and reply if any 
                MPI_Iprobe(neighbors[i-1], 123 , comm2D, &request_flag, MPI_STATUS_IGNORE);
                if(request_flag){
                    int recv;
                    MPI_Request send_reply_request;
                    MPI_Request receive_reply_request;
                    MPI_Irecv(&recv, 1, MPI_INT, neighbors[i-1], 123,comm2D,&receive_reply_request);
                    MPI_Isend(&num_port, 1, MPI_INT, neighbors[i-1], 321, comm2D,&send_reply_request);

                }    
                
            
            }

        }
        //check if all neighbour stations are full
        for(int i =1;i<5;i++){
            if(recvValues[i]>1){
                Full=false;
                break;
            }
        }
        if(num_port<=1){
            char message[250];
            
            
            
            if(!Full){
                //suggest neighbour stations
                MPI_Barrier(world_comm);  
                sprintf(message,"Station %d is full, please head to available neighbour stations:\n",my_rank);    
            
            
                for(int i=1;i<5;i++){
                    if(recvValues[i]>1){
                        char station[20];
                        sprintf(station,"Station %d\n",neighbors[i-1]);
                        strcat(message,station);
                    }
                } 
            }
            else{
                sprintf(message,"Station %d and  neighbour stations are full, please head to nearest available  stations:\n",my_rank);
                //printf("Station %d and neighbour stations are full :%d,please head to nearest stations: \n",my_rank,num_port);
                MPI_Request send;

                MPI_Pack_size(5, MPI_INT, world_comm, &pack_size);
                MPI_Pack_size(1, MPI_INT, world_comm, &int_size);
                MPI_Pack_size(101, MPI_CHAR, world_comm, &char_size);
                char* buffer = (char*)malloc(pack_size*2+int_size);

                int position = 0;
                int node[5] = {my_rank,nbr_j_lo, nbr_j_hi, nbr_i_lo, nbr_i_hi};
                //pack information and send alert to base station
                MPI_Pack(recvValues, 5, MPI_INT, buffer, pack_size*2+int_size, &position, world_comm);
                MPI_Pack(node,5, MPI_INT, buffer, pack_size*2+int_size, &position, world_comm);
                MPI_Pack(&iterations,1, MPI_INT, buffer, pack_size*2+int_size, &position, world_comm);
                MPI_Send(buffer, position, MPI_PACKED,  worldSize-1, MSG_FULL, world_comm);


                MPI_Barrier(world_comm);
                int receivePosition=0;
                
                int nearby[8]={-1,-1,-1,-1,-1,-1,-1,-1};
                int msg_size;
                //get reply from base station
                MPI_Recv(&msg_size, 1, MPI_INT, worldSize-1,MSG_SUGGEST, world_comm, &status);
                char suggestion[msg_size];
                MPI_Recv(suggestion,msg_size , MPI_CHAR, worldSize-1,MSG_SUGGEST, world_comm, &status);

                strcat(message,suggestion);
            }
            //suggest nearby stations that are nearby  
            printf("%s\n",message);             
        }
        else{
            MPI_Barrier(world_comm);
        }
        iterations++;        
    }



    MPI_Comm_free(&comm2D); 
    return 0;
}
//get rank based on coordinate
int getRank(int x,int y,int ncol){
    return y*ncol+x;
}
//function to get neighbours of a given node 
int getNeighbours(int nrows,int ncols,int rank,int *suggestion){
    int x=rank%ncols;
    int y=rank/ncols;
    int yhi,ylo,xhi,xlo;
    yhi=y+1;
    ylo=y-1;
    xhi=x+1;
    xlo=x-1;
    if (yhi<nrows){
        int neighbour =getRank(x,yhi,ncols);
        suggestion[neighbour]=1;
        
    }
    if (ylo>=0){
        int neighbour =getRank(x,ylo,ncols);
        suggestion[neighbour]=1;
    }
    if(xhi<ncols){
        int neighbour =getRank(xhi,y,ncols);
        suggestion[neighbour]=1;

    }
    if(xlo>=0){
        int neighbour =getRank(xlo,y,ncols);
        suggestion[neighbour]=1;

    }
    return 0;
}

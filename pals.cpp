#include <iostream>
#include <fstream>
#include "embedding.h"
#include "als.h"
#include "mpi.h"
using namespace std;
using namespace als;
int main(int argc, char *argv[]){
    if(argc != 6){
        cerr<<"usage: "<< argv[0] << " K data user_num movie_num iter_num" << endl;
        return 1;
    }
    MPI_Init(&argc, &argv);
    char processor_name[128];

    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    int world_rank;
    int namelen;
    MPI_Comm_rank(MPI_COMM_WORLD,&world_rank);
    MPI_Get_processor_name(processor_name,&namelen);
    fprintf(stderr,"Process %d on %s\n", world_rank, processor_name);

    if(world_rank == 0){
        printf("WorldSize=%d\n", world_size);
    }
    
    int K = atoi(argv[1]);
    const char *data = argv[2];
    int user_num = atoi(argv[3]);
    int movie_num = atoi(argv[4]);
    embedding user_embedding(user_num, K);
    embedding movie_embedding(movie_num, K);
    int iter_num = atoi(argv[5]);
    ifstream ifs(data);

    while(ifs){
        int u, m;
        FLOAT r;
        ifs>>u>>m>>r;
        user_embedding.add_rated(u, m, r);
        movie_embedding.add_rated(m, u, r);
    }
    user_embedding.init();
    movie_embedding.init();
    user_embedding.sync();
    movie_embedding.sync();

    for(int i = 0; i != iter_num; ++i){
        if(user_embedding.get_rank() == 0)
            printf("iter=%d\n", i);
        update(user_embedding, movie_embedding, 0.01);
        user_embedding.sync();
        update(movie_embedding, user_embedding, 0.01);
        movie_embedding.sync();
        test(user_embedding, movie_embedding);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}

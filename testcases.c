/*
 * testcases.c
 *
 *  Created on: Jul 17, 2019
 *      Author: tonglin
 */

#include "rootless_ops.h"

//Toy callback functions for proposal judging and approval.
int is_proposal_approved_cb(const void *buf, void *app_data);
int proposal_action_cb(const void *buf, void *app_data);



int get_world_size(){
    int world_size = -1;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    return world_size;
}

int is_proposal_approved_cb(const void *proposal, void *_app_ctx){

    int my_rank = get_my_rank();

//    printf("%s:%u - rank = %03d, proposal = %p, _app_ctx = %p\n",
//            __func__, __LINE__, my_rank, proposal, _app_ctx);
    ISP* my_isp = (ISP*)_app_ctx;
    //printf("          Callback function: received proposal = [%s], my proposal = [%s]\n", recved_prop, my_prop);

//    printf("%s:%u - rank = %03d, my_isp = %p, my_isp->my_proposal = %p\n",
//            __func__, __LINE__, my_rank, my_isp, my_isp->my_proposal);
    if(!my_isp->my_proposal || strlen(my_isp->my_proposal) == 0)//I don't have a proposal, so I approve anything coming in.
        return 1;

    if(!proposal)
        return 1;

//    printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
    if(strcmp(my_isp->my_proposal, proposal) == 0){//compatible
        printf("%s:%u return 1\n", __func__, __LINE__);
        return 1;
    }

//    printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
    if(((char*)proposal)[0] < ((char*)my_isp->my_proposal)[0]) {
        printf("%s:%u return 0\n", __func__, __LINE__);
        return 0;
    }else
        return 1;
}

int proposal_action_cb(const void *buf, void *app_data){
//    printf("          Callback function: Proposal action started.\n");
    return 0;
}

typedef struct DS_TMP{
    iar_cb_func_t my_func;
}ds_tmp;

int test_callback(){
    ds_tmp* my_ds = calloc(1, sizeof(ds_tmp));
    my_ds->my_func = &is_proposal_approved_cb;
    //iar_cb_func_t my_func = &is_proposal_approved_cb;
    char* buf = "<<This is a buf>>";
    char* ctx = "<<This is a contex.>>";
    printf("Callback caller output: returns %d\n", (*(my_ds->my_func))(buf, ctx));
    //_test_caller((is_proposal_approved_cb)(void));
    return 0;
}

int test_gen_bcast(bcomm* my_bcomm, int buf_size, int root_rank, int cnt){
    int my_rank = get_my_rank();
    //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
    assert(root_rank < get_world_size());
    if(buf_size > MSG_SIZE_MAX) {
        printf("Message size too big. Maximum allowed is %d\n", MSG_SIZE_MAX);
        return -1;
    }
    bcomm_engine_t* eng = progress_engine_new(my_bcomm, NULL, NULL, NULL);

    int recved_cnt = 0;
    unsigned long start = get_time_usec();
    unsigned long time_send = 0;
    bcomm_GEN_msg_t* recv_msg = NULL;

    if(my_rank == root_rank) {//send
        //load data for bcast
        char buf[64] = "";
        for(int i = 0; i < cnt; i++) {
            bcomm_GEN_msg_t* send_msg = msg_new_bc(eng, buf, strlen(buf));
            sprintf(buf, "msg_No.%d", i);
            memcpy(send_msg->data_buf, buf, 64);
            printf("Rank %d bcasting: msg = [%s], msg_usr.data = [%s]\n", my_rank, send_msg->data_buf, send_msg->msg_usr.data);
            bcast_gen(eng, send_msg, BCAST);
            //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
            make_progress_gen(eng, &recv_msg);
            //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
            recv_msg = NULL;
        }
    } else {//recv
        do{
            //sleep(1);
            recv_msg = NULL;
            //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
            make_progress_gen(eng, &recv_msg);
            //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
            user_msg* pickup_out = NULL;
            while(user_pickup_next(eng, &pickup_out)){
                //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
                recved_cnt++;
                printf("%s:%u - rank = %03d, received bcast msg:[%s], received %d already, expecting %d in total.\n", __func__, __LINE__,
                        my_rank, pickup_out->data, recved_cnt, cnt);
                user_msg_recycle(eng, pickup_out);//free it here if(fwd_done)
            }
            //sleep(1);
        } while(recved_cnt < cnt);
    }
    //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
    unsigned long end = get_time_usec();
    //MPI_Barrier(MPI_COMM_WORLD);
    time_send = end - start;

    printf("Rank %d: Received %d times, average costs %lu usec/run\n", my_rank, recved_cnt, (end - start)/cnt);
    engine_cleanup(eng);

    return 0;
}

//no_rank: the rank that votes "NO".
int test_IAllReduce_single_proposal(bcomm* my_bcomm, int starter, int no_rank, int agree) {
    ISP isp;
    isp.my_proposal = NULL;

    int my_rank = get_my_rank();
    int world_size = get_world_size();

    assert((starter < world_size) && (no_rank < world_size));
    bcomm_engine_t* eng = progress_engine_new(my_bcomm, &is_proposal_approved_cb, &isp, &proposal_action_cb);
    char* my_proposal = "111";
    char* proposal_2 = "333";



    int result = -1;
    int ret = -1;

    if (my_rank == starter) {
        printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
        isp.my_proposal = my_proposal;
        int my_proposal_id = my_rank;
         ret = iar_submit_proposal(eng, my_proposal, strlen(my_proposal), my_proposal_id);
         if(ret > -1){//done
             result = get_vote_my_proposal(eng);
             printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
         }else{//sample application logic loop
             while(check_proposal_state(eng, 0) != COMPLETED){
                 make_progress_gen(eng, NULL);
             }
             result = get_vote_my_proposal(eng);
         }
        printf("%s:%u - rank = %03d: proposal completed, decision = %d \n", __func__, __LINE__, my_rank, result);

    } else {//passive ranks
        usleep(200);
        if (my_rank  == no_rank) {// Passive opponent
            //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
            if(agree){
                isp.my_proposal = my_proposal;
            } else {
                isp.my_proposal = proposal_2;
            }
        } else {
          isp.my_proposal = "";
        }
        int tag_recv = -1;
        //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
        do {
            //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
            make_progress_gen(eng, NULL);
            //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
            user_msg* pickup_out = NULL;
            usleep(300);
            while(user_pickup_next(eng, &pickup_out)){
                //printf("%s:%u - rank = %03d: pickup_out = %p\n", __func__, __LINE__, my_rank, pickup_out);
                //PBuf* pbuf = malloc(sizeof(PBuf));
                //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
                //pbuf_deserialize(pickup_out->data_buf, pbuf);

                tag_recv = pickup_out->type;
                //printf("%s:%u - rank = %03d: tag_recv = %d\n", __func__, __LINE__, my_rank, tag_recv);
                switch (tag_recv) {//pickup_out->irecv_stat.MPI_TAG
                    case IAR_PROPOSAL: //2
                        printf("Rank %d: Received proposal: %d:%s.\n", my_rank, pickup_out->pid, pickup_out->data);
                        break;
                    case IAR_VOTE: //3
                        printf("Rank %d: Received vote: %d:%d.\n", my_rank, pickup_out->pid, pickup_out->vote);
                        break;
                    case IAR_DECISION: //4
                        printf("Rank %d: Received decision: %d:%d\n", my_rank, pickup_out->pid, pickup_out->vote);
                        break;

                    default:
                        printf("Warning: Rank %d: Received unexpected msg, tag = %d.\n", my_rank, tag_recv);
                        break;
                }
                user_msg_recycle(eng, pickup_out);
                pickup_out = NULL;

            }
        } while (tag_recv != IAR_DECISION);
    }

    if(my_rank == starter) {
        if (result) {
            printf("\n\n Starter: rank %d: =========== decision: Proposal approved =========== \n\n", starter);
        } else {
            printf("\n\n Starter: rank %d: =========== decision: Proposal declined =========== \n\n", starter);
        }
    }

    engine_cleanup(eng);
    return -1;
}

//Test case utility, used ONLY by a rank that has submitted a proposal.
int testcase_decision_receiver(bcomm_engine_t* eng, int decision_needed){
    make_progress_gen(eng, NULL);
    int my_rank = get_my_rank();

    user_msg* pickup_out = NULL;
    int decision_cnt = 0;
    int tag_recv = -1;
    while (decision_cnt < decision_needed) {
        make_progress_gen(eng, NULL);
        while (user_pickup_next(eng, &pickup_out)) {
            make_progress_gen(eng, NULL);
            //printf("%s:%u - rank = %03d: pickup_out = %p\n", __func__, __LINE__, my_rank, pickup_out);
            //PBuf* pbuf = malloc(sizeof(PBuf));
            //pbuf_deserialize(pickup_out->data_buf, pbuf);

            tag_recv = pickup_out->type;
            //printf("%s:%u - rank = %03d: tag_recv = %d\n", __func__, __LINE__, my_rank, tag_recv);
            switch (tag_recv) { //pickup_out->irecv_stat.MPI_TAG
                case IAR_PROPOSAL: //2
                    printf("Rank %d: Received proposal: %d:%s.\n", my_rank, pickup_out->pid,
                            pickup_out->data);
                    break;
                case IAR_VOTE: //3
                    printf("Rank %d: Received vote: %d:%d.\n", my_rank, pickup_out->pid, pickup_out->vote);
                    break;
                case IAR_DECISION: //4
                    decision_cnt++;
                    printf("Rank %d: Received decision: %d:%d, decision_cnt = %d, needed %d, data = [%s]\n",my_rank, pickup_out->pid,
                            pickup_out->vote, decision_cnt, decision_needed, pickup_out->data);
                    break;

                default:
                    printf("Warning: Rank %d: Received unexpected msg, tag = %d.\n", my_rank,
                            tag_recv);
                    break;
            }
            user_msg_recycle(eng, pickup_out);
            pickup_out = NULL;
        }
    }
    return decision_cnt;
}

int test_IAllReduce_multi_proposal(bcomm* my_bcomm, int active_1, int active_2_mod, int agree) {
    char* my_proposal = "555";
    ISP isp;
    isp.my_proposal = NULL;
    int world_size = get_world_size();
    assert((active_1 < world_size) && (active_2_mod < world_size));
    bcomm_engine_t* eng = progress_engine_new(my_bcomm, &is_proposal_approved_cb, &isp, &proposal_action_cb);

    int result = -1;
    int ret = -1;
    //Rank 0, first active, mod active rank
    int decision_needed = 1 + (world_size - 1) / active_2_mod + 1;
    int my_rank = get_my_rank();

    //printf("Rank %d: send_channel_cnt = %d, send_list_len = %d\n", my_rank, my_bcomm->send_channel_cnt, my_bcomm->send_list_len);

    if (my_rank == active_1) {
        printf(" \n-------- First active rank %d--------\n\n", my_rank);
        printf("\n%s:%u - rank = %03d:  world_size = %d, mod = %d, %d decisions are needed. \n\n",
                __func__, __LINE__, my_rank, world_size, active_2_mod, decision_needed);

        isp.my_proposal = my_proposal;

        int my_proposal_id = my_rank;

         ret = iar_submit_proposal(eng, my_proposal, strlen(my_proposal), my_proposal_id);
         if(ret > -1){//done
             result = get_vote_my_proposal(eng);
         }else{//sample application logic loop
             while(check_proposal_state(eng, 0) != COMPLETED){
                 make_progress_gen(eng, NULL);
             }
             result = get_vote_my_proposal(eng);

             make_progress_gen(eng, NULL);
             testcase_decision_receiver(eng, decision_needed - 1);
         }
//        printf("\n        %s:%u - rank = %03d: active proposal completed, decision = %d \n",
//                __func__, __LINE__, my_rank, result);

    } else if (my_rank % active_2_mod == 0) {
        printf(" \n-------- Mod active rank %d--------\n\n", my_rank);

        int my_proposal_id = my_rank;

        char* proposal_2 = "333";

        if(agree){
            isp.my_proposal = my_proposal;
        } else {
            isp.my_proposal = proposal_2;
        }

        ret = iar_submit_proposal(eng, isp.my_proposal, strlen(isp.my_proposal), my_proposal_id);
        if (ret > -1) { //done
            result = get_vote_my_proposal(eng);
        } else { //sample application logic loop
            while (check_proposal_state(eng, 0) != COMPLETED) {
                make_progress_gen(eng, NULL);
            }
            result = get_vote_my_proposal(eng);

            make_progress_gen(eng, NULL);
            testcase_decision_receiver(eng, decision_needed - 1);
        }//eld else: ret <= -1.
//
//        printf("\n        %s:%u - rank = %03d: active proposal completed, decision = %d \n",
//                __func__, __LINE__, my_rank, result);

    } else {//all passive ranks
        if(agree){
            isp.my_proposal = my_proposal;
        } else {
            isp.my_proposal = "111";
        }

        make_progress_gen(eng, NULL);
        testcase_decision_receiver(eng, decision_needed);
    }

    engine_cleanup(eng);

    //MPI_Barrier(MPI_COMM_WORLD);

    if(my_rank == active_1 || my_rank == active_2_mod) {
        if (result) {
            printf("\n\n Active rank %d: =========== decision: Proposal approved =========== \n\n", my_rank);
        } else {
            printf("\n\n Active rank %d: =========== decision: Proposal declined =========== \n\n", my_rank);
        }
    }

    return -1;
}

int get_prev_rank(int my_rank, int world_size) {
    return (my_rank + (world_size - 1)) % world_size;
}

int get_next_rank(int my_rank, int world_size) {
    return (my_rank + 1) % world_size;
}

int get_random_rank(int my_rank, int world_size) {
    int next_rank;

    do {
        next_rank = rand() % world_size;
    } while(next_rank == my_rank);

    return next_rank;
}

int hacky_sack_progress_engine(int cnt, bcomm* my_bcomm){

    bcomm_engine_t* eng = progress_engine_new(my_bcomm, NULL, NULL, NULL);

    unsigned long time_start;
    unsigned long time_end;
    unsigned long phase_1;
    int bcast_sent_cnt;
    int recv_msg_cnt = 0;
    int my_rank = get_my_rank();

    char buf_send[32] = "";
    /* Get pointer to sending buffer */
    total_pickup = 0;
    time_start = get_time_usec();
    int world_size = get_world_size();
    /* Compose message to send (in bcomm's send buffer) */
    int next_rank = get_random_rank(my_rank, world_size);;
            //get_random_rank(my_rank, world_size);
            //get_next_rank(my_rank, world_size);
            //get_prev_rank(my_rank, world_size);
    sprintf(buf_send, "%d", next_rank);

    printf("%s:%u - rank = %03d initial bcast rank = %d \n", __func__, __LINE__, my_rank, next_rank);
    /* Broadcast message */
    bcomm_GEN_msg_t* prev_rank_bc_msg = msg_new_bc(eng, buf_send, sizeof(buf_send));
    bcast_gen(eng, prev_rank_bc_msg, BCAST);
    bcast_sent_cnt = 0;

    user_msg* pickup_out = NULL;
    int t = 0;
    while (bcast_sent_cnt < cnt) {
        //sleep(1);
        //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);

        make_progress_gen(eng, NULL);

        while(user_pickup_next(eng, &pickup_out)){
            //sleep(1);
            //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
            recv_msg_cnt++;
            total_pickup++;
            printf("%s:%u - rank = %03d, pickup_out msg = [%s]\n", __func__, __LINE__, my_rank, pickup_out->data);
            int recv_rank = atoi((pickup_out->data));
            user_msg_recycle(eng, pickup_out);
            //make_progress_gen(eng, NULL);
            if(recv_rank == my_rank){
                //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
                char buf[32] = "";
                next_rank = get_next_rank(my_rank, world_size);
                sprintf(buf, "%d", next_rank);
                //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
                bcomm_GEN_msg_t* next_msg_send = msg_new_bc(eng, buf, sizeof(buf));
                bcast_gen(eng, next_msg_send, BCAST);
                bcast_sent_cnt++;
                //printf("%s:%u - rank = %03d\n", __func__, __LINE__, my_rank);
                //make_progress_gen(eng, NULL);
            }
            //make_progress_gen(eng, NULL);
        }
    }

    time_end = get_time_usec();
    phase_1 = time_end - time_start;

    engine_cleanup(eng);
    MPI_Barrier(MPI_COMM_WORLD);
    printf("Rank %02d reports: Hacky sack done passive bcast %d times. total pickup %d times. expected pickup = %d\n", my_rank,
            bcast_sent_cnt,  total_pickup,  (bcast_sent_cnt + 1) * (world_size - 1));

    return 0;
}

int main(int argc, char** argv) {
    bcomm* my_bcomm;
    time_t t;

    srand((unsigned) time(&t) + getpid());

    MPI_Init(NULL, NULL);

    int my_rank = get_my_rank();

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    printf("PID %d on %s ready for attach\n", getpid(), hostname);
    fflush(stdout);

    //sleep(60);

    if(NULL == (my_bcomm = bcomm_init(MPI_COMM_WORLD, MSG_SIZE_MAX)))
        return 0;

    printf("%s:%u - rank = %03d, pid = %d\n", __func__, __LINE__, my_rank, getpid());


    // ======================== Basic bcast test ========================
//    int init_rank = atoi(argv[1]);
//    int op_cnt = atoi(argv[2]);
//    test_gen_bcast(my_bcomm, MSG_SIZE_MAX, init_rank, op_cnt);

    // ======================== All-to-all bcast test: Hackysacking ========================
//    int op_cnt = atoi(argv[1]);
//    hacky_sack_progress_engine(op_cnt, my_bcomm);


    int init_rank = atoi(argv[1]);
    int no_rank = atoi(argv[2]); //Rank that says 'NO'
    int agree = atoi(argv[3]);

    //sleep(60);
    //test_IAllReduce_single_proposal(my_bcomm, init_rank, no_rank, agree);

    test_IAllReduce_multi_proposal(my_bcomm, init_rank, no_rank, agree);

    MPI_Finalize();

    return 0;
}

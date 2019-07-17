/*
 * rootless_ops.h
 *
 *  Created on: Jul 17, 2019
 *      Author: tonglin
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdbool.h>
#include <pthread.h>
#include <assert.h>
#include <sys/types.h>

#ifndef ROOTLESS_OPS_H_
#define ROOTLESS_OPS_H_

// ============= DEBUG GLOBALS =============
int total_pickup;
// ============= DEBUG GLOBALS =============

#define MSG_SIZE_MAX 32768
enum COM_TAGS {//Used as MPI_TAG. Class 1
    BCAST, //class 1
    JOB_DONE,
    IAR_PROPOSAL, //class 2, under BCAST
    IAR_VOTE, // //class 2, under P2P
    IAR_DECISION, //class 2, under BCAST
    BC_TEARDOWN, //class 2, under SYS, for teardowon use
    IAR_TEARDOWN, //class 2, under SYS, for teardowon use
    P2P, //class 1
    SYS, //class 1
    ANY_TAG // == MPI_ANY_TAG
};

typedef enum REQ_STATUS {
    COMPLETED,
    IN_PROGRESS,
    FAILED,
    INVALID // default.
}Req_stat;

unsigned long get_time_usec();
void get_time_str(char *str_out);


typedef int ID;
typedef int Vote;// used for & operation. 1 for yes, 0 for no.

typedef struct IAR_Single_Prop_CTX{
    void* my_proposal;
}ISP;

typedef int (*iar_cb_func_t)(const void *msg_buf, void *app_ctx);//const void *msg_buf, void *app_ctx


// Message package protocol and functions
typedef struct Proposal_buf{
    ID pid;
    Vote vote;//0 = vote NO, 1 = vote yes, -1 = proposal, -2 = decision.
    unsigned int data_len;
    char* data;
}PBuf;
int pbuf_serialize(ID pid_in, Vote vote, unsigned int data_len_in, char* data_in, char* buf_out, unsigned int* buf_len_out);
void pbuf_free(PBuf* pbuf);
int pbuf_deserialize(char* buf_in, PBuf* pbuf_out);

typedef struct BCastCommunicator bcomm;
bcomm *bcomm_init(MPI_Comm comm, size_t msg_size_max);

typedef struct bcomm_progress_engine bcomm_engine_t;

bcomm_engine_t* progress_engine_new(bcomm* my_bcomm, void* approv_cb_func, void* app_ctx, void* app_proposal_action);

typedef struct bcomm_generic_msg bcomm_GEN_msg_t;
typedef struct Proposal_state proposal_state;
struct Proposal_state{
    ID pid; // proposal ID, default = -1;
    int recv_proposal_from;             /* The rank from where I received a proposal, also report votes to this rank. */
//    int proposal_sent_cnt;
    Vote vote; //accumulated vote, default = 1;
    int votes_needed; //num of votes needed to report to parent, equals to the number of sends of a proposal
    int votes_recved;
    Req_stat state; //the state of this proposal: COMPLETED, IN_PROGRESS or FAILED.
    bcomm_GEN_msg_t* proposal_msg;//the last place holds a proposal, should be freed when decision is made and executed.
    bcomm_GEN_msg_t* decision_msg;
}; //clear when vote result is reported.

struct bcomm_generic_msg{
    char buf[MSG_SIZE_MAX + sizeof(int)];// Make this always be the first field, so a pointer to it is the same as a pointer to the message struct
    char* data_buf; //= buf + sizeof(int), so data_buf size is MSG_SIZE_MAX
    int id_debug;

    enum COM_TAGS post_irecv_type;//Only support BCAST, IAR_PROPOSAL for now, set this when the msg is in app pickup queue.
    enum COM_TAGS send_type; //BCAST, IAR_PROPOSAL(share the msg  with decision)
    MPI_Request irecv_req; //filled when repost irecv
    MPI_Status irecv_stat;
    MPI_Request* bc_isend_reqs; //array of reqs for bcasts' isends
    MPI_Status* bc_isend_stats; //array of status for bcasts' isends

    proposal_state* prop_state;

    bcomm_GEN_msg_t *prev, *next; //for generic queue ops

    int send_cnt; //how many isend to monitor
    int ref_cnt;
    int pickup_done; //user mark pickup is done.
    int fwd_done; // system mark forward is done.

    int bc_init;
        //if this is a bc msg or s forward msg, used for MPI_Testall(num_isends, ...)
        //By default it's set to 0, and set to 1 for a new bc msg.
};

bcomm_GEN_msg_t* msg_new_generic(bcomm_engine_t* eng);
bcomm_GEN_msg_t* msg_new_bc(bcomm_engine_t* eng, void* buf_in, int send_size);
int msg_free(bcomm_GEN_msg_t* msg_in);

// Entry point: turing gears for progress engine.
int make_progress_gen(bcomm_engine_t* eng, bcomm_GEN_msg_t** msg_out);
int bcast_gen(bcomm_engine_t* eng, bcomm_GEN_msg_t* msg_in, enum COM_TAGS tag);
int engine_cleanup(bcomm_engine_t* eng);

// Submit a proposal, add it to waiting list, then return.
int iar_submit_proposal(bcomm_engine_t* eng, char* proposal, unsigned long prop_size, ID my_proposal_id);
int check_proposal_state(bcomm_engine_t* eng, int pid);
int get_vote_my_proposal(bcomm_engine_t* eng);
int user_pickup_next(bcomm_engine_t* eng, bcomm_GEN_msg_t** msg_out);
int user_msg_done(bcomm_engine_t* eng, bcomm_GEN_msg_t* msg_in);




#endif /* ROOTLESS_OPS_H_ */

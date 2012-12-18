#include "nissa.h"
#include <mpi.h>
#include <stdlib.h>
#include <unistd.h>

int T=32,L=16;
int debug=0;

const int START_PATH_FLAG=1,DAG_LINK_FLAG=2,NONLOC_LINK_FLAG=4,nposs_path_flags=3;

struct movement_link_id
{
  int mov;
  int link_id;
  int ord;
  void set(int ext_mov,int gx,int mu){
    mov=ext_mov;
    link_id=gx*4+mu;
    int lx,rx;
    get_loclx_and_rank_of_glblx(&lx,&rx,gx);
    ord=((rank+nissa_nranks-rx)%nissa_nranks*loc_vol+lx)*4+mu; //sort according to recv rank
  }
};
  
int compare_movement_link_id(const void *a,const void *b)
{return ((movement_link_id*)a)->ord-((movement_link_id*)b)->ord;}

//NB: the list of movement is larger than the list of independent links!
struct paths_calculation_structure
{
  //initialization
  paths_calculation_structure(int npaths,int ntot_mov) : npaths(npaths),ntot_mov(ntot_mov),cur_path(0),cur_mov(0) {
    master_printf("initializing a new path calculation structure with %d movements and %d paths\n",ntot_mov,npaths);
    link_for_movements=nissa_malloc("link_for_movements",ntot_mov,int);

    //mark that we have not finished last path
    finished_last_path_flag=0;
    
    //we do not yet know hown many ranks we need to communicate with
    nranks_to_send=nranks_to_recv=0;
    nnonloc_links=nind_nonloc_links=0;
    
    //and we don't know which they are
    ranks_to_send_list=ranks_to_recv_list=NULL;
    
    //reset the list of links to send, and the amount of links to send and receive from each rank
    links_to_send_list=NULL;
    nlinks_to_recv_list=nlinks_to_send_list=NULL;
    
    //reset the list of pairs of link and movements
    movements_nonloc_links_id_list=NULL;
  };
  ~paths_calculation_structure() {
    nissa_free(link_for_movements);
    nissa_free(ind_nonloc_links_list);
    
    if(finished_last_path_flag)
      {
	//free the list of links to receive
	free(links_to_send_list);
	
	//the amount of links to send and receive from each rank
	free(nlinks_to_send_list);
	free(nlinks_to_recv_list);
	
	//and the list of ranks to communicate with
	free(ranks_to_send_list);
	free(ranks_to_recv_list);
      }
  }
  
  //parameters defining the set of paths
  int npaths,ntot_mov;
  int *link_for_movements;
  movement_link_id *movements_nonloc_links_id_list;
  
  //current global movement (link), path and position
  int cur_path,cur_mov;
  int pos;
  
  //relevant for MPI part
  int finished_last_path_flag;
  int nnonloc_links,nind_nonloc_links;
  int nranks_to_send,*ranks_to_send_list,*nlinks_to_send_list,ntot_links_to_send;
  int nranks_to_recv,*ranks_to_recv_list,*nlinks_to_recv_list,ntot_links_to_recv;
  int *links_to_send_list,*ind_nonloc_links_list;
  
  //commands
  void start_new_path_from_loclx(int lx) {
    pos=glblx_of_loclx[lx];
    link_for_movements[cur_mov]=START_PATH_FLAG;
    if(debug) master_printf("Starting a new path from local point %d, global point: %d\n",lx,pos);}
  void switch_to_next_step() {
    cur_mov++;
    if(cur_mov>ntot_mov) crash("exceded the number of allocatec movements, %d",ntot_mov);
    link_for_movements[cur_mov]=0;}
  void move_forward(int mu);
  void move_backward(int mu);
  void stop_current_path() {
    cur_path++;
    if(cur_path>npaths) crash("exceded the number of allocated paths, %d",npaths);
  }
  void finished_last_path();
  void gather_nonloc_start(MPI_Request *request,int &irequest,su3 *nonloc_links);
  void gather_nonloc_finish(MPI_Request *request,int &irequest,su3 *send_buff);
  void gather_nonloc_lx(su3 *paths,quad_su3 *conf);
  void gather_nonloc_eo(su3 *paths,quad_su3 **conf);
  void compute_lx(su3 *paths,quad_su3 *conf);
  void compute_eo(su3 *paths,quad_su3 **conf);
  
private:
  //avoid bare initialization without specification of nel
  paths_calculation_structure();
  //setu the communication buffers
  void setup_sender_receivers();
};

//add a forward move
void paths_calculation_structure::move_forward(int mu)
{
  //check not to have passed the max number of steps
  if(cur_mov==ntot_mov) crash("exceded the number of allocated movements, %d",ntot_mov);
  //find global pos
  int nx=glblx_neighup(pos,mu);
  //search rank hosting site and loclx
  int lx,rx;
  get_loclx_and_rank_of_glblx(&lx,&rx,pos);
  //if local link, mark it, otherwise add to the list of non-locals
  if(rx==rank) link_for_movements[cur_mov]+=(lx*4+mu)<<nposs_path_flags;
  else
    {
      movements_nonloc_links_id_list=(movement_link_id*)realloc(movements_nonloc_links_id_list,sizeof(movement_link_id)*(nnonloc_links+1));
      movements_nonloc_links_id_list[nnonloc_links++].set(cur_mov,pos,mu);
    }
  //set new pos
  if(debug) printf("Rank %d moved forward from %d in the direction %d to %d, mov: %d, tag: %d\n",rank,pos,mu,nx,cur_mov,0);//link_for_movements[cur_mov]%4);
  pos=nx;
  //switch to next step
  switch_to_next_step();
}

//add a backward move
void paths_calculation_structure::move_backward(int mu)
{
  //mark backward move
  link_for_movements[cur_mov]+=DAG_LINK_FLAG;
  //check not to have passed the max number of steps
  if(cur_mov==ntot_mov) crash("exceded the number of allocated movements, %d",ntot_mov);
  //find global pos
  int nx=glblx_neighdw(pos,mu);
  //search rank hosting site and loclx
  int lx,rx;
  get_loclx_and_rank_of_glblx(&lx,&rx,nx);
  //if local link, mark it, otherwise add to the list of non-locals
  if(rx==rank) link_for_movements[cur_mov]+=(lx*4+mu)<<nposs_path_flags;
  else
    {
      movements_nonloc_links_id_list=(movement_link_id*)realloc(movements_nonloc_links_id_list,sizeof(movement_link_id)*(nnonloc_links+1));
      movements_nonloc_links_id_list[nnonloc_links++].set(cur_mov,nx,mu);
    }
  //set new pos
  if(debug) printf("Rank %d moved backward from %d in the direction %d to %d, mov: %d, tag: %d\n",rank,pos,mu,nx,cur_mov,0);//link_for_movements[cur_mov]%4);
  pos=nx;
  //switch to next step
  switch_to_next_step();
}

//finish settings the paths, setup the send and receiver
void paths_calculation_structure::finished_last_path()
{
  if(cur_path!=npaths) crash("finished the path list at path %d while it was initialized for %d",cur_path,npaths);
  if(cur_mov!=ntot_mov) crash("finished the path list at mov %d while it was initialized for %d",cur_mov,ntot_mov);
  
  if(debug)
    for(int ilink=0;ilink<nnonloc_links;ilink++)
      printf("rank %d, mov %d: mov %d, link %d\n",rank,ilink,movements_nonloc_links_id_list[ilink].mov,movements_nonloc_links_id_list[ilink].link_id);
  
  //sort the list
  qsort(movements_nonloc_links_id_list,nnonloc_links,sizeof(movement_link_id),compare_movement_link_id);
  
  //asign the non local links for each movement one by one, counting the independent ones
  nind_nonloc_links=0;
  for(int ilink=0;ilink<nnonloc_links;ilink++)
    {
      link_for_movements[movements_nonloc_links_id_list[ilink].mov]+=(nind_nonloc_links<<nposs_path_flags)+NONLOC_LINK_FLAG;
      if(ilink==(nnonloc_links-1)||movements_nonloc_links_id_list[ilink].link_id!=movements_nonloc_links_id_list[ilink+1].link_id) nind_nonloc_links++;
    }
  if(debug||rank==nissa_nranks/2) printf("Rank %d, after resorting, %d indep links of %d total nonlocal\n",rank,nind_nonloc_links,nnonloc_links);
  
  //allocate the list of nonlocal indep links to ask, so we can free the full list
  ind_nonloc_links_list=nissa_malloc("ind_nlonloc_links_list",nind_nonloc_links,int);
  nind_nonloc_links=0;
  for(int ilink=0;ilink<nnonloc_links;ilink++)
    {
      ind_nonloc_links_list[nind_nonloc_links]=movements_nonloc_links_id_list[ilink].link_id;
      if(ilink==(nnonloc_links-1)||movements_nonloc_links_id_list[ilink].link_id!=movements_nonloc_links_id_list[ilink+1].link_id)
	{
	  nind_nonloc_links++;
	  
	  if(debug)//||rank==nissa_nranks/2)
	    if(ilink!=(nnonloc_links-1))
	      {
		int t=movements_nonloc_links_id_list[ilink].link_id;
		int gx=t/4,mu=t%4;
		int lx,rx;
		get_loclx_and_rank_of_glblx(&lx,&rx,gx);
		printf("Rank %d, found new indep link to ask # %d, %d, rank %d loc %d, lx %d, mu %d\n",rank,nind_nonloc_links,t,rx,lx*4+mu,lx,mu);
	      }
	}
    }
  
  //free here
  free(movements_nonloc_links_id_list);
  
  finished_last_path_flag=1;
  
  setup_sender_receivers();
}

//setup the sender and receiver buffers, finding which ranks are involved
void paths_calculation_structure::setup_sender_receivers()
{
  ntot_links_to_send=ntot_links_to_recv=0;
  
  if(debug/*||rank==nissa_nranks/2+1*/) printf("---Setupping sender and receivers---\n");
  
  //loop over the ranks displacement
  for(int delta_rank=1;delta_rank<nissa_nranks;delta_rank++)
    {
      int rank_to_send=(rank+delta_rank)%nissa_nranks;
      int rank_to_recv=(rank+nissa_nranks-delta_rank)%nissa_nranks;
      
      //counts the number of links to receive
      int nlinks_to_recv=0;
      for(int ilink=0;ilink<nind_nonloc_links;ilink++)
	{
	  int t=ind_nonloc_links_list[ilink];
	  int gx=t>>2;
	  int rx=rank_hosting_glblx(gx);
	  if(rx==rank_to_recv) nlinks_to_recv++;
	  if(debug) printf("Rank %d, link %d, t %d,  gx %d, found on rank: %d, while searching for those to ask to: %d\n",rank,ilink,t,gx,rx,rank_to_recv);
	}
      
      //if(debug||rank==nissa_nranks/2) printf("Rank %d, links to receive from %d: %d\n",rank,rank_to_recv,nlinks_to_recv);
      
      //send this piece of info and receive the number of links to send
      int nlinks_to_send=0;
      MPI_Sendrecv((void*)&(nlinks_to_recv),1,MPI_INT,rank_to_recv,rank_to_recv*nissa_nranks+rank,
		   (void*)&(nlinks_to_send),1,MPI_INT,rank_to_send,rank*nissa_nranks+rank_to_send,
		   MPI_COMM_WORLD,MPI_STATUS_IGNORE);
      
      if(debug/*||rank==nissa_nranks/2+1*/)
	{
	  //print the number of links to ask to and asked by other ranks
	  printf("Rank %d, delta %d, recv from rank %d: %d links, send to rank %d: %d links\n",rank,delta_rank,rank_to_recv,nlinks_to_recv,rank_to_send,nlinks_to_send);
	}
      
      //allocate a buffer where to store the list of links to ask
      int *links_to_ask=nissa_malloc("links_to_ask",nlinks_to_recv,int);
      nlinks_to_recv=0;
      for(int ilink=0;ilink<nind_nonloc_links;ilink++)
	{
	  int t=ind_nonloc_links_list[ilink];
	  int gx=t>>2;
	  int mu=t%4;
	  
	  //get lx and rank hosting the site
	  int lx,rx;
	  get_loclx_and_rank_of_glblx(&lx,&rx,gx);
	  
	  //copy in the list if appropriate rank
	  if(rx==rank_to_recv)
	    {
	      links_to_ask[nlinks_to_recv]=lx*4+mu;
	      nlinks_to_recv++;
	    }
	}
      if(debug)
	{
	  printf("Rank %d, total link to recv: %d\n",rank,nlinks_to_recv);
	  for(int ilink=0;ilink<nlinks_to_recv;ilink++)
	    {
	      int lx=links_to_ask[ilink]/4,mu=links_to_ask[ilink]%4;
	  
	      printf("Rank %d will recv from %d as link %d/%d link: %d, loc link: %d dir %d\n",rank,rank_to_recv,ilink,nlinks_to_recv,links_to_ask[ilink],lx,mu);
	    }
	}
      
      //allocate the list of link to send
      int *links_to_send=nissa_malloc("links_to_send",nlinks_to_send,int);
      
      //send this piece of info
      MPI_Sendrecv((void*)links_to_ask, nlinks_to_recv,MPI_INT,rank_to_recv,rank_to_recv*nissa_nranks+rank,
                   (void*)links_to_send,nlinks_to_send,MPI_INT,rank_to_send,rank*nissa_nranks+rank_to_send,
                   MPI_COMM_WORLD,MPI_STATUS_IGNORE);
      nissa_free(links_to_ask);
      
      if(debug/*||rank==nissa_nranks/2+1*/)
	for(int ilink=0;ilink<nlinks_to_send;ilink++)
	  {
	    int lx=links_to_send[ilink]/4,mu=links_to_send[ilink]%4;
	    
	    printf("Rank %d will send to %d as link %d/%d link: %d, loc link %d dir %d\n",rank,rank_to_send,ilink,nlinks_to_send,links_to_send[ilink],lx,mu);
	  }
      
      //store the sending rank id and list of links
      if(nlinks_to_send!=0)
	{
	  //prepare the space
	  ranks_to_send_list=(int*)realloc(ranks_to_send_list,sizeof(int)*(nranks_to_send+1));
	  links_to_send_list=(int*)realloc(links_to_send_list,sizeof(int)*(ntot_links_to_send+nlinks_to_send));
	  nlinks_to_send_list=(int*)realloc(nlinks_to_send_list,sizeof(int)*(nranks_to_send+1));
	  
	  //store
	  ranks_to_send_list[nranks_to_send]=rank_to_send;
	  memcpy(links_to_send_list+ntot_links_to_send,links_to_send,nlinks_to_send*sizeof(int));
	  nlinks_to_send_list[nranks_to_send]=nlinks_to_send;
	  
	  //increase
	  nranks_to_send++;
	  ntot_links_to_send+=nlinks_to_send;
	}
      nissa_free(links_to_send);

      //store the receiving rank id and list of links
      if(nlinks_to_recv!=0)
	{
	  //prepare the space
	  ranks_to_recv_list=(int*)realloc(ranks_to_recv_list,sizeof(int)*(nranks_to_recv+1));
	  nlinks_to_recv_list=(int*)realloc(nlinks_to_recv_list,sizeof(int)*(nranks_to_recv+1));
	  
	  //store
	  ranks_to_recv_list[nranks_to_recv]=rank_to_recv;
	  nlinks_to_recv_list[nranks_to_recv]=nlinks_to_recv;
	  
	  //increase
	  nranks_to_recv++;
	  ntot_links_to_recv+=nlinks_to_recv;
	}
    }
}

//open incoming communications
void paths_calculation_structure::gather_nonloc_start(MPI_Request *request,int &irequest,su3 *nonloc_links)
{
  if(debug/*||rank==nissa_nranks/2*/) printf("---Starting communication---\n");
  
  //open receiving communications
  su3 *recv_ptr=nonloc_links;
  for(int irecv=0;irecv<nranks_to_recv;irecv++)
    {
      MPI_Irecv((void*)recv_ptr,nlinks_to_recv_list[irecv],MPI_SU3,ranks_to_recv_list[irecv],rank*nissa_nranks+ranks_to_recv_list[irecv],cart_comm,request+irequest++);
      if(debug/*||rank==nissa_nranks/2*/) printf("Rank %d receiving from %d, recv ind %d, offset: %d\n",rank,ranks_to_recv_list[irecv],irecv,(int)(recv_ptr-nonloc_links));
      
      if(irecv+1!=nranks_to_recv) recv_ptr+=nlinks_to_recv_list[irecv];
    }
}

//communicate
void paths_calculation_structure::gather_nonloc_finish(MPI_Request *request,int &irequest,su3 *send_buff)
{
  //open sending communications
  su3 *send_ptr=send_buff;
  for(int isend=0;isend<nranks_to_send;isend++)
    {
      MPI_Isend((void*)send_ptr,nlinks_to_send_list[isend],MPI_SU3,ranks_to_send_list[isend],ranks_to_send_list[isend]*nissa_nranks+rank,cart_comm,request+irequest++);
      if(debug/*||rank==nissa_nranks/2+1*/) printf("Rank %d sending to %d, sending ind %d, offset %d\n",rank,ranks_to_send_list[isend],isend,(int)(send_ptr-send_buff));
      
      if(isend+1!=nranks_to_send) send_ptr+=nlinks_to_send_list[isend];
    }
  
  //wait communications to finish
  int ntot_req=nranks_to_send+nranks_to_recv;
  if(debug) printf("Rank %d, nranks_to_send+nranks_to_recv: %d\n",rank,ntot_req);
  MPI_Waitall(ntot_req,request,MPI_STATUS_IGNORE);
}

//collect all the independent links entering into the calculations into "links"
void paths_calculation_structure::gather_nonloc_lx(su3 *nonloc_links,quad_su3 *conf)
{
  //list of request
  MPI_Request request[nranks_to_send+nranks_to_recv];
  int irequest=0;
  
  //open incoming communications
  gather_nonloc_start(request,irequest,nonloc_links);
  
  //allocate the sending buffer, fill them
  if(debug/*||rank==nissa_nranks/2+1*/) printf("---List of buffering---\n");
  su3 *send_buff=nissa_malloc("buff",ntot_links_to_send,su3);
  for(int ilink=0;ilink<ntot_links_to_send;ilink++)
    {
      su3_copy(send_buff[ilink],((su3*)conf)[links_to_send_list[ilink]]);
      if(debug/*||rank==nissa_nranks/2+1*/) printf("link %d: %d\n",ilink,links_to_send_list[ilink]);
    }
  
  //finish the communications
  gather_nonloc_finish(request,irequest,send_buff);
  
  nissa_free(send_buff);
}

//collect all the independent links entering into the calculations into "links" using the infamous eo geometry
void paths_calculation_structure::gather_nonloc_eo(su3 *nonloc_links,quad_su3 **conf)
{
  //list of request
  MPI_Request request[nranks_to_send+nranks_to_recv];
  int irequest=0;
  
  //open incoming communications
  gather_nonloc_start(request,irequest,nonloc_links);
  
  //allocate the sending buffer, fill them
  if(debug/*||rank==nissa_nranks/2+1*/) printf("---List of buffering---\n");
  su3 *send_buff=nissa_malloc("buff",ntot_links_to_send,su3);
  for(int ilink=0;ilink<ntot_links_to_send;ilink++)
    {
      int t=links_to_send_list[ilink];
      int lx=t/4,mu=t%4;
      int p=loclx_parity[lx],eo=loceo_of_loclx[lx];
      su3_copy(send_buff[ilink],conf[p][eo][mu]);
      if(debug/*||rank==nissa_nranks/2+1*/) printf("link %d: %d\n",ilink,links_to_send_list[ilink]);
    }
  
  //finish the communications
  gather_nonloc_finish(request,irequest,send_buff);
  
  nissa_free(send_buff);
}

void paths_calculation_structure::compute_lx(su3 *paths,quad_su3 *conf)
{
  //buffer for non local links gathering
  if(debug) printf("Rank %d, nind_nonloc_links %d\n",rank,nind_nonloc_links);
  su3 *nonloc_links=nissa_malloc("links",nind_nonloc_links,su3);
  
  //gather non local links
  gather_nonloc_lx(nonloc_links,conf);
  
  //compute the paths one by one
  int ipath=0;
  for(int imov=0;imov<ntot_mov;imov++)
    {
      int ilink=link_for_movements[imov]>>nposs_path_flags;
      
      //take the flags
      int tag=link_for_movements[imov]%(1<<nposs_path_flags);
      int start=tag&START_PATH_FLAG;
      int herm=tag&DAG_LINK_FLAG;
      int nonloc=tag&NONLOC_LINK_FLAG;
      
      //check if starting a new path
      if(start==1)
	{
	  //if not the first mov, start the new path
	  if(imov!=0) ipath++;
	  su3_put_to_id(paths[ipath]);
	}

      //look whether we need to use local or non-local buffer
      su3 *links;
      if(nonloc) links=nonloc_links;
      else       links=(su3*)conf;
      
      if(debug) printf("Rank %d, mov %d, ind link: %d, tag: %d, herm: %d, start: %d, nonloc: %d\n",rank,imov,ilink,tag,herm,start,nonloc);
      
      //multiply for the link or the link daggered
      if(herm) safe_su3_prod_su3_dag(paths[ipath],paths[ipath],links[ilink]);
      else     safe_su3_prod_su3    (paths[ipath],paths[ipath],links[ilink]);
    }
  
  nissa_free(nonloc_links);
}

void paths_calculation_structure::compute_eo(su3 *paths,quad_su3 **conf)
{
  //buffer for non local links gathering
  if(debug) printf("Rank %d, nind_nonloc_links %d\n",rank,nind_nonloc_links);
  su3 *nonloc_links=nissa_malloc("links",nind_nonloc_links,su3);
  
  //gather non local links
  gather_nonloc_eo(nonloc_links,conf);
  
  //compute the paths one by one
  int ipath=0;
  for(int imov=0;imov<ntot_mov;imov++)
    {
      int ilink=link_for_movements[imov]>>nposs_path_flags;
      
      //take the flags
      int tag=link_for_movements[imov]%(1<<nposs_path_flags);
      int start=tag&START_PATH_FLAG;
      int herm=tag&DAG_LINK_FLAG;
      int nonloc=tag&NONLOC_LINK_FLAG;
      
      //check if starting a new path
      if(start==1)
	{
	  //if not the first mov, start the new path
	  if(imov!=0) ipath++;
	  su3_put_to_id(paths[ipath]);
	}

      //look whether we need to use local or non-local buffer
      su3 *link;
      if(nonloc) link=nonloc_links+ilink;
      else
	{
	  int lx=ilink/4,mu=ilink%4;
	  int p=loclx_parity[lx],eo=loceo_of_loclx[lx];
	  
	  link=conf[p][eo]+mu;
	}
      
      if(debug) printf("Rank %d, mov %d, ind link: %d, tag: %d, herm: %d, start: %d, nonloc: %d\n",rank,imov,ilink,tag,herm,start,nonloc);
      
      //multiply for the link or the link daggered
      if(herm) safe_su3_prod_su3_dag(paths[ipath],paths[ipath],*link);
      else     safe_su3_prod_su3    (paths[ipath],paths[ipath],*link);
    }
  
  nissa_free(nonloc_links);
}

int main(int narg,char **arg)
{
  init_nissa();

  init_grid(T,L);

  quad_su3 *conf=nissa_malloc("lx",loc_vol+bord_vol,quad_su3);
  quad_su3 *conf_eo[2]={nissa_malloc("e",loc_volh+bord_volh,quad_su3),nissa_malloc("o",loc_volh+bord_volh,quad_su3)};
  read_ildg_gauge_conf(conf,"../../../../Prace/Confs/conf.0100");
  split_lx_conf_into_eo_parts(conf_eo,conf);

  /////////////////////////
  
  paths_calculation_structure *a;

  a=new paths_calculation_structure(loc_vol*6,loc_vol*6*4);
  
  nissa_loc_vol_loop(ivol)
  {
    for(int mu=0;mu<4;mu++)
      for(int nu=mu+1;nu<4;nu++)
	{
	  a->start_new_path_from_loclx(ivol);
	  a->move_forward(mu);
	  a->move_forward(nu);
	  a->move_backward(mu);
	  a->move_backward(nu);
	  a->stop_current_path();
	}
  }
  
  a->finished_last_path();
  
  su3 *paths=nissa_malloc("paths",6*loc_vol,su3);
  a->compute_eo(paths,conf_eo);
 
  double re;
  for(int ivol=0;ivol<loc_vol*6;ivol++) re+=su3_real_trace(paths[ivol])/3;
  master_printf("plaq: %16.16lg\n",glb_reduce_double(re)/glb_vol/6);
  master_printf("True plaquette: %16.16lg\n",global_plaquette_lx_conf(conf));
  nissa_free(conf);
  nissa_free(conf_eo[0]);
  nissa_free(conf_eo[1]);
  nissa_free(paths);
    
  delete a;
  /*
    link_id_vector a;
    printf("%d\n",a.append(10,2));
    printf("%d\n",a.append(10,1));
    printf("%d\n",a.append(10,2));
    printf("%d\n",a.append(10,1));
  */
  
  /////////////////////////

  close_nissa();
  
  return 0;
}

#include <nissa.hpp>

#define EXTERN_CONTR
 #include "contr.hpp"

#include <set>

#include "prop.hpp"

namespace nissa
{
  //class to open or append a path, depending if it was already used
  class open_or_append_t
  {
    //list of already opened file
    std::set<std::string> opened;
    
  public:
    //open for write or append, depending
    FILE *open(const std::string &path,int force_append=false)
    {
      //detect the mode
      std::string mode;
      if((not force_append) and opened.find(path)==opened.end())
	{
	  mode="w";
	  opened.insert(path);
	}
      else
	mode="a";
      
      //open
      FILE *fout=open_file(path.c_str(),mode.c_str());
      
      return fout;
    }
  };
  
  //allocate mesonic contractions
  void allocate_mes2pts_contr()
  {
    mes2pts_contr_size=glb_size[0]*mes_gamma_list.size()*mes2pts_contr_map.size();
    mes2pts_contr=nissa_malloc("mes2pts_contr",mes2pts_contr_size,complex);
  }
  
  //free mesonic contractions
  void free_mes2pts_contr()
  {nissa_free(mes2pts_contr);}
  
  //compute a single scalar product
  THREADABLE_FUNCTION_3ARG(compute_prop_scalprod, double*,res, std::string,pr_dag, std::string, pr)
  {
    GET_THREAD_ID();
    
    master_printf("Computing the scalar product between %s and %s\n",pr_dag.c_str(),pr.c_str());
    
    complex *loc=nissa_malloc("loc",loc_vol,complex);
    vector_reset(loc);
    
    for(int idc_so=0;idc_so<nso_spi*nso_col;idc_so++)
      {
	spincolor *q_dag=Q[pr_dag][idc_so];
	spincolor *q=Q[pr][idc_so];
	
	NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
	  {
	    complex t;
	    spincolor_scalar_prod(t,q_dag[ivol],q[ivol]);
	    complex_summassign(loc[ivol],t);
	  }
      }
    THREAD_BARRIER();
    
    complex_vector_glb_collapse(res,loc,loc_vol);
    
    nissa_free(loc);
  }
  THREADABLE_FUNCTION_END
  
  //compute all the meson contractions
  THREADABLE_FUNCTION_1ARG(compute_mes2pts_contr, int,normalize)
  //void compute_mes2pts_contr(int normalize)
  {
    GET_THREAD_ID();
    
    master_printf("Computing meson 2pts_contractions\n");
    
    // Tr [ GSO G5 S1^+ G5 GSI S2 ]      GSI is on the sink
    // (GSO)_{ij(i)} (G5)_{j(i)} (S1*)^{ab}_{kj(i)} (G5)_k (GSI)_{kl(k)} (S2)^{ab}_{l(k)i}
    //
    // A(i)=(GSO)_{ij(i)} (G5)_{j(i)}
    // B(k)=(G5)_k (GSI)_{kl(k)}
    //
    // A(i) (S1*)^{ab}_{kj(i)} B(k) (S2)^{ab}_{l(k)i}
    
    if(IS_MASTER_THREAD) mes2pts_contr_time-=take_time();
    
    for(size_t icombo=0;icombo<mes2pts_contr_map.size();icombo++)
      {
	master_printf("icombo %d/%d\n",icombo,mes2pts_contr_map.size());
	qprop_t &Q1=Q[mes2pts_contr_map[icombo].a];
	qprop_t &Q2=Q[mes2pts_contr_map[icombo].b];
	double norm=12/sqrt(Q1.ori_source_norm2*Q2.ori_source_norm2); //12 in case of a point source
	for(size_t ihadr_contr=0;ihadr_contr<mes_gamma_list.size();ihadr_contr++)
	  {
	    int ig_so=mes_gamma_list[ihadr_contr].so;
	    int ig_si=mes_gamma_list[ihadr_contr].si;
	    if(nso_spi==1 and ig_so!=5) crash("implemented only g5 contraction on the source for non-diluted source");
	    
	    for(int i=0;i<nso_spi;i++)
	      {
		int j=(base_gamma+ig_so)->pos[i];
		
		complex A;
		unsafe_complex_prod(A,(base_gamma+ig_so)->entr[i],(base_gamma+5)->entr[j]);
		
		for(int b=0;b<nso_col;b++)
		  {
		    spincolor *q1=Q1[so_sp_col_ind(j,b)];
		    spincolor *q2=Q2[so_sp_col_ind(i,b)];
		    
		    for(int k=0;k<NDIRAC;k++)
		      {
			int l=(base_gamma+ig_si)->pos[k];
			
			//compute AB*norm
			complex B;
			unsafe_complex_prod(B,(base_gamma+5)->entr[k],(base_gamma+ig_si)->entr[k]);
			complex AB;
			unsafe_complex_prod(AB,A,B);
			if(normalize) complex_prodassign_double(AB,norm);
			
			NISSA_PARALLEL_LOOP(loc_t,0,loc_size[0])
			  for(int ispat=0;ispat<loc_spat_vol;ispat++)
			    {
			      int ivol=loc_t*loc_spat_vol+ispat;
			      int t=rel_time_of_loclx(ivol);
			      
			      complex c={0,0};
			      for(int a=0;a<NCOL;a++)
				complex_summ_the_conj1_prod(c,q1[ivol][k][a],q2[ivol][l][a]);
			      complex_summ_the_prod(mes2pts_contr[ind_mes2pts_contr(icombo,ihadr_contr,t)],c,AB);
			    }
		      }
		  }
	      }
	  }
      }
    THREAD_BARRIER();
    
    //stats
    if(IS_MASTER_THREAD)
      {
	nmes2pts_contr_made+=mes2pts_contr_map.size()*mes_gamma_list.size();
	mes2pts_contr_time+=take_time();
      }
  }
  THREADABLE_FUNCTION_END
  
  //print all mesonic 2pts contractions
  void print_mes2pts_contr(int n,int force_append,int skip_inner_header,const std::string &alternative_header_template)
  {
    //set the header template
    std::string header_template;
    if(alternative_header_template=="") header_template="\n # Contraction of %s ^ \\dag and %s\n\n";
    else header_template=alternative_header_template;
    
    contr_print_time-=take_time();
    
    //reduce and normalise
    glb_nodes_reduce_complex_vect(mes2pts_contr,mes2pts_contr_size);
    for(int i=0;i<mes2pts_contr_size;i++) complex_prodassign_double(mes2pts_contr[i],1.0);
    
    //list to open or append
    open_or_append_t list;
    
    for(size_t icombo=0;icombo<mes2pts_contr_map.size();icombo++)
      {
	auto& combo=mes2pts_contr_map[icombo];
	
	//path to use
	FILE *fout=list.open(combine("%s/%s_%s",outfolder,mes2pts_prefix.c_str(),combo.name.c_str()),force_append);
	
	master_fprintf(fout,header_template.c_str(),combo.a.c_str(),combo.b.c_str());
	
	print_contractions_to_file(fout,mes_gamma_list,mes2pts_contr+ind_mes2pts_contr(icombo,0,0),0,"",1.0/n,skip_inner_header);
	master_fprintf(fout,"\n");
	
	//close the file
	close_file(fout);
      }
    
    contr_print_time+=take_time();
  }
  
  //////////////////////////////////////// baryonic contractions //////////////////////////////////////////////////////////
  
  /*
    We follow eq.6.21 of Gattringer, pag 131 and compute the two Wick
    contractions separately, as in
    
    eps_{a,b,c} eps_{a',b',c'} (Cg5)_{al',be'} (Cg5)_{al,be}
    (P+-)_{ga,ga'} S_{be',be}{b',b} (
    S_{al',al}{a',a} S_{ga',ga}{c',c} -
    S_{al',ga}{a',c} S_{ga',al}{c',a})
    
     a',al'---------a,al           a',al'--@   @--a,al
       |             |		    |       \ /    |
     b',be'---------b,be           b',be'---------b,be
       |             |		    |       / \    |
     c',ga'---------c,ga	   c',ga'--@   @--c,ga
     
     insertions are labelled as abc on the source (left) side
   
  */
  
  //set Cg5=ig2g4g5
  void set_Cg5()
  {
    dirac_matr g2g4,C;
    dirac_prod(&g2g4,base_gamma+2,base_gamma+4);
    dirac_prod_idouble(&C,&g2g4,1);
    dirac_prod(&Cg5,&C,base_gamma+5);
  }
  
  //allocate baryionic contr
  void allocate_bar2pts_contr()
  {
    bar2pts_contr_size=ind_bar2pts_contr(bar2pts_contr_map.size()-1,2-1,glb_size[0]-1)+1;
    bar2pts_contr=nissa_malloc("bar2pts_contr",bar2pts_contr_size,complex);
  }
  
  //free them
  void free_bar2pts_contr()
  {nissa_free(bar2pts_contr);}
  
  //compute all contractions
  THREADABLE_FUNCTION_0ARG(compute_bar2pts_contr)
  {
    GET_THREAD_ID();
    master_printf("Computing barion 2pts contractions\n");
    
    //allocate loc storage
    complex *loc_contr=new complex[bar2pts_contr_size];
    memset(loc_contr,0,sizeof(complex)*bar2pts_contr_size);
    
    const int eps[3][2]={{1,2},{2,0},{0,1}},sign[2]={1,-1};
    
    void (*list_fun[2])(complex,const complex,const complex)={complex_summ_the_prod,complex_subt_the_prod};
    UNPAUSE_TIMING(bar2pts_contr_time);
    for(size_t icombo=0;icombo<bar2pts_contr_map.size();icombo++)
      {
	qprop_t &Q1=Q[bar2pts_contr_map[icombo].a];
	qprop_t &Q2=Q[bar2pts_contr_map[icombo].b];
	qprop_t &Q3=Q[bar2pts_contr_map[icombo].c];
	double norm=pow(12,1.5)/sqrt(Q1.ori_source_norm2*Q2.ori_source_norm2*Q3.ori_source_norm2); //12 is even in case of a point source
	
	for(int al=0;al<NDIRAC;al++)
	  for(int ga=0;ga<NDIRAC;ga++)
	    for(int b=0;b<NCOL;b++)
	      for(int iperm=0;iperm<2;iperm++)
		{
		  int c=eps[b][iperm],a=eps[b][!iperm];
		  int be=Cg5.pos[al];
		  
		  NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
		    {
		      int t=rel_time_of_loclx(ivol);
		      
		      int ga1_l[2][NDIRAC]={{0,1,2,3},{2,3,0,1}}; //ga1 index for 1 or gamma0 matrix
    			  int sign_idg0[2]={(t<(glb_size[0]/2))?1:-1,-1}; //gamma0 is -1 always
    			  for(int al1=0;al1<NDIRAC;al1++)
    			    for(int b1=0;b1<NCOL;b1++)
    			      {
    				complex diquark_dir={0,0},diquark_exc={0,0};
				
    				//build the diquark
    				for(int iperm1=0;iperm1<2;iperm1++)
    				  {
    				    int c1=eps[b1][iperm1],a1=eps[b1][!iperm1];
				    
    				    for(int idg0=0;idg0<2;idg0++)
    				      {
    					int isign=((sign[iperm]*sign[iperm1]*sign_idg0[idg0])==1);
    					int ga1=ga1_l[idg0][ga];
					
    					list_fun[isign](diquark_dir,Q1[so_sp_col_ind(al,a)][ivol][al1][a1],Q3[so_sp_col_ind(ga,c)][ivol][ga1][c1]); //direct
    					list_fun[isign](diquark_exc,Q1[so_sp_col_ind(ga,c)][ivol][al1][a1],Q3[so_sp_col_ind(al,a)][ivol][ga1][c1]); //exchange
    				      }
    				  }
				
    				//close it
    				complex w;
    				unsafe_complex_prod(w,Cg5.entr[al1],Cg5.entr[al]);
    				int be1=Cg5.pos[al1];
    				complex_prodassign_double(diquark_dir,w[RE]*norm);
    				complex_prodassign_double(diquark_exc,w[RE]*norm);
    				complex_summ_the_prod(loc_contr[ind_bar2pts_contr(icombo,0,t)],Q2[so_sp_col_ind(be,b)][ivol][be1][b1],diquark_dir);
    				complex_summ_the_prod(loc_contr[ind_bar2pts_contr(icombo,1,t)],Q2[so_sp_col_ind(be,b)][ivol][be1][b1],diquark_exc);
    			      }
		    }
		}
      }
    STOP_TIMING(bar2pts_contr_time);
    
    //reduce
    complex *master_reduced_contr=glb_threads_reduce_complex_vect(loc_contr,bar2pts_contr_size);
    NISSA_PARALLEL_LOOP(i,0,bar2pts_contr_size)
      {
	//remove border phase
	int t=i%glb_size[0];
	double arg=3*temporal_bc*M_PI*t/glb_size[0];
	complex phase={cos(arg),sin(arg)};
	complex_summ_the_prod(bar2pts_contr[i],master_reduced_contr[i],phase);
      }
    THREAD_BARRIER();
    delete[] loc_contr;
    
    //stats
    if(IS_MASTER_THREAD) nbar2pts_contr_made+=bar2pts_contr_map.size();
  }
  THREADABLE_FUNCTION_END
  
  //print all contractions
  void print_bar2pts_contr()
  {
    //list to open or append
    open_or_append_t list;
    
    //reduce
    glb_nodes_reduce_complex_vect(bar2pts_contr,bar2pts_contr_size);
    
    for(size_t icombo=0;icombo<bar2pts_contr_map.size();icombo++)
      for(int dir_exc=0;dir_exc<2;dir_exc++)
	{
	  //open output
	  FILE *fout=list.open(combine("%s/bar_contr_%s_%s",outfolder,(dir_exc==0)?"dir":"exc",bar2pts_contr_map[icombo].name.c_str()));
	  for(int t=0;t<glb_size[0];t++)
	    {
	      //normalize for nsources and 1+g0
	      complex c;
	      complex_prod_double(c,bar2pts_contr[ind_bar2pts_contr(icombo,dir_exc,t)],1.0/(2*nhits));
	      master_fprintf(fout,"%+16.16lg %+16.16lg\n",c[RE],c[IM]);
	    }
	  
	  close_file(fout);
	}
  }
  
  //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  
  //compute the matrix element of the conserved current between two propagators. If asking to revert, g5 is inserted between the two propagators
  THREADABLE_FUNCTION_7ARG(conserved_vector_current_mel, quad_su3*,conf, spin1field*,si, dirac_matr*,ext_g, int,r, const char*,name_bw, const char*,name_fw, bool,revert)
  {
    GET_THREAD_ID();
    
    vector_reset(si);
    
    //compute the gammas
    dirac_matr GAMMA[5],temp_gamma;
    if(twisted_run>0)	dirac_prod_idouble(&temp_gamma,base_gamma+5,-tau3[r]);
    else                temp_gamma=base_gamma[0];
    
    //Add g5 on the gamma, only if asked to revert
    if(revert)
      {
	dirac_prod(GAMMA+4,base_gamma+5,&temp_gamma);
	for(int mu=0;mu<NDIM;mu++) dirac_prod(GAMMA+mu,base_gamma+5,base_gamma+igamma_of_mu[mu]);
      }
    else
      {
	GAMMA[4]=temp_gamma;
	for(int mu=0;mu<NDIM;mu++) GAMMA[mu]=base_gamma[igamma_of_mu[mu]];
      }
    
    dirac_matr g;
    if(revert) dirac_prod(&g,ext_g,base_gamma+5);
    else       g=*ext_g;
    
    for(int iso_spi_bw=0;iso_spi_bw<nso_spi;iso_spi_bw++)
      for(int iso_col=0;iso_col<nso_col;iso_col++)
	{
	  int iso_spi_fw=g.pos[iso_spi_bw];
	  int idc_fw=so_sp_col_ind(iso_spi_fw,iso_col);
	  int idc_bw=so_sp_col_ind(iso_spi_bw,iso_col);
	  
	  //get componentes
	  spincolor *Qfw=Q[name_fw][idc_fw];
	  spincolor *Qbw=Q[name_bw][idc_bw];
	  
	  communicate_lx_spincolor_borders(Qfw);
	  communicate_lx_spincolor_borders(Qbw);
	  communicate_lx_quad_su3_borders(conf);
	  
	  NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
	    for(int mu=0;mu<NDIM;mu++)
	      {
		int ivol_fw=loclx_neighup[ivol][mu];
		spincolor f,Gf;
		complex c;
		
		//piece psi_ivol U_ivol psi_fw
		unsafe_su3_prod_spincolor(f,conf[ivol][mu],Qfw[ivol_fw]);
		unsafe_dirac_prod_spincolor(Gf,GAMMA+4,f);
		dirac_subt_the_prod_spincolor(Gf,GAMMA+mu,f);
		spincolor_scalar_prod(c,Qbw[ivol],Gf);
		complex_prodassign(c,g.entr[iso_spi_bw]);
		complex_summ_the_prod_idouble(si[ivol][mu],c,-0.5);
		
		//piece psi_fw U_ivol^dag psi_ivol
		unsafe_su3_dag_prod_spincolor(f,conf[ivol][mu],Qfw[ivol]);
		unsafe_dirac_prod_spincolor(Gf,GAMMA+4,f);
		dirac_summ_the_prod_spincolor(Gf,GAMMA+mu,f);
		spincolor_scalar_prod(c,Qbw[ivol_fw],Gf);
		complex_prodassign(c,g.entr[iso_spi_bw]);
		complex_summ_the_prod_idouble(si[ivol][mu],c,+0.5);
	      }
	}
  }
  THREADABLE_FUNCTION_END
  
  //compute the matrix element of the current between two propagators
  THREADABLE_FUNCTION_6ARG(vector_current_mel, spin1field*,si, dirac_matr*,ext_g, int,r, const char*,id_Qbw, const char*,id_Qfw, bool,revert)
  {
    GET_THREAD_ID();
    
    vector_reset(si);
    
    dirac_matr GAMMA[NDIM];
    for(int mu=0;mu<NDIM;mu++)
      if(revert) dirac_prod(GAMMA+mu,base_gamma+5,base_gamma+igamma_of_mu[mu]);
      else       GAMMA[mu]=base_gamma[igamma_of_mu[mu]];
    
    dirac_matr g;
    if(revert) dirac_prod(&g,ext_g,base_gamma+5);
    else       g=*ext_g;
    
    for(int iso_spi_fw=0;iso_spi_fw<nso_spi;iso_spi_fw++)
      for(int iso_col=0;iso_col<nso_col;iso_col++)
	{
	  int iso_spi_bw=g.pos[iso_spi_fw];
	  
	  //get componentes
	  spincolor *Qbw=Q[id_Qbw][so_sp_col_ind(iso_spi_bw,iso_col)];
	  spincolor *Qfw=Q[id_Qfw][so_sp_col_ind(iso_spi_fw,iso_col)];
	  
	  NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
	    for(int mu=0;mu<NDIM;mu++)
	      {
		spincolor temp;
		complex c;
		
		unsafe_dirac_prod_spincolor(temp,GAMMA+mu,Qfw[ivol]);
		spincolor_scalar_prod(c,Qbw[ivol],temp);
		complex_summ_the_prod(si[ivol][mu],c,g.entr[iso_spi_fw]);
	      }
	}
  }
  THREADABLE_FUNCTION_END
  
  //compute local or conserved vector current matrix element
  void local_or_conserved_vector_current_mel(spin1field *si,dirac_matr &g,const std::string &prop_name_bw,const std::string &prop_name_fw,bool revert)
  {
    if(loc_hadr_curr) vector_current_mel(si,&g,Q[prop_name_fw].r,prop_name_bw.c_str(),prop_name_fw.c_str(),revert);
    else
      {
	double plain_bc[NDIM];
	plain_bc[0]=temporal_bc;
	for(int mu=1;mu<NDIM;mu++) plain_bc[mu]=0.0;
	quad_su3 *conf=get_updated_conf(Q[prop_name_fw].charge,plain_bc,glb_conf);
	int r=Q[prop_name_fw].r;
	conserved_vector_current_mel(conf,si,&g,r,prop_name_bw.c_str(),prop_name_fw.c_str(),revert);
      }
  }
  
  //                                                          handcuffs
  
  THREADABLE_FUNCTION_0ARG(compute_handcuffs_contr)
  {
    GET_THREAD_ID();
    master_printf("Computing handcuffs contractions\n");
    
    //allocate all sides
    std::map<std::string,spin1field*> sides;
    
    //loop over sides
    for(size_t iside=0;iside<handcuffs_side_map.size();iside++)
      {
	//allocate
	handcuffs_side_map_t &h=handcuffs_side_map[iside];
	std::string side_name=h.name;
	spin1field *si=sides[side_name]=nissa_malloc(side_name.c_str(),loc_vol,spin1field);
	vector_reset(sides[side_name]);
	
	//check r are the same (that is, opposite!)
	if(twisted_run and Q[h.fw].r==Q[h.bw].r and (not Q[h.bw].is_source))
	  crash("conserved current needs opposite r (before reverting), but quarks %s and %s have the same",h.fw.c_str(),h.bw.c_str());
	
	//compute dirac combo
	dirac_matr g;
	int ig=::abs(handcuffs_side_map[iside].igamma);
	int revert=(handcuffs_side_map[iside].igamma>=0); //reverting only if positive ig asked
	if(ig!=5 and !diluted_spi_source) crash("ig %d not available if not diluting in spin",ig);
	dirac_prod(&g,base_gamma+5,base_gamma+ig);
	
	//compute the matrix element
	local_or_conserved_vector_current_mel(si,g,h.bw,h.fw,revert);
	
	//if(h.store) store_spin1field(combine("%s/handcuff_side_%s",outfolder,h.name.c_str()),si);
      }
    
    //add the photon
    for(size_t ihand=0;ihand<handcuffs_map.size();ihand++)
      {
	handcuffs_map_t &h=handcuffs_map[ihand];
	std::string right_with_photon=h.right+"_photon";
	master_printf("Inserting photon to compute %s\n",right_with_photon.c_str());
	
	spin1field *rp;
	if(sides.find(right_with_photon)!=sides.end()) rp=sides[right_with_photon];
	else
	  {
	    sides[right_with_photon]=rp=nissa_malloc(right_with_photon.c_str(),loc_vol,spin1field);
	    multiply_by_tlSym_gauge_propagator(rp,sides[h.right],photon);
	  }
      }
    
    //compute the hands
    for(size_t ihand=0;ihand<handcuffs_map.size();ihand++)
      if(sides.find(handcuffs_map[ihand].left)==sides.end() or
	 sides.find(handcuffs_map[ihand].right)==sides.end())
	crash("Unable to find sides: %s or %s",handcuffs_map[ihand].left.c_str(),handcuffs_map[ihand].right.c_str());
      else
	{
	  crash("check race");
	  NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
	    for(int mu=0;mu<NDIM;mu++)
	      complex_summ_the_prod(handcuffs_contr[ind_handcuffs_contr(ihand)],
				    sides[handcuffs_map[ihand].left][ivol][mu],
				    sides[handcuffs_map[ihand].right+"_photon"][ivol][mu]);
	}
    
    //free
    for(std::map<std::string,spin1field*>::iterator it=sides.begin();it!=sides.end();it++)
      nissa_free(it->second);
  }
  THREADABLE_FUNCTION_END
  
  //allocate handcuff contractions
  void allocate_handcuffs_contr()
  {
    handcuffs_contr_size=ind_handcuffs_contr(handcuffs_map.size()-1)+1;
    handcuffs_contr=nissa_malloc("handcuffs_contr",handcuffs_contr_size,complex);
  }
  
  //free handcuff contractions
  void free_handcuffs_contr()
  {nissa_free(handcuffs_contr);}
  
  //print handcuffs contractions
  void print_handcuffs_contr()
  {
    //list to open or append
    open_or_append_t list;
    
    contr_print_time-=take_time();
    
    //reduce and normalise
    glb_nodes_reduce_complex_vect(handcuffs_contr,handcuffs_contr_size);
    
    //Open if size different from zero
    FILE *fout=NULL;
    if(handcuffs_map.size()) fout=list.open(combine("%s/handcuffs",outfolder));
    
    for(size_t icombo=0;icombo<handcuffs_map.size();icombo++)
      {
	//normalize for nsources
	complex c;
	complex_prod_double(c,handcuffs_contr[ind_handcuffs_contr(icombo)],1.0/nhits);
	master_fprintf(fout,"%s %+16.16lg %+16.16lg\n",handcuffs_map[icombo].name.c_str(),c[RE],c[IM]);
      }
    //close the file
    if(fout) close_file(fout);
    
    contr_print_time+=take_time();
  }
}

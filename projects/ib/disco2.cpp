#include "nissa.hpp"

using namespace nissa;

const int ALL_TIMES=-1;
momentum_t theta={-1,0,0,0};

//photon
gauge_info photon_pars;
int free_theory;
int random_conf;

namespace free_th
{
  spinspin *qu;
  spin1prop *ph;
  
  //allocate the free quark and photon props
  void allocate_props()
  {
    qu=nissa_malloc("qu",loc_vol+bord_vol,spinspin);
    ph=nissa_malloc("ph",loc_vol+bord_vol,spin1prop);
  }
  
  //compute quark and photon props
  void precompute_propagators()
  {
    tm_quark_info qu_pars;
    qu_pars.bc[0]=-1;
    qu_pars.kappa=0.125;
    qu_pars.mass=0.0;
    qu_pars.r=0;
    
    compute_x_space_twisted_propagator_by_fft(qu,qu_pars,MAX_TWIST_BASE);
    
    /////////////////////////////////////////////////////////////////
    
    compute_x_space_tlSym_gauge_propagator_by_fft(ph,photon_pars);
  }
  
  //free the free quark and photon props
  void free_props()
  {
    nissa_free(qu);
    nissa_free(ph);
  }
}

namespace mel
{
  //buffer for reduction
  complex *buffer;
  
  //compute the local matrix element between source and prop of gamma[igamma]
  THREADABLE_FUNCTION_4ARG(local_mel, double*,out, spincolor*,source, int,igamma, spincolor*,prop)
  {
    GET_THREAD_ID();
    
    NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
      {
	spincolor t;
	unsafe_dirac_prod_spincolor(t,base_gamma+igamma,prop[ivol]);
	spincolor_scalar_prod(buffer[ivol],source[ivol],t);
      }
    THREAD_BARRIER();
    
    complex_vector_glb_collapse(out,buffer,loc_vol);
  }
  THREADABLE_FUNCTION_END
  
  //compute the matrix element of the conserved current between two propagators. If asking to revert, g5 is inserted between the two propagators
  THREADABLE_FUNCTION_5ARG(conserved_vector_current_mel, spin1field*,out, spincolor*,source, quad_su3*,conf, int,r, spincolor*,prop)
  {
    GET_THREAD_ID();
    
    vector_reset(out);
    
    //compute the gammas
    dirac_matr GAMMA[5];
    dirac_prod_idouble(GAMMA+4,base_gamma+5,-tau3[r]);
    for(int mu=0;mu<NDIM;mu++) GAMMA[mu]=base_gamma[igamma_of_mu[mu]];
    
    communicate_lx_spincolor_borders(source);
    communicate_lx_spincolor_borders(prop);
    communicate_lx_quad_su3_borders(conf);
    
    NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
      for(int mu=0;mu<NDIM;mu++)
	{
	  int ivol_fw=loclx_neighup[ivol][mu];
	  spincolor f,Gf;
	  complex c;
	  
	  //piece psi_ivol U_ivol psi_fw
	  unsafe_su3_prod_spincolor(f,conf[ivol][mu],prop[ivol_fw]);
	  unsafe_dirac_prod_spincolor(Gf,GAMMA+4,f);
	  dirac_subt_the_prod_spincolor(Gf,GAMMA+mu,f);
	  spincolor_scalar_prod(c,source[ivol],Gf);
	  complex_summ_the_prod_idouble(out[ivol][mu],c,-0.5);
	  
	  //piece psi_fw U_ivol^dag psi_ivol
	  unsafe_su3_dag_prod_spincolor(f,conf[ivol][mu],prop[ivol]);
	  unsafe_dirac_prod_spincolor(Gf,GAMMA+4,f);
	  dirac_summ_the_prod_spincolor(Gf,GAMMA+mu,f);
	  spincolor_scalar_prod(c,source[ivol_fw],Gf);
	  complex_summ_the_prod_idouble(out[ivol][mu],c,+0.5);
	}
  }
  THREADABLE_FUNCTION_END
  
  //compute the summ of the product of the two vectors
  THREADABLE_FUNCTION_3ARG(global_product, double*,out, spin1field*,a, spin1field*,b)
  {
    GET_THREAD_ID();
    
    NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
      {
	complex_put_to_zero(buffer[ivol]);
	for(int mu=0;mu<NDIM;mu++)
	  complex_summ_the_prod(buffer[ivol],a[ivol][mu],b[ivol][mu]);
      }
    THREAD_BARRIER();
    
    complex_vector_glb_collapse(out,buffer,loc_vol);
  }
  THREADABLE_FUNCTION_END
}

// THREADABLE_FUNCTION_8ARG(fill_eigenpart, spincolor**,eigvec_conv, spincolor**,eigvec, int,neig, quad_su3*,conf, double,kappa, double,am, int,r, double,eig_precision)
// {
//   GET_THREAD_ID();
  
//   //compute all eigenvectors
//   complex lambda[neig];
//   //wrap the application of Q2 into an object that can be passed to the eigenfinder
//   spincolor *temp_imp_mat=nissa_malloc("temp",loc_vol+bord_vol,spincolor);
//   const auto imp_mat=[conf,kappa,mu=am*tau3[r],temp_imp_mat](complex *out,complex *in)
//     {
//       // apply_tmQ2((spincolor*)out,conf,kappa,temp_imp_mat,mu,(spincolor*)in);
      
//       // apply_tmQ((spincolor*)out,conf,kappa,mu,(spincolor*)in);
      
//       apply_tmQ(temp_imp_mat,conf,kappa,mu,(spincolor*)in);
//       GET_THREAD_ID();
//       NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
//         unsafe_dirac_prod_spincolor(((spincolor*)out)[ivol],base_gamma+5,temp_imp_mat[ivol]);
//       set_borders_invalid(out);
//     };
  
//   //parameters of the eigensolver
//   const bool min_max=0;
//   const int mat_size=loc_vol*sizeof(spincolor)/sizeof(complex);
//   const int mat_size_to_allocate=(loc_vol+bord_vol)*sizeof(spincolor)/sizeof(complex);
//   const int niter_max=10000000;
  
//   //wrap the generation of the test vector into an object that can be passed to the eigenfinder
//   const auto filler=[](complex *a)
//     {
//       generate_undiluted_source((spincolor*)a,RND_GAUSS,ALL_TIMES);
//     };
  
//   // master_printf("EigenFinding\n");
//   print_all_eigenstuff(imp_mat,mat_size);
//   crash("test");
  
//   //launch the eigenfinder
//   double eig_time=-take_time();
//   complex Q2_eig_val[neig];
//   eigenvalues_of_hermatr_find((complex**)eigvec,Q2_eig_val,neig,min_max,mat_size,mat_size_to_allocate,imp_mat,eig_precision,niter_max,filler);
//   eig_time+=take_time();
//   master_printf("Eigenvalues time: %lg\n",eig_time);
  
//   //prints eigenvalues of QQ for check
//   master_printf("Eigenvalues of QQ:\n");
//   for(int ieig=0;ieig<neig;ieig++)
//     master_printf("%d (%.16lg,%.16lg)\n",ieig,Q2_eig_val[ieig][RE],Q2_eig_val[ieig][IM]);
//   master_printf("\n");
  
//   //find the eigenvalues of Q
//   master_printf("Eigenvalues of D:\n");
//   for(int ieig=0;ieig<neig;ieig++)
//     {
//       master_printf(" (norm of vec: %lg)\n",sqrt(double_vector_glb_norm2(eigvec[ieig],loc_vol)));
      
//       //apply the matrix
//       apply_tmQ(temp_imp_mat,conf,kappa,am*tau3[r],eigvec[ieig]);
//             GET_THREAD_ID();
//       NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
//       	safe_dirac_prod_spincolor(((spincolor*)temp_imp_mat)[ivol],base_gamma+5,temp_imp_mat[ivol]);
//       set_borders_invalid(temp_imp_mat);
      
//       //compute eigenvalue
//       complex_vector_glb_scalar_prod(lambda[ieig],(complex*)(eigvec[ieig]),(complex*)temp_imp_mat,mat_size);
      
//       complex c={0,0};
//       for(int ivol=0;ivol<loc_vol;ivol++)
// 	{
// 	  spincolor temp;
// 	  safe_dirac_prod_spincolor(temp,base_gamma+5,eigvec[ieig][ivol]);
// 	  complex a;
// 	  spincolor_scalar_prod(a,eigvec[ieig][ivol],temp);
// 	  complex_summassign(c,a);
	  
// 	  if(ivol<2)
// 	  for(int id=0;id<NDIRAC;id++)
// 	    for(int ic=0;ic<NCOL;ic++)
// 	      {
// 		complex &c=eigvec[ieig][ivol][id][ic];
// 		master_printf("ivol %d id %d ic %d, %.16lg %.16lg\n",ivol,id,ic,c[RE],c[IM]);
// 	      }
// 	}
//       master_printf(" g5story: (%.16lg,%.16lg)\n",c[RE],c[IM]);
      
//       //compute residue
//       complex_vector_subtassign_complex_vector_prod_complex((complex*)temp_imp_mat,(complex*)(eigvec[ieig]),lambda[ieig],mat_size);
//       master_printf("%d (%.16lg,%.16lg), residue: %lg\n",ieig,lambda[ieig][RE],lambda[ieig][IM],sqrt(double_vector_glb_norm2(temp_imp_mat,loc_vol)));
//     }
//   master_printf("\n");
  
//   //close vectors
//   for(int ieig=0;ieig<neig;ieig++)
//     {
//       apply_tmQ(temp_imp_mat,conf,kappa,-am*tau3[r],eigvec[ieig]);
//       inv_tmQ2_RL_cg(eigvec_conv[ieig],NULL,conf, kappa,0,am*tau3[r],1000000,1e-11,temp_imp_mat);
//       NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
//       	safe_dirac_prod_spincolor(eigvec_conv[ieig][ivol],base_gamma+5,eigvec_conv[ieig][ivol]);
      
//       // complex one_over_lambda;
//       // complex_reciprocal(one_over_lambda,lambda[ieig]);
//       // NISSA_PARALLEL_LOOP(ivol,0,loc_vol)
//       // 	{
//       // 	  unsafe_dirac_prod_spincolor(temp_imp_mat[ivol],base_gamma+5,eigvec[ieig][ivol]);
//       // 	  spincolor_prodassign_complex(temp_imp_mat[ivol],one_over_lambda);
// 	// }
//       set_borders_invalid(eigvec_conv[ieig]);
//     }
  
//   //rotate the opposite way, to compensate for the missing rotation
//   for(int ieig=0;ieig<neig;ieig++)
//     {
//       safe_dirac_prod_spincolor(eigvec[ieig],(tau3[!r]==-1)?&Pminus:&Pplus,eigvec[ieig]);
//       safe_dirac_prod_spincolor(eigvec_conv[ieig],(tau3[!r]==-1)?&Pminus:&Pplus,eigvec_conv[ieig]);
//     }
  
//   nissa_free(temp_imp_mat);
// }
// THREADABLE_FUNCTION_END

//compute all propagators
//THREADABLE_FUNCTION_10ARG(compute_orthogonal_part, spincolor**,phi, spincolor**,eta, int,nhits, spincolor**,eigvec, int,neig, quad_su3*,conf, double,kappa, double,am, int,r, double,solver_precision)
THREADABLE_FUNCTION_8ARG(compute_propagators, spincolor**,phi, spincolor**,eta, int,nhits, quad_su3*,conf, double,kappa, double,am, int,r, double,solver_precision)
{
  //source and solution for the solver
  spincolor *source=nissa_malloc("source",loc_vol+bord_vol,spincolor);
  spincolor *solution=nissa_malloc("solution",loc_vol+bord_vol,spincolor);
  
  for(int ihit=0;ihit<nhits;ihit++)
    {
      master_printf("Prop Hit %d\n",ihit);
      
      //prepare the source
      vector_copy(source,eta[ihit]);
      
      //subtract longitudinal part
      // for(int ieig=0;ieig<neig;ieig++)
      // 	{
      // 	  int s=loc_vol*sizeof(spincolor)/sizeof(complex);
      // 	  complex n;
      // 	  complex_vector_glb_scalar_prod(n,(complex*)(eigvec[ieig]),(complex*)(source),s);
      // 	  complex_vector_subtassign_complex_vector_prod_complex((complex*)(source),(complex*)(eigvec[ieig]),n,s);
      // 	  set_borders_invalid(source);
      // 	}
      
      //rotate to twisted basis
      safe_dirac_prod_spincolor(source,(tau3[r]==-1)?&Pminus:&Pplus,source);
      
      if(free_theory)
	{
	  tm_quark_info qu(kappa,am,r,theta);
	  tm_basis_t basis=WILSON_BASE;
	  multiply_from_left_by_x_space_twisted_propagator_by_fft(solution,source,qu,basis,false);
	}
      else
	inv_tmD_cg_eoprec(solution,NULL,conf,kappa,am*tau3[r],1000000,solver_precision,source);
      
      //rotate back
      safe_dirac_prod_spincolor(phi[ihit],(tau3[r]==-1)?&Pminus:&Pplus,solution);
    }
  
  nissa_free(solution);
  nissa_free(source);
}
THREADABLE_FUNCTION_END

void in_main(int narg,char **arg)
{
  //to be read
  photon_pars.alpha=FEYNMAN_ALPHA;
  photon_pars.c1=WILSON_C1;
  photon_pars.zms=UNNO_ALEMANNA;
  
  std::string input_path;
  
  //parse opts
  int c;
  while((c=getopt(narg,arg,"i:"))!= -1)
    switch (c)
      {
      case 'i': input_path=optarg; break;
      default: crash("Unknown option -%c",optopt);
      }
  
  if(input_path=="") crash("Please specify -i");
  open_input(input_path);
  
  //geometry
  int L,T;
  read_str_int("L",&L);
  read_str_int("T",&T);
  init_grid(T,L);
  
  //Wall time
  double wall_time;
  read_str_double("WallTime",&wall_time);
  
  //local random generator
  int seed;
  read_str_int("Seed",&seed);
  start_loc_rnd_gen(seed);
  
  //fermion
  int r=0;
  //Read kappa
  double kappa;
  read_str_double("Kappa",&kappa);
  double am;
  read_str_double("Mass",&am);
  double residue;
  read_str_double("Residue",&residue);
  
  //read about nhits
  int nhits;
  read_str_int("NHits",&nhits);
  
  //read the calculation of eigenvalues
  // int neig;
  // read_str_int("Neig",&neig);
  // double eig_precision;
  // read_str_double("EigPrecision",&eig_precision);
  
  //allocate the source and prop
  spincolor *eta[nhits];
  spincolor *phi[nhits];
  for(int ihit=0;ihit<nhits;ihit++)
    {
      eta[ihit]=nissa_malloc("eta",loc_vol+bord_vol,spincolor);
      phi[ihit]=nissa_malloc("phi",loc_vol+bord_vol,spincolor);
    }
  
  //store eigenvectors and their convoution with eigenvalue
  // spincolor *eigvec[neig];
  // spincolor *eigvec_conv[neig];
  // for(int ieig=0;ieig<neig;ieig++)
  //   {
  //     eigvec[ieig]=nissa_malloc("eigvec",loc_vol+bord_vol,spincolor);
  //     eigvec_conv[ieig]=nissa_malloc("eigvec_conv",loc_vol+bord_vol,spincolor);
  //   }
  
  //compute the tadpole coefficient
  momentum_t tadpole_coeff;
  compute_tadpole(tadpole_coeff,photon_pars);
  
  //free theory
  read_str_int("FreeTheory",&free_theory);
  
  //divert if we are doing only the free theory
  if(free_theory)
    {
      free_th::allocate_props();
      free_th::precompute_propagators();
      
      free_th::free_props();
    }
  
  if(not free_theory)
    read_str_int("RandomConf",&random_conf);
  
  //conf
  int nconfs;
  read_str_int("NGaugeConfs",&nconfs);
  quad_su3 *conf=nissa_malloc("conf",loc_vol+bord_vol,quad_su3);
  
  //currents
  spin1field *J_stoch[nhits];
  // spin1field *J_eig[neig];
  for(int ihit=0;ihit<nhits;ihit++)
    J_stoch[ihit]=nissa_malloc("J_stoch",loc_vol+bord_vol,spin1field);
  // for(int ieig=0;ieig<neig;ieig++)
  //   J_eig[ieig]=nissa_malloc("J_eig",loc_vol+bord_vol,spin1field);
  spin1field *xi=nissa_malloc("xi",loc_vol+bord_vol,spin1field);
  
  //buffer for local matrix element
  mel::buffer=nissa_malloc("loc_mel::buffer",loc_vol,complex);
  
  //propagator used for tadpole
  spincolor *tadpole_prop=nissa_malloc("tadpole_prop",loc_vol+bord_vol,spincolor);
  
  /////////////////////////////////////////////////////////////////
  
  int iconf=0;
  do
    {
      //input conf and output dir
      char conf_path[1024];
      read_str(conf_path,1024);
      char outfolder[1024];
      read_str(outfolder,1024);
      
      //generate the source
      for(int ihit=0;ihit<nhits;ihit++)
	generate_undiluted_source(eta[ihit],RND_GAUSS,ALL_TIMES);
      
      if(file_exists(combine("%s/running",outfolder))) master_printf(" Skipping %s\n",conf_path);
      else
	{
	  int ris=create_dir(outfolder);
	  if(ris==0) master_printf(" Output path \"%s\" not present, created.\n",outfolder);
	  else       crash(" Failed to create the output \"%s\" for conf \"%s\".",outfolder,conf_path);
	  
	  //read the configuration and put phases
	  if(free_theory) generate_cold_lx_conf(conf);
	  else
	    {
	      if(random_conf) generate_hot_lx_conf(conf);
	      else read_ildg_gauge_conf(conf,conf_path);
	    }
	  
	  momentum_t old_theta;
	  old_theta[0]=0;old_theta[1]=old_theta[2]=old_theta[3]=0;
	  adapt_theta(conf,old_theta,theta,0,0);
	  
	  /////////////////////////////////////////////////////////////////
	  
	  // if(neig)
	  //   fill_eigenpart(eigvec_conv,eigvec,neig,conf,kappa,am,r,eig_precision);
	  
	  //compute_orthogonal_part(phi,eta,nhits,eigvec,neig,conf,kappa,am,r,residue);
	  compute_propagators(phi,eta,nhits,conf,kappa,am,r,residue);
	  
	  //compute all currents
	  for(int ihit=0;ihit<nhits;ihit++)
	    mel::conserved_vector_current_mel(J_stoch[ihit],eta[ihit],conf,r,phi[ihit]);
	  // for(int ieig=0;ieig<neig;ieig++)
	  //   mel::conserved_vector_current_mel(J_eig[ieig],eigvec[ieig],conf,r,eigvec_conv[ieig]);
	  
	  //compute diagrams EU1, EU2 and EU4
	  complex EU1_stoch={0.0,0.0},EU2_stoch={0.0,0.0},EU4_stoch={0.0,0.0};
	  
	  //open the output files
	  FILE *fout_EU1_stoch=open_file(combine("%s/EU1_stoch",outfolder),"w");
	  // FILE *fout_EU1_eigvec=open_file(combine("%s/EU1_eigvec",outfolder),"w");
	  FILE *fout_EU2_stoch=open_file(combine("%s/EU2_stoch",outfolder),"w");
	  // FILE *fout_EU2_eigvec=open_file(combine("%s/EU2_eigvec",outfolder),"w");
	  FILE *fout_EU4_stoch=open_file(combine("%s/EU4_stoch",outfolder),"w");
	  FILE *fout_EU5_stoch=open_file(combine("%s/EU5_stoch",outfolder),"w");
	  FILE *fout_EU6_stoch=open_file(combine("%s/EU6_stoch",outfolder),"w");
	  
	  // complex EU1_eigvec={0.0,0.0},EU2_eigvec={0.0,0.0};
	  // for(int ieig=0;ieig<neig;ieig++)
	  //   {
	  //     complex temp;
	      
	  //     //Pseudo
	  //     mel::local_mel(temp,eigvec[ieig],5,eigvec_conv[ieig]);
	  //     complex_summ_the_prod_idouble(EU1_eigvec,temp,-1.0);
	  //     master_fprintf(fout_EU1_eigvec,"%.16lg %.16lg\n",EU1_eigvec[RE],EU1_eigvec[IM]);
	      
	  //     //Scalar
	  //     mel::local_mel(temp,eigvec[ieig],0,eigvec_conv[ieig]);
	  //     complex_summassign(EU2_eigvec,temp);
	  //     master_fprintf(fout_EU2_eigvec,"%.16lg %.16lg\n",EU2_eigvec[RE],EU2_eigvec[IM]);
	  //   }
	  
	  for(int ihit=0;ihit<nhits;ihit++)
	    {
	      complex temp;
	      
	      //in this way, in the output file, there are all the value of EU1 for each value of eta, not average values - Lorenzo.
	      
	      //Pseudo
	      mel::local_mel(temp,eta[ihit],5,phi[ihit]);
	      complex_prod_idouble(EU1_stoch,temp,-1.0);
	      master_fprintf(fout_EU1_stoch,"%.16lg %.16lg\n",EU1_stoch[RE],EU1_stoch[IM]);
	      
	      //Scalar
	      mel::local_mel(EU2_stoch,eta[ihit],0,phi[ihit]);
	      master_fprintf(fout_EU2_stoch,"%.16lg %.16lg\n",EU2_stoch[RE],EU2_stoch[IM]);
	      
	      //Tadpole
	      insert_tm_tadpole(tadpole_prop,conf,phi[ihit],r,tadpole_coeff,ALL_TIMES);
	      mel::local_mel(EU4_stoch,eta[ihit],0,tadpole_prop);
	      master_fprintf(fout_EU4_stoch,"%.16lg %.16lg\n",EU4_stoch[RE],EU4_stoch[IM]);
	    }
	  
	  //Compute diagram EU5
	  complex EU5={0.0,0.0};
	  for(int ihit=0;ihit<nhits;ihit++)
	    {
	      multiply_by_tlSym_gauge_propagator(xi,J_stoch[ihit],photon_pars);
	      
	      for(int jhit=0;jhit<ihit;jhit++)
		{
		  mel::global_product(EU5,xi,J_stoch[jhit]);
		  master_fprintf(fout_EU5_stoch,"%.16lg %.16lg\n",EU5[RE],EU5[IM]);
		}
	    }
	  
	  //Compute diagram EU6
	  complex EU6={0.0,0.0};
	  for(int ihit=0;ihit<nhits;ihit++)
	    {
	      for(int jhit=0;jhit<ihit;jhit++)
		{
		  mel::conserved_vector_current_mel(J_stoch[ihit],eta[ihit],conf,r,phi[jhit]);
		  mel::conserved_vector_current_mel(J_stoch[jhit],eta[jhit],conf,r,phi[ihit]);
		  multiply_by_tlSym_gauge_propagator(xi,J_stoch[ihit],photon_pars);
		  mel::global_product(EU6,J_stoch[jhit],xi);
		  master_fprintf(fout_EU6_stoch,"%.16lg %.16lg\n",EU6[RE],EU6[IM]);
		}
	    }
	  
	  close_file(fout_EU1_stoch);
	  // close_file(fout_EU1_eigvec);
	  close_file(fout_EU2_stoch);
	  // close_file(fout_EU2_eigvec);
	  close_file(fout_EU4_stoch);
	  close_file(fout_EU5_stoch);
	  close_file(fout_EU6_stoch);
	}
      
      iconf++;
    }
  while(iconf<nconfs);
  
  /////////////////////////////////////////////////////////////////
  
  nissa_free(tadpole_prop);
  nissa_free(xi);
  nissa_free(mel::buffer);
  for(int ihit=0;ihit<nhits;ihit++)
    nissa_free(J_stoch[ihit]);
  // for(int ieig=0;ieig<neig;ieig++)
  //   nissa_free(J_eig[ieig]);
  nissa_free(conf);
  
  //free the source and prop
  for(int ihit=0;ihit<nhits;ihit++)
    {
      nissa_free(eta[ihit]);
      nissa_free(phi[ihit]);
    }
  
  //free eigvec and convolution
  // for(int ieig=0;ieig<neig;ieig++)
  //   {
  //     nissa_free(eigvec[ieig]);
  //     nissa_free(eigvec_conv[ieig]);
  //   }
}

int main(int narg,char **arg)
{
  init_nissa_threaded(narg,arg,in_main);
  close_nissa();
  
  return 0;
}

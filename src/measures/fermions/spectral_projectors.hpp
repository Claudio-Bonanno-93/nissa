#ifndef _SPECTRAL_PROJECTORS_HPP
#define _SPECTRAL_PROJECTORS_HPP

#include "eigenvalues/eigenvalues.hpp"
#include "fermionic_meas.hpp"
#include "hmc/gauge/topological_action.hpp"
#include "hmc/theory_pars.hpp"
#include "operations/smearing/smooth.hpp"

namespace nissa
{
  //parameters to measure topology properties
  struct spectr_proj_meas_pars_t : base_fermionic_meas_t
  {
    double m_Adams;       //Adams mass
    int neigs;            //number of eigenvalues required
    double eig_precision; //relative precision between eigenvalues
    int wspace_size;      //size of Krylov space for the Arnoldi algorithm (it would be clipped in [2*neigs,mat_size])
    std::string def_path(){return "pettirosso";}
    double def_m_Adams(){return 0.0;}
    int def_neigs(){return 5;}
    double def_eig_precision(){return 1e-5;}
    int def_wspace_size(){return DEFAULT_EIGPROB_WSPACE_SIZE;}
    
    int master_fprintf(FILE *fout,bool full) {return nissa::master_fprintf(fout,"%s",get_str().c_str());}
    std::string get_str(bool full=false);
    
    int is_nonstandard()
    {
      return
	base_fermionic_meas_t::is_nonstandard() or
	path!=def_path() or
	m_Adams!=def_m_Adams() or
	neigs!=def_neigs() or
	eig_precision!=def_eig_precision() or
	wspace_size!=def_wspace_size();
    }
    
    spectr_proj_meas_pars_t() :
      base_fermionic_meas_t(),
      m_Adams(def_m_Adams()),
      neigs(def_neigs()),
      eig_precision(def_eig_precision()),
      wspace_size(def_wspace_size())
    {
      path=def_path();
    }
    virtual ~spectr_proj_meas_pars_t(){}
  };
  
  void measure_spectral_proj(quad_su3 **conf,theory_pars_t &theory_pars,spectr_proj_meas_pars_t &pars, int iconf,bool conf_created);
}

#endif

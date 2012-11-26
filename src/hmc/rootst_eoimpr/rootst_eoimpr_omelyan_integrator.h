#ifndef _ROOTST_EOIMPR_OMELYAN_INTEGRATOR_H
#define _ROOTST_EOIMPR_OMELYAN_INTEGRATOR_H
void eo_conf_unitarize_explicitly_inverting(quad_su3 **conf);
void evolve_conf_with_momenta(quad_su3 **eo_conf,quad_su3 **H,double dt);
void evolve_momenta_with_full_rootst_eoimpr_force(quad_su3 **H,quad_su3 **conf,color **pf,theory_pars *physic,rat_approx *appr,double residue,double dt,hmc_force_piece force_piece);
void omelyan_rootst_eoimpr_evolver(quad_su3 **H,quad_su3 **conf,color **pf,theory_pars *physic,rat_approx *appr,hmc_evol_pars *simul,multistep_level multilev=MACRO_STEP);
#endif

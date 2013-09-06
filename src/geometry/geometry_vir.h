#ifndef _GEOMETRY_VIR_H
#define _GEOMETRY_VIR_H

void lx_conf_remap_to_virlx(bi_oct_su3 *out,quad_su3 *in);
void lx_conf_remap_to_vireo(bi_oct_su3 **out,quad_su3 *in);
void eo_conf_remap_to_vireo(bi_oct_su3 **out,quad_su3 **in);

void virlx_spincolor_remap_to_lx(spincolor *out,bi_spincolor *in);
void lx_spincolor_remap_to_virlx(bi_spincolor *out,spincolor *in);

void vireo_spincolor_remap_to_lx(spincolor *out,bi_spincolor **in);
void vireo_color_remap_to_lx(color *out,bi_color **in);
void virevn_or_odd_color_remap_to_evn_or_odd(color *out,bi_color *in);
void lx_spincolor_remap_to_vireo(bi_spincolor **out,spincolor *in);
void lx_color_remap_to_vireo(bi_color **out,color *in);
void evn_or_odd_color_remap_to_virevn_or_odd(bi_color *out,color *in);

void set_vir_geometry();
void unset_vir_geometry();

#endif

// Albany 3.0: Copyright 2016 National Technology & Engineering Solutions of
// Sandia, LLC (NTESS). This Software is released under the BSD license detailed
// in the file license.txt in the top-level Albany directory.

#include "Moertel_ExplicitTemplateInstantiation.hpp"

#if defined(HAVE_MOERTEL_EXPLICIT_INSTANTIATION)
#include "Moertel_PointT.hpp"
#include "Moertel_PointT_Def.hpp"

namespace MoertelT {

MOERTEL_INSTANTIATE_TEMPLATE_CLASS(PointT)

}  // namespace MoertelT

// non-member operators at global scope
#if defined(HAVE_MOERTEL_INST_DOUBLE_INT_INT)
template std::ostream&
operator<<(std::ostream& os, const MoertelT::PointT<3, double, int, int, KokkosNode>& inter);
template std::ostream&
operator<<(std::ostream& os, const MoertelT::PointT<2, double, int, int, KokkosNode>& inter);
#endif
#if defined(HAVE_MOERTEL_INST_DOUBLE_INT_LONGLONGINT)
template std::ostream&
operator<<(std::ostream& os, const MoertelT::PointT<3, double, int, long long, KokkosNode>& inter);
template std::ostream&
operator<<(std::ostream& os, const MoertelT::PointT<2, double, int, long long, KokkosNode>& inter);
#endif

#endif

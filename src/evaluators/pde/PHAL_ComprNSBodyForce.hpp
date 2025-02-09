// Albany 3.0: Copyright 2016 National Technology & Engineering Solutions of
// Sandia, LLC (NTESS). This Software is released under the BSD license detailed
// in the file license.txt in the top-level Albany directory.

#ifndef PHAL_COMPRNSBODYFORCE_HPP
#define PHAL_COMPRNSBODYFORCE_HPP

#include "PHAL_Dimension.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_MDField.hpp"
#include "Phalanx_config.hpp"

namespace PHAL {
/** \brief Finite Element Interpolation Evaluator

    This evaluator interpolates nodal DOF values to quad points.

*/

template <typename EvalT, typename Traits>
class ComprNSBodyForce : public PHX::EvaluatorWithBaseImpl<Traits>, public PHX::EvaluatorDerived<EvalT, Traits>
{
 public:
  typedef typename EvalT::ScalarT ScalarT;

  ComprNSBodyForce(Teuchos::ParameterList const& p);

  void
  postRegistrationSetup(typename Traits::SetupData d, PHX::FieldManager<Traits>& vm);

  void
  evaluateFields(typename Traits::EvalData d);

 private:
  typedef typename EvalT::MeshScalarT MeshScalarT;

  // Input:
  PHX::MDField<const MeshScalarT, Cell, QuadPoint, Dim> coordVec;
  Teuchos::Array<double>                                gravity;

  // Output:
  PHX::MDField<ScalarT, Cell, QuadPoint, VecDim> force;

  // Force types
  enum BFTYPE
  {
    NONE,
    TAYLOR_GREEN_VORTEX
  };
  BFTYPE bf_type;

  std::size_t numQPs;
  std::size_t numDims;
  std::size_t vecDim;
};
}  // namespace PHAL

#endif

// Albany 3.0: Copyright 2016 National Technology & Engineering Solutions of
// Sandia, LLC (NTESS). This Software is released under the BSD license detailed
// in the file license.txt in the top-level Albany directory.

#ifndef PHAL_TEPROP_HPP
#define PHAL_TEPROP_HPP

#include "Albany_SacadoTypes.hpp"
#include "PHAL_Dimension.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_MDField.hpp"
#include "Phalanx_config.hpp"
#include "Sacado_ParameterAccessor.hpp"
#include "Teuchos_Array.hpp"
#include "Teuchos_ParameterList.hpp"

namespace PHAL {
/**
 * \brief Evaluates thermal conductivity, either as a constant or a truncated
 * KL expansion.
 */

template <typename EvalT, typename Traits>
class TEProp : public PHX::EvaluatorWithBaseImpl<Traits>, public PHX::EvaluatorDerived<EvalT, Traits>, public Sacado::ParameterAccessor<EvalT, SPL_Traits>
{
 public:
  typedef typename EvalT::ScalarT     ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;

  TEProp(Teuchos::ParameterList& p);

  void
  postRegistrationSetup(typename Traits::SetupData d, PHX::FieldManager<Traits>& vm);

  void
  evaluateFields(typename Traits::EvalData d);

  ScalarT&
  getValue(std::string const& n);

 private:
  int
  whichMat(const MeshScalarT& x);

  std::size_t                                           numQPs;
  std::size_t                                           numDims;
  PHX::MDField<ScalarT, Cell, QuadPoint>                rhoCp;
  PHX::MDField<ScalarT, Cell, QuadPoint>                permittivity;
  PHX::MDField<ScalarT, Cell, QuadPoint>                thermalCond;
  PHX::MDField<ScalarT const, Cell, QuadPoint>          Temp;
  PHX::MDField<const MeshScalarT, Cell, QuadPoint, Dim> coordVec;

  int                     mats;
  Teuchos::Array<ScalarT> elecCs;
  Teuchos::Array<double>  thermCs;
  Teuchos::Array<double>  rhoCps;
  Teuchos::Array<double>  factor;
  Teuchos::Array<double>  xBounds;
};
}  // namespace PHAL

#endif

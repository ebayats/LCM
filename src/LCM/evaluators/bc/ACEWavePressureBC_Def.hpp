// Albany 3.0: Copyright 2016 National Technology & Engineering Solutions of
// Sandia, LLC (NTESS). This Software is released under the BSD license detailed
// in the file license.txt in the top-level Albany directory.

#include <string>

#include "Albany_Macros.hpp"
#include "Phalanx_DataLayout.hpp"

namespace LCM {

//*****
template <typename EvalT, typename Traits>
ACEWavePressureBC_Base<EvalT, Traits>::ACEWavePressureBC_Base(Teuchos::ParameterList& p)
    : PHAL::Neumann<EvalT, Traits>(p)
{
  use_new_wave_press_nbc = p.get<bool>("Use New Wave Press NBC", false);
  timeValues             = p.get<Teuchos::Array<RealType>>("Time Values").toVector();
  waterHeightValues      = p.get<Teuchos::Array<RealType>>("Water Height Values").toVector();
  heightAboveWaterOfMaxPressure =
      p.get<Teuchos::Array<RealType>>("Height Above Water of Max Pressure Values").toVector();
  waveLengthValues = p.get<Teuchos::Array<RealType>>("Wave Length Values").toVector();
  waveNumberValues = p.get<Teuchos::Array<RealType>>("Wave Number Values").toVector();
  sValues          = p.get<Teuchos::Array<RealType>>("Still Water Level Values").toVector();
  wValues          = p.get<Teuchos::Array<RealType>>("Wave Height Values").toVector();

  // IKT, 8/19/2021: the following checks are overkill, as we do the same checks
  // in Albany::BCUtils
  if (use_new_wave_press_nbc == false) {
    ALBANY_PANIC(
        !(timeValues.size() == waterHeightValues.size()),
        "Dimension of \"Time Values\" and \"water Height Values\" do not match\n");
    ALBANY_PANIC(
        !(timeValues.size() == heightAboveWaterOfMaxPressure.size()),
        "Dimension of \"Time Values\" and \"Height Above Water of Max Pressure Values\" do not match\n");
    ALBANY_PANIC(
        !(timeValues.size() == waveLengthValues.size()),
        "Dimension of \"Time Values\" and \"Wave Length Values\" do not match\n");
    ALBANY_PANIC(
        !(timeValues.size() == waveNumberValues.size()),
        "Dimension of \"Time Values\" and \"Wave Number Values\" do not match\n");
  } else {
    ALBANY_PANIC(
        !(timeValues.size() == sValues.size()),
        "Dimension of \"Time Values\" and \"Still Water Level Values\" do not match\n");
    ALBANY_PANIC(
        !(timeValues.size() == wValues.size()),
        "Dimension of \"Time Values\" and \"Wave Height Values\" do not match\n");
  }
}

//*****
template <typename EvalT, typename Traits>
void
ACEWavePressureBC_Base<EvalT, Traits>::computeVal(RealType time)
{
  ALBANY_PANIC(time > timeValues.back(), "Time is growing unbounded!");
  ScalarT      Val;
  RealType     slope;
  unsigned int Index(0);

  while (timeValues[Index] < time) Index++;

  if (Index == 0) {
    if (use_new_wave_press_nbc == false) {
      this->water_height_val                       = waterHeightValues[Index];
      this->height_above_water_of_max_pressure_val = heightAboveWaterOfMaxPressure[Index];
      this->wave_length_val                        = waveLengthValues[Index];
      this->wave_number_val                        = waveNumberValues[Index];
    } else {
      this->s_val = sValues[Index];
      this->w_val = wValues[Index];
    }
  } else {
    if (use_new_wave_press_nbc == false) {
      slope = (waterHeightValues[Index] - waterHeightValues[Index - 1]) / (timeValues[Index] - timeValues[Index - 1]);
      this->water_height_val = waterHeightValues[Index - 1] + slope * (time - timeValues[Index - 1]);
      slope                  = (heightAboveWaterOfMaxPressure[Index] - heightAboveWaterOfMaxPressure[Index - 1]) /
              (timeValues[Index] - timeValues[Index - 1]);
      this->height_above_water_of_max_pressure_val =
          heightAboveWaterOfMaxPressure[Index - 1] + slope * (time - timeValues[Index - 1]);
      slope = (waveLengthValues[Index] - waveLengthValues[Index - 1]) / (timeValues[Index] - timeValues[Index - 1]);
      this->wave_length_val = waveLengthValues[Index - 1] + slope * (time - timeValues[Index - 1]);
      slope = (waveNumberValues[Index] - waveNumberValues[Index - 1]) / (timeValues[Index] - timeValues[Index - 1]);
      this->wave_number_val = waveNumberValues[Index - 1] + slope * (time - timeValues[Index - 1]);
    } else {
      slope       = (sValues[Index] - sValues[Index - 1]) / (timeValues[Index] - timeValues[Index - 1]);
      this->s_val = sValues[Index - 1] + slope * (time - timeValues[Index - 1]);
      slope       = (wValues[Index] - wValues[Index - 1]) / (timeValues[Index] - timeValues[Index - 1]);
      this->w_val = wValues[Index - 1] + slope * (time - timeValues[Index - 1]);
    }
    // std::cout << "IKT computeVal water_height_val = " << this->water_height_val << "\n";
  }

  // IKT question: can water height be non-positive?  If not, add throw.
  if (use_new_wave_press_nbc == false) {
    ALBANY_PANIC(
        this->height_above_water_of_max_pressure_val <= 0, "Height above water of max pressure is non-positive!");
    ALBANY_PANIC(this->wave_length_val <= 0, "Wave length is non-positive!");
    ALBANY_PANIC(this->wave_number_val <= 0, "Wave number is non-positive!");
  }

  return;
}

template <typename EvalT, typename Traits>
ACEWavePressureBC<EvalT, Traits>::ACEWavePressureBC(Teuchos::ParameterList& p)
    : ACEWavePressureBC_Base<EvalT, Traits>(p)
{
}

template <typename EvalT, typename Traits>
void
ACEWavePressureBC<EvalT, Traits>::evaluateFields(typename Traits::EvalData workset)
{
  RealType time = workset.current_time;

  switch (this->bc_type) {
    case PHAL::NeumannBase<EvalT, Traits>::ACEPRESS:
      // calculate scalar value of BC based on current time
      this->computeVal(time);
      break;

    default:

      ALBANY_ABORT(
          "ACE Time dependent Neumann boundary condition of type - "
          << this->bc_type << " is not supported.  Only 'ACE P' NBC is supported.");
      break;
  }

  PHAL::Neumann<EvalT, Traits>::evaluateFields(workset);
}

}  // namespace LCM

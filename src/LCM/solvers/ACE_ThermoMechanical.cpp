// Albany 3.0: Copyright 2016 National Technology & Engineering Solutions of
// Sandia, LLC (NTESS). This Software is released under the BSD license detailed
// in the file license.txt in the top-level Albany directory.

#include "ACE_ThermoMechanical.hpp"

#include "Albany_ModelEvaluator.hpp"
#include "Albany_STKDiscretization.hpp"
#include "Albany_SolverFactory.hpp"
#include "Albany_Utils.hpp"
#include "MiniTensor.h"
#include "Piro_LOCASolver.hpp"
#include "Piro_TempusSolver.hpp"

namespace {
// In "Mechanics 3D", extract "Mechanics".
inline std::string
getName(std::string const& method)
{
  if (method.size() < 3) return method;
  return method.substr(0, method.size() - 3);
}
}

namespace LCM {

ACEThermoMechanical::ACEThermoMechanical(
    Teuchos::RCP<Teuchos::ParameterList> const&   app_params,
    Teuchos::RCP<Teuchos::Comm<int> const> const& comm) :
       fos_(Teuchos::VerboseObjectBase::getDefaultOStream())
{
  Teuchos::ParameterList& alt_system_params = app_params->sublist("Alternating System");

  // Get names of individual model input files
  Teuchos::Array<std::string> model_filenames = alt_system_params.get<Teuchos::Array<std::string>>("Model Input Files");

  //IKT FIXME - 6/4/2020: I believe the following 4 values are not relevant, except if we want 
  //to try to make the coupling tighter later.  Not sure if we want to keepe or remove these...
  //Keeping for now.
  min_iters_         = alt_system_params.get<int>("Minimum Iterations", 1);
  max_iters_         = alt_system_params.get<int>("Maximum Iterations", 1024);
  rel_tol_           = alt_system_params.get<ST>("Relative Tolerance", 1.0e-08);
  abs_tol_           = alt_system_params.get<ST>("Absolute Tolerance", 1.0e-08);
  
  maximum_steps_     = alt_system_params.get<int>("Maximum Steps", 0);
  initial_time_      = alt_system_params.get<ST>("Initial Time", 0.0);
  final_time_        = alt_system_params.get<ST>("Final Time", 0.0);
  initial_time_step_ = alt_system_params.get<ST>("Initial Time Step", 1.0);

  auto const dt  = initial_time_step_;
  auto const dt2 = dt * dt;

  min_time_step_    = alt_system_params.get<ST>("Minimum Time Step", dt);
  max_time_step_    = alt_system_params.get<ST>("Maximum Time Step", dt);
  reduction_factor_ = alt_system_params.get<ST>("Reduction Factor", 1.0);
  increase_factor_  = alt_system_params.get<ST>("Amplification Factor", 1.0);
  output_interval_  = alt_system_params.get<int>("Exodus Write Interval", 1);
  std_init_guess_   = alt_system_params.get<bool>("Standard Initial Guess", false);

  tol_factor_vel_ = alt_system_params.get<ST>("Tolerance Factor Velocity", dt);
  tol_factor_acc_ = alt_system_params.get<ST>("Tolerance Factor Acceleration", dt2);

  std::string convergence_str = alt_system_params.get<std::string>("Convergence Criterion", "BOTH");

  std::transform(convergence_str.begin(), convergence_str.end(), convergence_str.begin(), ::toupper);

  if (convergence_str == "ABSOLUTE") {
    criterion_ = ConvergenceCriterion::ABSOLUTE;
  } else if (convergence_str == "RELATIVE") {
    criterion_ = ConvergenceCriterion::RELATIVE;
  } else if (convergence_str == "BOTH") {
    criterion_ = ConvergenceCriterion::BOTH;
  } else {
    ALBANY_ABORT("Unknown Convergence Criterion");
  }

  std::string operator_str = alt_system_params.get<std::string>("Convergence Operator", "AND");

  std::transform(operator_str.begin(), operator_str.end(), operator_str.begin(), ::toupper);

  if (operator_str == "AND") {
    operator_ = ConvergenceLogicalOperator::AND;
  } else if (operator_str == "OR") {
    operator_ = ConvergenceLogicalOperator::OR;
  } else {
    ALBANY_ABORT("Unknown Convergence Logical Operator");
  }

  // Firewalls
  ALBANY_ASSERT(min_iters_ >= 1, "");
  ALBANY_ASSERT(max_iters_ >= 1, "");
  ALBANY_ASSERT(max_iters_ >= min_iters_, "");
  ALBANY_ASSERT(rel_tol_ >= 0.0, "");
  ALBANY_ASSERT(abs_tol_ >= 0.0, "");
  ALBANY_ASSERT(maximum_steps_ >= 1, "");
  ALBANY_ASSERT(final_time_ >= initial_time_, "");
  ALBANY_ASSERT(initial_time_step_ > 0.0, "");
  ALBANY_ASSERT(max_time_step_ > 0.0, "");
  ALBANY_ASSERT(min_time_step_ > 0.0, "");
  ALBANY_ASSERT(max_time_step_ >= min_time_step_, "");
  ALBANY_ASSERT(reduction_factor_ <= 1.0, "");
  ALBANY_ASSERT(reduction_factor_ > 0.0, "");
  ALBANY_ASSERT(increase_factor_ >= 1.0, "");
  ALBANY_ASSERT(output_interval_ >= 1, "");

  // number of models
  num_subdomains_ = model_filenames.size();

  // throw error if number of model filenames provided is not 2.
  ALBANY_ASSERT(num_subdomains_ == 2, "ACEThermoMechanical solver requires 2 models!"); 

  //IKT FIXME 6/4/2020 - not sure if the following is needed for ACE 
  // Create application name-index map used for Schwarz BC.
  Teuchos::RCP<std::map<std::string, int>> app_name_index_map = Teuchos::rcp(new std::map<std::string, int>);

  for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
    std::string const& app_name = model_filenames[subdomain];

    std::pair<std::string, int> app_name_index = std::make_pair(app_name, subdomain);

    app_name_index_map->insert(app_name_index);
  }

  // Arrays to cache useful info for each subdomain for later use
  apps_.resize(num_subdomains_);
  solvers_.resize(num_subdomains_);
  stk_mesh_structs_.resize(num_subdomains_);
  discs_.resize(num_subdomains_);
  model_evaluators_.resize(num_subdomains_);
  sub_inargs_.resize(num_subdomains_);
  sub_outargs_.resize(num_subdomains_);
  curr_x_.resize(num_subdomains_);
  prev_step_x_.resize(num_subdomains_);
  internal_states_.resize(num_subdomains_);

  //IKT NOTE 6/4/2020:the xdotdot arrays are 
  //not relevant for thermal problems,
  //but they are constructed anyway. 
  ics_x_.resize(num_subdomains_);
  ics_xdot_.resize(num_subdomains_);
  ics_xdotdot_.resize(num_subdomains_);
  prev_x_.resize(num_subdomains_);
  prev_xdot_.resize(num_subdomains_);
  prev_xdotdot_.resize(num_subdomains_);
  this_x_.resize(num_subdomains_);
  this_xdot_.resize(num_subdomains_);
  this_xdotdot_.resize(num_subdomains_);
  do_outputs_.resize(num_subdomains_);
  do_outputs_init_.resize(num_subdomains_);
  prob_types_.resize(num_subdomains_); 

  //IKT QUESTION 6/4/2020: do we want to support quasistatic for thermo-mechanical
  //coupling??  Leaving it in for now.
  bool is_static{false};
  bool is_dynamic{false};

  // Initialization
  for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
    // Get parameters for each subdomain
    Albany::SolverFactory solver_factory(model_filenames[subdomain], comm);

    solver_factory.setSchwarz(true);

    Teuchos::ParameterList& params = solver_factory.getParameters();

    Teuchos::ParameterList& problem_params = params.sublist("Problem", true);

    //Get problem name to figure out if we have a thermal or mechanical problem for 
    //this subdomain, and populate prob_types_ vector using this information.
    //IKT 6/4/2020: This is added to allow user to specify mechanics and thermal problems in 
    //different orders.  I'm not sure if this will be of interest ultimately or not. 
    const std::string problem_name = getName(problem_params.get<std::string>("Name"));
    if (problem_name == "Mechanics") {
      prob_types_[subdomain] = MECHANICS; 
    }
    else if (problem_name == "ACE Thermal") {
      prob_types_[subdomain] = THERMAL; 
    }
    else {
      //Throw error if problem name is not Mechanics or ACE Thermal. 
      //IKT 6/4/2020: I assume we only want to support Mechanics and ACE Thermal coupling.
      ALBANY_ASSERT(false, "ACE Sequential thermo-mechanical solver only supports coupling of 'Mechanics' and 'ACE Thermal' problems!");
    }
    
    // Add application array for later use in Schwarz BC.
    params.set("Application Array", apps_);

    // See application index for use with Schwarz BC.
    params.set("Application Index", subdomain);

    // Add application name-index map for later use in Schwarz BC.
    params.set("Application Name Index Map", app_name_index_map);

    // Add NOX pre-post-operator for Schwarz loop convergence criterion.
    bool const have_piro = params.isSublist("Piro");

    ALBANY_ASSERT(have_piro == true, "Error! Piro sublist not found.\n");

    Teuchos::ParameterList& piro_params = params.sublist("Piro");

    std::string const msg{"All subdomains must have the same solution method (NOX or Tempus)"};

    if (subdomain == 0) {
      is_dynamic  = piro_params.isSublist("Tempus");
      is_static   = !is_dynamic;
      is_static_  = is_static;
      is_dynamic_ = is_dynamic;
    }
    if (is_static == true) { ALBANY_ASSERT(piro_params.isSublist("NOX") == true, msg); }
    if (is_dynamic == true) {
      ALBANY_ASSERT(piro_params.isSublist("Tempus") == true, msg);

      Teuchos::ParameterList& tempus_params = piro_params.sublist("Tempus");

      tempus_params.set("Abort on Failure", false);

      Teuchos::ParameterList& time_step_control_params =
          piro_params.sublist("Tempus").sublist("Tempus Integrator").sublist("Time Step Control");

      std::string const integrator_step_type = time_step_control_params.get("Integrator Step Type", "Constant");

      std::string const msg2{
          "Non-constant time-stepping through Tempus not supported "
          "with dynamic alternating Schwarz; \n"
          "In this case, variable time-stepping is "
          "handled within the Schwarz loop.\n"
          "Please rerun with 'Integrator Step Type: "
          "Constant' in 'Time Step Control' sublist.\n"};
      ALBANY_ASSERT(integrator_step_type == "Constant", msg2);
    }

    Teuchos::RCP<Albany::Application> app{Teuchos::null};

    Teuchos::RCP<Thyra::ResponseOnlyModelEvaluatorBase<ST>> solver =
        solver_factory.createAndGetAlbanyApp(app, comm, comm);

    solvers_[subdomain] = solver;

    //IKT 6/4/2020: I think we still need to call the following,
    //looking at the code, but we may want to rename the routine if
    //it ends up being used for thermo-mechanical too in addition
    //to Schwarz.

    app->setSchwarzAlternating(true);

    apps_[subdomain] = app;

    // Get STK mesh structs to control Exodus output interval
    Teuchos::RCP<Albany::AbstractDiscretization> disc = app->getDiscretization();

    discs_[subdomain] = disc;

    Albany::STKDiscretization& stk_disc = *static_cast<Albany::STKDiscretization*>(disc.get());

    Teuchos::RCP<Albany::AbstractSTKMeshStruct> ams = stk_disc.getSTKMeshStruct();

    do_outputs_[subdomain]      = ams->exoOutput;
    do_outputs_init_[subdomain] = ams->exoOutput;

    stk_mesh_structs_[subdomain] = ams;

    model_evaluators_[subdomain] = solver_factory.returnModel();

    curr_x_[subdomain] = Teuchos::null;
  }

  // Parameters
  Teuchos::ParameterList& problem_params = app_params->sublist("Problem");
  bool const have_parameters = problem_params.isSublist("Parameters");
  ALBANY_ASSERT(have_parameters == false, "Parameters not supported.");

  // Responses
  bool const have_responses = problem_params.isSublist("Response Functions");
  ALBANY_ASSERT(have_responses == false, "Responses not supported.");
  
  //Check that map has both mechanics and thermal problem; if not, throw error.
  bool mechanics_found = false;
  bool thermal_found = false; 
  for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
    PROB_TYPE prob_type = prob_types_[subdomain];
    if (prob_type == MECHANICS) {
      mechanics_found = true; 
    }
    else if (prob_type == THERMAL) {
      thermal_found = true; 
    } 
  }
  ALBANY_ASSERT(mechanics_found == true, "'Mechanics' needs to be one of the coupled problems, but it is not found!");
  ALBANY_ASSERT(thermal_found == true, "'ACE Thermal' needs to be one of the coupled problems, but it is not found!");

  return;
}

ACEThermoMechanical::~ACEThermoMechanical() { return; }

Teuchos::RCP<Thyra_VectorSpace const>
ACEThermoMechanical::get_x_space() const
{
  return Teuchos::null;
}

Teuchos::RCP<Thyra_VectorSpace const>
ACEThermoMechanical::get_f_space() const
{
  return Teuchos::null;
}

Teuchos::RCP<Thyra_VectorSpace const>
ACEThermoMechanical::get_p_space(int) const
{
  return Teuchos::null;
}

Teuchos::RCP<Thyra_VectorSpace const>
ACEThermoMechanical::get_g_space(int) const
{
  return Teuchos::null;
}

Teuchos::RCP<const Teuchos::Array<std::string>>
ACEThermoMechanical::get_p_names(int) const
{
  return Teuchos::null;
}

Teuchos::ArrayView<std::string const>
ACEThermoMechanical::get_g_names(int) const
{
  ALBANY_ABORT("not implemented");
  return Teuchos::ArrayView<std::string const>(Teuchos::null);
}

Thyra_ModelEvaluator::InArgs<ST>
ACEThermoMechanical::getNominalValues() const
{
  return this->createInArgsImpl();
}

Thyra_ModelEvaluator::InArgs<ST>
ACEThermoMechanical::getLowerBounds() const
{
  return Thyra_ModelEvaluator::InArgs<ST>();  // Default value
}

Thyra_ModelEvaluator::InArgs<ST>
ACEThermoMechanical::getUpperBounds() const
{
  return Thyra_ModelEvaluator::InArgs<ST>();  // Default value
}

Teuchos::RCP<Thyra::LinearOpBase<ST>>
ACEThermoMechanical::create_W_op() const
{
  return Teuchos::null;
}

Teuchos::RCP<Thyra::PreconditionerBase<ST>>
ACEThermoMechanical::create_W_prec() const
{
  return Teuchos::null;
}

Teuchos::RCP<const Thyra::LinearOpWithSolveFactoryBase<ST>>
ACEThermoMechanical::get_W_factory() const
{
  return Teuchos::null;
}

Thyra_ModelEvaluator::InArgs<ST>
ACEThermoMechanical::createInArgs() const
{
  return this->createInArgsImpl();
}

Teuchos::ArrayRCP<Teuchos::RCP<Albany::Application>>
ACEThermoMechanical::getApps() const
{
  return apps_;
}

void
ACEThermoMechanical::set_failed(char const* msg)
{
  failed_          = true;
  failure_message_ = msg;
  return;
}

void
ACEThermoMechanical::clear_failed()
{
  failed_ = false;
  return;
}

bool
ACEThermoMechanical::get_failed() const
{
  return failed_;
}

// Create operator form of dg/dx for distributed responses
Teuchos::RCP<Thyra::LinearOpBase<ST>>
ACEThermoMechanical::create_DgDx_op_impl(int /* j */) const
{
  return Teuchos::null;
}

// Create operator form of dg/dx_dot for distributed responses
Teuchos::RCP<Thyra::LinearOpBase<ST>>
ACEThermoMechanical::create_DgDx_dot_op_impl(int /* j */) const
{
  return Teuchos::null;
}

// Create InArgs
Thyra_InArgs
ACEThermoMechanical::createInArgsImpl() const
{
  Thyra::ModelEvaluatorBase::InArgsSetup<ST> ias;

  ias.setModelEvalDescription(this->description());

  ias.setSupports(Thyra_ModelEvaluator::IN_ARG_x, true);
  ias.setSupports(Thyra_ModelEvaluator::IN_ARG_x_dot, true);
  ias.setSupports(Thyra_ModelEvaluator::IN_ARG_x_dot_dot, true);
  ias.setSupports(Thyra_ModelEvaluator::IN_ARG_t, true);
  ias.setSupports(Thyra_ModelEvaluator::IN_ARG_alpha, true);
  ias.setSupports(Thyra_ModelEvaluator::IN_ARG_beta, true);
  ias.setSupports(Thyra_ModelEvaluator::IN_ARG_W_x_dot_dot_coeff, true);

  return static_cast<Thyra_InArgs>(ias);
}

// Create OutArgs
Thyra_OutArgs
ACEThermoMechanical::createOutArgsImpl() const
{
  Thyra::ModelEvaluatorBase::OutArgsSetup<ST> oas;

  oas.setModelEvalDescription(this->description());

  oas.setSupports(Thyra_ModelEvaluator::OUT_ARG_f, true);
  oas.setSupports(Thyra_ModelEvaluator::OUT_ARG_W_op, true);
  oas.setSupports(Thyra_ModelEvaluator::OUT_ARG_W_prec, false);

  oas.set_W_properties(Thyra_ModelEvaluator::DerivativeProperties(
      Thyra_ModelEvaluator::DERIV_LINEARITY_UNKNOWN, Thyra_ModelEvaluator::DERIV_RANK_FULL, true));

  return static_cast<Thyra_OutArgs>(oas);
}

// Evaluate model on InArgs
void
ACEThermoMechanical::evalModelImpl(Thyra_ModelEvaluator::InArgs<ST> const&, Thyra_ModelEvaluator::OutArgs<ST> const&)
    const
{
  if (is_dynamic_ == true) { ThermoMechanicalLoopDynamics(); }
  //IKT 6/4/2020: for now, throw error if trying to run quasi-statically.
  //Not sure if we want to ultimately support that case or not.
  ALBANY_ASSERT(is_static_ == false, "ACE Sequential Thermo-Mechanical solver currently supports dynamics only!"); 
  //if (is_static_ == true) { ThermoMechanicalLoopQuasistatics(); }
  return;
}

namespace {

std::string
centered(std::string const& str, int width)
{
  assert(width >= 0);
  int const length  = static_cast<int>(str.size());
  int const padding = width - length;
  if (padding <= 0) return str;
  int const left  = padding / 2;
  int const right = padding - left;
  return std::string(left, ' ') + str + std::string(right, ' ');
}

}  // namespace

void
ACEThermoMechanical::updateConvergenceCriterion() const
{
  abs_error_ = norm_diff_;
  rel_error_ = norm_final_ > 0.0 ? norm_diff_ / norm_final_ : norm_diff_;

  bool const converged_absolute = abs_error_ <= abs_tol_;
  bool const converged_relative = rel_error_ <= rel_tol_;

  switch (criterion_) {
    default: ALBANY_ABORT("Unknown Convergence Criterion"); break;
    case ConvergenceCriterion::ABSOLUTE: converged_ = converged_absolute; break;
    case ConvergenceCriterion::RELATIVE: converged_ = converged_relative; break;
    case ConvergenceCriterion::BOTH:
      switch (operator_) {
        default: ALBANY_ABORT("Unknown Convergence Logical Operator"); break;
        case ConvergenceLogicalOperator::AND: converged_ = converged_absolute && converged_relative; break;
        case ConvergenceLogicalOperator::OR: converged_ = converged_absolute || converged_relative; break;
      }
      break;
  }
}

bool
ACEThermoMechanical::continueSolve() const
{
  ++num_iter_;

  // If failure has occurred, stop immediately.
  if (failed_ == true) return false;

  // Regardless of other criteria, if error is zero stop solving.
  bool const zero_error = ((abs_error_ > 0.0) == false);

  if (zero_error == true) return false;

  // Minimum iterations takes precedence over maximum iterations and
  // convergence. Continue solving if not exceeded.
  bool const exceeds_min_iter = num_iter_ >= min_iters_;

  if (exceeds_min_iter == false) return true;

  // Maximum iterations takes precedence over convergence.
  // Stop solving if exceeded.
  bool const exceeds_max_iter = num_iter_ >= max_iters_;

  if (exceeds_max_iter == true) return false;

  // Lastly check for convergence.
  bool const continue_solve = (converged_ == false);

  return continue_solve;
}

void
ACEThermoMechanical::reportFinals(std::ostream& os) const
{
  std::string const conv_str = converged_ == true ? "YES" : "NO";

  os << '\n';
  os << "ACE Sequential Thermo-Mechanical Method converged: " << conv_str << '\n';
  os << "Minimum iterations :" << min_iters_ << '\n';
  os << "Maximum iterations :" << max_iters_ << '\n';
  os << "Total iterations   :" << num_iter_ << '\n';
  os << "Last absolute error:" << abs_error_ << '\n';
  os << "Absolute tolerance :" << abs_tol_ << '\n';
  os << "Last relative error:" << rel_error_ << '\n';
  os << "Relative tolerance :" << rel_tol_ << '\n';
  os << std::endl;
}

// Sequential ThermoMechanical coupling loop, dynamic
void
ACEThermoMechanical::ThermoMechanicalLoopDynamics() const
{
  std::cout << "IKT in ThermoMechanicalLoopDynamics()!\n";

  minitensor::Filler const ZEROS{minitensor::Filler::ZEROS};
  minitensor::Vector<ST>   norms_init(num_subdomains_, ZEROS);
  minitensor::Vector<ST>   norms_final(num_subdomains_, ZEROS);
  minitensor::Vector<ST>   norms_diff(num_subdomains_, ZEROS);
  std::string const        delim(72, '=');

  *fos_ << std::scientific << std::setprecision(17);

  ST  time_step{initial_time_step_};
  int stop{0};
  ST  current_time{initial_time_};

  // Set ICs and PrevSoln vecs and write initial configuration to Exodus file
  setDynamicICVecsAndDoOutput(initial_time_);

  // Time-stepping loop
  while (stop < maximum_steps_ && current_time < final_time_) {
    *fos_ << delim << std::endl;
    *fos_ << "Time stop          :" << stop << '\n';
    *fos_ << "Time               :" << current_time << '\n';
    *fos_ << "Time step          :" << time_step << '\n';
    *fos_ << delim << std::endl;

    // Before the Schwarz loop, get internal states
    for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
      auto& app       = *apps_[subdomain];
      auto& state_mgr = app.getStateMgr();
      fromTo(state_mgr.getStateArrays(), internal_states_[subdomain]);
    }

    ST const next_time{current_time + time_step};
    num_iter_ = 0;

    // Schwarz loop
    do {
      bool const is_initial_state = stop == 0 && num_iter_ == 0;
      for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
        *fos_ << delim << std::endl;
        const PROB_TYPE prob_type = prob_types_[subdomain]; 
	//IKT FIXME 6/5/2020: remove the following print statment 
        *fos_ << "Schwarz iteration  :" << num_iter_ << '\n';
        *fos_ << "Subdomain          :" << subdomain << '\n';
	//IKT FIXME 6/5/2020: currently there is a lot of code duplication in 
	//AdvanceMechanicsDynamics and AdvanceThermalDynamics b/c they effectively 
	//do the thing.  These routines may diverge as we modify them to do the
	//sequential coupling, which is why I kept them separate.  We may want to think 
	//of ways to combine them at a later point to avoid some of the code duplication.
	if (prob_type == MECHANICS) {
	  *fos_ << "Problem            :Mechanics\n";
	  failed_ = AdvanceMechanicsDynamics(subdomain, is_initial_state,
			                      current_time, next_time, time_step);  
	}
	else {
          *fos_ << "Problem            :Thermal\n"; 
	  failed_ = AdvanceThermalDynamics(subdomain, is_initial_state, current_time,
			            next_time, time_step);  
	}
        if (failed_ == true) { 
          // Break out of the subdomain loop
          break;
	}
      }  // Subdomains loop

      if (failed_ == true) {
        *fos_ << "INFO: Unable to continue Schwarz iteration " << num_iter_;
        *fos_ << "\n";
        // Break out of the Schwarz loop.
        break;
      }

      norm_init_  = minitensor::norm(norms_init);
      norm_final_ = minitensor::norm(norms_final);
      norm_diff_  = minitensor::norm(norms_diff);

      updateConvergenceCriterion();

      *fos_ << delim << std::endl;
      *fos_ << "Schwarz iteration         :" << num_iter_ << '\n';

      std::string const line(72, '-');

      *fos_ << line << std::endl;

      *fos_ << centered("Sub", 6);
      *fos_ << centered("Initial norm", 22);
      *fos_ << centered("Final norm", 22);
      *fos_ << centered("Difference norm", 22);
      *fos_ << std::endl;
      *fos_ << centered("dom", 6);
      *fos_ << centered("||X0||", 22);
      *fos_ << centered("||Xf||", 22);
      *fos_ << centered("||Xf-X0||", 22);
      *fos_ << std::endl;
      *fos_ << line << std::endl;

      for (auto m = 0; m < num_subdomains_; ++m) {
        *fos_ << std::setw(6) << m;
        *fos_ << std::setw(22) << norms_init(m);
        *fos_ << std::setw(22) << norms_final(m);
        *fos_ << std::setw(22) << norms_diff(m);
        *fos_ << std::endl;
      }

      *fos_ << line << std::endl;
      *fos_ << centered("Norm", 6);
      *fos_ << std::setw(22) << norm_init_;
      *fos_ << std::setw(22) << norm_final_;
      *fos_ << std::setw(22) << norm_diff_;
      *fos_ << std::endl;
      *fos_ << line << std::endl;
      *fos_ << "Absolute error     :" << abs_error_ << '\n';
      *fos_ << "Absolute tolerance :" << abs_tol_ << '\n';
      *fos_ << "Relative error     :" << rel_error_ << '\n';
      *fos_ << "Relative tolerance :" << rel_tol_ << '\n';
      *fos_ << delim << std::endl;

    } while (continueSolve() == true);

    // One of the subdomains failed to solve. Reduce step.
    if (failed_ == true) {
      failed_ = false;

      auto const reduced_step = reduction_factor_ * time_step;

      if (time_step <= min_time_step_) {
        *fos_ << "ERROR: Cannot reduce step. Stopping execution.\n";
        *fos_ << "INFO: Requested step    :" << reduced_step << '\n';
        *fos_ << "INFO: Minimum time step :" << min_time_step_ << '\n';
        return;
      }

      if (reduced_step > min_time_step_) {
        *fos_ << "INFO: Reducing step from " << time_step << " to ";
        *fos_ << reduced_step << '\n';
        time_step = reduced_step;
      } else {
        *fos_ << "INFO: Reducing step from " << time_step << " to ";
        *fos_ << min_time_step_ << '\n';
        time_step = min_time_step_;
      }

      // Restore previous solutions
      for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
        Thyra::put_scalar(0.0, this_x_[subdomain].ptr());
        Thyra::copy(*ics_x_[subdomain], this_x_[subdomain].ptr());
        Thyra::put_scalar(0.0, this_xdot_[subdomain].ptr());
        Thyra::copy(*ics_xdot_[subdomain], this_xdot_[subdomain].ptr());
        Thyra::put_scalar(0.0, this_xdotdot_[subdomain].ptr());
        Thyra::copy(*ics_xdotdot_[subdomain], this_xdotdot_[subdomain].ptr());

        // restore the state manager with the state variables from the previous
        // loadstep.
        auto& app       = *apps_[subdomain];
        auto& state_mgr = app.getStateMgr();
        fromTo(internal_states_[subdomain], state_mgr.getStateArrays());

        // restore the solution in the discretization so the schwarz solver gets
        // the right boundary conditions!
        Teuchos::RCP<Thyra_Vector const> disp_rcp_thyra = ics_x_[subdomain];
        Teuchos::RCP<Thyra_Vector const> velo_rcp_thyra = ics_xdot_[subdomain];
        Teuchos::RCP<Thyra_Vector const> acce_rcp_thyra = ics_xdotdot_[subdomain];

        Teuchos::RCP<Albany::AbstractDiscretization> const& app_disc = app.getDiscretization();

        app_disc->writeSolutionToMeshDatabase(*disp_rcp_thyra, *velo_rcp_thyra, *acce_rcp_thyra, current_time);
      }

      // Jump to the beginning of the time-step loop without advancing
      // time to try to use a reduced step.
      continue;
    }

    reportFinals(*fos_);

    // Update IC vecs and output solution to exodus file

    for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
      if (do_outputs_init_[subdomain] == true) {
        do_outputs_[subdomain] = output_interval_ > 0 ? (stop + 1) % output_interval_ == 0 : false;
      }
    }

    setDynamicICVecsAndDoOutput(next_time);

    ++stop;
    current_time += time_step;

    // Step successful. Try to increase the time step.
    auto const increased_step = std::min(max_time_step_, increase_factor_ * time_step);

    if (increased_step > time_step) {
      *fos_ << "\nINFO: Increasing step from " << time_step << " to ";
      *fos_ << increased_step << '\n';
      time_step = increased_step;
    } else {
      *fos_ << "\nINFO: Cannot increase step. Using " << time_step << '\n';
    }

  }  // Time-step loop

  return;
}

bool
ACEThermoMechanical::AdvanceThermalDynamics(const int subdomain, const bool is_initial_state,
                                            const double current_time, const double next_time, 
					    const double time_step) const 
{
  std::cout << "IKT in AdvanceThermalDynamics()\n"; 
  // Restore solution from previous Schwarz iteration before solve
  if (is_initial_state == true) {
    auto&       me        = dynamic_cast<Albany::ModelEvaluator&>(*model_evaluators_[subdomain]);
    auto const& nv        = me.getNominalValues();
    prev_x_[subdomain] = Thyra::createMember(me.get_x_space());
    Thyra::copy(*(nv.get_x()), prev_x_[subdomain].ptr());
    prev_xdot_[subdomain] = Thyra::createMember(me.get_x_space());
    Thyra::copy(*(nv.get_x_dot()), prev_xdot_[subdomain].ptr());
  } 
  else {
    Thyra::put_scalar(0.0, prev_x_[subdomain].ptr());
    Thyra::copy(*this_x_[subdomain], prev_x_[subdomain].ptr());
    Thyra::put_scalar(0.0, prev_xdot_[subdomain].ptr());
    Thyra::copy(*this_xdot_[subdomain], prev_xdot_[subdomain].ptr());
  }
  // Solve for each subdomain
  Thyra::ResponseOnlyModelEvaluatorBase<ST>& solver = *(solvers_[subdomain]);

  Piro::TempusSolver<ST>& piro_tempus_solver = dynamic_cast<Piro::TempusSolver<ST>&>(solver);

  piro_tempus_solver.setStartTime(current_time);
  piro_tempus_solver.setFinalTime(next_time);
  piro_tempus_solver.setInitTimeStep(time_step);

  std::string const        delim(72, '=');
  *fos_ << "Initial time       :" << current_time << '\n';
  *fos_ << "Final time         :" << next_time << '\n';
  *fos_ << "Time step          :" << time_step << '\n';
  *fos_ << delim << std::endl;

  Thyra_ModelEvaluator::InArgs<ST>  in_args  = solver.createInArgs();
  Thyra_ModelEvaluator::OutArgs<ST> out_args = solver.createOutArgs();

  auto& me = dynamic_cast<Albany::ModelEvaluator&>(*model_evaluators_[subdomain]);

  //IKT FIXME 6/5/2020: need to check if this does the right thing for thermal problem
  //The only relevant internal state here would be the ice saturation
  // Restore internal states
  auto& app       = *apps_[subdomain];
  auto& state_mgr = app.getStateMgr();
  fromTo(internal_states_[subdomain], state_mgr.getStateArrays());

  Teuchos::RCP<Tempus::SolutionHistory<ST>> solution_history;
  Teuchos::RCP<Tempus::SolutionState<ST>>   current_state;
  
  Teuchos::RCP<Thyra_Vector>                ic_disp_rcp = Thyra::createMember(me.get_x_space());
  Teuchos::RCP<Thyra_Vector> ic_velo_rcp = Thyra::createMember(me.get_x_space());

  // set ic_disp_rcp and ic_velo_rcp
  // by making copy of what is in ics_x_[subdomain], etc.
  Thyra_Vector& ic_disp = *ics_x_[subdomain];
  Thyra_Vector& ic_velo = *ics_xdot_[subdomain];

  Thyra::copy(ic_disp, ic_disp_rcp.ptr());
  Thyra::copy(ic_velo, ic_velo_rcp.ptr());

  piro_tempus_solver.setInitialState(current_time, ic_disp_rcp, ic_velo_rcp);

  if (std_init_guess_ == false) { piro_tempus_solver.setInitialGuess(prev_x_[subdomain]); }

  solver.evalModel(in_args, out_args);

  // Allocate current solution vectors
  this_x_[subdomain] = Thyra::createMember(me.get_x_space());
  this_xdot_[subdomain] = Thyra::createMember(me.get_x_space());

  // Check whether solver did OK.
  auto const status = piro_tempus_solver.getTempusIntegratorStatus();

  if (status == Tempus::Status::FAILED) {
    *fos_ << "\nINFO: Unable to solve Thermal problem for subdomain " << subdomain << '\n';
    //the following sets failed_ = true
    return true;  
  }
        
  // If solver is OK, extract solution

  solution_history = piro_tempus_solver.getSolutionHistory();
  current_state    = solution_history->getCurrentState();

  Thyra::copy(*current_state->getX(), this_x_[subdomain].ptr());
  Thyra::copy(*current_state->getXDot(), this_xdot_[subdomain].ptr());

  Teuchos::RCP<Thyra_Vector> disp_diff_rcp = Thyra::createMember(me.get_x_space());
  Thyra::put_scalar<ST>(0.0, disp_diff_rcp.ptr());
  Thyra::V_VpStV(disp_diff_rcp.ptr(), *this_x_[subdomain], -1.0, *prev_x_[subdomain]);

  Teuchos::RCP<Thyra_Vector> velo_diff_rcp = Thyra::createMember(me.get_x_space());
  Thyra::put_scalar<ST>(0.0, velo_diff_rcp.ptr());
  Thyra::V_VpStV(velo_diff_rcp.ptr(), *this_xdot_[subdomain], -1.0, *prev_xdot_[subdomain]);

  // After solve, save solution and get info to check convergence
  // IKT 6/5/2020: the following came from Schwarz and is irrelevant here for now,
  // so I am commenting it out .
  /*norms_init(subdomain)  = Thyra::norm(*prev_x_[subdomain]);
  norms_final(subdomain) = Thyra::norm(*this_x_[subdomain]);
  norms_diff(subdomain)  = Thyra::norm(*disp_diff_rcp);

  auto const dt = tol_factor_vel_;

  norms_init(subdomain) += dt * Thyra::norm(*prev_xdot_[subdomain]);
  norms_final(subdomain) += dt * Thyra::norm(*this_xdot_[subdomain]);
  norms_diff(subdomain) += dt * Thyra::norm(*velo_diff_rcp);*/

  return false; 
}

bool
ACEThermoMechanical::AdvanceMechanicsDynamics(const int subdomain, const bool is_initial_state,
                                              const double current_time, const double next_time, 
					      const double time_step) const 
{
  std::cout << "IKT in AdvanceMechanicsDynamics()\n"; 
  // Restore solution from previous Schwarz iteration before solve
  if (is_initial_state == true) {
    auto&       me        = dynamic_cast<Albany::ModelEvaluator&>(*model_evaluators_[subdomain]);
    auto const& nv        = me.getNominalValues();
    prev_x_[subdomain] = Thyra::createMember(me.get_x_space());
    Thyra::copy(*(nv.get_x()), prev_x_[subdomain].ptr());
    prev_xdot_[subdomain] = Thyra::createMember(me.get_x_space());
    Thyra::copy(*(nv.get_x_dot()), prev_xdot_[subdomain].ptr());
    prev_xdotdot_[subdomain] = Thyra::createMember(me.get_x_space());
    Thyra::copy(*(nv.get_x_dot_dot()), prev_xdotdot_[subdomain].ptr());
  } 
  else {
    Thyra::put_scalar(0.0, prev_x_[subdomain].ptr());
    Thyra::copy(*this_x_[subdomain], prev_x_[subdomain].ptr());
    Thyra::put_scalar(0.0, prev_xdot_[subdomain].ptr());
    Thyra::copy(*this_xdot_[subdomain], prev_xdot_[subdomain].ptr());
    Thyra::put_scalar(0.0, prev_xdotdot_[subdomain].ptr());
    Thyra::copy(*this_xdotdot_[subdomain], prev_xdotdot_[subdomain].ptr());
  }
  // Solve for each subdomain
  Thyra::ResponseOnlyModelEvaluatorBase<ST>& solver = *(solvers_[subdomain]);

  Piro::TempusSolver<ST>& piro_tempus_solver = dynamic_cast<Piro::TempusSolver<ST>&>(solver);

  piro_tempus_solver.setStartTime(current_time);
  piro_tempus_solver.setFinalTime(next_time);
  piro_tempus_solver.setInitTimeStep(time_step);

  std::string const        delim(72, '=');
  *fos_ << "Initial time       :" << current_time << '\n';
  *fos_ << "Final time         :" << next_time << '\n';
  *fos_ << "Time step          :" << time_step << '\n';
  *fos_ << delim << std::endl;

  Thyra_ModelEvaluator::InArgs<ST>  in_args  = solver.createInArgs();
  Thyra_ModelEvaluator::OutArgs<ST> out_args = solver.createOutArgs();

  auto& me = dynamic_cast<Albany::ModelEvaluator&>(*model_evaluators_[subdomain]);

  // Restore internal states
  auto& app       = *apps_[subdomain];
  auto& state_mgr = app.getStateMgr();

  fromTo(internal_states_[subdomain], state_mgr.getStateArrays());

  Teuchos::RCP<Tempus::SolutionHistory<ST>> solution_history;
  Teuchos::RCP<Tempus::SolutionState<ST>>   current_state;
  Teuchos::RCP<Thyra_Vector>                ic_disp_rcp = Thyra::createMember(me.get_x_space());

  Teuchos::RCP<Thyra_Vector> ic_velo_rcp = Thyra::createMember(me.get_x_space());

  Teuchos::RCP<Thyra_Vector> ic_acce_rcp = Thyra::createMember(me.get_x_space());

  // set ic_disp_rcp, ic_velo_rcp and ic_acce_rcp
  // by making copy of what is in ics_x_[subdomain], etc.
  Thyra_Vector& ic_disp = *ics_x_[subdomain];
  Thyra_Vector& ic_velo = *ics_xdot_[subdomain];
  Thyra_Vector& ic_acce = *ics_xdotdot_[subdomain];

  Thyra::copy(ic_disp, ic_disp_rcp.ptr());
  Thyra::copy(ic_velo, ic_velo_rcp.ptr());
  Thyra::copy(ic_acce, ic_acce_rcp.ptr());

  piro_tempus_solver.setInitialState(current_time, ic_disp_rcp, ic_velo_rcp, ic_acce_rcp);

  if (std_init_guess_ == false) { piro_tempus_solver.setInitialGuess(prev_x_[subdomain]); }

  solver.evalModel(in_args, out_args);

  // Allocate current solution vectors
  this_x_[subdomain] = Thyra::createMember(me.get_x_space());
  this_xdot_[subdomain] = Thyra::createMember(me.get_x_space());
  this_xdotdot_[subdomain] = Thyra::createMember(me.get_x_space());

  // Check whether solver did OK.
  auto const status = piro_tempus_solver.getTempusIntegratorStatus();

  if (status == Tempus::Status::FAILED) {
    *fos_ << "\nINFO: Unable to solve Mechanics problem for subdomain " << subdomain << '\n';
    //The following sets failed_ = true 
    return true;  
  }
  // If solver is OK, extract solution

  solution_history = piro_tempus_solver.getSolutionHistory();
  current_state    = solution_history->getCurrentState();

  Thyra::copy(*current_state->getX(), this_x_[subdomain].ptr());
  Thyra::copy(*current_state->getXDot(), this_xdot_[subdomain].ptr());
  Thyra::copy(*current_state->getXDotDot(), this_xdotdot_[subdomain].ptr());

  Teuchos::RCP<Thyra_Vector> disp_diff_rcp = Thyra::createMember(me.get_x_space());
  Thyra::put_scalar<ST>(0.0, disp_diff_rcp.ptr());
  Thyra::V_VpStV(disp_diff_rcp.ptr(), *this_x_[subdomain], -1.0, *prev_x_[subdomain]);

  Teuchos::RCP<Thyra_Vector> velo_diff_rcp = Thyra::createMember(me.get_x_space());
  Thyra::put_scalar<ST>(0.0, velo_diff_rcp.ptr());
  Thyra::V_VpStV(velo_diff_rcp.ptr(), *this_xdot_[subdomain], -1.0, *prev_xdot_[subdomain]);

  Teuchos::RCP<Thyra_Vector> acce_diff_rcp = Thyra::createMember(me.get_x_space());
  Thyra::put_scalar<ST>(0.0, acce_diff_rcp.ptr());
  Thyra::V_VpStV(acce_diff_rcp.ptr(), *this_xdotdot_[subdomain], -1.0, *prev_xdotdot_[subdomain]);

  // After solve, save solution and get info to check convergence
  // IKT 6/5/2020: the following came from Schwarz and is irrelevant here for now,
  // so I am commenting it out 
  /*norms_init(subdomain)  = Thyra::norm(*prev_x_[subdomain]);
  norms_final(subdomain) = Thyra::norm(*this_x_[subdomain]);
  norms_diff(subdomain)  = Thyra::norm(*disp_diff_rcp);

  auto const dt = tol_factor_vel_;

  norms_init(subdomain) += dt * Thyra::norm(*prev_xdot_[subdomain]);
  norms_final(subdomain) += dt * Thyra::norm(*this_xdot_[subdomain]);
  norms_diff(subdomain) += dt * Thyra::norm(*velo_diff_rcp);

  auto const dt2 = tol_factor_acc_;

  norms_init(subdomain) += dt2 * Thyra::norm(*prev_xdotdot_[subdomain]);
  norms_final(subdomain) += dt2 * Thyra::norm(*this_xdotdot_[subdomain]);
  norms_diff(subdomain) += dt2 * Thyra::norm(*acce_diff_rcp);*/

  return false; 
}

void
ACEThermoMechanical::setExplicitUpdateInitialGuessForSchwarz(ST const current_time, ST const time_step) const
{
  // do an explicit update to form the initial guess for the schwarz
  // iteration
  for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
    auto& app = *apps_[subdomain];

    Thyra_Vector& ic_disp = *ics_x_[subdomain];
    Thyra_Vector& ic_velo = *ics_xdot_[subdomain];
    Thyra_Vector& ic_acce = *ics_xdotdot_[subdomain];

    auto& me = dynamic_cast<Albany::ModelEvaluator&>(*model_evaluators_[subdomain]);
    if (current_time == 0) {
      this_x_[subdomain] = Thyra::createMember(me.get_x_space());
      this_xdot_[subdomain] = Thyra::createMember(me.get_x_space());
      this_xdotdot_[subdomain] = Thyra::createMember(me.get_x_space());
    }

    const ST aConst = time_step * time_step / 2.0;
    Thyra::V_StVpStV(this_x_[subdomain].ptr(), time_step, ic_velo, aConst, ic_acce);
    Thyra::Vp_V(this_x_[subdomain].ptr(), ic_disp, 1.0);

    // This is the initial guess that I want to apply to the subdomains before
    // the schwarz solver starts
    auto disp_rcp = this_x_[subdomain];
    auto velo_rcp = this_xdot_[subdomain];
    auto acce_rcp = this_xdotdot_[subdomain];

    // setting the displacement in the albany application
    app.setX(disp_rcp);
    app.setXdot(velo_rcp);
    app.setXdotdot(acce_rcp);

    // in order to get the Schwarz boundary conditions right, we need to set the
    // state in the discretization
    Teuchos::RCP<Albany::AbstractDiscretization> const& app_disc = app.getDiscretization();

    app_disc->writeSolutionToMeshDatabase(*disp_rcp, *velo_rcp, *acce_rcp, current_time);
  }
}

void
ACEThermoMechanical::setDynamicICVecsAndDoOutput(ST const time) const
{
  bool is_initial_time = false;

  if (time == initial_time_) is_initial_time = true;

  for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {

    const PROB_TYPE prob_type = prob_types_[subdomain]; 

    Albany::AbstractSTKMeshStruct& stk_mesh_struct = *stk_mesh_structs_[subdomain];

    Albany::AbstractDiscretization& abs_disc = *discs_[subdomain];

    Albany::STKDiscretization& stk_disc = static_cast<Albany::STKDiscretization&>(abs_disc);

    stk_mesh_struct.exoOutputInterval = 1;
    stk_mesh_struct.exoOutput         = do_outputs_[subdomain];

    if (is_initial_time == true) {  // initial time-step: get initial solution
                                    // from nominalValues in ME

      auto& me = dynamic_cast<Albany::ModelEvaluator&>(*model_evaluators_[subdomain]);

      auto const& nv = me.getNominalValues();

      ics_x_[subdomain] = Thyra::createMember(me.get_x_space());
      Thyra::copy(*(nv.get_x()), ics_x_[subdomain].ptr());

      ics_xdot_[subdomain] = Thyra::createMember(me.get_x_space());
      Thyra::copy(*(nv.get_x_dot()), ics_xdot_[subdomain].ptr());

      if (prob_type == MECHANICS) {
        ics_xdotdot_[subdomain] = Thyra::createMember(me.get_x_space());
        Thyra::copy(*(nv.get_x_dot_dot()), ics_xdotdot_[subdomain].ptr());
      }

      // Write initial condition to STK mesh
      Teuchos::RCP<Thyra_MultiVector const> const xMV = apps_[subdomain]->getAdaptSolMgr()->getOverlappedSolution();

      stk_disc.writeSolutionMV(*xMV, initial_time_, true);

    }

    else {  // subsequent time steps: update ic vecs based on fields in stk
            // discretization

      Teuchos::RCP<Thyra_MultiVector> disp_mv = stk_disc.getSolutionMV();

      // Update ics_x_ and its time-derivatives
      ics_x_[subdomain] = Thyra::createMember(disp_mv->col(0)->space());
      Thyra::copy(*disp_mv->col(0), ics_x_[subdomain].ptr());

      ics_xdot_[subdomain] = Thyra::createMember(disp_mv->col(1)->space());
      Thyra::copy(*disp_mv->col(1), ics_xdot_[subdomain].ptr());

      if (prob_type == MECHANICS) {
        ics_xdotdot_[subdomain] = Thyra::createMember(disp_mv->col(2)->space());
        Thyra::copy(*disp_mv->col(2), ics_xdotdot_[subdomain].ptr());
      }

      if (do_outputs_[subdomain] == true) {  // write solution to Exodus

        stk_disc.writeSolutionMV(*disp_mv, time);
      }
    }

    stk_mesh_struct.exoOutput = false;
  }
}

void
ACEThermoMechanical::doQuasistaticOutput(ST const time) const
{
  for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
    if (do_outputs_[subdomain] == true) {
      auto& stk_mesh_struct = *stk_mesh_structs_[subdomain];

      stk_mesh_struct.exoOutputInterval = 1;
      stk_mesh_struct.exoOutput         = true;

      auto& abs_disc = *discs_[subdomain];
      auto& stk_disc = static_cast<Albany::STKDiscretization&>(abs_disc);

      // Do not dereference this RCP. Leads to SEGFAULT (!?)
      auto disp_mv_rcp = stk_disc.getSolutionMV();
      stk_disc.writeSolutionMV(*disp_mv_rcp, time);
      stk_mesh_struct.exoOutput = false;
    }
  }
}

// Sequential ThermoMechanical coupling loop, quasistatic
void
ACEThermoMechanical::ThermoMechanicalLoopQuasistatics() const
{
  //IKT FIXME 6/4/2020: the code below that is commented out is cut/paste
  //from Schwarz_Alternating.cpp.  I'm not sure if we want to support 
  //quasistatic ACE sequential thermo-mechanical coupling.  If we do,
  //the code below needs to be uncommented and modified for this case.  
  //If not, this routine should be removed.  For now, I am throwing 
  //an error if the user tries to run the code quasi-statically. 
  
  
  /*minitensor::Filler const ZEROS{minitensor::Filler::ZEROS};
  minitensor::Vector<ST>   norms_init(num_subdomains_, ZEROS);
  minitensor::Vector<ST>   norms_final(num_subdomains_, ZEROS);
  minitensor::Vector<ST>   norms_diff(num_subdomains_, ZEROS);

  std::string const delim(72, '=');

  *fos_ << std::scientific << std::setprecision(17);

  ST  time_step{initial_time_step_};
  int stop{0};
  ST  current_time{initial_time_};

  // Output initial configuration.
  doQuasistaticOutput(current_time);

  // Continuation loop. We do continuation manually for Schwarz instead of
  // using LOCA. It turned out to be too complicated to use LOCA to sync
  // continuation between different subdomains.
  while (stop < maximum_steps_ && current_time < final_time_) {
    ST const next_time{current_time + time_step};

    *fos_ << delim << std::endl;
    *fos_ << "Global time stop   :" << stop << '\n';
    *fos_ << "Start time         :" << current_time << '\n';
    *fos_ << "Stop time          :" << next_time << '\n';
    *fos_ << "Time step          :" << time_step << '\n';
    *fos_ << delim << std::endl;

    // This object is necessary to be able to set an initial solution
    // for the model evaluator to a desired value. This is used to save
    // previous values of the solution at each Schwarz iteration for
    // each subdomain.
    Thyra::ModelEvaluatorBase::InArgsSetup<ST> nv;
    nv.setModelEvalDescription(this->description());
    nv.setSupports(Thyra_ModelEvaluator::IN_ARG_x, true);

    // Before the Schwarz loop, save the solutions for each subdomain in case
    // the solve fails. Then the load step is reduced and the Schwarz
    // loop is restarted from scratch.
    for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
      // Set these initial values explicitly to zero so that no
      // extra logic is necessary for initial values in the
      // Schwarz and subdomain loops.
      if (stop == 0) {
        auto& me = dynamic_cast<Albany::ModelEvaluator&>(*model_evaluators_[subdomain]);

        auto zero_disp_rcp = Thyra::createMember(me.get_x_space());
        auto zero_disp_ptr = zero_disp_rcp.ptr();

        Thyra::put_scalar<ST>(0.0, zero_disp_ptr);

        prev_step_x_[subdomain] = zero_disp_rcp;
        curr_x_[subdomain]      = zero_disp_rcp;
      } else {
        prev_step_x_[subdomain] = curr_x_[subdomain];
      }

      auto& app       = *apps_[subdomain];
      auto& state_mgr = app.getStateMgr();
      fromTo(state_mgr.getStateArrays(), internal_states_[subdomain]);
    }

    num_iter_ = 0;

    // Schwarz loop
    do {
      // Subdomain loop
      for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
        *fos_ << delim << std::endl;
        *fos_ << "Schwarz iteration  :" << num_iter_ << '\n';
        *fos_ << "Subdomain          :" << subdomain << '\n';
        *fos_ << "Start time         :" << current_time << '\n';
        *fos_ << "Stop time          :" << next_time << '\n';
        *fos_ << "Time step          :" << time_step << '\n';
        *fos_ << delim << std::endl;

        // Save solution from previous Schwarz iteration before solve
        auto& me = dynamic_cast<Albany::ModelEvaluator&>(*model_evaluators_[subdomain]);

        auto        prev_x_rcp = curr_x_[subdomain];
        auto const& prev_disp     = *prev_x_rcp;

        // Restore internal states
        auto& app       = *apps_[subdomain];
        auto& state_mgr = app.getStateMgr();
        fromTo(internal_states_[subdomain], state_mgr.getStateArrays());

        // Restore solution from previous time step
        auto prev_step_x_rcp = prev_step_x_[subdomain];

        nv.set_x(prev_x_rcp);
        me.setNominalValues(nv);

        // Target time
        me.setCurrentTime(next_time);

        // Solve for each subdomain
        auto& solver          = *(solvers_[subdomain]);
        auto& piro_nox_solver = dynamic_cast<Piro::NOXSolver<ST>&>(solver);
        auto  in_args         = solver.createInArgs();
        auto  out_args        = solver.createOutArgs();

        solver.evalModel(in_args, out_args);

        // Check whether solver did OK.
        auto const& thyra_nox_solver = *piro_nox_solver.getSolver();
        auto const& const_nox_solver = *thyra_nox_solver.getNOXSolver();
        auto&       nox_solver       = const_cast<NOX::Solver::Generic&>(const_nox_solver);
        auto const  status           = nox_solver.getStatus();

        if (status == NOX::StatusTest::Failed) {
          *fos_ << "\nINFO: Unable to solve for subdomain " << subdomain << '\n';
          failed_ = true;
          // Break out of the subdomain loop
          break;
        }

        // Solver OK, extract solution
        auto        curr_x_rcp = thyra_nox_solver.get_current_x()->clone_v();
        auto const& curr_x     = *curr_x_rcp;

        // Compute difference between previous and current solutions
        auto disp_diff_rcp = Thyra::createMember(me.get_x_space());
        auto disp_diff_ptr = disp_diff_rcp.ptr();

        Thyra::put_scalar<ST>(0.0, disp_diff_ptr);
        Thyra::V_VpStV(disp_diff_ptr, curr_x, -1.0, prev_disp);

        auto& disp_diff = *disp_diff_rcp;

        // After solve, save solution and get info to check convergence
        curr_x_[subdomain]  = curr_x_rcp;
        norms_init(subdomain)  = Thyra::norm(prev_disp);
        norms_final(subdomain) = Thyra::norm(curr_x);
        norms_diff(subdomain)  = Thyra::norm(disp_diff);

      }  // Subdomain loop

      if (failed_ == true) {
        *fos_ << "INFO: Unable to continue Schwarz iteration " << num_iter_;
        *fos_ << "\n";
        // Break out of the Schwarz loop.
        break;
      }

      norm_init_  = minitensor::norm(norms_init);
      norm_final_ = minitensor::norm(norms_final);
      norm_diff_  = minitensor::norm(norms_diff);

      updateConvergenceCriterion();

      *fos_ << delim << std::endl;
      *fos_ << "Schwarz iteration         :" << num_iter_ << '\n';

      std::string const line(72, '-');

      *fos_ << line << std::endl;

      *fos_ << centered("Sub", 6);
      *fos_ << centered("Initial norm", 22);
      *fos_ << centered("Final norm", 22);
      *fos_ << centered("Difference norm", 22);
      *fos_ << std::endl;
      *fos_ << centered("dom", 6);
      *fos_ << centered("||X0||", 22);
      *fos_ << centered("||Xf||", 22);
      *fos_ << centered("||Xf-X0||", 22);
      *fos_ << std::endl;
      *fos_ << line << std::endl;

      for (auto m = 0; m < num_subdomains_; ++m) {
        *fos_ << std::setw(6) << m;
        *fos_ << std::setw(22) << norms_init(m);
        *fos_ << std::setw(22) << norms_final(m);
        *fos_ << std::setw(22) << norms_diff(m);
        *fos_ << std::endl;
      }

      *fos_ << line << std::endl;
      *fos_ << centered("Norm", 6);
      *fos_ << std::setw(22) << norm_init_;
      *fos_ << std::setw(22) << norm_final_;
      *fos_ << std::setw(22) << norm_diff_;
      *fos_ << std::endl;
      *fos_ << line << std::endl;
      *fos_ << "Absolute error     :" << abs_error_ << '\n';
      *fos_ << "Absolute tolerance :" << abs_tol_ << '\n';
      *fos_ << "Relative error     :" << rel_error_ << '\n';
      *fos_ << "Relative tolerance :" << rel_tol_ << '\n';
      *fos_ << delim << std::endl;

    } while (continueSolve() == true);  // Schwarz loop

    // One or more of the subdomains failed to solve. Reduce step.
    if (failed_ == true) {
      failed_ = false;

      auto const reduced_step = reduction_factor_ * time_step;

      if (time_step <= min_time_step_) {
        *fos_ << "ERROR: Cannot reduce step. Stopping execution.\n";
        *fos_ << "INFO: Requested step    :" << reduced_step << '\n';
        *fos_ << "INFO: Minimum time step :" << min_time_step_ << '\n';
        return;
      }

      if (reduced_step > min_time_step_) {
        *fos_ << "INFO: Reducing step from " << time_step << " to ";
        *fos_ << reduced_step << '\n';
        time_step = reduced_step;
      } else {
        *fos_ << "INFO: Reducing step from " << time_step << " to ";
        *fos_ << min_time_step_ << '\n';
        time_step = min_time_step_;
      }

      // Restore previous solutions
      for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
        curr_x_[subdomain] = prev_step_x_[subdomain];

        // Restore the state manager with the state variables from the previous
        // load step.
        auto& app = *apps_[subdomain];

        auto& state_mgr = app.getStateMgr();

        fromTo(internal_states_[subdomain], state_mgr.getStateArrays());

        // Restore the solution in the discretization so the schwarz solver gets
        // the right boundary conditions!
        Teuchos::RCP<Thyra_Vector const>                    disp_rcp_thyra = curr_x_[subdomain];
        Teuchos::RCP<Albany::AbstractDiscretization> const& app_disc       = app.getDiscretization();

        app_disc->writeSolutionToMeshDatabase(*disp_rcp_thyra, current_time);
      }

      // Jump to the beginning of the continuation loop without advancing
      // time to try to use a reduced step.
      continue;
    }

    reportFinals(*fos_);

    // Output converged solution if at specified interval

    for (auto subdomain = 0; subdomain < num_subdomains_; ++subdomain) {
      if (do_outputs_init_[subdomain] == true) {
        do_outputs_[subdomain] = output_interval_ > 0 ? (stop + 1) % output_interval_ == 0 : false;
      }
    }

    doQuasistaticOutput(next_time);

    ++stop;
    current_time += time_step;

    // Step successful. Try to increase the time step.
    auto const increased_step = std::min(max_time_step_, increase_factor_ * time_step);

    if (increased_step > time_step) {
      *fos_ << "\nINFO: Increasing step from " << time_step << " to ";
      *fos_ << increased_step << '\n';
      time_step = increased_step;
    } else {
      *fos_ << "\nINFO: Cannot increase step. Using " << time_step << '\n';
    }

  }  // Continuation loop */
}

}  // namespace LCM
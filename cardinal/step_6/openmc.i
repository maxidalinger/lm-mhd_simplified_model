Re               = 103.87303                      # Reynolds Number [-]
rho              = 9838.0092                      # Density [kg/m3]
mu               = 2.10838375e-3                  # Viscosity [Pa*s]
Dh               = 9.6750e-02                     # Hydraulic diameter [m]
U                = ${fparse Re * mu / (rho * Dh)} # Velocity [m/s]
dt_nek           = 1.0e-4                         # NekRS non-dimensional time step [-]
t_nek            = ${fparse dt_nek * Dh / U}      # NekRS dimensional time step [s]
num_steps_nekrs  = 2000                          # Number of steps in NekRS [-]
num_steps_openmc = 1                              # Number of steps in OpenMC [-]

[Mesh]
  [file]
    type = FileMeshGenerator
    file = lm-mhd_nek_mesh.exo # Mesh in [m]
  []
  allow_renumbering = false
[]

[ICs]
  [temp]
    type = ConstantIC
    variable = temp
    value = 293.0 #[K]
  []
[]

[AuxVariables]
  [cell_temperature]
    family = MONOMIAL
    order = CONSTANT
  []
[]

[AuxKernels]
  [cell_temperature]
    type = CellTemperatureAux
    variable = cell_temperature
  []
[]

[Problem]
  type = OpenMCCellAverageProblem
  verbose = false
  
  source_strength = 1.069e15 # [neutrons/second] = (2.2*10^13 [neutrons/sec/cm^2]) * ((11.175 [cm] * 2) * (10.875 [cm] * 2))
  scaling = 100.0            # Moose mesh in [m]
  cell_level = 0
  temperature_blocks = '0 1'
  
  [Tallies]
    [flux]
      type = MeshTally
      name = "heating_local tritium"
      score = "heating_local H3_production"
      estimator = collision
      mesh_template = lm-mhd_nek_mesh.exo
      output = 'unrelaxed_tally_std_dev'
      tally_sum_tol = 1e-3
      check_tally_sum = false
    []
  []
  
  batches = 100
  particles = 1000000
  skip_statepoint = true
[]

[MultiApps]
  [nek]
    type = TransientMultiApp
    app_type = CardinalApp
    input_files = 'nek.i'
    sub_cycling = true
    execute_on = timestep_end
  []
[]

[Transfers]
  # TRANSFERS FROM OPENMC TO NEKRS
  [power_to_nek]
    #type = MultiAppGeneralFieldNearestLocationTransfer
    #type = MultiAppGeneralFieldShapeEvaluationTransfer
    type = MultiAppProjectionTransfer
    to_multi_app = nek
    source_variable = heating_local
    variable = heat_source
    fixed_meshes = true
  []
  [power_integral_to_nek]
    type = MultiAppPostprocessorTransfer
    to_multi_app = nek
    from_postprocessor = heating_local_integral
    to_postprocessor = heat_source_integral
  []
  [tritium_to_nek]
    type = MultiAppProjectionTransfer 
    to_multi_app = nek
    source_variable = tritium
    variable = tritium_source
    fixed_meshes = true
  []
  [tritium_integral_to_nek]
    type = MultiAppPostprocessorTransfer
    to_multi_app = nek
    from_postprocessor = tritium_integral
    to_postprocessor = tritium_source_integral
  []
  [synchronize_in]
    type = MultiAppPostprocessorTransfer
    to_multi_app = nek
    from_postprocessor = synchronize
    to_postprocessor = transfer_in
  []
[]

[Executioner]
  type = Transient
  dt = ${fparse num_steps_nekrs * t_nek}
  num_steps = ${num_steps_openmc}
[]

[Outputs]
  exodus = true
  time_step_interval = ${fparse num_steps_nekrs * num_steps_openmc}
  hide = 'heating_local_integral tritium_integral synchronize'
  execute_on = 'final'
[]

[Postprocessors]
  [heating_local_integral]
    type = ElementIntegralVariablePostprocessor
    variable = heating_local
  []
  [tritium_integral]
    type = ElementIntegralVariablePostprocessor
    variable = tritium
  []
  [synchronize]
    type = Receiver
    default = 1.0
  []
[]
# Input to run NekRS with Cardinal, in standalone mode.
# LM-MHD.
#
# To run in Frontier:
#   ./job_frontier [casename] <nodes> <hh:mm>
#-------------------------------------------------------------------------------
#U              = 0.00023008816                  # Reference velocity [m/s]
Re              = 103.87303                      # Reynolds Number [-]
rho             = 9838.0092                      # Density [kg/m3]
mu              = 2.10838375e-3                  # Viscosity [Pa*s]
Cp              = 189.7752                       # Heat Capacity [J/(kg*K)]
Dh              = 9.6750e-02                     # Hydraulic diameter [m]
Tin             = 0.0                            # Inlet temperature [K]
deltaT          = 1.0                            # Delta temperature inlet-outlet [K]
U               = ${fparse Re * mu / (rho * Dh)} # Velocity [m/s]
s02             = 0.0                            # Delta tritium factor [1/m^3]
ds02            = 1.0e16                         # Tritium scaling factor [1/m^3]
num_steps_nekrs = 2000                          # Number of steps in NekRS [-]

[Mesh]
  type = NekRSMesh
  volume = true
  order = FIRST
  scaling = ${Dh}
[]

[Problem]
  type = NekRSProblem
  casename = 'bz'
  synchronization_interval = parent_app
  n_usrwrk_slots = 3
  
  [Dimensionalize]
    L    = ${Dh}     # [m]
    U    = ${U}      # [m/s]
    T    = ${Tin}    # [K]
    dT   = ${deltaT} # [K]
    rho  = ${rho}    # [kg/m3]
    Cp   = ${Cp}     # [J/(kg*K)]
    s02  = ${s02}    # [1/m^3]
    ds02 = ${ds02}   # [1/m^3]
  []
  
  [FieldTransfers]
    [heat_source]
      type = NekVolumetricSource
      direction = to_nek
      usrwrk_slot = 0
      postprocessor_to_conserve = heat_source_integral
      normalization_abs_tol = 1e3
      normalization_rel_tol = 1e-3
    []
    [tritium_source]
      type = NekVolumetricSource
      direction = to_nek
      usrwrk_slot = 2
      postprocessor_to_conserve = tritium_source_integral
      normalization_abs_tol = 1e3
      normalization_rel_tol = 1e-3
    []
  
    [velocity]
      type = NekFieldVariable
      direction = from_nek
    []
    [temperature]
      type = NekFieldVariable
      direction = from_nek
    []
    [tritium]
      type = NekFieldVariable
      direction = from_nek
      field = scalar02
    []
  []
[]

[Executioner]
  type = Transient
  [TimeStepper]
    type = NekTimeStepper
  []
[]

[Outputs]
  csv = true
  time_step_interval = ${num_steps_nekrs}
  [exodus]
    type = Exodus
    hide = 'heat_source_integral tritium_source_integral'
    execute_on = 'final'
  []
[]
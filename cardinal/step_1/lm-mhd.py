import openmc
from pathlib import Path

source_energy_ev = 14.1e6
density_scale_factor = 1.0
dz = 1.0e-6

#################################################################################
# 1. MATERIALES
materials_map = {'pbli': "Pb157Li90",
                'water_lower': "Water",
                'water_upper': "Water",
                'pipewall_lower': "SS316LNIG",
                'pipewall_upper': "SS316LNIG",
                'baffle': "SS316LNIG",
                'enclosure': "SS316LNIG"}

# Load materiales from xml file
materials_source_file = Path("PureFusionMaterials_openmcAtomfrac.xml").resolve()
library_materials = openmc.Materials.from_xml(materials_source_file)
library_by_name = {material.name: material for material in library_materials}

# Generate materials list
materials_list = []
for key in materials_map:
    material = library_by_name[materials_map[key]].clone()
    material.set_density(material.density_units, material.density * density_scale_factor)
    material.name = key
    materials_list.append(material)

# Export materials
materials = openmc.Materials(materials_list)
materials.export_to_xml()

#################################################################################
# 2. GEOMETRY
# Load DAGMC geometry (cm)
dagmc_univ = openmc.DAGMCUniverse('lm-mhd_dagmc.h5m',
                                  auto_geom_ids = True)

# Build bounding box with boundary conditions
min_pt, max_pt = dagmc_univ.bounding_box
margin = 1e-2
xmin = openmc.XPlane(x0 = min_pt[0] - margin, boundary_type = 'vacuum')
xmax = openmc.XPlane(x0 = max_pt[0] + margin, boundary_type = 'vacuum')
ymin = openmc.YPlane(y0 = min_pt[1] - margin, boundary_type = 'vacuum')
ymax = openmc.YPlane(y0 = max_pt[1] + margin, boundary_type = 'vacuum')
zmin = openmc.ZPlane(z0 = min_pt[2] - margin, boundary_type = 'vacuum')
zmax = openmc.ZPlane(z0 = max_pt[2] + margin, boundary_type = 'vacuum')
region = +xmin & -xmax & +ymin & -ymax & +zmin & -zmax
cell = openmc.Cell(cell_id = 1_000_000, fill = dagmc_univ, region = region)

# Export geometry
geometry = openmc.Geometry([cell])
geometry.export_to_xml()

#################################################################################
# 3. SETTINGS and SOURCE
settings = openmc.Settings()
settings.run_mode = 'fixed source'
settings.batches = 500
settings.particles = 10_000
settings.max_history_splits = 1000

# Source
# It will be located on z = 0, with y [200, 800], x [-250, 250].
space_dist  = openmc.stats.CartesianIndependent(x = openmc.stats.Discrete(44.5, 44.5 + dz),
                                                y = openmc.stats.Uniform(-11.175, 11.175),
                                                z = openmc.stats.Uniform(-10.875, 10.875))
energy_dist = openmc.stats.Discrete([source_energy_ev], [1.0])
angle_dist  = openmc.stats.Monodirectional(reference_uvw = (-1.0, 0.0, 0.0))
source = openmc.IndependentSource(particle = 'neutron', 
                                  space = space_dist, 
                                  energy = energy_dist,
                                  angle = angle_dist)
settings.source = source

# Export settings
settings.export_to_xml()

##################################################################################
## 4. TALLIES
#tallies = openmc.Tallies()
#
## Define Mesh Filter (m)
#filename = "lm-mhd_mesh.exo" 
#mesh = openmc.UnstructuredMesh(filename=filename, library="libmesh", length_multiplier=100.0)
#mesh_filter = openmc.MeshFilter(mesh)
#
## Generate Tally
#tally = openmc.Tally(name = "tally_1", tally_id = 1)
#tally.filters = [mesh_filter]
#tally.scores = ["flux", "heating"]
#tally.estimator = "collision"
#tallies.append(tally)
#
## Export Tally
#tallies.export_to_xml()
#
###################################################################################
## 5. RUN
#openmc.run()
#
###################################################################################
## 6. RESULTS
## Load tally
#sp = openmc.StatePoint('statepoint.{}.h5'.format(settings.batches))
#tally = sp.get_tally(name = "tally_1")
#
## Separate tallies
#tally_heat = tally.get_slice(scores = ['heating'])
#tally_flux = tally.get_slice(scores = ['flux'])
#
## Extract mesh
#mesh_filter = tally.find_filter(openmc.MeshFilter)
#mesh = mesh_filter.mesh
#
## Write VTK file
#mesh.write_data_to_vtk(filename = 'openmc_results.vtk',
#                       datasets = {'Heating': tally_heat.mean.flatten(),
#                                   'Flux':    tally_flux.mean.flatten()})
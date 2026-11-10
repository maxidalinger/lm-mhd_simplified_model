#ifndef PINK_HPP
#define PINK_HPP

#ifndef __okl__
//
// pink.hpp -- the machinery. Self-contained: it declares the types and takes the case data
// as a PinkCase argument, so it may be included before the .udf's tables.
// See plans/G_split_hpp.md (the split) and plans/H_case_struct.md (PinkCase).
//
// Two halves, one per udfMake target. The host half compiles into `udf`; the
// #ifdef __okl__ half at the bottom compiles into `OKL`, the only target with __okl__
// defined. Each half is guarded, so either include position works for either target.
//
// Switches must be DEFINED by the udf before this include, in both targets: `#if X` treats
// an undefined X as 0, so a typo would silently disable physics instead of failing.
#ifndef RECYCLE_PHI_ON
#  error "the .udf must #define RECYCLE_PHI_ON (USE_RECYCLE && USE_MHD && USE_RECYCLE_PHI)"
#endif
#if USE_JOULE && !USE_MHD
#  error "USE_JOULE needs USE_MHD -- there is no current density to dissipate"
#endif
#if USE_ISO_IMPLICIT && !USE_ISOTOPES
#  error "USE_ISO_IMPLICIT needs USE_ISOTOPES -- there is no decay term to make implicit"
#endif


// ================================================================================================================
// TYPES -- the shapes the .udf fills in. Instances (the actual numbers) live in the .udf.

enum DomainKind { FLUID, SOLID };

// One row per eTag, in SI. Straight from README_scales.md; scale::derive_s2() does the rest.
struct S1Domain {
  const char *name;       //          domain label, used in every chk- print
  DomainKind  kind;       //          FLUID or SOLID
  dfloat rho;             // [kg/m3]  mass density
  dfloat nu;              // [m2/s]   kinematic viscosity      (0 for solids)
  dfloat Cp;              // [J/kg-K] specific heat
  dfloat k;               // [W/m-K]  thermal conductivity
  dfloat sigma;           // [S/m]    electrical conductivity
  dfloat beta;            // [1/K]    thermal expansion        (0 for solids)
  dfloat T0;              // [C]      initial / reference temperature
  int    bid_outlet;      //          outlet boundary id, -1 if none (solids)
};

// The reference scales. s1_L ALSO rescales the mesh: the usr does 1/s1_L on xm1.
struct S1Units {
  int    dm_id;           //          eTag of the reference domain (PbLi)
  dfloat L;               // [m]      length scale, the PbLi inlet diameter
  dfloat Tr;              // [K]      temperature scale
  dfloat Tinf;            // [C]      temperature offset, T+ = (T - Tinf)/Tr
  dfloat g;               // [m/s2]   gravity
  dfloat B0;              // [T]      applied field; B is nondim by Br = B0, so by = 1
  dfloat mu0;             // [V-s/A-m] magnetic permeability
};

// One row per inlet, in SI. The speed is U when given, else derived as mdot/(rho A).
// D is the CHARACTERISTIC LENGTH (Re_local/Ha_local/N_local use it) and A is the CROSS-SECTION
// AREA mdot is divided by -- they are independent and both user-typed because a duct inlet
// (e.g. PbLi's 193.5x92.75mm rectangular BZ channel) has a characteristic length (the Hartmann
// half-height, used for Ha/Re/N) that does NOT correspond to a circular pipe of that same D.
// inletU() used to derive A = pi/4 D^2 (a true pipe), which silently mis-sized any duct inlet;
// derive_s2's chk-s1 cross-checks the typed A against that circular estimate and WARNS (not
// fatal -- a duct is expected to disagree) when they differ by more than a loose tolerance.
struct S1Inlet {
  const char *name;       //          inlet label
  int    bid;             //          velocity boundary id
  int    eTag;            //          owning domain, 1-based
  dfloat D;               // [m]      characteristic length, for Re_local/Ha_local/N_local
  dfloat A;               // [m2]     cross-section area mdot is divided by -- typed by hand,
                           //          NEVER derived from D (a duct's D is not a pipe diameter).
                           //          0 is fine if U is given directly (mdot/A unused then).
  dfloat mdot;            // [kg/s]   mass flow rate
  dfloat T;               // [C]      inlet temperature (the Dirichlet value at bid)
  bool   setsDomainU;     //          this inlet fixes its domain's s3 scale (exactly one each)
  dfloat U;               // [m/s]    inlet speed; 0 = derive from mdot/(rho A). LAST: a row may
                           //          omit it -- value-initialises to 0.0, i.e. "derive from mdot".
};

struct IsotopeSpecies {
  const char *name;       //          species label
  dfloat T_half;          // [s]      half-life; lambda = ln2/T_half * L/U
  dfloat n_ref;           // [1/m3]   concentration reference: n_hat = n_SI / n_ref.
                          //          PER SPECIES: the steady state is production x residence, and
                          //          N-16 is decay-limited (1/lambda = 10.3 s) while tritium is
                          //          flow-limited -- 7 decades apart for the same source (plan Z).
                          //          Omitting it in a row value-initialises to 0: fatal in
                          //          scale::check, since both fields are dfloat and the compiler
                          //          cannot see a short row.
};

// Is a species' outlet fed back to its own inlets? There is NO default: a domain with an outlet
// must state one, so every loop in the case is explicit where it is declared (plan V).
// LOOP_UNSET is what an omitted table column value-initialises to, and it is fatal.
enum LoopMode : int { LOOP_UNSET = 0, LOOP_CLOSED = 1, LOOP_OPEN = 2 };

inline const char *loopName(int m)
{
  return (m == LOOP_CLOSED) ? "CLOSED" : (m == LOOP_OPEN) ? "OPEN" : "n/a";
}

// Per eTag, same order as s1_domain. See PHYSICS.md sec 4 / notes/08.
// One species in one domain. Tables of these are indexed [species][eTag].
struct IsotopeParams {
  dfloat D;      // [m2/s] in the input table; scale::setup() makes it 1/Pe_c ; /U in s3
  dfloat delay;  // production yield [-]    ; never rescaled
  dfloat lambda; // decay constant, POSITIVE; the sink is -lambda*c. Set by scale::setup()
  int    loop;   // LoopMode. LAST: an omitted column is LOOP_UNSET, which scale::check makes
                 // fatal wherever bid_outlet > 0. int, so a mis-ordered dfloat row fails to
                 // compile (double -> int is narrowing) instead of shifting silently.
};

// Which fields setup_ic re-initializes on a RESTART. A fresh start always sets everything.
// [CASEDATA] resetFields = velocity,temperature,phi,isotopes | all | none
enum ResetField : unsigned {
  RESET_VELOCITY    = 1u << 0,
  RESET_TEMPERATURE = 1u << 1,
  RESET_PHI         = 1u << 2,
  RESET_ISOTOPES    = 1u << 3,
  RESET_ALL         = 0xfu,
};

// ---- derived, filled by scale::derive_s2() ------------------------------------------------------------------
// mue/conT are viscosity/conductivity (already inverted from Re/Pe).
struct Domain {
  const char *name;
  DomainKind  kind;
  dfloat rho, mue, rhoCp, conT, conE, temp0, RiRhoBeta;
  int    bid_outlet;
  dfloat Uscale;          // s3 divisor for this domain (1 in s2), set by scale::setup()
  dfloat jouleCoeff;      // (sigma_r/sigma)*N_r*Ec_r, the q_M''' coefficient (eq.pdf eq.14)
};

struct Inlet {
  const char *name;
  int    bid;
  int    eTag;
  dfloat U_inlet;         // s2, = mdot/(rho A) / U_r
  dfloat T_inlet;         // s2, = (T - Tinf)/Tr
  bool   setsDomainU;
};

// ---- scale control ---------------------------------------------------------------------------------------------
// Scales (notes/07_scale_formulation.md):
//   s1  SI standard unit.
//   s2  nondimensional units based on PbLi. The target scale. all I/O (ckpt, ascent) is s2.
//   s3  s2 + per-fluid velocity rescale so U_inlet = O(1), for a bigger dt, initial transient.
// Only u and p are rescaled. phi, sigma, T, k, J and isotope concentrations stay s2:
// The solid is never rescaled

enum class ScaleMode { S2, S3 };

struct ScaleConfig {
  ScaleMode mode = ScaleMode::S3;
  dfloat U_ref = 1.0;        // conducting fluid (PbLi), sets the global MHD coeffs
  dfloat lorentzCoeff = 1.0; // global, on the reference fluid. = N_r (plan 2A); the
                             // per-fluid U is o_ulocScale's job, not this one.
};

// ================================================================================================================
// THE CASE -- every table the .udf owns, plus what is derived from it. Passed to each setup
// function instead of being reached for as a global, so this header depends on no name the
// .udf happens to have defined first. Construct it once in UDF_Setup0:
//
//   PinkCase cs{s1_domain, s1_inlet, s2_species, s1_unit, s2_isotope};
//
// Aggregate init, so the order below is the argument order. Everything after s2_isotope is
// derived and starts empty.
struct PinkCase {
  // ---- input, owned by the .udf (references: the .udf keeps them alive and const) ----
  const std::vector<S1Domain>       &s1_domain;
  const std::vector<S1Inlet>        &s1_inlet;
  const std::vector<IsotopeSpecies> &s2_species;
  const S1Units                     &s1_unit;
  const std::vector<std::vector<IsotopeParams>> &s2_isotope; // [species][eTag]

  // ---- heat model, in SI. Set by the .udf, the scales are derived in scale::derive_s2(). ----
  dfloat s1_qflux = 0.0;     // [W/m2] plasma surface flux on the FW
  dfloat s1_I0    = 0.0;     // [W/m2] incident neutron intensity at the FW
  dfloat p_recycLength = 0.0;         // [m]  recycling plane offset from the inlet
  dfloat p_iso_transit = 0.0;         // [-]  outlet->inlet transit, in L/U_r
  dfloat p_iso_transportCoeff = 1.0;
  unsigned p_resetFields = 0;         // which fields setup_ic re-inits on a restart

  // ---- s2, derived by scale::derive_s2() ----
  dfloat U_r = 0.0;           // [m/s] reference speed = the reference domain's inlet speed
  dfloat N_r = 0.0;           // interaction parameter on the reference domain; scale::setup
                              // puts it on lorentzCoeff (plan 2A)
  std::vector<Domain> s2_domain;
  std::vector<Inlet>  s2_inlet;
  dfloat p_qflux_scale = 0.0; // rho.Cp.U.Tr    [W/m2]
  dfloat p_qvol_scale  = 0.0; // rho.Cp.Tr.U/L  [W/m3]
  dfloat p_qflux       = 0.0; // s1_qflux / p_qflux_scale
  dfloat p_I0          = 0.0; // = s1_I0, used with p_qvol_scale in the qvol profile

  // ---- working set: the s2 tables in the active scale. Filled by scale::setup/scale::apply.
  std::vector<Domain>        domain;    // per eTag
  std::vector<std::vector<IsotopeParams>> isotope; // [species][eTag]
  std::vector<dfloat>        p_U_inlet; // per inlet
  ScaleConfig                scaleCfg;  // the per-domain s3 divisor lives in domain[].Uscale
};

deviceMemory<dfloat> o_U_star;     // Uscale per eTag, device copy
deviceMemory<dfloat> o_U_star_inv; // 1/Uscale per eTag

// runtime control
static int debug_setup = 0; // pass to usr to dump files and inspect stuff
static int hScale = 1; // h refinement scaling factor for eTag
static int ioStepAscent = 0;
static int ioStepCheckpoint = -1; // manually control checkpoint freq

// per-inlet flow-rate multiplier for easier start. scale Re,Pe with it and N,Ri against it (chk-Re Re_modify).
std::vector<dfloat> p_inletFlowFactor; // sized from s1_inlet in UDF_Setup0, never a hardcoded count
std::vector<dfloat> p_visc_scale = {1.0, 1.0, 1.0}; // per fluid domain: Re_modify = Re_local/visc_scale

// properties control
static int updateProperties = 1;
deviceMemory<int> o_eTag; // prepare for kernel
std::vector<int> eTag;
static int NumOfDomain = 0; // = s1_domain.size(); set in etag::setup, never a constant
static int NumOfIsotopes = 0;
int p_bc_usrwrk_nfld = 0;
int p_bc_usrwrk_size = 0; // = nfld * offset + isotopes (or other runtime var)

deviceMemory<dfloat> o_RiRhoBeta; // for buoyancy
deviceMemory<dfloat> o_tempBase; // for buoyancy
deviceMemory<dfloat> o_qvol; // for nertronic heat source
deviceMemory<dfloat> o_jouleCoeff; // per-eTag, for ohmic heating

std::vector<occa::memory> o_iso_negLambda; // -lambda: the coefficient of c in the source
std::vector<occa::memory> o_iso_delay;
std::vector<occa::memory> o_iso_src;      // per-species production, loaded [nondim]
std::vector<occa::memory> o_iso_srcScale; // its per-eTag coefficient: 1/Uscale
#if USE_ISO_IMPLICIT
std::vector<deviceMemory<dfloat>> o_iso_implicit; // lambda*transportCoeff, the lambda1 diagonal
std::vector<int> iso_scalarIdx;                   // scalar index of G(g+1), for the callback
#endif

// recycling
struct RecyclingData {
  int bID = -1;
  deviceMemory<int> o_bID;
  dfloat area = 0.0;
  std::vector<dfloat> normal; // outward
  planarCopy* recyc = nullptr;
};

deviceMemory<dfloat> o_planarWork;
std::vector<RecyclingData> rec; // one per inlet, same order as s2_inlet
#if RECYCLE_PHI_ON
RecyclingData recPhi;           // PbLi inlet, potential
#endif

// insitu
deviceMemory<dfloat> o_vel_mag;
deviceMemory<dfloat> o_cur_mag;

// ================================================================================================================
void print_timestamp(int tstep)
{
  time_t t= time(NULL);
  if (platform->comm.mpiRank() == 0) {
    printf("timestamp step= %d  UTC:       %s", tstep, asctime(gmtime(&t)));
    printf("timestamp step= %d  local:     %s", tstep, asctime(localtime(&t)));
    fflush(stdout);
  }
}

void print_vec(const char *tag, const char *name, const std::vector<dfloat> &vec)
{
  printf("%s %s= ", tag, name);
  for (const auto &v : vec) {
    printf("%.6e ", v);
  }
  printf("\n");
}


// ================================================================================================================
// PHYSICS BLOCKS -- one namespace per scattered physics. Each declares only the hooks it
// needs; the UDF entry points call them under a single #if. See PHYSICS.md.

// ================================================================================================================
namespace etag {

// Fill a nodal field with one value per eTag. Device-side: eTagFill is the gpu
// implementation this used to stage through the host for.
void setProp(mesh_t *mesh, occa::memory o_fld, const std::vector<dfloat> &perDomain)
{
  static deviceMemory<dfloat> o_coeff;
  o_coeff.resize(perDomain.size());
  o_coeff.copyFrom(perDomain.data(), perDomain.size());
  eTagFill(mesh->Nelements, o_eTag, o_coeff, o_fld);
}

// through host
void printMinMax(mesh_t *mesh,
                      const std::vector<int> &eTag,
                      occa::memory o_fld,
                      double time,
                      int tstep,
                      const std::string &name)
{
  std::vector<dfloat> tmin(NumOfDomain, std::numeric_limits<dfloat>::max());
  std::vector<dfloat> tmax(NumOfDomain, -std::numeric_limits<dfloat>::max());
  std::vector<long long int> count(NumOfDomain, 0); // an eTag absent from THIS mesh has 0
  std::vector<dfloat> work(mesh->Nlocal);

  o_fld.copyTo(work.data(), mesh->Nlocal);

  // eTag is sized for meshT. A field on meshV covers only the fluid prefix (notes/04 sec 3),
  // so the solid tags are never visited and must not be reported as a min/max.
  nekrsCheck(mesh->Nelements > static_cast<dlong>(eTag.size()),
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "printMinMax(%s): mesh has %d elements, eTag has %d -- not a meshT subset\n",
             name.c_str(), static_cast<int>(mesh->Nelements), static_cast<int>(eTag.size()));

  for (int e = 0; e < mesh->Nelements; ++e) {
    const int id = eTag[e] - 1; // convert 1-based tag -> 0-based index
    if (id < 0 || id >= NumOfDomain) {
      continue;
    }
    count[id] += mesh->Np;

    dfloat emin = tmin[id];
    dfloat emax = tmax[id];
    for (int i = 0; i < mesh->Np; ++i) {
      const int n = e * mesh->Np + i;
      emin = std::min(emin, work[n]);
      emax = std::max(emax, work[n]);
    }

    tmin[id] = emin;
    tmax[id] = emax;
  }

  MPI_Allreduce(MPI_IN_PLACE, tmin.data(), NumOfDomain, MPI_DFLOAT, MPI_MIN, platform->comm.mpiComm());
  MPI_Allreduce(MPI_IN_PLACE, tmax.data(), NumOfDomain, MPI_DFLOAT, MPI_MAX, platform->comm.mpiComm());
  MPI_Allreduce(MPI_IN_PLACE, count.data(), NumOfDomain, MPI_LONG_LONG, MPI_SUM,
                platform->comm.mpiComm());

  if (platform->comm.mpiRank() == 0) {

    dfloat gmin = std::numeric_limits<dfloat>::max();
    dfloat gmax = -std::numeric_limits<dfloat>::max();
    for (int id = 0; id < NumOfDomain; ++id) {
      // An eTag absent from THIS field's mesh (velocity on meshV never reaches a solid) would
      // otherwise report the accumulator's sentinel, +-1e308. Print 0: it is the physical value
      // there, and every column stays numeric for the post-processing scripts.
      const dfloat lo = count[id] ? tmin[id] : 0.0;
      const dfloat hi = count[id] ? tmax[id] : 0.0;
      printf("chk-eTagMinMax %s %.4e %d", name.c_str(), time, tstep);
      printf("  eTag=%2d  min=% .8e  max=% .8e\n", id + 1, lo, hi);
      gmin = std::min(gmin, lo);
      gmax = std::max(gmax, hi);
    }
    printf("chk-eTagMinMax %s %.4e %d", name.c_str(), time, tstep);
    printf("  all      min=% .8e  max=% .8e\n", gmin, gmax);
    fflush(stdout);
  }
}

// Per-eTag total of a nodal field, MPI-reduced, returned to EVERY rank so a caller can use the
// numbers. `nodes` is optional and reports how many nodes each bin actually visited.
//
// Accumulated in double regardless of how dfloat is configured: a sum over ~1e7 nodes in fp32
// loses ~23 bits. printMinMax can stay in dfloat because a min is a comparison and loses nothing;
// a total cannot. Per-element partial sums keep the accumulation shallow.
std::vector<double> sum(mesh_t *mesh,
                        const std::vector<int> &eTag,
                        occa::memory o_fld,
                        std::vector<long long int> *nodes = nullptr)
{
  // Same meshT-subset requirement as printMinMax: a meshV field covers only the fluid prefix.
  nekrsCheck(mesh->Nelements > static_cast<dlong>(eTag.size()),
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "etag::sum: mesh has %d elements, eTag has %d -- not a meshT subset\n",
             static_cast<int>(mesh->Nelements), static_cast<int>(eTag.size()));

  std::vector<double> tsum(NumOfDomain, 0.0);
  std::vector<long long int> count(NumOfDomain, 0);
  std::vector<dfloat> work(mesh->Nlocal);

  o_fld.copyTo(work.data(), mesh->Nlocal);

  for (int e = 0; e < mesh->Nelements; ++e) {
    const int id = eTag[e] - 1; // 1-based tag -> 0-based index
    if (id < 0 || id >= NumOfDomain) {
      continue;
    }
    count[id] += mesh->Np;

    double acc = 0.0;
    for (int i = 0; i < mesh->Np; ++i) {
      acc += static_cast<double>(work[e * mesh->Np + i]);
    }
    tsum[id] += acc;
  }

  MPI_Allreduce(MPI_IN_PLACE, tsum.data(), NumOfDomain, MPI_DOUBLE, MPI_SUM,
                platform->comm.mpiComm());
  MPI_Allreduce(MPI_IN_PLACE, count.data(), NumOfDomain, MPI_LONG_LONG, MPI_SUM,
                platform->comm.mpiComm());

  if (nodes) {
    *nodes = count;
  }
  return tsum;
}

// Reporting wrapper for etag::sum. Returns the per-eTag totals so a caller needs no second
// (collective) pass -- see printVolume.
std::vector<double> printSum(mesh_t *mesh,
                             const std::vector<int> &eTag,
                             occa::memory o_fld,
                             double time,
                             int tstep,
                             const std::string &name)
{
  std::vector<long long int> nodes;
  const std::vector<double> tsum = sum(mesh, eTag, o_fld, &nodes);

  if (platform->comm.mpiRank() == 0) {
    double gsum = 0.0;
    long long int gnodes = 0;
    for (int id = 0; id < NumOfDomain; ++id) {
      // nodes=0 means the tag is absent from THIS mesh, which a sum of 0.0 cannot be told apart
      // from a field that is genuinely zero there. No sentinel is involved (notes/15) -- 0.0 is
      // the right answer either way -- but the count is what makes the two readable.
      printf("chk-eTagSum %s %.4e %d", name.c_str(), time, tstep);
      printf("  etag=%2d  nodes= %10lld  sum= % .8e\n", id + 1, nodes[id], tsum[id]);
      gsum += tsum[id];
      gnodes += nodes[id];
    }
    printf("chk-eTagSum %s %.4e %d", name.c_str(), time, tstep);
    printf("  all      nodes= %10lld  sum= % .8e\n", gnodes, gsum);
    fflush(stdout);
  }
  return tsum;
}

// Domain volumes. o_LMM is the mass matrix (J*w, aliased o_Jw), so its sum over a tag IS that
// domain's volume. mesh->volume is the same field reduced by linAlg (meshSetup.cpp:508), so the
// two agree only if every element carries a valid tag -- print the comparison rather than leave
// it to be worked out. The .usr's chk_etag_glsum(bm1, ...) is a third, independent route.
void printVolume(mesh_t *mesh, const std::vector<int> &eTag)
{
  const std::vector<double> v = printSum(mesh, eTag, mesh->o_LMM, 0.0, 0, "VOL");

  double tagged = 0.0;
  for (const double x : v) {
    tagged += x;
  }

  if (platform->comm.mpiRank() == 0) {
    const double whole = static_cast<double>(mesh->volume);
    const double rel = (whole != 0.0) ? (tagged - whole) / whole : 0.0;
    printf("chk-eTagSum VOL tagged= % .8e  mesh->volume= % .8e  reldiff= % .3e\n",
           tagged, whole, rel);
    fflush(stdout);
  }
}

void setup(const PinkCase &cs)
{
  // eTag is assigned in the usr (setup_etag_bc, usrdat2) and registered there; the udf
  // only reads, validates and prints it. One owner, so the two cannot drift.
  auto mesh = nrs->meshT;
  const int *etag_usr = nek::ptr<int>("etag");

  NumOfDomain = static_cast<int>(cs.s1_domain.size()); // the domain table is the count
  eTag.resize(mesh->Nelements, 0);
  o_eTag.resize(mesh->Nelements);

  std::vector<long long int> eHist(NumOfDomain + 1, 0); // [0]=invalid, [1..N] tag counts

  int ierr = 0;
  for (int e = 0; e < mesh->Nelements; e++) { // Nelements, never lelt
    const int tag = etag_usr[e];
    if (tag < 1 || tag > NumOfDomain) {
      ++ierr;
      eHist[0]++;
    } else {
      eTag[e] = tag;
      eHist[tag]++;
    }
  }

  MPI_Allreduce(MPI_IN_PLACE, &ierr, 1, MPI_INT, MPI_SUM, platform->comm.mpiComm());
  MPI_Allreduce(MPI_IN_PLACE, eHist.data(), NumOfDomain+1, MPI_LONG_LONG, MPI_SUM, platform->comm.mpiComm());

  if (platform->comm.mpiRank() == 0) {
    printf("chk-eTag (from usr, hScale=%d)\n", hScale);
    for (int i = 1; i <= NumOfDomain; ++i) {
      printf("chk-eTag %-9s eTag=%d  nelem= %lld\n", cs.s2_domain[i-1].name, i, eHist[i]);
    }
    printf("chk-eTag invalid= %lld\n", eHist[0]);
    fflush(stdout);
  }

  nekrsCheck(ierr != 0,
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "eTag: %d elements are not tagged\n",
             ierr);

  o_eTag.copyFrom(eTag.data(), mesh->Nelements);
}

// Gather one Domain member into a contiguous per-eTag array, for etag::setProp and the eTag kernels.
std::vector<dfloat> field(const PinkCase &cs, dfloat Domain::*member)
{
  std::vector<dfloat> v(cs.domain.size());
  for (size_t i = 0; i < cs.domain.size(); ++i) {
    v[i] = cs.domain[i].*member;
  }
  return v;
}

} // namespace etag

// ================================================================================================================
namespace scale {

// the inlet that defines a domain's scale (checked unique in scale::check)
const Inlet &inletOfDomain(const PinkCase &cs, int eTag)
{
  for (const auto &in : cs.s2_inlet) {
    if (in.eTag == eTag && in.setsDomainU) return in;
  }
  return cs.s2_inlet[0]; // unreachable: scale::check asserts one per fluid domain
}

// The SI inlet speed: the table's U when given, else mdot/(rho A), A the table's OWN
// cross-section (never derived from D -- D is only a characteristic length, see S1Inlet).
// The ONE definition -- derive_s2 and every report go through it, so they cannot disagree.
dfloat inletU(const PinkCase &cs, const S1Inlet &in)
{
  const dfloat rho = cs.s1_domain[in.eTag - 1].rho;
  return (in.U > 0.0) ? in.U : in.mdot / (rho * in.A);
}

// The reference inlet: the one flagged setsDomainU in the reference domain. Its speed IS U_r,
// so there is no typed reference velocity that can drift from the table.
const S1Inlet &referenceInlet(const PinkCase &cs)
{
  const S1Inlet *ref = nullptr;
  for (const auto &in : cs.s1_inlet) {
    if (in.eTag == cs.s1_unit.dm_id && in.setsDomainU) ref = &in;
  }
  nekrsCheck(ref == nullptr,
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "reference domain eTag %d has no inlet flagged setsDomainU: U_r is undefined\n",
             cs.s1_unit.dm_id);
  return *ref;
}

// Echo every SI input. Rank 0, no state -- what the udf typed, before anything derives from it.
// Every field of S1Units/S1Domain/S1Inlet must appear here; q_report_check.py enforces that.
void report_s1(const PinkCase &cs)
{
  if (platform->comm.mpiRank() != 0) return;

  const auto &r = cs.s1_domain[cs.s1_unit.dm_id - 1];
  printf("chk-s1 ---- inputs (SI), the only hand-entered numbers in the case ----\n");
  printf("chk-s1 unit   ref= %s (eTag %d)  L= %.6e [m]  Tr= %.4e [K]  Tinf= %.4e [C]\n",
         r.name, cs.s1_unit.dm_id, cs.s1_unit.L, cs.s1_unit.Tr, cs.s1_unit.Tinf);
  printf("chk-s1 unit   g= %.6e [m/s2]  B0= %.4e [T]  mu0= %.6e [V-s/A-m]\n",
         cs.s1_unit.g, cs.s1_unit.B0, cs.s1_unit.mu0);
  printf("chk-s1 heat   qflux= %.6e [W/m2]  I0= %.6e [W/m2]  recycLength= %.4e [m]"
         "  isoTransit= %.4e [L/U_r]  isoTransportCoeff= %.4e\n",
         cs.s1_qflux, cs.s1_I0, cs.p_recycLength, cs.p_iso_transit, cs.p_iso_transportCoeff);

  for (const auto &d : cs.s1_domain) {
    printf("chk-s1 domain %-9s %-5s rho= %.6e [kg/m3] nu= %.6e [m2/s] Cp= %.6e [J/kg-K]"
           " k= %.6e [W/m-K] sigma= %.6e [S/m] beta= %.6e [1/K] T0= %.4e [C] bid_outlet= %d\n",
           d.name, (d.kind == FLUID) ? "FLUID" : "SOLID", d.rho, d.nu, d.Cp, d.k, d.sigma,
           d.beta, d.T0, d.bid_outlet);
  }

  // U says which branch of inletU() produced the speed -- the one thing the inputs do not show.
  // A_circ is NOT used anywhere -- it is a sanity reference only, printed so a reader can see
  // whether this inlet's typed A is close to a circular pipe of diameter D (expected to MATCH
  // for a true pipe, e.g. the water inlets; expected to DIFFER for a duct, e.g. PbLi's
  // 193.5x92.75mm rectangular BZ channel, whose D is the Hartmann half-height, not a diameter).
  for (const auto &in : cs.s1_inlet) {
    const dfloat A_circ = M_PI * 0.25 * in.D * in.D;
    const dfloat A_reldiff = (in.A > 0.0) ? std::abs(in.A - A_circ) / in.A : 0.0;
    printf("chk-s1 inlet  %-9s bid= %d eTag= %d D= %.6e [m] A= %.6e [m2] mdot= %.6e [kg/s]"
           " T= %.4e [C] setsDomainU= %s U= %.6e [m/s] (%s)\n",
           in.name, in.bid, in.eTag, in.D, in.A, in.mdot, in.T, in.setsDomainU ? "yes" : "no",
           inletU(cs, in), (in.U > 0.0) ? "table" : "from mdot");
    printf("chk-s1 inlet  %-9s A_circ(from D, pi/4 D^2)= %.6e [m2]  reldiff vs typed A= %.3e"
           " -- EXPECTED to match for a round pipe, to DIFFER for a duct (D = char. length)\n",
           in.name, A_circ, A_reldiff);
  }

  // bounded by both tables: scale::check validates that they match, but it runs after this.
  for (size_t g = 0; g < std::min(cs.s2_species.size(), cs.s2_isotope.size()); ++g) {
    printf("chk-s1 iso    %-9s T_half= %.6e [s]  n_ref= %.6e [1/m3]",
           cs.s2_species[g].name, cs.s2_species[g].T_half, cs.s2_species[g].n_ref);
    for (size_t e = 0; e < std::min(cs.s1_domain.size(), cs.s2_isotope[g].size()); ++e) {
      printf("  %s: D= %.4e [m2/s] delay= %.2f loop= %s", cs.s1_domain[e].name,
             cs.s2_isotope[g][e].D, cs.s2_isotope[g][e].delay,
             loopName(cs.s2_isotope[g][e].loop));
    }
    printf("\n");
  }
  fflush(stdout);
}

// s1 (SI) -> s2 (nondimensional on the reference fluid). The ONLY place the
// nondimensional properties come into existence. See notes/07 sec 3.
void derive_s2(PinkCase &cs)
{
  report_s1(cs); // first statement: the inputs are echoed before any check below can abort on them

  // p_inletFlowFactor is sized in UDF_Setup0; empty here means the two were reordered, which
  // would read past the end of a zero-length vector at every per-inlet site below.
  nekrsCheck(cs.s1_inlet.empty() || p_inletFlowFactor.size() != cs.s1_inlet.size(),
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "p_inletFlowFactor has %d entries, s1_inlet declares %d: size it in UDF_Setup0\n",
             static_cast<int>(p_inletFlowFactor.size()), static_cast<int>(cs.s1_inlet.size()));

  const auto &r = cs.s1_domain[cs.s1_unit.dm_id - 1];

  // the reference speed is the reference domain's own inlet speed, never a typed constant.
  // bound here, on every rank: referenceInlet's nekrsCheck is collective, so it must not be
  // called from inside the rank-0 block below.
  const S1Inlet &ref_inlet = referenceInlet(cs);
  cs.U_r = inletU(cs, ref_inlet);

  const dfloat Re_r = cs.U_r * cs.s1_unit.L / r.nu;                    // reference Reynolds
  const dfloat Pe_r = r.rho * r.Cp * cs.U_r * cs.s1_unit.L / r.k;      // reference Peclet
  const dfloat Rm   = cs.s1_unit.mu0 * r.sigma * cs.U_r * cs.s1_unit.L;        // magnetic Reynolds (report only)
  // B is nondimensionalized by B_r = B0, so the par sets by = 1. conE is the pure ratio
  // sigma/sigma_r (1 in PbLi) and N_r rides on lorentzCoeff -- eq.pdf sec 1.5, plan 2A.
  const dfloat N_r  = r.sigma * cs.s1_unit.B0 * cs.s1_unit.B0 * cs.s1_unit.L / (r.rho * cs.U_r);
  cs.N_r = N_r; // scale::setup needs it, and it runs after derive_s2
  const dfloat Ri_r = cs.s1_unit.g * r.beta * cs.s1_unit.Tr * cs.s1_unit.L / (cs.U_r * cs.U_r);
  const dfloat Ec_r = cs.U_r * cs.U_r / (r.Cp * cs.s1_unit.Tr); // Eckert, for Joule heating

  cs.p_qflux_scale = r.rho * r.Cp * cs.U_r * cs.s1_unit.Tr; // [W/m2]
  cs.p_qvol_scale  = cs.p_qflux_scale / cs.s1_unit.L;        // [W/m3]
  cs.p_qflux       = cs.s1_qflux / cs.p_qflux_scale;
  cs.p_I0          = cs.s1_I0;

  cs.s2_domain.clear();
  for (const auto &d : cs.s1_domain) {
    Domain o{};
    o.name  = d.name;
    o.kind  = d.kind;
    o.rho   = d.rho / r.rho;
    o.rhoCp = d.rho * d.Cp / (r.rho * r.Cp);
    o.conT  = 1.0 / (Pe_r * (r.k / d.k));
    o.conE  = d.sigma / r.sigma;  // lorentzCoeff carries N_r
    o.temp0 = (d.T0 - cs.s1_unit.Tinf) / cs.s1_unit.Tr;
    o.bid_outlet = d.bid_outlet;
    o.Uscale = 1.0;
    o.jouleCoeff = N_r * Ec_r * (r.sigma / d.sigma); // q_M''' = (sigma_r/sigma) N_r Ec_r (j*)^2
    // nu and beta are 0 in the solids -> Re and Ri undefined there
    o.mue       = (d.kind == FLUID) ? 1.0 / (Re_r * (r.nu / d.nu) * (r.rho / d.rho)) : 0.0;
    // a FORCE-DENSITY coefficient (eq.pdf, README_scales.md Ri_rho_beta). o_explicitTerms
    // wants it divided by rho -- the udf does that at the point of use (notes/18).
    o.RiRhoBeta = (d.kind == FLUID) ? Ri_r * (d.rho / r.rho) * (d.beta / r.beta) : 0.0;
    cs.s2_domain.push_back(o);
  }

  // inlets: U from the table when given, else mdot/(rho A), A the table's OWN cross-section
  // (never derived from D); nondimensionalized by U_r
  cs.s2_inlet.clear();
  for (const auto &in : cs.s1_inlet) {
    nekrsCheck(!(in.D > 0.0),
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "inlet %s: D must be > 0 (the characteristic length for Re/Ha/N)\n", in.name);
    nekrsCheck(!(in.U > 0.0) && !(in.mdot > 0.0),
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "inlet %s: set U or mdot (> 0) in s1_inlet\n", in.name);
    nekrsCheck(!(in.U > 0.0) && in.mdot > 0.0 && !(in.A > 0.0),
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "inlet %s: mdot is used (U not set), so A must be > 0"
               " (the cross-section mdot is divided by)\n", in.name);
    cs.s2_inlet.push_back({in.name, in.bid, in.eTag,
                           inletU(cs, in) / cs.U_r,
                           (in.T - cs.s1_unit.Tinf) / cs.s1_unit.Tr,
                           in.setsDomainU});
  }

  if (platform->comm.mpiRank() == 0) {
    printf("chk-s2 ref= %s  L= %.6e [m]  U_r= %.6e [m/s] (from inlet %s)  Tr= %.4e [K]\n",
           r.name, cs.s1_unit.L, cs.U_r, ref_inlet.name, cs.s1_unit.Tr);
    printf("chk-s2 Re_r= %.6e  Pe_r= %.6e  Rm= %.6e  Ri_r= %.6e  N_r= %.6e  Ec_r= %.6e\n",
           Re_r, Pe_r, Rm, Ri_r, N_r, Ec_r);
    printf("chk-s2 Br= B0 = %.4f T -> par [MHD INDUCTIONLESS] bx,by,bz nondimensionalize by B0;"
           " |b|=1 gives the full field, chk-Ha below reports the actual |b|-scaled Ha/N\n",
           cs.s1_unit.B0);
    printf("chk-s2 t_ref= L/U_r= %.6e [s]  (an OpenMC [1/m3/s] source is multiplied by "
           "t_ref/n_ref, n_ref per species -- see chk-s1 iso)\n", cs.s1_unit.L / cs.U_r);
    printf("chk-s2 qflux_scale= %.6e  qvol_scale= %.6e  qflux= %.6e\n",
           cs.p_qflux_scale, cs.p_qvol_scale, cs.p_qflux);
    for (const auto &o : cs.s2_domain) {
      printf("chk-s2 %-9s rho= %.6e mue= %.6e rhoCp= %.6e conT= %.6e conE= %.6e Ri= %.6e joule= %.6e\n",
             o.name, o.rho, o.mue, o.rhoCp, o.conT, o.conE, o.RiRhoBeta, o.jouleCoeff);
    }
    // three distinct Reynolds numbers -- see PHYSICS.md sec 2c
    //   Re_r      reference: sets the coefficients. U_r and L, in the reference fluid.
    //   Re_local  physical:  each inlet at its own U and D. Matches README_scales.md.
    //   Re_modify solved:    Re_local * inlet_flow_factor / visc_scale.
    printf("chk-Re reference  Re_r= %.6e   (%s at L= %.4f m, U_r= %.6e m/s)\n",
           Re_r, r.name, cs.s1_unit.L, cs.U_r);
#if USE_MHD
    // B_eff = B0 * |b|, |b| = sqrt(bx^2+by^2+bz^2) from the par -- the par's bx/by/bz knob
    // scales B0 down (e.g. by=0.01 for a 1/100 Ha sweep), so the report must read it too or
    // these prints silently ignore the knob (plan: chk-Ha by-scaling fix).
    dfloat bx = 0.0, by = 1.0, bz = 0.0;
    platform->options.getArgs("MHD MAGNETIC BX", bx);
    platform->options.getArgs("MHD MAGNETIC BY", by);
    platform->options.getArgs("MHD MAGNETIC BZ", bz);
    const dfloat bMag = std::sqrt(bx * bx + by * by + bz * bz);
    const dfloat B_eff = cs.s1_unit.B0 * bMag;

    // Ha = B.Lc.sqrt(sigma/(rho.nu)), N = sigma.B^2.Lc/(rho.U). N = Ha^2/Re identically.
    const dfloat Ha_r = B_eff * cs.s1_unit.L * std::sqrt(r.sigma / (r.rho * r.nu));
    printf("chk-Ha reference  B_eff= %.6e T (B0= %.4f x |b|= %.6e)  Ha_r= %.6e  N_r= %.6e"
           "  (= Ha_r^2/Re_r = %.6e)\n",
           B_eff, cs.s1_unit.B0, bMag, Ha_r, N_r, Ha_r * Ha_r / Re_r);
#endif
    for (size_t i = 0; i < cs.s2_inlet.size(); ++i) {
      const auto &d1 = cs.s1_domain[cs.s1_inlet[i].eTag - 1];
      const dfloat Ui = inletU(cs, cs.s1_inlet[i]);
      const dfloat Re_local = Ui * cs.s1_inlet[i].D / d1.nu;
      const dfloat vs = (cs.s1_inlet[i].eTag - 1 < static_cast<int>(p_visc_scale.size()))
                          ? p_visc_scale[cs.s1_inlet[i].eTag - 1] : 1.0;
      const dfloat Re_modify = Re_local * p_inletFlowFactor[i] / vs;
      printf("chk-s2 inlet %-9s bid= %d eTag= %d  U= %.6e m/s  U/U_r= %.6e  T= %.6e\n",
             cs.s2_inlet[i].name, cs.s2_inlet[i].bid, cs.s2_inlet[i].eTag, Ui,
             cs.s2_inlet[i].U_inlet, cs.s2_inlet[i].T_inlet);
      printf("chk-Re %-9s Re_local= %.4e  Re_modify= %.4e  (flow x%.3f / visc x%.3f)\n",
             cs.s2_inlet[i].name, Re_local, Re_modify, p_inletFlowFactor[i], vs);
#if USE_MHD
      // at inlet scale: Lc = this inlet's D, U = its own speed, properties from its domain.
      // The knobs move them the way they move Re: nu -> nu.vs, U -> U.f.
      // B_eff (= B0 * |b|, bx/by/bz from the par) computed once above, outside this loop.
      const dfloat Ha_local = B_eff * cs.s1_inlet[i].D * std::sqrt(d1.sigma / (d1.rho * d1.nu));
      const dfloat N_local  = d1.sigma * B_eff * B_eff * cs.s1_inlet[i].D / (d1.rho * Ui);
      printf("chk-Ha %-9s Ha_local= %.4e  N_local= %.4e  Ha_modify= %.4e  N_modify= %.4e\n",
             cs.s2_inlet[i].name, Ha_local, N_local,
             Ha_local / std::sqrt(vs), N_local / p_inletFlowFactor[i]);
#endif
    }
    fflush(stdout);
  }
}

// Fill scaleCfg + domain[].Uscale from the par. Called from UDF_Setup0, before the mhd ctor.
void setup(PinkCase &cs)
{
  std::string mode_str = "s3";
  platform->par->extract("casedata", "scalemode", mode_str);
  nekrsCheck(mode_str != "s2" && mode_str != "s3",
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "casedata scaleMode must be s2 or s3, got '%s'\n",
             mode_str.c_str());
  cs.scaleCfg.mode = (mode_str == "s2") ? ScaleMode::S2 : ScaleMode::S3;

  // Uscale per domain: from the inlet flagged setsDomainU (1 everywhere in s2). Solids stay 1.
  cs.domain = cs.s2_domain;
  cs.isotope = cs.s2_isotope;

  // lambda = ln2/T_half * L/U_r > 0 in every fluid. Gated on transport, not production: a
  // species can arrive where it is not bred (T permeates PbLi -> coolant) and must still decay.
  // D and lambda are the two SI isotope inputs; U_r is a runtime value, so neither can be a
  // constexpr in the udf. 1/Pe_c = D/(L U_r).
  for (size_t g = 0; g < cs.isotope.size(); ++g) {
    const dfloat rate = std::log(2.0) / cs.s2_species[g].T_half * cs.s1_unit.L / cs.U_r;
    for (size_t e = 0; e < cs.isotope[g].size(); ++e) {
      cs.isotope[g][e].D      = cs.s2_isotope[g][e].D / (cs.s1_unit.L * cs.U_r);
      cs.isotope[g][e].lambda = (cs.s2_domain[e].kind == FLUID) ? rate : 0.0;
      // .loop is a pass-through: cs.isotope = cs.s2_isotope above already copied it, and
      // scale::apply leaves it alone. Nothing to derive; validated in scale::check.
    }
  }
  for (auto &d : cs.domain) {
    d.Uscale = 1.0;
  }
  if (cs.scaleCfg.mode == ScaleMode::S3) {
    for (const auto &in : cs.s2_inlet) {
      if (in.setsDomainU) {
        cs.domain[in.eTag - 1].Uscale = in.U_inlet;
      }
    }
  }
  cs.scaleCfg.U_ref = cs.domain[cs.s1_unit.dm_id - 1].Uscale;

  // MHD coeffs: only U_ref, since both are global scalars (note 07 sec 5)
  // o_ulocScale carries the per-fluid U inside J; what is left for lorentzCoeff is N_r,
  // which conE no longer holds (plan 2A). computeCurrent applies sigma alone, so o_CURRENT
  // is the true j* = sigma^ (-grad phi + u x b) and the force picks up N_r here.
  cs.scaleCfg.lorentzCoeff = cs.N_r;

  platform->options.setArgs("MHD INDUCTIONLESS LORENTZ COEFF", to_string_f(cs.scaleCfg.lorentzCoeff));

  if (platform->comm.mpiRank() == 0) {
    printf("chk-scale mode= %s\n", (cs.scaleCfg.mode == ScaleMode::S2) ? "s2" : "s3");
    for (const auto &d : cs.domain) {
      printf("chk-scale %-9s Uscale= %.8e\n", d.name, d.Uscale);
    }
    printf("chk-scale U_ref= %.8e  N_r= %.8e  lorentzCoeff= %.8e\n",
           cs.scaleCfg.U_ref, cs.N_r, cs.scaleCfg.lorentzCoeff);
    fflush(stdout);
  }
}

// Guard the table + the assumptions the global MHD coeffs rest on. Called from UDF_Setup.
void check(const PinkCase &cs)
{
  nekrsCheck(static_cast<int>(cs.s2_domain.size()) != NumOfDomain,
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "s2_domain has %d rows, s1_domain declares %d domains\n",
             static_cast<int>(cs.s2_domain.size()), NumOfDomain);

  // NumOfDomain IS s1_domain.size() (etag::setup), so the two cannot disagree; what can go
  // wrong is running before it is set, which would silently make every per-domain loop and
  // malloc zero-length instead of failing.
  nekrsCheck(NumOfDomain < 1,
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "NumOfDomain=%d: etag::setup must run before scale::check\n",
             NumOfDomain);

  // isotope is [species][eTag]: one outer row per species, each as wide as the domain table
  nekrsCheck(cs.s2_isotope.size() != cs.s2_species.size(),
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "isotope table has %d species rows, species table has %d\n",
             static_cast<int>(cs.s2_isotope.size()), static_cast<int>(cs.s2_species.size()));

  for (size_t g = 0; g < cs.s2_isotope.size(); ++g) {
    // n_ref is the LAST field of IsotopeSpecies and a dfloat after another dfloat, so a row
    // written {"N16", 7.13} still COMPILES and value-initialises it to 0 -- plan V's int-last
    // trick is unavailable here. This is the only thing standing between that and a divide by
    // zero in srcFactor.
    nekrsCheck(!(cs.s2_species[g].n_ref > 0.0),
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "species %s has n_ref=%g -- every s2_species row must state it [1/m3]\n",
               cs.s2_species[g].name, cs.s2_species[g].n_ref);
    nekrsCheck(cs.s2_isotope[g].size() != cs.s2_domain.size(),
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "isotope row %s has %d domains, domain table has %d\n",
               cs.s2_species[g].name,
               static_cast<int>(cs.s2_isotope[g].size()),
               static_cast<int>(cs.s2_domain.size()));

    // Every loop that exists is stated explicitly (plan V): a domain with an outlet
    // recirculates, so it must declare CLOSED or OPEN. An omitted table column arrives here as
    // LOOP_UNSET, so this is fatal rather than defaulted -- there is no silent path to either
    // behaviour. Solids have no loop, so the value is inert there and left unconstrained.
    for (int e = 0; e < NumOfDomain; ++e) {
      const int m = cs.s2_isotope[g][e].loop;
      nekrsCheck(cs.s2_domain[e].bid_outlet > 0 && m != LOOP_CLOSED && m != LOOP_OPEN,
                 platform->comm.mpiComm(),
                 EXIT_FAILURE,
                 "isotope %s in %s: outlet bid %d recirculates, so its row must state "
                 "LOOP_CLOSED or LOOP_OPEN (got %d)\n",
                 cs.s2_species[g].name, cs.s2_domain[e].name, cs.s2_domain[e].bid_outlet, m);
    }
  }

  // every inlet points at a fluid domain, bids are distinct, one setsDomainU per domain
  std::vector<int> nSet(NumOfDomain, 0);
  for (const auto &in : cs.s2_inlet) {
    nekrsCheck(in.eTag < 1 || in.eTag > NumOfDomain ||
                   cs.s2_domain[in.eTag - 1].kind != FLUID,
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "inlet %s: eTag=%d is not a fluid domain\n",
               in.name, in.eTag);
    if (in.setsDomainU) {
      nSet[in.eTag - 1]++;
    }
    for (const auto &other : cs.s2_inlet) {
      nekrsCheck(&in != &other && in.bid == other.bid,
                 platform->comm.mpiComm(),
                 EXIT_FAILURE,
                 "inlets %s and %s share bid %d\n",
                 in.name, other.name, in.bid);
    }
  }
  for (int i = 0; i < NumOfDomain; ++i) {
    nekrsCheck(cs.s2_domain[i].kind == FLUID && nSet[i] != 1,
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "fluid domain %s has %d inlets with setsDomainU, need exactly 1\n",
               cs.s2_domain[i].name, nSet[i]);
  }
}

// Transform the working tables from s2 into the active scale.
// Untouched by design: conT (k) and conE (sigma) -- both appear in interface conditions.
void apply(PinkCase &cs)
{
  cs.p_U_inlet.resize(cs.s2_inlet.size());
  for (size_t i = 0; i < cs.s2_inlet.size(); ++i) {
    cs.p_U_inlet[i] = cs.s2_inlet[i].U_inlet;
  }

  // user knob, applied once here (not inside uservp). Gives Re_modify (chk-Re).
  const int nvisc = std::min<int>(cs.domain.size(), p_visc_scale.size());
  for (int i = 0; i < nvisc; ++i) {
    cs.domain[i].mue *= p_visc_scale[i];
  }

  for (auto &d : cs.domain) {
    const dfloat U = d.Uscale;
    d.mue       /= U;
    d.rhoCp     *= U;
    d.RiRhoBeta /= (U * U);
    // q''' absorbs no U, but the solver stores J as j_s2/U^2, so squaring it costs U^4.
    // Reaches ~1e18 in waterSH -- fine in fp64, would need reformulating in fp32 (plan J sec 7).
    d.jouleCoeff *= (U * U * U * U);
  }

  // isotopes: c is not rescaled. lambda and D are rates -> /U. delay is a yield [-] and loop is
  // a mode -> neither is scaled. Every IsotopeParams field is accounted for in that sentence;
  // adding one without deciding its rule here is the classic bug this function attracts.
  for (auto &per_domain : cs.isotope) {
    for (size_t e = 0; e < per_domain.size(); ++e) {
      const dfloat U = cs.domain[e].Uscale;
      per_domain[e].lambda /= U;
      per_domain[e].D     /= U;
    }
  }

  // inlet velocity follows its own domain's scale
  for (size_t i = 0; i < cs.s2_inlet.size(); ++i) {
    cs.p_U_inlet[i] /= cs.domain[cs.s2_inlet[i].eTag - 1].Uscale;
  }

  if (platform->comm.mpiRank() == 0) {
    for (const auto &d : cs.domain) {
      printf("chk-prop %-9s rho= %.6e mue= %.6e rhoCp= %.6e conT= %.6e conE= %.6e\n",
             d.name, d.rho, d.mue, d.rhoCp, d.conT, d.conE);
    }
    for (size_t i = 0; i < cs.s2_inlet.size(); ++i) {
      printf("chk-prop inlet %-9s bid= %d eTag= %d U_inlet= %.6e T_inlet= %.6e\n",
             cs.s2_inlet[i].name, cs.s2_inlet[i].bid, cs.s2_inlet[i].eTag,
             cs.p_U_inlet[i], cs.s2_inlet[i].T_inlet);
    }
    fflush(stdout);
  }
}

// Device copies of Uscale per eTag, for the field conversions. Needs the device, so UDF_Setup.
void upload(const PinkCase &cs)
{
  const auto U = etag::field(cs, &Domain::Uscale);
  std::vector<dfloat> U_inv(U.size());
  for (size_t i = 0; i < U.size(); ++i) { U_inv[i] = 1.0 / U[i]; }

  o_U_star.resize(U.size());
  o_U_star.copyFrom(U.data(), U.size());
  o_U_star_inv.resize(U_inv.size());
  o_U_star_inv.copyFrom(U_inv.data(), U_inv.size());
}

// s2 -> s3. Only u and p; everything else is s2 in memory and on disk.
void toSolve(const PinkCase &cs, double time, int tstep)
{
  if (cs.scaleCfg.mode == ScaleMode::S2) return;

  auto &fluid = nrs->fluid;
  auto mesh = fluid->mesh;
  for (int i = 0; i < mesh->dim; i++) {
    auto o_Ui = fluid->o_U + i * fluid->fieldOffset;
    eTagScale(mesh->Nelements, o_eTag, o_U_star_inv, o_Ui);
  }
  eTagScale(mesh->Nelements, o_eTag, o_U_star_inv, fluid->o_P); // p / U^2
  eTagScale(mesh->Nelements, o_eTag, o_U_star_inv, fluid->o_P);

#if USE_MHD
  if (nrs->mhdIndless) { // J back to the solver's J_s2/U^2
    auto &m = nrs->mhdIndless;
    auto o_J = m->o_currentDensity();
    for (int i = 0; i < m->mesh->dim; i++) {
      auto o_Ji = o_J + i * m->fieldOffset;
      eTagScale(m->mesh->Nelements, o_eTag, o_U_star_inv, o_Ji);
      eTagScale(m->mesh->Nelements, o_eTag, o_U_star_inv, o_Ji);
    }
  }
#endif

  if (platform->comm.mpiRank() == 0) {
    printf("chk-scale %.4e %d  to s3\n", time, tstep);
    fflush(stdout);
  }
}

// s3 -> s2, before any output.
void toOutput(const PinkCase &cs, double time, int tstep)
{
  if (cs.scaleCfg.mode == ScaleMode::S2) return;

  auto &fluid = nrs->fluid;
  auto mesh = fluid->mesh;
  for (int i = 0; i < mesh->dim; i++) {
    auto o_Ui = fluid->o_U + i * fluid->fieldOffset;
    eTagScale(mesh->Nelements, o_eTag, o_U_star, o_Ui);
  }
  eTagScale(mesh->Nelements, o_eTag, o_U_star, fluid->o_P);
  eTagScale(mesh->Nelements, o_eTag, o_U_star, fluid->o_P);

#if USE_MHD
  // J. With useUlocScale the solver leaves o_CURRENT at J_s2/U^2 (note 07 sec 6), so two
  // multiplies bring it to s2. On meshT, not meshV: J is defined in the solid too.
  if (nrs->mhdIndless) {
    auto &m = nrs->mhdIndless;
    auto o_J = m->o_currentDensity();
    for (int i = 0; i < m->mesh->dim; i++) {
      auto o_Ji = o_J + i * m->fieldOffset;
      eTagScale(m->mesh->Nelements, o_eTag, o_U_star, o_Ji);
      eTagScale(m->mesh->Nelements, o_eTag, o_U_star, o_Ji);
    }
  }
#endif

  if (platform->comm.mpiRank() == 0) {
    printf("chk-scale %.4e %d  to s2\n", time, tstep);
    fflush(stdout);
  }
}

} // namespace scale

#if USE_OPENMC_SOURCE
// ================================================================================================================
// Load a source field from a Nek checkpoint (OpenMC output), interpolated onto this mesh.
//
// The three sources are in DIFFERENT physical units and nondimensionalise differently:
// Each source is one scalar slot of a Nek checkpoint (plan Y); the same file may hold all three.
// Par value is "<file>[+id=N][+int]": scalarNN (default 00), +int to interpolate (gfldr).
//   heating_local    [W/m3]           -> / p_qvol_scale
//   tritium          [particles/m3/s] -> * (L/U_r) / n_ref[species]
//   water_activation [particles/m3/s] -> * (L/U_r) / n_ref[species]
// See plans/W_source_file.md sec 3 and plans/Z_per_species_nref.md. n_ref is PER SPECIES,
// so n_SI = n_hat * s2_species[g].n_ref -- read a concentration back with ITS OWN n_ref.
namespace source {

enum SourceField : int { SRC_QVOL = 0, SRC_N16 = 1, SRC_T = 2 };

// species = nullptr for the heat source; otherwise the s2_species name this file feeds.
struct SrcSpec { const char *key; const char *tag; const char *species; };
inline SrcSpec srcSpec(SourceField w)
{
  switch (w) {
  case SRC_QVOL: return {"srcfileqvol", "QVOL",    nullptr};
  case SRC_N16:  return {"srcfilen16",  "SRC-N16", "N16"};
  default:       return {"srcfilet",    "SRC-T",   "T"};
  }
}

// SI -> nondimensional, for one source. ONE double, computed once and applied once: the raw field
// reaches ~1e18 and dfloat may be float, so it must not sit unscaled or be re-derived per node
// (plan W sec 4). save_source divides by this same expression to put a field back in SI, so the
// round trip is exact by construction -- there is no second copy of the conversion to keep in step.
inline double srcFactor(const PinkCase &cs, SourceField which, int g)
{
  const double t_ref = cs.s1_unit.L / cs.U_r;
  if (which == SRC_QVOL) {
    return 1.0 / cs.p_qvol_scale;
  }
  // g is the species index both callers already resolve BY NAME. n_ref is per species (plan Z).
  return t_ref / cs.s2_species[g].n_ref;
}

// Resolved by NAME, so reordering s2_species cannot silently redirect a source into the wrong
// species -- the same reason plan R took the bid map off the kernel. -1 = not in the table.
inline int speciesIndex(const PinkCase &cs, const char *name)
{
  for (int g = 0; g < static_cast<int>(cs.s2_species.size()); ++g) {
    if (std::strcmp(cs.s2_species[g].name, name) == 0) { return g; }
  }
  return -1;
}

// Fatal on anything that would let a bad field through silently. gfldr only WARNS when it cannot
// locate a point in the source mesh and leaves those nodes untouched, so an unvalidated read can
// be quietly garbage -- this is the check that catches a wrong comp, a wrong mesh, or an n_ref
// off by decades.
void validate(occa::memory o_f, dlong n, const char *tag, const char *fname, bool interp)
{
  std::vector<dfloat> h(n);
  o_f.copyTo(h.data(), n);
  double amax = 0.0;
  long long nbad = 0;
  for (dlong i = 0; i < n; ++i) {
    const double v = static_cast<double>(h[i]);
    if (std::isnan(v) || std::isinf(v)) { ++nbad; continue; }
    amax = std::max(amax, std::fabs(v));
  }
  MPI_Allreduce(MPI_IN_PLACE, &nbad, 1, MPI_LONG_LONG, MPI_SUM, platform->comm.mpiComm());
  MPI_Allreduce(MPI_IN_PLACE, &amax, 1, MPI_DOUBLE, MPI_MAX, platform->comm.mpiComm());
  // WARN, never abort (user's direction, 2026-08-14). Both reductions stay collective and stay
  // OUTSIDE the rank guard -- amax and nbad are global, and a reduction reached on rank 0 only is
  // a hang (notes/14). Only the reaction changed.
  if (platform->comm.mpiRank() == 0) {
    printf("chk-src %-8s max|f|= %.6e  (nondimensional)  <- %s%s\n", tag, amax, fname,
           interp ? "  [interpolated]" : "");
    if (nbad > 0) {
      printf("chk-src WARNING %s from %s: %lld non-finite nodes -- gfldr leaves points it cannot "
             "locate untouched, and a NaN here propagates into every later step\n",
             tag, fname, nbad);
    }
    if (!(amax > 1.0e-8 && amax < 1.0e4)) {
      printf("chk-src WARNING %s from %s: max|field| = %.6e is outside [1e-8, 1e4]. Wrong slot, "
             "wrong units, a missing source-rate normalisation, or this species' n_ref off by "
             "decades?\n", tag, fname, amax);
    }
    fflush(stdout);
  }
}

// which: the source. Everything else -- file, scalar slot, interpolation -- comes from the par.
void load_source(const PinkCase &cs, SourceField which)
{
  const SrcSpec sp = srcSpec(which);
  auto mesh = nrs->meshT;

  // Resolve the destination before touching the file, so a table mistake fails before the I/O.
  int g = -1;
  if (which != SRC_QVOL) {
    g = speciesIndex(cs, sp.species);
    nekrsCheck(g < 0, platform->comm.mpiComm(), EXIT_FAILURE,
               "[CASEDATA] %s feeds species \"%s\", which is not in s2_species\n",
               sp.key, sp.species);
    nekrsCheck(g >= static_cast<int>(o_iso_src.size()), platform->comm.mpiComm(), EXIT_FAILURE,
               "%s: isotopes::setup has not run yet -- load_source must follow it\n", sp.key);
  }

  std::string spec;
  nekrsCheck(!platform->par->extract("casedata", sp.key, spec) || spec.empty(),
             platform->comm.mpiComm(), EXIT_FAILURE,
             "USE_OPENMC_SOURCE needs [CASEDATA] %s = \"<file>0.f00000\" "
             "(+id=N picks scalarNN, default 0; append +int to interpolate from another mesh)\n",
             sp.key);

  // "<file>+id=N+int" -- the same `+`-separated syntax [GENERAL] startFrom uses (nrs.cpp:662),
  // parsed with the same helper, so a user who knows one knows this. Order does not matter.
  // Interpolation is OPT-IN: it is only correct when the source lives on a different mesh, and
  // gfldr is expensive and merely WARNS on points it cannot locate. On this case's own mesh the
  // file must be read directly.
  auto specOpts = serializeString(spec, '+');
  // serializeString drops empty tokens, so "+int" yields {"int"} and "+" yields {}: .at(0) would
  // throw, or an option would be silently accepted as the filename. Reject anything that parses
  // as an option in the first position -- otherwise "+id=0" aborts later with a message that
  // suggests appending id= to a filename literally called "id=0".
  const bool firstIsOption = !specOpts.empty()
                             && (lowerCase(specOpts.at(0)) == "int"
                                 || lowerCase(specOpts.at(0)).rfind("id=", 0) == 0);
  nekrsCheck(specOpts.empty() || firstIsOption, platform->comm.mpiComm(), EXIT_FAILURE,
             "[CASEDATA] %s = \"%s\" names no file before the \"+\".\n", sp.key, spec.c_str());
  const std::string fname = specOpts.at(0);
  specOpts.erase(specOpts.begin());

  bool interpolate = false;
  int slot = -1;
  for (const auto &o : specOpts) {
    const std::string lo = lowerCase(o);
    // Exact match, not a substring: restartFromFiles uses find("int") != npos, which would also
    // accept a mistyped "interpolate" or "int=1" -- and, more to the point, silently swallow any
    // other option containing those three letters. An unknown option here is fatal instead.
    if (lo == "int") {
      interpolate = true;
    } else if (lo.rfind("id=", 0) == 0) {
      // Last-wins would be silent; two id= in one value means the intent is unclear.
      nekrsCheck(slot >= 0, platform->comm.mpiComm(), EXIT_FAILURE,
                 "[CASEDATA] %s = \"%s\" states \"id=\" more than once.\n", sp.key, spec.c_str());
      const std::string v = lo.substr(3);
      // Digits only, and short enough that stoi cannot throw out_of_range on e.g. id=99999999999.
      nekrsCheck(v.empty() || v.size() > 2 || v.find_first_not_of("0123456789") != std::string::npos,
                 platform->comm.mpiComm(), EXIT_FAILURE,
                 "[CASEDATA] %s: \"+id=%s\" is not an integer in 0..99.\n", sp.key, v.c_str());
      slot = std::stoi(v);
    } else {
      nekrsAbort(platform->comm.mpiComm(), EXIT_FAILURE,
                 "[CASEDATA] %s: unknown option \"+%s\". Supported: \"+id=N\", \"+int\".\n",
                 sp.key, o.c_str());
    }
  }
  // Default scalar00: the common case is one file per source, where the slot is not interesting.
  // Sharing one file needs an explicit +id= on at least two of the three keys -- the -1 sentinel
  // above exists to catch a repeated +id=, not to force one.
  if (slot < 0) {
    slot = 0;
  }
  // scalar\d{2} is the reader's own regex (iofldNek.cpp:6), so 0..99 is the bound -- enforced
  // above by the two-digit limit, which is also what keeps %02d from widening the name.
  char sname[16];
  snprintf(sname, sizeof(sname), "scalar%02d", slot); // the only place the name is formed

  const double factor = srcFactor(cs, which, g);

  auto io = iofldFactory::create("nek");
  io->open(mesh, iofld::mode::read, fname, -1); // step -1: fname used verbatim

  // gfldr aborts with a bare 'source does not contain a mesh!' if the file has no coordinates.
  // Say which file, before it does. Only interpolation needs them.
  const auto avail = io->availableVariables();
  const auto has = [&](const char *v) { return std::find(avail.begin(), avail.end(), v) != avail.end(); };
  nekrsCheck(interpolate && !has("mesh"), platform->comm.mpiComm(), EXIT_FAILURE,
             "%s (%s) has no mesh coordinates, so it cannot be interpolated. Write it with "
             "the mesh (step 0, or outputMesh), or drop \"+int\".\n", sp.key, fname.c_str());
  // A slot the file does not have is NOT an error to the reader: iofldNek::read walks the
  // registered names against the file's own scalar list and simply never calls populateVariable,
  // leaving the buffer exactly as it was -- no message. Same silent-garbage class as gfldr's
  // warn-on-unlocated-point. This is the guard that turns it into an abort. (plan Y sec 2)
  std::string availList;
  for (const auto &v : avail) { availList += (availList.empty() ? "" : ", ") + v; }
  nekrsCheck(!has(sname), platform->comm.mpiComm(), EXIT_FAILURE,
             "%s (%s) has no %s. Available: %s\n", sp.key, fname.c_str(), sname, availList.c_str());

  auto o_src = platform->device.malloc<dfloat>(mesh->Nlocal);
  // Zeroed, not just allocated: if the guard above were ever bypassed, an untouched buffer would
  // be whatever was on the device. At 0 it reaches validate() and the band check makes it fatal.
  platform->linAlg->fill(mesh->Nlocal, 0.0, o_src);
  io->addVariable(sname, o_src);
  if (interpolate) {
    io->readAttribute("interpolate", "true"); // "+int": the source is on a different mesh
  }
  // Forward the run's h-refine schedule, as nrs.cpp:857-860 does for startFrom. Without it
  // hRefineDiffSchedule() returns immediately (iofld.hpp: `if (hRefineScheduleSim.size()==0)
  // return;`) and takes the ONLY element-count check in the read path with it -- fldData.nelgr is
  // consumed nowhere else, and mfi_getv reads by the sim's own nelt. An unrefined file read into an
  // h-refined run would then be read at the wrong offsets with nothing said.
  std::string hSchedule;
  if (platform->options.getArgs("MESH HREFINEMENT SCHEDULE", hSchedule)) {
    nekrsCheck(interpolate, platform->comm.mpiComm(), EXIT_FAILURE,
               "%s: \"+int\" cannot be combined with [MESH] hRefine (iofldNek.cpp:149). Load the "
               "source in an unrefined run, save it with srcfilesave, and reload that.\n", sp.key);
    io->readAttribute("hSchedule", hSchedule);
  }
  io->process();
  io->close();

  // o_qvol is a deviceMemory<dfloat>, o_iso_src an occa::memory -- size each on its own type.
  if (which == SRC_QVOL) {
    o_qvol.resize(mesh->Nlocal);
  } else if (!o_iso_src[g].isInitialized()) {
    o_iso_src[g] = platform->device.malloc<dfloat>(mesh->Nlocal);
  }
  occa::memory o_dst = (which == SRC_QVOL) ? static_cast<occa::memory &>(o_qvol) : o_iso_src[g];

  // the single factor, applied once: o_dst = factor * o_src
  platform->linAlg->axpby(mesh->Nlocal, static_cast<dfloat>(factor), o_src, 0.0, o_dst);
  validate(o_dst, mesh->Nlocal, sp.tag, fname.c_str(), interpolate);
  etag::printMinMax(mesh, eTag, o_dst, 0.0, 0, sp.tag);
}

// Write the three loaded sources to ONE checkpoint at low order, so a later run reads them back
// with no "+int" at all. This is the point of the whole thing: reading a file whose nxr differs
// from lx1 goes through Nek's element-local spectral interpolation (mapab, ic.f:1194), NOT gfldr's
// point search -- so a same-mesh file at ANY order is exact-by-construction on the element it came
// from, cheap, and free of gfldr's silent warn-on-unlocated-point (plan W sec 4).
//
// Order is heat, T, N16 -- the user's, and it is NOT the SourceField order (QVOL, N16, T). It is
// written once here and read back by the same table below, so the two cannot drift; nothing else
// in the case may assume either order.
struct SaveSlot { SourceField which; const char *tag; };
const std::vector<SaveSlot> saveOrder = {
    {SRC_QVOL, "heat"},
    {SRC_T,    "T"},
    {SRC_N16,  "N16"},
};

// The saved file holds the fields back in SI -- [W/m3] and [particles/m3/s], exactly the units the
// OpenMC file had. It is therefore a DROP-IN REPLACEMENT for it: the same par keys, the same
// USE_OPENMC_SOURCE 1, only "+int" dropped. Writing the nondimensional values instead would have
// been one line shorter and would have made every reload a double-scaling trap.
//
// The inverse uses srcFactor(), the same expression load_source multiplies by, so the round trip
// cannot drift. It is applied to a SCRATCH buffer: o_qvol and o_iso_src[] are live -- the solver
// reads them every step -- so scaling them in place, even transiently, would corrupt the run that
// is doing the saving.
void save_source(const PinkCase &cs)
{
  std::string fname;
  if (!platform->par->extract("casedata", "srcfilesave", fname) || fname.empty()) {
    return; // the key is the on/off switch: absent or empty means do not save
  }

  auto mesh = nrs->meshT;

  int Nsave = 3; // the default the user asked for; overridable, and bounded by iofld itself
  platform->par->extract("casedata", "srcfilesaven", Nsave);
  nekrsCheck(Nsave < 1 || Nsave > mesh->N, platform->comm.mpiComm(), EXIT_FAILURE,
             "[CASEDATA] srcfilesaveN=%d must be in 1..%d (the run's own order; saving ABOVE it "
             "would invent data)\n", Nsave, mesh->N);

  // Every slot must actually hold a field. o_qvol is sized by load_source or heat::setupQvol;
  // o_iso_src[g] only by load_source. Writing an uninitialised occa::memory would abort inside
  // addVariable with a message about the variable, not about the cause.
  std::vector<occa::memory> o_fld;
  for (const auto &sl : saveOrder) {
    occa::memory o_f;
    if (sl.which == SRC_QVOL) {
      o_f = static_cast<occa::memory &>(o_qvol);
    } else {
      const int g = speciesIndex(cs, srcSpec(sl.which).species);
      nekrsCheck(g < 0 || g >= static_cast<int>(o_iso_src.size()),
                 platform->comm.mpiComm(), EXIT_FAILURE,
                 "srcfilesave: %s has no isotope slot -- is USE_ISOTOPES on?\n", sl.tag);
      o_f = o_iso_src[g];
    }
    nekrsCheck(!o_f.isInitialized() || o_f.size() < static_cast<size_t>(mesh->Nlocal),
               platform->comm.mpiComm(), EXIT_FAILURE,
               "srcfilesave: the %s source is not loaded, so there is nothing to save\n", sl.tag);

    // Back to SI, into scratch -- never in place, the solver is reading these. The scratch then
    // holds ~1e18 in dfloat, which is exactly what load_source's read buffer holds on the way in
    // (fp32 reaches 3.4e38; it is the SQUARE that would overflow, and nothing squares this). The
    // file itself is fp64, so the round trip is limited by dfloat either way -- same as the load.
    const double f = srcFactor(cs, sl.which, (sl.which == SRC_QVOL) ? -1
                                            : speciesIndex(cs, srcSpec(sl.which).species));
    nekrsCheck(!(f > 0.0), platform->comm.mpiComm(), EXIT_FAILURE,
               "srcfilesave: the %s unit factor is %g, so it cannot be inverted\n", sl.tag, f);
    auto o_si = platform->device.malloc<dfloat>(mesh->Nlocal);
    platform->linAlg->axpby(mesh->Nlocal, static_cast<dfloat>(1.0 / f), o_f, 0.0, o_si);
    o_fld.push_back(o_si);
  }

  auto io = iofldFactory::create("nek");
  io->open(mesh, iofld::mode::write, fname, 0);
  io->writeAttribute("polynomialOrder", std::to_string(Nsave));
  // Follows the case's own [GENERAL] checkpointPrecision, like every other write in this file.
  // NOTE it defaults to FP32 (nekrs.cpp:72) and this par does not set it, so the saved sources are
  // fp32 unless you set checkpointPrecision = 64. That is ~7 significant digits on a field whose
  // SI values reach ~1e18; the round trip through srcFactor is then good to ~1e-7 relative, which
  // is far below the neutronics' own uncertainty but is NOT bit-exact. Set 64 if you want it.
  auto FP64 = platform->options.compareArgs("CHECKPOINT PRECISION", "FP64");
  io->writeAttribute("precision", (FP64) ? "64" : "32");
  io->writeAttribute("outputMesh", "true"); // it must be reloadable on its own
  // Record the h-refine schedule the data was produced on, as nrs.cpp:1539-1542 does. The fields
  // come off the REFINED meshT, so a file that claimed no schedule would misdescribe itself: a
  // later reader comparing base meshes would divide the sim's count by the scale and the file's by
  // 1, and abort on a mismatch that is not real.
  std::string hSchedule;
  if (platform->options.getArgs("MESH HREFINEMENT SCHEDULE", hSchedule)) {
    io->writeAttribute("hSchedule", hSchedule);
  }
  for (size_t i = 0; i < saveOrder.size(); ++i) {
    char sname[16];
    snprintf(sname, sizeof(sname), "scalar%02zu", i);
    io->addVariable(sname, o_fld[i]);
  }
  io->process();
  io->close();

  if (platform->comm.mpiRank() == 0) {
    printf("chk-src saved N=%d %s (SI units) -> %s0.f00000\n",
           Nsave, FP64 ? "fp64" : "fp32", fname.c_str());
    for (size_t i = 0; i < saveOrder.size(); ++i) {
      printf("chk-src   scalar%02zu = %-4s  [CASEDATA] %s = \"%s0.f00000+id=%zu\"\n",
             i, saveOrder[i].tag, srcSpec(saveOrder[i].which).key, fname.c_str(), i);
    }
    printf("chk-src   drop-in for the OpenMC file: same keys, USE_OPENMC_SOURCE 1, no +int\n");
    fflush(stdout);
  }
}

} // namespace source
#endif // USE_OPENMC_SOURCE

#if USE_QVOL || USE_JOULE
// ================================================================================================================
namespace heat {
#endif

#if USE_QVOL
// Neutron heating q''' into o_qvol, once. Beer-Lambert inward from the FW, in SI metres:
// exponential through the TBM, zero across the vacuum gap, exponential again in the shield
// carrying the TBM's attenuation, zero past d3. See PHYSICS.md sec 4 / eq.pdf sec 1.6.
// TODO replace with OpenMC.
void setupQvol(const PinkCase &cs)
{
  constexpr dfloat d1 = 0.7;                  // [m] FW -> back of the TBM
  constexpr dfloat d2 = 0.7 + 0.275;          // [m] end of the vacuum gap
  constexpr dfloat d3 = 0.7 + 0.275 + 1.359;  // [m] back of the shield
  constexpr dfloat alpha_TBM = 4.992887122589985; // [1/m] attenuation, TBM
  constexpr dfloat alpha_SH  = 7.59417922895606;  // [1/m] attenuation, shield

  auto mesh = nrs->meshT;
  o_qvol.resize(mesh->Nlocal);

  const auto x_FW = platform->linAlg->min(mesh->Nlocal, mesh->o_x, platform->comm.mpiComm());
  auto [X, Y, Z] = mesh->xyzHost();

  std::vector<dfloat> Q(mesh->Nlocal, 0.0);
  for (int n = 0; n < mesh->Nlocal; ++n) {
    const dfloat s = std::fabs(X[n] - x_FW) * cs.s1_unit.L; // [m] depth from the FW

    dfloat q = 0.0; // beyond d3, and across the gap
    if (s <= d1) {
      q = alpha_TBM * std::exp(-alpha_TBM * s);
    } else if (s >= d2 && s <= d3) {
      q = alpha_SH * std::exp(-alpha_TBM * d1) * std::exp(-alpha_SH * (s - d2));
    }
    Q[n] = q * cs.p_I0 / cs.p_qvol_scale;
  }

  o_qvol.copyFrom(Q.data(), Q.size());
  etag::printMinMax(mesh, eTag, o_qvol, 0.0, 0, "QVOL");
}
#endif // USE_QVOL

#if USE_JOULE
// Per-eTag jouleCoeff to the device, once. Needs cs.domain in the active scale, so after
// scale::apply(); the s3 U^4 is already folded in there.
void setupJoule(const PinkCase &cs)
{
  const auto c = etag::field(cs, &Domain::jouleCoeff);
  o_jouleCoeff.resize(c.size());
  o_jouleCoeff.copyFrom(c.data(), c.size());
}

// Ohmic dissipation q_M''' = (sigma_r/sigma) N_r Ec_r (j*)^2, added on top of the neutron
// q'''. eq.pdf eq.14. o_J is read only. j is from the previous step (computeCurrent runs
// after userSource) -- a standard lag, and this term is ~0.1% of q_R'''.
// q_M''' into o_dst, no accumulation. Shared by jouleSource and the QJOU report.
void jouleField(occa::memory o_dst)
{
  auto &m = nrs->mhdIndless;
  platform->linAlg->magSqrVector(m->mesh->Nlocal, m->fieldOffset, m->o_currentDensity(), o_dst);
  eTagScale(m->mesh->Nelements, o_eTag, o_jouleCoeff, o_dst);
}

void jouleSource()
{
  auto mesh = nrs->mhdIndless->mesh;
  poolDeviceMemory<dfloat> o_j2(mesh->Nlocal);
  jouleField(o_j2);
  platform->linAlg->axpby(mesh->Nlocal, 1.0, o_j2, 1.0,
                          nrs->scalar->o_explicitTerms("temperature"));
}
#endif // USE_JOULE

#if USE_QVOL || USE_JOULE
} // namespace heat
#endif

#if USE_MHD
namespace mhd {

// o_sigma and o_ulocScale are both slices of the solver's o_prop, so they are set together
// here. Must run before the step-0 computeCurrent/computeLorentz, which is why the properties
// callback and not UDF_Setup: see notes/11.
void properties(const PinkCase &cs)
{
  auto &m = nrs->mhdIndless;
  etag::setProp(m->mesh, m->o_diffusionCoeff(), etag::field(cs, &Domain::conE));

  // only allocated when the par sets useUlocScale (mhdInductionlessSolver.cpp:99-105)
  if (m->useUlocScale) {
    etag::setProp(m->mesh, m->o_ulocScale, etag::field(cs, &Domain::Uscale));
  }
}

// the inductionless solver must actually be enabled in the par
void setup(const PinkCase &cs)
{
  nekrsCheck(!nrs->mhdIndless,
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "%s\n",
             "USE_MHD is on but the inductionless solver is not");

  // o_ulocScale carries the per-fluid U into the MHD terms (note 07 sec 5). The solver only
  // allocates the slice when the par enables it (mhdInductionlessSolver.cpp:99-105), so the par
  // decides whether properties() writes it. s2 runs without; s3 needs it, so that combo is fatal.
  const bool on = nrs->mhdIndless->useUlocScale;
  nekrsCheck(cs.scaleCfg.mode == ScaleMode::S3 && !on,
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "%s\n",
             "scaleMode = s3 needs [MHD INDUCTIONLESS] useUlocScale = true in the par");

  if (platform->comm.mpiRank() == 0) {
    printf("chk-scale ulocScale= %s (par); Uscale per domain above\n", on ? "on" : "off");
    fflush(stdout);
  }
}

#ifdef NEKRS_ASCENT_ENABLED
// o_cur_mag is filled by report_ascent_fields(), which also prints its min/max per domain.

void addAscentVars(mesh_t *meshT)
{
  if (!nrs->mhdIndless) return;
  auto &m = nrs->mhdIndless;

  deviceMemory<dfloat> o_Phi(m->o_Phi);
  nekAscent::addVariable("potential", meshT, std::vector<deviceMemory<dfloat>>{o_Phi});

  auto o_J = m->o_currentDensity();
  std::vector<deviceMemory<dfloat>> o_Jvec;
  for (int i = 0; i < 3; i++) {
    o_Jvec.push_back(deviceMemory<dfloat>(o_J.slice(i * m->fieldOffset)));
  }
  nekAscent::addVariable("current", meshT, o_Jvec);

  o_cur_mag.resize(meshT->Nlocal);
  nekAscent::addVariable("current_mag", meshT, std::vector<deviceMemory<dfloat>>{o_cur_mag});
}
#endif

} // namespace mhd
#endif // USE_MHD

#if USE_ISOTOPES
// Length of the bc->id-indexed recirculation buffer. PURE -- no collectives: updateBC calls it
// every timestep, and a nekrsCheck here would be an MPI_Allreduce per step (notes/14).
int isoBidSlots(const PinkCase &cs)
{
  int maxBid = 0;
  for (const auto &in : cs.s1_inlet) {
    maxBid = std::max(maxBid, in.bid);
  }
  return maxBid + 1; // slot 0 is unused: NekRS boundary ids start at 1
}

// Fatal on anything that would make the bid -> domain scatter ambiguous or out of range.
// Called once, from allocate_usrwrk, on every rank.
void validate_inlet_bids(const PinkCase &cs)
{
  const int ndom = static_cast<int>(cs.s1_domain.size());
  std::vector<int> owner(isoBidSlots(cs), -1);
  for (size_t k = 0; k < cs.s1_inlet.size(); ++k) {
    const auto &in = cs.s1_inlet[k];
    nekrsCheck(in.bid < 1 || in.eTag < 1 || in.eTag > ndom,
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "inlet %s has bid %d eTag %d: bid must be >= 1 and eTag in [1,%d]\n",
               in.name, in.bid, in.eTag, ndom);
    nekrsCheck(cs.s1_domain[in.eTag - 1].kind != FLUID,
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "inlet %s points at eTag %d (%s), which is not a FLUID domain\n",
               in.name, in.eTag, cs.s1_domain[in.eTag - 1].name);
    nekrsCheck(owner[in.bid] >= 0,
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "bid %d is claimed by inlets %s and %s: the scatter would be ambiguous\n",
               in.bid, cs.s1_inlet[owner[in.bid]].name, in.name);
    owner[in.bid] = static_cast<int>(k);
  }
}
#endif

#if USE_ISOTOPES
namespace isotopes {

// D per species, transportCoeff = 1 (was [SCALAR G*] in the par)
void properties(const PinkCase &cs)
{
  auto &scalar = nrs->scalar;
  std::vector<dfloat> D(NumOfDomain), tc(NumOfDomain, cs.p_iso_transportCoeff);
  for (int g = 0; g < NumOfIsotopes; ++g) {
    const std::string key = "G" + std::to_string(g + 1);
    auto mesh = scalar->mesh(key);
    for (int e = 0; e < NumOfDomain; ++e) { D[e] = cs.isotope[g][e].D; }
    etag::setProp(mesh, scalar->o_diffusionCoeff(key), D);
    etag::setProp(mesh, scalar->o_transportCoeff(key), tc);
  }
}

#if USE_ISO_IMPLICIT
// lambda1 diagonal for one scalar, or o_NULL for anything that is not an isotope.
// The solver adds this to (g0/dt)*rho and solves lambda1*c - div(lambda0 grad c) = rhs,
// so a POSITIVE entry damps -- and lambda is positive, so the entry IS lambda.
occa::memory implicitDecay(double time, int scalarIdx)
{
  for (size_t g = 0; g < iso_scalarIdx.size(); ++g) {
    if (scalarIdx == iso_scalarIdx[g]) return o_iso_implicit[g];
  }
  return o_NULL;
}

// Fill lambda*transportCoeff per node and resolve each G scalar's integer index, which is
// what userImplicitLinearTerm is handed. Both are constant in time, so this runs once.
void setupImplicit(const PinkCase &cs)
{
  auto &scalar = nrs->scalar;

  o_iso_implicit.resize(NumOfIsotopes);
  iso_scalarIdx.assign(NumOfIsotopes, -1);

  std::vector<dfloat> lam(NumOfDomain, 0.0);
  for (int g = 0; g < NumOfIsotopes; ++g) {
    const std::string key = "G" + std::to_string(g + 1);
    auto mesh = scalar->mesh(key);

    auto it = scalar->nameToIndex.find(lowerCase(key));
    nekrsCheck(it == scalar->nameToIndex.end(),
               platform->comm.mpiComm(), EXIT_FAILURE,
               "USE_ISO_IMPLICIT: scalar %s is not in the par\n", key.c_str());
    iso_scalarIdx[g] = it->second;

    // the hook lives in the elliptic path only (scalarSolver.cpp setupEllipticSolver skips
    // CVODE scalars), so a CVODE G would silently drop the decay term.
    nekrsCheck(platform->options.compareArgs(
                   "SCALAR" + scalarDigitStr(it->second) + " SOLVER", "CVODE"),
               platform->comm.mpiComm(), EXIT_FAILURE,
               "USE_ISO_IMPLICIT: scalar %s uses CVODE; userImplicitLinearTerm is elliptic-only\n",
               key.c_str());

    for (int e = 0; e < NumOfDomain; ++e) {
      lam[e] = cs.isotope[g][e].lambda * cs.p_iso_transportCoeff;
    }
    o_iso_implicit[g].resize(mesh->Nlocal);
    etag::setProp(mesh, o_iso_implicit[g], lam);

    if (platform->comm.mpiRank() == 0) {
      printf("chk-iso-implicit %s idx=%d  lambda1 +=", key.c_str(), iso_scalarIdx[g]);
      for (int e = 0; e < NumOfDomain; ++e) { printf(" %.6e", lam[e]); }
      printf("\n");
    }
  }
  if (platform->comm.mpiRank() == 0) fflush(stdout);

  scalar->userImplicitLinearTerm = &implicitDecay;
}
#endif // USE_ISO_IMPLICIT

// Check the par declares the G scalars and the table has a row per species, then upload
// the per-eTag decay/delay coefficients. Needs NumOfDomain, so call after etag::setup.
void setup(const PinkCase &cs)
{
  for (int g = 0; g < NumOfIsotopes; ++g) {
    const std::string key = "G" + std::to_string(g + 1);
    nekrsCheck(!nrs->scalar || !nrs->scalar->mesh(key),
               platform->comm.mpiComm(),
               EXIT_FAILURE,
               "USE_ISOTOPES requires scalar %s in the par (scalars = temperature, g1, g2)\n",
               key.c_str());
  }

  nekrsCheck(NumOfIsotopes > static_cast<int>(cs.s2_isotope.size()),
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "NumOfIsotopes=%d exceeds the isotope table's %d species rows\n",
             NumOfIsotopes, static_cast<int>(cs.s2_isotope.size()));

  // source() writes FU = delay*q_R - lambda*c unweighted, but NekRS treats FU as rho-weighted
  // (sumMakef.okl) and lambda1 carries transportCoeff. The two paths agree only at 1.
  nekrsCheck(cs.p_iso_transportCoeff != 1.0,
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "p_iso_transportCoeff=%g: isotopes::source() assumes 1.0\n",
             cs.p_iso_transportCoeff);

  if (platform->comm.mpiRank() == 0) {
    for (int e = 0; e < NumOfDomain; ++e) {
      printf("chk-iso %-9s", cs.domain[e].name);
      for (int g = 0; g < NumOfIsotopes; ++g) {
        printf("  %s: lambda= %.6e delay= %.3f D= %.6e loop= %s",
               cs.s2_species[g].name, cs.isotope[g][e].lambda, cs.isotope[g][e].delay,
               cs.isotope[g][e].D, loopName(cs.isotope[g][e].loop));
      }
      printf("\n");
    }
    fflush(stdout);
  }

  o_iso_negLambda.resize(NumOfIsotopes);
  o_iso_delay.resize(NumOfIsotopes);
#if USE_OPENMC_SOURCE
  o_iso_src.resize(NumOfIsotopes);
  o_iso_srcScale.resize(NumOfIsotopes);
#endif

  std::vector<dfloat> tmp(NumOfDomain, 0.0);
  for (int i = 0; i < NumOfIsotopes; ++i) {
    // eTagAxpbyzField only adds, so the sink's minus lives in the coefficient: -lambda.
    // USE_ISO_IMPLICIT moves it to the lambda1 diagonal, so the explicit coefficient must
    // then be zero or the term is counted twice. Decided here and nowhere else.
    for (int e = 0; e < NumOfDomain; ++e) { tmp[e] = USE_ISO_IMPLICIT ? 0.0 : -cs.isotope[i][e].lambda; }
    o_iso_negLambda[i] = platform->device.malloc<dfloat>(NumOfDomain);
    o_iso_negLambda[i].copyFrom(tmp.data(), NumOfDomain);

    // The SOURCE must carry 1/Uscale in s3, and this is the only place it can.
    //
    // scale::apply preserves the s3 steady state for temperature by rhoCp *= U, leaving Q alone
    // (notes/07). The isotope scalars take the other route -- transportCoeff = 1, with D and
    // lambda divided by U -- and the two are NOT equivalent: dividing advection, diffusion and
    // the sink by U while leaving the source alone makes the source effectively U times too
    // large (321x in waterTBM, 333x in waterSH; PbLi is 1 so tritium never showed it). Dividing
    // the source coefficient here closes it. s2 is unaffected: every Uscale is 1.
    for (int e = 0; e < NumOfDomain; ++e) { tmp[e] = cs.isotope[i][e].delay / cs.domain[e].Uscale; }
    o_iso_delay[i] = platform->device.malloc<dfloat>(NumOfDomain);
    o_iso_delay[i].copyFrom(tmp.data(), NumOfDomain);

#if USE_OPENMC_SOURCE
    // The loaded field is already the production rate, so its coefficient is 1 -- times the
    // same 1/Uscale. No `delay`: the OpenMC field carries its own spatial distribution and the
    // hand table must not override the neutronics.
    for (int e = 0; e < NumOfDomain; ++e) { tmp[e] = 1.0 / cs.domain[e].Uscale; }
    o_iso_srcScale[i] = platform->device.malloc<dfloat>(NumOfDomain);
    o_iso_srcScale[i].copyFrom(tmp.data(), NumOfDomain);
#endif
  }

#if USE_ISO_IMPLICIT
  setupImplicit(cs);
#endif
}

// S_g = R_g - lambda_g * c_g,  R_g = delay_g * q_R'''   (per eTag; lambda is positive)
// The driver is the NEUTRON source o_qvol, not the temperature explicit terms: breeding and
// activation follow the neutron flux, and Joule heating (USE_JOULE) produces no neutrons.
// Reading o_qvol directly also removes any ordering dependence on userSource. See plan J.
void source()
{
  auto &scalar = nrs->scalar;

#if USE_QVOL
  auto o_qR = o_qvol;
#else
  // no neutron source -> only the sink. Zeroed once, reused every step.
  static deviceMemory<dfloat> o_zero;
  if (o_zero.size() < static_cast<size_t>(nrs->meshT->Nlocal)) {
    o_zero.resize(nrs->meshT->Nlocal);
    platform->linAlg->fill(nrs->meshT->Nlocal, 0.0, o_zero);
  }
  auto o_qR = o_zero;
#endif

  for (int g = 0; g < NumOfIsotopes; ++g) {
    const std::string key = "G" + std::to_string(g + 1);
    auto mesh = scalar->mesh(key);
#if USE_OPENMC_SOURCE
    // The loaded field IS the production rate: added directly, no delay coefficient.
    // Its per-eTag slot carries 1/Uscale only.
    eTagAxpbyzField(mesh->Nelements, o_eTag,
                    o_iso_srcScale[g], o_iso_src[g],
                    o_iso_negLambda[g], scalar->o_solution(key),
                    scalar->o_explicitTerms(key));
#else
    // Legacy: a per-domain yield times the SHARED HEAT field. The slot carries delay/Uscale.
    eTagAxpbyzField(mesh->Nelements, o_eTag,
                    o_iso_delay[g], o_qR,
                    o_iso_negLambda[g], scalar->o_solution(key),
                    scalar->o_explicitTerms(key));
#endif
  }
}

void updateBC(const PinkCase &cs, double time, int tstep, bool verbose)
{
  auto &scalar = nrs->scalar;
  auto mesh = nrs->fluid->mesh;

  auto surfAverage = [&](int bid, occa::memory o_fld) {

    const int h_bid[1] = {bid};

    static deviceMemory<int> o_bid;
    if (!o_bid.isInitialized()) {
      o_bid.resize(1);
    }
    o_bid.copyFrom(h_bid, 1);

    const dfloat integral = mesh->surfaceAreaMultiplyIntegrate(o_bid, o_fld.slice(0, mesh->Nlocal));

    return integral;
  };

  static bool firstCall = true;
  static deviceMemory<int> o_bid;
  static std::vector<dfloat> outletArea;
  static std::vector<dfloat> Tout;
  if (firstCall) {
    deviceMemory<dfloat> o_one(mesh->Nlocal);
    platform->linAlg->fill(o_one.size(), 1.0, o_one);

    outletArea.assign(NumOfDomain, 0.0);
    for (int d = 0; d < NumOfDomain; ++d) {
      if (cs.s2_domain[d].bid_outlet > 0) {
        outletArea[d] = surfAverage(cs.s2_domain[d].bid_outlet, o_one);
        // bid_outlet > 0 only says an id was declared, not that the mesh has faces with it.
        // A missing boundary gives area 0 and the recirculation below divides 0/0 -> NaN,
        // which then enters every inlet through the BC. Fail here instead.
        nekrsCheck(!(outletArea[d] > 0.0),
                   platform->comm.mpiComm(),
                   EXIT_FAILURE,
                   "domain %s declares outlet bid %d but its area is %.3e: no such boundary?\n",
                   cs.s2_domain[d].name, cs.s2_domain[d].bid_outlet, outletArea[d]);
      }
    }

    // Indexed [bid * NumOfIsotopes + i] so the kernel can read it with bc->id and needs no
    // bid->domain map of its own. Slots for bids that are not inlets stay 0.
    Tout.resize(isoBidSlots(cs) * NumOfIsotopes, 0);
    firstCall = false;
  }

  // outlet -> inlet recirculation, attenuated by decay over the external transit time.
  // lambda is POSITIVE, so the attenuation is exp(-lambda*tau), in (0,1].
  // lambda is s3-rescaled per domain but p_iso_transit is an s2 time: pair here (plan L).
  std::vector<dfloat> perDomain(NumOfDomain, 0.0);
  for (int i=0; i<NumOfIsotopes; i++) { /// IIIII
    const std::string key = "G" + std::to_string(i + 1);
    for (int d = 0; d < NumOfDomain; ++d) {
      perDomain[d] = 0.0;
      if (cs.s2_domain[d].bid_outlet <= 0) continue;
      const dfloat tau = cs.p_iso_transit * cs.domain[d].Uscale;
      // LOOP_OPEN zeroes the returned outlet average: once-through, the inlet sees fresh fluid.
      // Multiply rather than skip the surfAverage -- it is collective (notes/14), and making a
      // collective conditional to save two Allreduces per step is not a trade worth making.
      const dfloat loopFac = (cs.isotope[i][d].loop == LOOP_CLOSED) ? 1.0 : 0.0;
      perDomain[d] = (surfAverage(cs.s2_domain[d].bid_outlet, scalar->o_solution(key)) / outletArea[d]) *
                     exp(-cs.isotope[i][d].lambda * tau) * loopFac;
    }
    // scatter domain -> inlet bid. This is the ONLY place the bid->domain map is applied.
    for (const auto &in : cs.s1_inlet) {
      Tout[in.bid * NumOfIsotopes + i] = perDomain[in.eTag - 1];
    }
  }

  auto o_usrwrk = platform->app->bc->o_usrwrk.slice(p_bc_usrwrk_nfld * nrs->fluid->fieldOffset);
  o_usrwrk.copyFrom(Tout.data(), isoBidSlots(cs) * NumOfIsotopes);

  if ((platform->verbose() || verbose) && platform->comm.mpiRank() == 0) {
    for (int i = 0; i < NumOfIsotopes; ++i) {
      printf("chk-isotopes-bc step=%d time=%.8e  G%d:", tstep, time, i + 1);
      for (const auto &in : cs.s1_inlet) {
        printf("  %s= %.8e", in.name, Tout[in.bid * NumOfIsotopes + i]);
      }
      printf("\n");
    }
    fflush(stdout);
  }
}

} // namespace isotopes
#endif // USE_ISOTOPES

// ================================================================================================================
namespace recycle {

void setup(const PinkCase &cs)
{
  // internal work for planarCopy
  o_planarWork.resize(3 * nrs->fluid->fieldOffset);

  deviceMemory<dfloat> o_one(nrs->meshT->Nlocal);
  platform->linAlg->fill(o_one.size(), 1.0, o_one);

  // one routine for both velocity (nFields=dim) and potential (nFields=1)
  auto set_recycling = [&](RecyclingData &r,
                           mesh_t *mesh,
                           const occa::memory &o_fld,
                           int nFields,
                           dlong fieldOffset,
                           int bid,
                           dfloat length)
  {
    auto o_one1 = o_one.slice(0, mesh->Nlocal);

    r.bID = bid;

    const int h_bID[1] = {bid};
    r.o_bID.resize(1);
    r.o_bID.copyFrom(h_bID, 1);

    // get inlet area and outward normal
    r.area = mesh->surfaceAreaMultiplyIntegrate(r.o_bID, o_one1);
    r.normal = mesh->surfaceAreaNormalMultiplyIntegrate(r.o_bID, o_one1);
    for (auto &v : r.normal) {
      v /= r.area;
    }
    dfloat norm2 = 0.0;
    for (auto &v : r.normal) {
      norm2 += v * v;
    }
    const dfloat scale = 1.0 / std::sqrt(norm2);
    for (auto &v : r.normal) {
      v *= scale;
    }

    // offset to recycle
    const dfloat dx = -r.normal[0] * length;
    const dfloat dy = -r.normal[1] * length;
    const dfloat dz = -r.normal[2] * length;

    r.recyc = new planarCopy(mesh, o_fld, nFields, fieldOffset, dx, dy, dz, bid, o_planarWork);

    if (platform->comm.mpiRank() == 0) {
      printf("chk-rec0  bid= %d  area= %.4e  dxyz= %.4e %.4e %.4e\n",
             bid, r.area, dx, dy, dz);
      fflush(stdout);
    }
  };

  auto &fluid = nrs->fluid;
  rec.resize(cs.s2_inlet.size());
  for (size_t i = 0; i < cs.s2_inlet.size(); ++i) {
    set_recycling(rec[i], fluid->mesh, fluid->o_solution(), fluid->mesh->dim,
                  fluid->fieldOffset, cs.s2_inlet[i].bid, cs.p_recycLength / cs.s1_unit.L);
  }

#if RECYCLE_PHI_ON
  { // potential at the PbLi inlet, same routine
    auto &m = nrs->mhdIndless;
    set_recycling(recPhi, m->mesh, m->o_Phi, 1, m->fieldOffset,
                  scale::inletOfDomain(cs, cs.s1_unit.dm_id).bid, cs.p_recycLength / cs.s1_unit.L);
  }
#endif
}

#if RECYCLE_PHI_ON
void applyPhi()
{
  auto &fluid = nrs->fluid;
  auto mesh = nrs->mhdIndless->mesh;
  auto o_usrwrk = platform->app->bc->o_usrwrk.slice(fluid->fieldOffsetSum);
  platform->linAlg->fill(fluid->fieldOffset, 0.0, o_usrwrk);

  recPhi.recyc->execute(); // reset o_planarWork
  axpbySurface(mesh->Nelements, recPhi.bID, 1, fluid->fieldOffset,
               mesh->o_vmapM, mesh->o_EToB, 1.0, o_planarWork, 1.0, o_usrwrk);
}
#endif

void apply(const PinkCase &cs, double time, int tstep, bool verbose)
{
  auto& fluid = nrs->fluid;

  platform->linAlg->fill(fluid->fieldOffsetSum, 0.0, platform->app->bc->o_usrwrk);

  auto upd_recycling = [&](RecyclingData &r, dfloat uBulk)
  {
    auto mesh = fluid->mesh;
    r.recyc->execute(); // reset o_planarWork

    const auto flux =
      mesh->surfaceAreaNormalMultiplyVectorIntegrate(fluid->fieldOffset,
                                                     r.o_bID,
                                                     o_planarWork);
    const auto scale = -uBulk * r.area / flux;
    axpbySurface(mesh->Nelements,
                 r.bID,
                 mesh->dim,
                 fluid->fieldOffset,
                 mesh->o_vmapM, mesh->o_EToB,
                 scale, o_planarWork, 1.0, platform->app->bc->o_usrwrk);

    if ((platform->verbose() || verbose) && platform->comm.mpiRank() == 0) {
      printf("chk-rec %d %g bid= %d  flux= %.4e  uBulk= %.4e  scale= %.4e\n",
             tstep, time, r.bID, flux, uBulk, scale);
      fflush(stdout);
    }
  };

  for (size_t i = 0; i < cs.s2_inlet.size(); ++i) {
    upd_recycling(rec[i], p_inletFlowFactor[i] * cs.p_U_inlet[i]);
  }

#if RECYCLE_PHI_ON
  recycle::applyPhi();
#endif
}

} // namespace recycle


// slots reported by chk-usrwrk; 0 when isotopes are off, where the block does not exist
int isoBidSlotsOrZero(const PinkCase &cs)
{
#if USE_ISOTOPES
  return isoBidSlots(cs);
#else
  (void)cs;
  return 0;
#endif
}

// for BC usrwrk
void allocate_usrwrk(const PinkCase &cs)
{
  p_bc_usrwrk_size = p_bc_usrwrk_nfld * nrs->fluid->fieldOffset;
  const int nbid = isoBidSlotsOrZero(cs); // resolved before the rank-0 print below uses it
#if USE_ISOTOPES
  // one recirculation value per (inlet bid, species): the kernel indexes it by bc->id
  nekrsCheck(NumOfIsotopes < 1,
             platform->comm.mpiComm(),
             EXIT_FAILURE,
             "allocate_usrwrk needs NumOfIsotopes (%d) set first\n",
             NumOfIsotopes);
  validate_inlet_bids(cs); // the one place the bid table is checked; collective, so not guarded
  p_bc_usrwrk_size += NumOfIsotopes * nbid;
#endif
  const int size0 = p_bc_usrwrk_size; // for print
  p_bc_usrwrk_size = alignStride<dfloat>(p_bc_usrwrk_size);

  platform->app->bc->o_usrwrk.resize(p_bc_usrwrk_size);

  if (platform->comm.mpiRank() == 0) {
    printf("chk-usrwrk: nfid= %d  nvar= %d  nbid= %d  size %d %d\n",
           p_bc_usrwrk_nfld, NumOfIsotopes, nbid, size0, p_bc_usrwrk_size);
    fflush(stdout);
  }
}

// [CASEDATA] resetFields = all | none | velocity,temperature,phi,isotopes
// Absent or "none" leaves it 0, so a restart restarts. Call from UDF_Setup0.
void read_resetFields(PinkCase &cs)
{
  std::string str;
  if (!platform->par->extract("casedata", "resetfields", str)) return;

  if (str == "all") {
    cs.p_resetFields = RESET_ALL;
  } else if (str != "none") {
    for (const auto &f : serializeString(str, ',')) {
      if      (f == "velocity")    cs.p_resetFields |= RESET_VELOCITY;
      else if (f == "temperature") cs.p_resetFields |= RESET_TEMPERATURE;
      else if (f == "phi")         cs.p_resetFields |= RESET_PHI;
      else if (f == "isotopes")    cs.p_resetFields |= RESET_ISOTOPES;
      else nekrsCheck(1, platform->comm.mpiComm(), EXIT_FAILURE,
                      "unknown resetFields entry '%s'\n", f.c_str());
    }
  }

  if (platform->comm.mpiRank() == 0) {
    printf("chk-prop resetFields= 0x%x (%s)\n", cs.p_resetFields, str.c_str());
    fflush(stdout);
  }
}

// Initial conditions. On a fresh start every field is set. On a restart only the fields
// named by [CASEDATA] resetFields are re-initialized; the rest come from the file.
// Logs what it did, per field, so a long run records its own starting state.
void setup_ic(PinkCase &cs, double time)
{
  auto &fluid = nrs->fluid;
  auto &mhdIndless = nrs->mhdIndless;
  auto &scalar = nrs->scalar;

  const bool fresh = platform->options.getArgs("RESTART FILE NAME").empty();
  auto want = [&](unsigned f) { return fresh || (cs.p_resetFields & f); };

  auto report = [&](const char *what, bool did) {
    if (platform->comm.mpiRank() == 0) {
      printf("chk-ic %.4e %-11s %s\n", time, what, did ? "SET" : "KEPT from file");
    }
  };

  if (platform->comm.mpiRank() == 0) {
    printf("chk-ic %.4e restart= %s  resetFields= 0x%x\n", time, fresh ? "no" : "yes",
           fresh ? RESET_ALL : cs.p_resetFields);
  }

  if (want(RESET_VELOCITY)) { // small seed, per domain, from its own inlet
    auto mesh = fluid->mesh;
    std::vector<dfloat> U(mesh->dim * fluid->fieldOffset, 0.0);
    for (int e = 0; e < mesh->Nelements; ++e) {
      const int d = eTag[e] - 1;
      const dfloat u0 = 0.01 * p_inletFlowFactor[0] * ((d >= 0) ? cs.domain[d].Uscale : 1.0);
      for (int i = 0; i < mesh->Np; ++i) {
        const int n = e * mesh->Np + i;
        for (int c = 0; c < mesh->dim; ++c) {
          U[n + c * fluid->fieldOffset] = u0;
        }
      }
    }
    fluid->o_solution().copyFrom(U.data(), U.size());
  }
  report("velocity", want(RESET_VELOCITY));

  if (want(RESET_TEMPERATURE)) {
    etag::setProp(nrs->meshT, scalar->o_solution("temperature"), etag::field(cs, &Domain::temp0));
  }
  report("temperature", want(RESET_TEMPERATURE));

#if USE_ISOTOPES
  if (want(RESET_ISOTOPES)) {
    for (int g = 0; g < NumOfIsotopes; ++g) {
      const std::string key = "G" + std::to_string(g + 1);
      platform->linAlg->fill(scalar->mesh(key)->Nlocal, 0.0, scalar->o_solution(key));
    }
  }
  report("isotopes", want(RESET_ISOTOPES));
#endif

#if USE_MHD
  if (want(RESET_PHI)) {
    platform->linAlg->fill(mhdIndless->mesh->Nlocal, 0.0, mhdIndless->o_Phi);
  }
  report("phi", want(RESET_PHI));
#endif

  if (platform->comm.mpiRank() == 0) {
    fflush(stdout);
  }
}

void setup_ascent()
{
#ifdef NEKRS_ASCENT_ENABLED
  {
    platform->par->extract("casedata", "ioStepAscent", ioStepAscent);

    auto meshV = nrs->fluid->mesh;
    auto meshT = nrs->meshT;

    deviceMemory<dfloat> o_ux(nrs->fluid->o_U.slice(0 * nrs->fluid->fieldOffset));
    deviceMemory<dfloat> o_uy(nrs->fluid->o_U.slice(1 * nrs->fluid->fieldOffset));
    deviceMemory<dfloat> o_uz(nrs->fluid->o_U.slice(2 * nrs->fluid->fieldOffset));
    std::vector<deviceMemory<dfloat>> o_U{o_ux, o_uy, o_uz};
    nekAscent::addVariable("velocity", meshV, o_U);

    o_vel_mag.resize(meshV->Nlocal);
    nekAscent::addVariable("velocity_mag", meshV, std::vector<deviceMemory<dfloat>>{o_vel_mag});

    deviceMemory<dfloat> o_T(nrs->scalar->o_solution("temperature"));
    nekAscent::addVariable("temperature", meshT, std::vector<deviceMemory<dfloat>>{o_T});

#if USE_MHD
    mhd::addAscentVars(meshT);
#endif

    const int Nviz = std::min(static_cast<int>(meshT->N * 1.5), 13);
    const auto uniform = true;
    const auto stageThroughHost = false; // true has issue for second vis
    const auto async = false;//true;
    nekAscent::setup(meshT, "ascent.yaml", Nviz, uniform, stageThroughHost, async);
  }
#endif
}

// Clip every scalar to its physical range. T and the isotopes are always s2, so these
// bounds are scale-independent.
void clipt(double tmax, double isomax)
{
  auto &scalar = nrs->scalar;

  clipOKL(nrs->meshT->Nelements, 0.0, tmax, scalar->o_solution("temperature")); // T [degC]

#if USE_ISOTOPES
  for (int g = 0; g < NumOfIsotopes; ++g) {
    const std::string key = "G" + std::to_string(g + 1);
    clipOKL(scalar->mesh(key)->Nelements, 0.0, isomax, scalar->o_solution(key));
  }
#endif
}

// modified nrs_t::computeCFL for domain cfl
void computeCFL(double time, int tstep)
{
  auto mesh = nrs->fluid->mesh;
  auto o_U = nrs->fluid->o_U;
  auto dt = nrs->dt[0];

  auto o_invDxRst = [&]() {
    static occa::memory o_dx;
    if (o_dx.isInitialized()) {
      return o_dx;
    }

    o_dx = platform->device.malloc<dfloat>(mesh->N + 1);

    auto mesh = nrs->meshT;
    std::vector<dfloat> dx(mesh->N + 1);
    for (int n = 0; n < (mesh->N + 1); n++) {
      if (n == 0) {
        dx[n] = mesh->gllz[n + 1] - mesh->gllz[n];
      } else if (n == mesh->N) {
        dx[n] = mesh->gllz[n] - mesh->gllz[n - 1];
      } else {
        dx[n] = 0.5 * (mesh->gllz[n + 1] - mesh->gllz[n - 1]);
      }
      dx[n] = 1.0 / dx[n];
    }
    o_dx.copyFrom(dx.data());

    return o_dx;
  }();

  auto o_cfl = platform->deviceMemoryPool.reserve<dfloat>(mesh->Nelements);
  launchKernel("nrs-cflHex3D",
               mesh->Nelements,
               dt,
               mesh->o_vgeo,
               o_invDxRst,
               nrs->fluid->fieldOffset,
               o_U,
               (nrs->geom) ? nrs->geom->o_U : o_NULL,
               o_cfl);

  auto scratch = platform->memoryPool.reserve<dfloat>(o_cfl.size());
  auto scratchPtr = scratch.ptr<dfloat>();

  o_cfl.copyTo(scratchPtr);

  std::vector<dfloat> cflMax(NumOfDomain + 1, 0.0); // [0] = all, [1..NumOfDomain] = per tag

  for (dlong n = 0; n < mesh->Nelements; ++n) {
    const int tag = eTag[n];
    if (1 <= tag && tag <= NumOfDomain) {
      cflMax[tag] = std::max(cflMax[tag], scratchPtr[n]);
      cflMax[0] = std::max(cflMax[0], scratchPtr[n]);
    }
  }

  MPI_Allreduce(MPI_IN_PLACE, cflMax.data(), NumOfDomain + 1, MPI_DFLOAT, MPI_MAX, platform->comm.mpiComm());

  if (platform->comm.mpiRank() == 0) {
    printf("chk-cflMax %d %.8e  (eTag): ", tstep, time);
    for (int tag = 1; tag <= NumOfDomain; ++tag) {
      printf("  %.4e", cflMax[tag]);
    }
    printf("  (all %.4e)\n", cflMax[0]);
    fflush(stdout);
  }
}

// Per-step reporting. Everything that only prints lives here, so UDF_ExecuteStep shows the
// step's actions and not its diagnostics.
// Collective: etag::printMinMax reduces over all ranks. Verbosity only, no rank guard.
void report_solution_minmax(double time, int tstep, bool verbose)
{
  if (!(platform->verbose() || verbose)) return;

  print_timestamp(tstep);
  nrs->printSolutionMinMax();
  etag::printMinMax(nrs->meshT, eTag, nrs->scalar->o_solution("temperature"), time, tstep, "TEMP");

  { // velocity components + magnitude
    auto &fluid = nrs->fluid;
    auto mesh = fluid->mesh;
    for (int d = 0; d < mesh->dim; ++d) {
      const char *tag[] = {"VX  ", "VY  ", "VZ  "};
      etag::printMinMax(mesh, eTag, fluid->o_U + d * fluid->fieldOffset, time, tstep, tag[d]);
    }
  }
#if USE_MHD
  if (nrs->mhdIndless) { // current density components + magnitude
    auto &m = nrs->mhdIndless;
    auto mesh = m->mesh;
    auto o_J = m->o_currentDensity();
    for (int d = 0; d < mesh->dim; ++d) {
      const char *tag[] = {"JX  ", "JY  ", "JZ  "};
      etag::printMinMax(mesh, eTag, o_J + d * m->fieldOffset, time, tstep, tag[d]);
    }
  }
#endif
#if USE_JOULE
  { // q_M''' recomputed for the report; expect ~1e-3 of QVOL. See plan J.
    auto mesh = nrs->mhdIndless->mesh;
    poolDeviceMemory<dfloat> o_j2(mesh->Nlocal);
    heat::jouleField(o_j2);
    etag::printMinMax(mesh, eTag, o_j2, time, tstep, "QJOU");
  }
#endif
#if USE_ISOTOPES
  for (int g = 0; g < NumOfIsotopes; ++g) {
    const std::string key = "G" + std::to_string(g + 1);
    etag::printMinMax(nrs->meshT, eTag, nrs->scalar->o_solution(key), time, tstep, key);
  }
#endif
}

#ifdef NEKRS_ASCENT_ENABLED
// The Ascent-only fields: fills o_vel_mag / o_cur_mag (allocated by setup_ascent), which
// Ascent renders. Call AFTER scale::toOutput -- it rescales o_U in place, so a magnitude
// taken before it would be s3 while every other Ascent variable is s2.
// etag::printMinMax is collective (MPI_Allreduce) and guards rank 0 itself, so `report`
// must stay a pure verbosity test -- a rank guard around it would deadlock.
void report_ascent_fields_minmax(double time, int tstep, bool verbose)
{
  const bool report = platform->verbose() || verbose;

  { // velocity components + magnitude
    auto &fluid = nrs->fluid;
    auto mesh = fluid->mesh;
    platform->linAlg->magVector(mesh->Nlocal, fluid->fieldOffset, fluid->o_U, o_vel_mag);
    if (report) {
      for (int d = 0; d < mesh->dim; ++d) {
        const char *tag[] = {"VX  ", "VY  ", "VZ  "};
        etag::printMinMax(mesh, eTag, fluid->o_U + d * fluid->fieldOffset, time, tstep, tag[d]);
      }
      etag::printMinMax(mesh, eTag, o_vel_mag, time, tstep, "VMAG");
    }
  }

#if USE_MHD
  if (nrs->mhdIndless) { // current density components + magnitude
    auto &m = nrs->mhdIndless;
    auto mesh = m->mesh;
    auto o_J = m->o_currentDensity();
    platform->linAlg->magVector(mesh->Nlocal, m->fieldOffset, o_J, o_cur_mag);
    if (report) {
      for (int d = 0; d < mesh->dim; ++d) {
        const char *tag[] = {"JX  ", "JY  ", "JZ  "};
        etag::printMinMax(mesh, eTag, o_J + d * m->fieldOffset, time, tstep, tag[d]);
      }
      etag::printMinMax(mesh, eTag, o_cur_mag, time, tstep, "JMAG");
    }
  }
#endif
}
#endif // NEKRS_ASCENT_ENABLED

void dump_extra_mhd_files(double time)
{
  auto& mhdIndless = nrs->mhdIndless;
  if (!mhdIndless) return;

  static std::unique_ptr<iofld> io;
  if (!io) {
    io = iofldFactory::create();
  }

  auto mesh = mhdIndless->mesh;

  if (!io->isInitialized()) {
    io->open(mesh, iofld::mode::write, std::string("curr") + platform->options.getArgs("CASENAME"));

    std::vector<occa::memory> o_J;
    for (int i = 0; i < mesh->dim; i++) {
      o_J.push_back(mhdIndless->o_currentDensity().slice(i * mhdIndless->fieldOffset, mesh->Nlocal));
    }
    io->addVariable("velocity", o_J);
  }

  io->writeAttribute("polynomialOrder", std::to_string(mesh->N));

  auto FP64 = platform->options.compareArgs("CHECKPOINT PRECISION", "FP64");
  auto outXYZ = platform->options.compareArgs("CHECKPOINT OUTPUT MESH", "TRUE");
  io->writeAttribute("precision", (FP64) ? "64" : "32");
  io->writeAttribute("uniform", "false");
  io->writeAttribute("outputMesh", (outXYZ) ? "true" : "false");

  io->addVariable("time", time);
  io->process();
}
#endif // none okl

#ifdef __okl__
// ================================================================================================================
// Generic per-eTag kernels. No case topology here -- they take eTag plus a
// coefficient array. Reusable by any multi-domain case.


@kernel void clipOKL(const dlong Nelements,
                     const dfloat tmin,
                     const dfloat tmax,
                     @restrict dfloat * TEMP)
{
  for(dlong e=0;e<Nelements;++e;@outer(0)){
    for (int j = 0; j < p_Nq; ++j; @inner(1)) {
      for (int i = 0; i < p_Nq; ++i; @inner(0)) {
        for (int k = 0; k < p_Nq; ++k) {
          const dlong id = e * p_Np + k * p_Nq * p_Nq + j * p_Nq + i;
          dfloat temp = TEMP[id];
          if(temp > tmax) temp = tmax;
          if(temp < tmin) temp = tmin;
          TEMP[id] = temp;
        }
      }
    }
  }
}

@kernel void axpbySurface(const dlong Nelements, // This only works when no corners
                          const dlong bcid,
                          const int nFields,
                          const dlong offset,
                          @ restrict const dlong *vmapM,
                          @ restrict const int *EToB,
                          const dfloat alpha,
                          @ restrict const dfloat *x,
                          const dfloat beta,
                          @ restrict dfloat *y)
{
  for (dlong e = 0; e < Nelements; e++; @outer(0))
    for (int f = 0; f < p_Nfaces; f++) {
      @barrier();
      for (int m = 0; m < p_Nfp; ++m; @inner(0)) {
        const int n = m + f * p_Nfp;
        const int idM = vmapM[e * p_Nfp * p_Nfaces + n];
        const int id = EToB[f + p_Nfaces * e];
        if (id == bcid) {
          for(int i = 0; i < nFields; i++) {
            y[idM + i * offset] = alpha * x[idM + i * offset] + beta * y[idM + i * offset];
          }
        }
      }
    }
}


@kernel void eTagScale(const dlong Nelements,
                       @restrict const dlong *eTag,
                       @restrict const dfloat *eCoeff,
                       @restrict dfloat *FLD)
{
  for(dlong e=0;e<Nelements;++e;@outer(0)){
    const dlong tag = eTag[e] - 1; // 1-base back to 0-base
    const dfloat coef = eCoeff[tag];
    for (int j = 0; j < p_Nq; ++j; @inner(1)) {
      for (int i = 0; i < p_Nq; ++i; @inner(0)) {
        for (int k = 0; k < p_Nq; ++k) {
          const dlong id = e * p_Np + k * p_Nq * p_Nq + j * p_Nq + i;
          FLD[id] *= coef;
        }
      }
    }
  }
}

@kernel void eTagFill(const dlong Nelements,
                      @restrict const dlong *eTag,
                      @restrict const dfloat *eCoeff,
                      @restrict dfloat *FLD)
{
  for(dlong e=0;e<Nelements;++e;@outer(0)){
    const dlong tag = eTag[e] - 1; // 1-base back to 0-base
    const dfloat coef = eCoeff[tag];
    for (int j = 0; j < p_Nq; ++j; @inner(1)) {
      for (int i = 0; i < p_Nq; ++i; @inner(0)) {
        for (int k = 0; k < p_Nq; ++k) {
          const dlong id = e * p_Np + k * p_Nq * p_Nq + j * p_Nq + i;
          FLD[id] = coef;
        }
      }
    }
  }
}

// Z = a * X + b * Y, a,b per-eTag scalars, X,Y,Z nodal fields
@kernel void eTagAxpbyzField(const dlong Nelements,
                        @restrict const dlong *eTag,
                        @restrict const dfloat *eCoeffa,
                        @restrict const dfloat *X,
                        @restrict const dfloat *eCoeffb,
                        @restrict const dfloat *Y,
                        @restrict dfloat *Z)
{
  for(dlong e=0;e<Nelements;++e;@outer(0)){
    const dlong tag = eTag[e] - 1; // 1-base back to 0-base
    const dfloat a = eCoeffa[tag];
    const dfloat b = eCoeffb[tag];
    for (int j = 0; j < p_Nq; ++j; @inner(1)) {
      for (int i = 0; i < p_Nq; ++i; @inner(0)) {
        for (int k = 0; k < p_Nq; ++k) {
          const dlong id = e * p_Np + k * p_Nq * p_Nq + j * p_Nq + i;
          Z[id] = a * X[id] + b * Y[id];
        }
      }
    }
  }
}

#endif // __okl__

#endif // PINK_HPP

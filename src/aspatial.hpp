/** @file aspatial.hpp
 * @brief Residual and Jacobian computations
 * @author Aditya Kashi
 * @date 2017-03-04
 */

#ifndef __ASPATIAL_H
#define __ASPATIAL_H 1

#ifndef __ACONSTANTS_H
#include "aconstants.hpp"
#endif

#ifndef __AARRAY2D_H
#include "aarray2d.hpp"
#endif

#ifndef __AMESH2DH_H
#include "amesh2dh.hpp"
#endif

#ifndef __AELEMENTS_H
#include "aelements.hpp"
#endif

#ifndef __ARECONSTRUCTION_H
#include "areconstruction.hpp"
#endif

#include <Eigen/LU>

namespace acfd {

/// Base class for spatial discretization and integration of weak forms of PDEs
/**
 * Provides residual computation, and potentially residual Jacobian evaluation, interface for all solvers.
 * The template parameter nvars is the number of variables in the PDE system.
 * \note Make sure compute_topological() has been called on the mesh object prior to initialzing an object of any subclass.
 */
class SpatialBase
{
protected:
	const UMesh2dh* m;								///< Mesh context; requires compute_topological() and compute_boundary_maps() to have been called
	std::vector<Matrix> minv;						///< Inverse of mass matrix for each variable of each element
	std::vector<Matrix> res;						///< Residuals - a Matrix contains residual DOFs for an element
	int p_degree;									///< Polynomial degree of trial/test functions
	a_int ntotaldofs;								///< Total number of DOFs in the discretization (for 1 physical variable)
	char basis_type;								///< Type of basis to use - Lagrange ('l') or Taylor ('t')
	bool reconstruct;								///< Use reconstruction or not

	/// Maximum allowable explicit time step for each element
	/** For Euler, stores (for each elem i) Vol(i) / \f$ \sum_{j \in \partial\Omega_I} \int_j( |v_n| + c) d \Gamma \f$, 
	 * where v_n and c are average values of the cell faces
	 */
	std::vector<a_real> mets;

	Quadrature2DTriangle* dtquad;				///< Domain quadrature context
	Quadrature2DSquare* dsquad;					///< Domain quadrature context
	Quadrature1D* bquad;						///< Boundary quadrature context
	LagrangeMapping2D* map2d;					///< Array containing geometric mapping data for each element
	LagrangeMapping1D* map1d;					///< Array containing geometric mapping data for each face
	Element** elems;							///< List of finite elements
	Element* dummyelem;							///< Empty element used for ghost elements
	FaceElement* faces;							///< List of face elements

	///@{
	/// Integral of fluxes across each face for all dofs of left and right element
	/** The entries corresponding to different DOFs of a given flow variable are stored contiguously.
	 */
	std::vector<Matrix> leftfaceterms;
	std::vector<Matrix> rightfaceterms;
	///@}

	/// vector of unknowns
	/** Each Eigen3 (E3) Matrix contains the DOF values of all physical variables for an element.
	 */
	std::vector<Matrix> u;

	amat::Array2d<a_real> scalars;			///< Holds density, Mach number and pressure for each mesh point
	amat::Array2d<a_real> velocities;		///< Holds velocity components for each mesh point

	/* Reconstruction-related stuff - currently not implemented
	//Reconstruction* rec;						///< Reconstruction context
	//FaceDataComputation* lim;					///< Limiter context

	// Ghost cell centers
	amat::Array2d<a_real> ghc;

	/// Ghost elements' flow quantities
	std::vector<Vector> ug;

	/// computes ghost cell centers assuming symmetry about the midpoint of the boundary face
	void compute_ghost_cell_coords_about_midpoint();

	/// computes ghost cell centers assuming symmetry about the face
	void compute_ghost_cell_coords_about_face();
	*/

	/// Computes the L2 error in a FE function on an element
	/** \param[in] comp The index of the row of ug whose error is to be computed
	 */
	a_real computeElemL2Error2(const int ielem, const int comp, const Matrix& ug, a_real (* const exact)(a_real, a_real, a_real), const double time) const;

	/// Computes the L2 norm of a FE function on an element
	a_real computeElemL2Norm2(const int ielem, const Vector& ug) const;

public:
	/// Constructor
	/** \param[in] mesh is the mesh context
	 * \param _p_degree is the polynomial degree for FE basis functions
	 */
	SpatialBase(const UMesh2dh* mesh, const int _p_degree, char basistype);

	virtual ~SpatialBase();

	/// Compute all finite element data, including mass matrix, needed for the spatial discretization
	void computeFEData();

	/// Computes L2 norm of the the specified component of some vector quantity w
	a_real computeL2Norm(const std::vector<Matrix> w, const int comp) const;

	/// Access to vector of unknowns
	std::vector<Matrix>& unk() {
		return u;
	}

	/// Access to residual vector
	std::vector<Matrix>& residual() {
		return res;
	}

	/// Maximum allowable explicit time steps for all elements
	const std::vector<a_real>& maxExplicitTimeStep() const {
		return mets;
	}

	/// Inverse of mass matrix
	const std::vector<Matrix>& massInv() const {
		return minv;
	}

	a_int numTotalDOFs() const { return ntotaldofs; }

	/// Calls functions to add contribution to the [right hand side](@ref residual), and also compute [time steps](@ref mets)
	virtual void update_residual(const std::vector<Matrix>& ustage) = 0;

	/// Adds source term contribution to residual
	/** As implemented in this class, does nothing.
	 */
	virtual void add_source( a_real (*const rhs)(a_real, a_real, a_real), a_real t);

	/// Compute quantities to export
	virtual void postprocess() = 0;

	/// Read-only access to output quantities
	virtual const amat::Array2d<a_real>& getOutput() const = 0;

	/// Sets initial conditions using a function describing a variable
	void setInitialConditionNodal( const int comp, double (**const init)(a_real, a_real));

	/// Sets initial conditions using functions for a variable and its space derivatives
	void setInitialConditionModal( const int comp, double (**const init)(a_real, a_real));
};

}	// end namespace
#endif

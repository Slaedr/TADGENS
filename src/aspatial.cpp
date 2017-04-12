/** @file aspatial.cpp
 * @brief Implements spatial discretizations.
 * @author Aditya Kashi
 * @date 2016-02-24
 */

#include "asolverbase.hpp"

namespace acfd {

SpatialBase::SpatialBase(const UMesh2dh* mesh, const int _p_degree) : m(mesh), p_degree(_p_degree)
{
	std::cout << "SpatialBase: Setting up spaital integrator for FE polynomial degree " << p_degree << std::endl;
	
	// set quadrature strength
	int dom_quaddegree = 2*p_degree;
	if(m->degree() == 2) dom_quaddegree += 1;
	int boun_quaddegree = 2*p_degree;
	if(m->degree() == 2) boun_quaddegree += 1;

	dtquad = new Quadrature2DTriangle();
	dtquad.initialize(dom_quaddegree);
	dsquad = new Quadrature2DSquare();
	dsquad.initialize(dom_quaddegree);
	bquad = new Quadrature1D();
	bquad.initialize(boun_quaddegree);

	map2d = new LagrangeMapping2D[m->gnelem()];
	elems = new TaylorElement[m->gnelem()];
	dummyelem = new DummyElement();

	map1d = new LagrangeMapping1D[m->gnaface()];
	faces = new FaceElement[m->gnaface()];
}

SpatialBase::~SpatialBase()
{
	delete dtquad;
	delete dsquad;
	delete bquad;
	delete [] map2d;
	delete [] map1d;
	delete [] faces;
	delete [] elems;
	delete dummyelem;
}

SpatialBase::computeFEData()
{
	std::cout << " SpatialBase: computeFEData(): Computing basis functions, basis gradients and mass matrices for each element" << std::endl;
	minv.resize(m.gnelem());
	ntotaldofs = 0;

	// loop over elements to setup maps and elements and compute mass matrices
	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		amat::Array2d<a_real> phynodes(m->gnnode(iel),NDIM);
		for(int i = 0; i < m->gnnode(iel); i++)
			for(int j = 0; j < NDIM; j++)
				phynodes(i,j) = m->gcoords(m->ginpoel(iel,i),j);

		if(m->gnnode(iel) == 4 || m->gnnode(iel) == 9)
			map2d[iel].setAll(m->degree(), phynodes, dsquad);
		else
			map2d[iel].setAll(m->degree(), phynodes, dtquad);

		elems[iel].initialize(p_degree, &map2d[iel]);
		ntotaldofs += elems[iel].getNumDOFs();

		// allocate mass matrix
		minv[iel].resize(elems[iel].getNumDOFs(), elems[iel].getNumDOFs());
		minv[iel] = Matrix::Zero();

		// compute mass matrix
		const Quadrature2D* lquad = map2d[iel].getQuadrature();
		for(int ig = 0; ig < lquad->numGauss(); ig++)
		{
			for(int idof = 0; idof < elems[iel].getNumDOFs(); idof++)
				for(int jdof = 0; jdof < elems[iel].getNumDOFs(); jdof++)
					minv[iel](idof,jdof) += elems[iel].bFunc(ig)(idof)*elems[iel].bFunc(ig)(jdof)*map2d[iel].jacDet(ig);
		}
	}
	std::printf(" SpatialBase: computeFEData: Total number of DOFs = %d\n", ntotaldofs);

	dummyelem->initialize(p_degree, &map2d[0]);

	// loop over faces
	for(int iface = 0; iface < m->gnbface(); iface++)
	{
		int lelem = m->gintfac(iface,0);
		amat::Array2d<a_real> phynodes(m->gnnofa(iface),NDIM);
		for(int i = 0; i < m->gnnofa(iface); i++)
			for(int j = 0; j < NDIM; j++)
				phynodes(i,j) = m->gcoords(m->gintfac(iface,2+i),j);

		map1d[iface].setAll(m->degree(), phynodes, bquad);
		map1d[iface].computeAll();

		faces[iface].initialize(&elems[lelem], dummyelem, &map1d[iface], m->gfacelocalnum(iface,0), m->gfacelocalnum(iface,1));
	}

	for(int iface = m->gnbface(); iface < m->gnaface(); iface++)
	{
		int lelem = m->gintfac(iface,0);
		int relem = m->gintfac(iface,1);
		amat::Array2d<a_real> phynodes(m->gnnofa(iface),NDIM);
		for(int i = 0; i < m->gnnofa(iface); i++)
			for(int j = 0; j < NDIM; j++)
				phynodes(i,j) = m->gcoords(m->gintfac(iface,2+i),j);

		map1d[iface].setAll(m->degree(), phynodes, bquad);
		map1d[iface].computeAll();

		faces[iface].initialize(&elems[lelem], &elems[relem], &map1d[iface], m->gfacelocalnum(iface,0), m->gfacelocalnum(iface,1));
	}
	
	std::cout << " SpatialBase: computeFEData(): Done." << std::endl;
}

LaplaceSIP::LaplaceSIP(const UMesh2dh* mesh, const int _p_degree, 
			a_real(*const f)(a_real,a_real), a_real(*const exact_sol)(a_real,a_real), 
			a_real(*const exact_gradx)(a_real,a_real), a_real(*const exact_grady)(a_real,a_real))
	: SpatialBase(mesh, _p_degree), rhs(f), exact(exact_sol), exactgradx(exact_gradx), exactgrady(exact_grady)
{
	bstates.resize(m->gnbface());
	for(int i = 0; i < m->gnbface(); i++)
		bstates[i].resize(2);

	computeFEData();

	for(int iface = 0; iface < m->gnaface(); iface++)
		bfaces[iface].computeBasisGrads();

	int ndofs = elems[0].getNumDOFs();
	dirdofflags.resize(m->gnelem()*elems[0].getNumDOFs(), 0);
	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		for(int ino = 0; ino < m->gnnode(iel); ino++) {
			a_int pno = m->ginpoel(iel,ino);
			if(m->gflag_bpoin(pno) == 1)
				dirdofflags[iel*ndofs+ino] = 1;
		}
	}
	ndirdofs = 0;
	for(int i = 0; i < dirdofflags.size(); i++)
		ndirdofs += dirdofflags[i];
	printf(" LaplaceSIP: No. of Dirichlet DOFs = %d\n", ndirdofs);

	Ag.resize(ntotaldofs, ntotaldofs);
	bg = Vector::Zero(ntotaldofs);
	cbig = 1.0e30;
}

void LaplaceSIP::compute_boundary_states(const std::vector<Vector>& instates, std::vector<Vector>& bounstates)
{
}

void LaplaceSIP::computeLHS()
{
	typedef Eigen::SparseMatrix<a_real>::StorageIndex StorageIndex;
	class Triplet
	{
	public:
	  Triplet() : m_row(0), m_col(0), m_value(0) {}

	  Triplet(const StorageIndex& i, const StorageIndex& j, const Scalar& v = Scalar(0))
		: m_row(i), m_col(j), m_value(v)
	  {}

	  /** \returns the row index of the element */
	  const StorageIndex& row() const { return m_row; }

	  /** \returns the column index of the element */
	  const StorageIndex& col() const { return m_col; }

	  /** \returns the value of the element */
	  const a_real& value() const { return m_value; }

	  /// Accessor
	  a_real& _value() { return m_value; }
	protected:
	  StorageIndex m_row, m_col;
	  a_real m_value;
	};

	// declare LHS in coordinate (triplet) form for assembly
	typedef Triplet<a_real> COO;
	std::vector<COO> coo; 
	int ndofs = elems[0].getNumDOFs();
	
	// domain integral
	for(int ielem = 0; ielem < m->gnelem(); ielem++)
	{
		const std::vector<Matrix>& bgrad = elems[ielem].bGrad();
		const GeomMapping2D* gmap = elems[ielem].getGeometricMapping();
		int ng = gmap->getQuadrature()->numGauss();
		const amat::Array2d<a_real>& wts = gmap->getQuadrature()->weights();

		Matrix A = Matrix::Zero(ndofs,ndofs);
		for(int ig = 0; ig < ng; ig++)
		{
			for(int i = 0; i < ndofs; i++)
				for(int j = 0; j < ndofs; j++) {
					A(i,j) += nu * bgrad[ig].row(i).dot(bgrad[ig].row(j)) * wts(ig) * gmap->jacDet()[ig];
				}
		}

		for(int i = 0; i < ndofs; i++)
			for(int j = 0; j < ndofs; j++) {
				coo.push_back(COO(ielem*ndofs+i, ielem*ndofs+j, A(i,j)));
			}
	}

	// face integrals
	a_int nbf = m->gnbface();
	for(int iface = nbf; iface < m->gnaface(); iface++)
	{
		a_int lelem = m->gintfac(iface,0);
		a_int relem = m->gintfac(iface,1);

		Matrix Bkk = Matrix::Zero(ndofs,ndofs), Bkkp = Matrix::Zero(ndofs,ndofs), Bkpk = Matrix::Zero(ndofs,ndofs), Bkpkp = Matrix::Zero(ndofs,ndofs);
		Matrix Skk = Matrix::Zero(ndofs,ndofs), Skkp = Matrix::Zero(ndofs,ndofs), Skpk = Matrix::Zero(ndofs,ndofs), Skpkp = Matrix::Zero(ndofs,ndofs);

		// inverse of (approx) measure of face
		a_real hinv = 1.0/std::sqrt( std::pow(m->gcoords(m->gintfac(iface,2),0) - m->gcoords(m->gintfac(iface,3),0),2) + 
				std::pow(m->gcoords(m->gintfac(iface,2),1) - m->gcoords(m->gintfac(iface,3),1),2) );

		int ng = map1d[iface].getQuadrature()->numGauss();
		const amat::Array2d<a_real>& wts = bquad.weights();
		const std::vector<Vector>& n = map1d[iface].normal();
		const std::vector<Matrix>& lgrad = faces[iface].leftBasisGrad();
		const std::vector<Matrix>& rgrad = faces[iface].rightBasisGrad();
		const amat::Array2d<a_real>& lbas = faces[iface].leftBasis();
		const amat::Array2d<a_real>& rbas = faces[iface].rightBasis();

		for(int ig = 0; ig < ng; ig++)
		{
			a_real weightandspeed = wts(ig) * map1d[iface].speed()[ig];
			for(int i = 0; i < ndofs; i++)
				for(int j = 0; j < ndofs; j++) {
					Bkk(i,j) +=   nu*0.5 * lgrad[ig].row(j).dot(n) * lbas(ig,i) * weightandspeed;
					Bkkp(i,j) +=  nu*0.5 * rgrad[ig].row(j).dot(n) * lbas(ig,i) * weightandspeed;
					Bkpk(i,j) +=  nu*0.5 * lgrad[ig].row(j).dot(n) * rbas(ig,i) * weightandspeed;
					Bkpkp(i,j) += nu*0.5 * rgrad[ig].row(j).dot(n) * rbas(ig,i) * weightandspeed;

					Skk(i,j) +=   nu*hinv * lbas(ig,i)*lbas(ig,j) * weightandspeed;
					Skkp(i,j) +=  nu*hinv * lbas(ig,i)*rbas(ig,j) * weightandspeed;
					Skpk(i,j) +=  nu*hinv * rbas(ig,i)*lbas(ig,j) * weightandspeed;
					Skpkp(i,j) += nu*hinv * rbas(ig,i)*rbas(ig,j) * weightandspeed;
				}
		}

		// add to global stiffness matrix
		for(int i = 0; i < ndofs; i++)
			for(int j = 0; j < ndofs; j++)
			{
				coo.push_back(COO( lelem*ndofs+i, lelem*ndofs+j, -Bkk(i,j)  +Bkpk(i,j) -Bkk(j,i)  -Bkkp(j,i) +Skk(i,j)  -Skpk(i,j) ));
				coo.push_back(COO( lelem*ndofs+i, relem*ndofs+j, -Bkkp(i,j) +Bkpkp(i,j)+Bkpk(j,i) +Bkpkp(j,i)+Skpkp(i,j)-Skkp(i,j) ));
				coo.push_back(COO( relem*ndofs+i, lelem*ndofs+j,  Bkpk(i,j) -Bkk(i,j)  -Bkkp(j,i) -Bkk(j,i)  +Skk(i,j)  -Skpk(i,j) ));
				coo.push_back(COO( relem*ndofs+i, relem*ndofs+j,  Bkpkp(i,j)-Bkkp(i,j) +Bkpkp(j,i)+Bkpk(j,i) +Skpkp(i,j)-Skkp(i,j) ));
			}
	}
	
	for(int iface = 0; iface < m->gnbface(); iface++)
	{
		a_int lelem = m->gintfac(iface,0);

		Matrix Bkk = Matrix::Zero(ndofs,ndofs), Skk = Matrix::Zero(ndofs,ndofs);

		// inverse of (approx) measure of face
		a_real hinv = 1.0/std::sqrt( std::pow(m->gcoords(m->gintfac(iface,2),0) - m->gcoords(m->gintfac(iface,3),0),2) + 
				std::pow(m->gcoords(m->gintfac(iface,2),1) - m->gcoords(m->gintfac(iface,3),1),2) );

		int ng = map1d[iface].getQuadrature()->numGauss();
		const amat::Array2d<a_real>& wts = bquad.weights();
		const std::vector<Vector>& n = map1d[iface].normal();
		const std::vector<Matrix>& lgrad = faces[iface].leftBasisGrad();
		const amat::Array2d<a_real>& lbas = faces[iface].leftBasis();

		for(int ig = 0; ig < ng; ig++)
		{
			a_real weightandspeed = wts(ig) * map1d[iface].speed()[ig];
			for(int i = 0; i < ndofs; i++)
				for(int j = 0; j < ndofs; j++) {
					Bkk(i,j) +=   nu*0.5 * lgrad[ig].row(j).dot(n) * lbas(ig,i) * weightandspeed;

					Skk(i,j) +=   nu*hinv * lbas(ig,i)*lbas(ig,j) * weightandspeed;
				}
		}

		// add to global stiffness matrix
		for(int i = 0; i < ndofs; i++)
			for(int j = 0; j < ndofs; j++)
				coo.push_back(COO( lelem*ndofs+i, lelem*ndofs+j, -Bkk(i,j)-Bkk(j,i) +Skk(i,j) ));
	}

	// apply Dirichlet penalties
	for(int i = 0; i < coo.size(); i++)
	{
		if(coo[i].row() == coo[i].col()){
			if(dirdofflags[coo[i].row()])
				coo[i]._value() = cbig*coo[i].value();
		}
	}

	// assemble
	lhs.setFromTriplets(coo.begin(), coo.end());
}

void LaplaceSIP::computeRHS()
{
	int ndofs = elems[0].getNumDOFs();
	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		Vector bl(ndofs);
		int ng = map2d[iel].getQuadrature()->numGauss();
		const amat::Array2d<a_real>& wts = map2d[iel].getQuadrature()->weights();
		const amat::Array2d<a_real>& quadp = map2d[iel].map();
		const amat::Array2d<a_real>& basis = elems[iel].bFunc();

		for(int ig = 0; ig < ng; ig++) {
			a_real weightAndJDet = wts(ig)*map2d[iel].jacDet()[ig];
			for(int i = 0; i < ndofs; i++)
				bl(i) += rhs(quadp(ig,0),quadp(ig,1)) * basis(ig,i) * weightAndJDet;
		}

		for(int i = 0; i < ndofs; i++)
			bg(iel*ndofs+i) = bl(i);
	}

	// Homogeneous Dirichlet BCs
	for(int i = 0; i < ntotaldofs; i++)
		if(dirdofflags[i])
			bg(i) = 0;
}

void LaplaceSIP::solve()
{
	printf(" LaplaceSIP: solve: Assembling LHS and RHS\n");
	computeLHS();
	computeRHS();
	printf(" LaplaceSIP: solve: Analyzing and factoring LHS...\n");
	SparseLU<SparseMatrix<a_real>> solver;
	solver.analyzePattern(Ag);
	solver.factorize(Ag);
	printf(" LaplaceSIP: solve: Solving\n");
	ug = solver.solve(bg);
	printf(" LaplaceSIP: solve: Done.\n");

	/*int ndofs = elems[0].getNumDOFs();
	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		for(int ino = 0; ino < m->gnnode(iel); ino++) {
			a_int pno = m->ginpoel(iel,ino);
			if(m->gflag_bpoin(pno) == 1)
				ug(iel*ndofs+ino) = 0;
		}
	}*/
}

void LaplaceSIP::postprocess()
{
	output.resize(m->gnpoin());
	std::vector<int> surelems(m->gnpoin(),0);
	int ndofs = elems[0].getNumDOFs();

	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		// iterate over vertices of element
		for(int ino = 0; ino < m->gnfael(iel); ino++) {
			output(m->ginpoel(iel,ino)) += ug(iel*ndofs+ino);
			surelems[m->ginpoel(iel,ino)] += 1;
		}
	}
	for(int ip = 0; ip < m->gnpoin(); ip++)
		output(ip) /= (a_real)surelems[ip];
}

void computeErrors(a_real& __restrict__ l2error, a_real& __restrict__ siperror) const
{
	std::printf(" LaplaceSIP: computeErrors: Computing the L2 and SIP norm of the error\n");
	int ndofs = elems[0].getNumDOFs();
	l2error = 0; siperror = 0;
	
	// domain integral
	for(int ielem = 0; ielem < m->gnelem(); ielem++)
	{
		const std::vector<Matrix>& bgrad = elems[ielem].bGrad();
		const amat::Array2d<a_real>& bfunc = elems[ielem].bFunc();
		const GeomMapping2D* gmap = elems[ielem].getGeometricMapping();
		int ng = gmap->getQuadrature()->numGauss();
		const amat::Array2d<a_real>& wts = gmap->getQuadrature()->weights();
		const amat::Array2d<a_real>& qp = gmap->map();

		for(int ig = 0; ig < ng; ig++)
		{
			a_real lu = 0, lux = 0, luy = 0;
			for(int j = 0; j < ndofs; j++) {
				lu += ug(ielem*ndofs+j)*bfunc(ig,j);
				lux += ug(ielem*ndofs+j)*bgrad[ig](j,0);
				luy += ug(ielem*ndofs+j)*bgrad[ig](j,1);
			}
			l2error += std::pow(lu-exact(qp(ig,0),qp(ig,1)),2) * wts(ig) * gmap->jacDet()[ig];
			siperror += ( std::pow(lux-exactgradx(qp(ig,0),qp(ig,1)),2) + std::pow(luy-exactgrady(qp(ig,0),qp(ig,1)),2) ) * wts(ig) * gmap->jacDet()[ig];
		}
	}

	// face integrals
	a_int nbf = m->gnbface();
	for(int iface = nbf; iface < m->gnaface(); iface++)
	{
		a_int lelem = m->gintfac(iface,0);
		a_int relem = m->gintfac(iface,1);
		
		// inverse of (approx) measure of face
		a_real hinv = 1.0/std::sqrt( std::pow(m->gcoords(m->gintfac(iface,2),0) - m->gcoords(m->gintfac(iface,3),0),2) + 
				std::pow(m->gcoords(m->gintfac(iface,2),1) - m->gcoords(m->gintfac(iface,3),1),2) );

		int ng = map1d[iface].getQuadrature()->numGauss();
		const amat::Array2d<a_real>& wts = bquad.weights();
		const std::vector<Vector>& n = map1d[iface].normal();
		const amat::Array2d<a_real>& lbas = faces[iface].leftBasis();
		const amat::Array2d<a_real>& rbas = faces[iface].rightBasis();

		for(int ig = 0; ig < ng; ig++)
		{
			a_real weightandspeed = wts(ig) * map1d[iface].speed()[ig];
			a_real lu = 0;
			for(int j = 0; j < ndofs; j++) {
				lu += ug(lelem*ndofs+j)*lbas(ig,j) - ug(relem+j*ndofs)*rbas(ig,j);
			}
			siperror += hinv * lu*lu * weightandspeed;
		}
	}
	
	for(int iface = 0; iface < m->gnbface(); iface++)
	{
		a_int lelem = m->gintfac(iface,0);

		Matrix Bkk = Matrix::Zero(ndofs,ndofs), Skk = Matrix::Zero(ndofs,ndofs);

		// inverse of (approx) measure of face
		a_real hinv = 1.0/std::sqrt( std::pow(m->gcoords(m->gintfac(iface,2),0) - m->gcoords(m->gintfac(iface,3),0),2) + 
				std::pow(m->gcoords(m->gintfac(iface,2),1) - m->gcoords(m->gintfac(iface,3),1),2) );

		int ng = map1d[iface].getQuadrature()->numGauss();
		const amat::Array2d<a_real>& wts = bquad.weights();
		const std::vector<Vector>& n = map1d[iface].normal();
		const std::Array2d<a_real>& qp = map1d[iface].map();
		const amat::Array2d<a_real>& lbas = faces[iface].leftBasis();

		for(int ig = 0; ig < ng; ig++)
		{
			a_real weightandspeed = wts(ig) * map1d[iface].speed()[ig];
			a_real lu = 0;
			for(int j = 0; j < ndofs; j++) {
				lu += ug(lelem*ndofs+j)*lbas(ig,j);
			}
			siperror += hinv * pow(lu-exact(qp(ig,0),qp(ig,1)),2) * weightandspeed;
		}
	}

	l2error = std::sqrt(l2error); siperror = std::sqrt(siperror);
	std::printf(" LaplaceSIP: computeErrors: Done.\n");
}

EulerFlow::EulerFlow(const UMesh2dh* mesh, const int _p_degree, a_real gamma, Vector& u_inf, Vector& u_in, Vector& u_out, int boun_ids[6])
	: SpatialBase(mesh, _p_degree), uinf(u_inf), uin(u_in), uout(u_out), g(gamma),
	  slipwall_id(boun_ids[0]), inflow_id(boun_ids[1]), outflow_id(boun_ids[2]), farfield_id(boun_ids[3]), periodic_id(boun_id[4]), symmetry_id(boun_ids[5])
{
	Vector condinf = u_inf;
	uinf(0) = condinf(0);
	uinf(1) = uinf(0)*condinf(1)*std::cos(condinf(2));
	uinf(2) = uinf(0)*condinf(1)*std::sin(condinf(2));
	a_real p = uinf(0)*condinf(1)*condinf(1)/(g*condinf(3)*condinf(3));
	uinf(3) = p/(g-1.0) + 0.5*uinf(0)*condinf(1)*condinf(1);
}

void EulerFlow::compute_boundary_states(const a_real ins[NVARS], const Vector& n, int iface, a_real bs[NVARS])
{
	a_real vni = (ins[1]*n[0] + ins[2]*n[1])/ins[0];
	a_real pi = (g-1.0)*(ins[3] - 0.5*(pow(ins[1],2)+pow(ins[2],2))/ins[0]);
	a_real pinf = (g-1.0)*(uinf[3] - 0.5*(pow(uinf[1],2)+pow(uinf[2],2))/uinf[0]);
	a_real ci = sqrt(g*pi/ins[0]);
	a_real Mni = vni/ci;

	if(m->gintfacbtags(iface,0) == slipwall_id)
	{
		bs[0] = ins[0];
		bs[1] = ins[1] - 2*vni*n[0]*bs[0];
		bs[2] = ins[2] - 2*vni*n[1]*bs[0];
		bs[3] = ins[3];
	}
	
	if(m->gintfacbtags(iface,0) == periodic_id) {
		// TODO: Implement periodic boundary here //
	}

	if(m->gintfacbtags(iface,0) == freestream_id)
	{
		//if(Mni <= -1.0)
		{
			for(int i = 0; i < NVARS; i++)
				bs[i] = uinf[i];
		}
		/*else if(Mni > -1.0 && Mni < 0)
		{
			// subsonic inflow, specify rho and u according to FUN3D BCs paper
			for(i = 0; i < NVARS-1; i++)
				bs(ied,i) = uinf.get(0,i);
			bs(ied,3) = pi/(g-1.0) + 0.5*( uinf.get(0,1)*uinf.get(0,1) + uinf.get(0,2)*uinf.get(0,2) )/uinf.get(0,0);
		}
		else if(Mni >= 0 && Mni < 1.0)
		{
			// subsonic ourflow, specify p accoording FUN3D BCs paper
			for(i = 0; i < NVARS-1; i++)
				bs(ied,i) = ins.get(ied,i);
			bs(ied,3) = pinf/(g-1.0) + 0.5*( ins.get(ied,1)*ins.get(ied,1) + ins.get(ied,2)*ins.get(ied,2) )/ins.get(ied,0);
		}
		else
			for(i = 0; i < NVARS; i++)
				bs(ied,i) = ins.get(ied,i);*/
	}
}

void EulerFlow::compute_RHS()
{
	//std::cout << "Computing res ---\n";
#pragma omp parallel default(shared)
	{
#pragma omp for simd
		for(int iel = 0; iel < m->gnelem(); iel++)
		{
			for(int i = 0; i < NVARS; i++)
				residual(iel,i) = 0.0;
			integ(iel) = 0.0;
		}

		// first, set cell-centered values of boundary cells as left-side values of boundary faces
#pragma omp for
		for(a_int ied = 0; ied < m->gnbface(); ied++)
		{
			a_int ielem = m->gintfac(ied,0);
			for(int ivar = 0; ivar < NVARS; ivar++)
				uleft(ied,ivar) = u.get(ielem,ivar);
		}
	}

	if(order == 2)
	{
		// get cell average values at ghost cells using BCs
		compute_boundary_states(uleft, ug);

		rec->compute_gradients();
		lim->compute_face_values();
	}
	else
	{
		// if order is 1, set the face data same as cell-centred data for all faces

		// set both left and right states for all interior faces
#pragma omp parallel for
		for(a_int ied = m->gnbface(); ied < m->gnaface(); ied++)
		{
			a_int ielem = m->gintfac(ied,0);
			a_int jelem = m->gintfac(ied,1);
			for(int ivar = 0; ivar < NVARS; ivar++)
			{
				uleft(ied,ivar) = u.get(ielem,ivar);
				uright(ied,ivar) = u.get(jelem,ivar);
			}
		}
	}

	// set right (ghost) state for boundary faces
	compute_boundary_states(uleft,uright);

	/** Compute fluxes.
	 * The integral of the maximum magnitude of eigenvalue over each face is also computed:
	 * \f[
	 * \int_{f_i} (|v_n| + c) \mathrm{d}l
	 * \f]
	 * so that time steps can be calculated for explicit time stepping.
	 */

	std::vector<a_real> ci(m->gnaface()), vni(m->gnaface()), cj(m->gnaface()), vnj(m->gnaface());

#pragma omp parallel default(shared)
	{
#pragma omp for
		for(a_int ied = 0; ied < m->gnaface(); ied++)
		{
			a_real n[NDIM];
			n[0] = m->ggallfa(ied,0);
			n[1] = m->ggallfa(ied,1);
			a_real len = m->ggallfa(ied,2);

			const a_real* ulp = uleft.const_row_pointer(ied);
			const a_real* urp = uright.const_row_pointer(ied);
			a_real* fluxp = fluxes.row_pointer(ied);

			// compute flux
			inviflux->get_flux(ulp, urp, n, fluxp);

			// integrate over the face
			for(int ivar = 0; ivar < NVARS; ivar++)
					fluxp[ivar] *= len;

			//calculate presures from u
			a_real pi = (g-1)*(uleft.get(ied,3) - 0.5*(pow(uleft.get(ied,1),2)+pow(uleft.get(ied,2),2))/uleft.get(ied,0));
			a_real pj = (g-1)*(uright.get(ied,3) - 0.5*(pow(uright.get(ied,1),2)+pow(uright.get(ied,2),2))/uright.get(ied,0));
			//calculate speeds of sound
			ci[ied] = sqrt(g*pi/uleft.get(ied,0));
			cj[ied] = sqrt(g*pj/uright.get(ied,0));
			//calculate normal velocities
			vni[ied] = (uleft.get(ied,1)*n[0] +uleft.get(ied,2)*n[1])/uleft.get(ied,0);
			vnj[ied] = (uright.get(ied,1)*n[0] + uright.get(ied,2)*n[1])/uright.get(ied,0);
		}

		// update residual and integ
		//std::cout << "Beginning new loop --- \n";
#pragma omp for
		for(a_int iel = 0; iel < m->gnelem(); iel++)
		{
			for(int ifael = 0; ifael < m->gnfael(iel); ifael++)
			{
				a_int ied = m->gelemface(iel,ifael);
				a_real len = m->ggallfa(ied,2);
				a_int nbdelem = m->gesuel(iel,ifael);

				if(nbdelem > iel) {
					for(int ivar = 0; ivar < NVARS; ivar++)
						residual(iel,ivar) -= fluxes(ied,ivar);
					integ(iel) += (fabs(vni[ied]) + ci[ied])*len;
				}
				else {
					for(int ivar = 0; ivar < NVARS; ivar++)
						residual(iel,ivar) += fluxes(ied,ivar);
					integ(iel) += (fabs(vnj[ied]) + cj[ied])*len;
				}
			}
		}
	} // end parallel region
}

a_real EulerFlow::compute_entropy()
{
	postprocess_cell();
	a_real vmaginf2 = uinf(0,1)/uinf(0,0)*uinf(0,1)/uinf(0,0) + uinf(0,2)/uinf(0,0)*uinf(0,2)/uinf(0,0);
	a_real sinf = ( uinf(0,0)*(g-1) * (uinf(0,3)/uinf(0,0) - 0.5*vmaginf2) ) / pow(uinf(0,0),g);

	amat::Array2d<a_real> s_err(m->gnelem(),1);
	a_real error = 0;
	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		s_err(iel) = (scalars(iel,2)/pow(scalars(iel,0),g) - sinf)/sinf;
		error += s_err(iel)*s_err(iel)*m->gjacobians(iel)/2.0;
	}
	error = sqrt(error);

	//a_real h = sqrt((m->jacobians).max());
	a_real h = 1.0/sqrt(m->gnelem());

	std::cout << "EulerFV:   " << log10(h) << "  " << std::setprecision(10) << log10(error) << std::endl;

	return error;
}

void EulerFlow::postprocess_point()
{
	std::cout << "SpatialBase: postprocess_point(): Creating output arrays...\n";
	scalars.setup(m->gnpoin(),3);
	velocities.setup(m->gnpoin(),2);
	amat::Array2d<a_real> c(m->gnpoin(),1);

	amat::Array2d<a_real> areasum(m->gnpoin(),1);
	amat::Array2d<a_real> up(m->gnpoin(), NVARS);
	up.zeros();
	areasum.zeros();

	int inode, ivar;
	a_int ielem, iface, ip1, ip2, ipoin;

	for(ielem = 0; ielem < m->gnelem(); ielem++)
	{
		for(inode = 0; inode < m->gnnode(ielem); inode++)
			for(ivar = 0; ivar < NVARS; ivar++)
			{
				up(m->ginpoel(ielem,inode),ivar) += u.get(ielem,ivar)*m->garea(ielem);
				areasum(m->ginpoel(ielem,inode)) += m->garea(ielem);
			}
	}
	for(iface = 0; iface < m->gnbface(); iface++)
	{
		ielem = m->gintfac(iface,0);
		ip1 = m->gintfac(iface,2);
		ip2 = m->gintfac(iface,3);
		for(ivar = 0; ivar < NVARS; ivar++)
		{
			up(ip1,ivar) += ug.get(iface,ivar)*m->garea(ielem);
			up(ip2,ivar) += ug.get(iface,ivar)*m->garea(ielem);
			areasum(ip1) += m->garea(ielem);
			areasum(ip2) += m->garea(ielem);
		}
	}

	for(ipoin = 0; ipoin < m->gnpoin(); ipoin++)
		for(ivar = 0; ivar < NVARS; ivar++)
			up(ipoin,ivar) /= areasum(ipoin);

	for(ipoin = 0; ipoin < m->gnpoin(); ipoin++)
	{
		scalars(ipoin,0) = up.get(ipoin,0);
		velocities(ipoin,0) = up.get(ipoin,1)/up.get(ipoin,0);
		velocities(ipoin,1) = up.get(ipoin,2)/up.get(ipoin,0);
		//velocities(ipoin,0) = dudx(ipoin,1);
		//velocities(ipoin,1) = dudy(ipoin,1);
		a_real vmag2 = pow(velocities(ipoin,0), 2) + pow(velocities(ipoin,1), 2);
		scalars(ipoin,2) = up.get(ipoin,0)*(g-1) * (up.get(ipoin,3)/up.get(ipoin,0) - 0.5*vmag2);		// pressure
		c(ipoin) = sqrt(g*scalars(ipoin,2)/up.get(ipoin,0));
		scalars(ipoin,1) = sqrt(vmag2)/c(ipoin);
	}
	std::cout << "EulerFV: postprocess_point(): Done.\n";
}

amat::Array2d<a_real> EulerFlow::getOutput() const
{
	return scalars;
}

}	// end namespace

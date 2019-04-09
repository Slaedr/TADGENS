/** @file poisson.cpp
 * @brief Main function for SIP Poisson solver
 * @author Aditya Kashi
 * @date 2017 April 11
 */

#undef NDEBUG
#include "spatial/aspatialpoisson.hpp"
#include "spatial/aoutput.hpp"

using namespace amat;
using namespace std;
using namespace acfd;

// double exactsol(double x, double y) {
// 	return sin(PI*x)*sin(PI*y);
// }
// double exactgradx(double x, double y) {
// 	return PI*cos(PI*x)*sin(PI*y);
// }
// double exactgrady(double x, double y) {
// 	return sin(PI*x)*PI*cos(PI*y);
// }
// double rhs(double x, double y) {
// 	return 2.0*PI*PI*sin(PI*x)*sin(PI*y);
// }

int main(int argc, char* argv[])
{
	if(argc < 2)
	{
		printf("Please give a control file name.\n");
		return -1;
	}

	// Read control file
	ifstream control(argv[1]);

	string dum, meshprefix, outprefix, outf;
	double stab;
	int degree, nmesh;

	control >> dum; control >> nmesh;
	control >> dum; control >> meshprefix;
	control >> dum; control >> outprefix;
	control >> dum; control >> outf;
	control >> dum; control >> degree;
	control >> dum; control >> stab;
	control.close();

	vector<string> mfiles(nmesh), sfiles(nmesh);
	vector<double> h(nmesh,0), l2err(nmesh,0), siperr(nmesh,0);
	string names[] = {"poisson"};

	for(int i = 0; i < nmesh; i++) {
		mfiles[i] = meshprefix + to_string(i) + ".msh";
		sfiles[i] = outprefix + to_string(i) + "-SIP.vtu";
	}

	for(int imesh = 0; imesh < nmesh; imesh++)
	{
		UMesh2dh m; m.readGmsh2(mfiles[imesh], NDIM); m.compute_topological();
		LaplaceSIP sd(&m, degree, stab);
		sd.assemble();
		sd.solve();
		std::vector<Matrix> dum;
		sd.postprocess(dum);
		sd.computeErrors(l2err[imesh], siperr[imesh]);
		
		l2err[imesh] = log10(l2err[imesh]); siperr[imesh] = log10(siperr[imesh]);
		h[imesh] = log10(sqrt(1.0/m.gnelem()));
		printf("Mesh %d: Log mesh size = %f, log L2 error = %f, log SIP error = %f\n",
		       imesh, h[imesh], l2err[imesh], siperr[imesh]);

		const Array2d<a_real>& u = sd.getOutput();
		Array2d<a_real> vecs;
		writeScalarsVectorToVtu_PointData(sfiles[imesh], m, u, names, vecs, "none");
	}

	// ofstream convf(outf);
	// for(int i = 0; i < nmesh; i++)
	// 	convf << h[i] << " " << l2err[i] << " " << siperr[i] << "\n";
	// convf.close();
	if(nmesh > 1) {
		const double l2slope = (l2err[nmesh-1]-l2err[nmesh-2])/(h[nmesh-1]-h[nmesh-2]);
		const double sipslope = (siperr[nmesh-1]-siperr[nmesh-2])/(h[nmesh-1]-h[nmesh-2]);
		printf("L2 slope = %f, SIP slope = %f\n", l2slope, sipslope); fflush(stdout);
		assert(l2slope > 1.8);
		assert(l2slope < 2.2);
	}

	printf("---\n\n");
	return 0;
}

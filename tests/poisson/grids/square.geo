// Simple square test case for Poisson problem with DGFEM
// Aditya Kashi

h = 0.9;
Point(1) = {0, 0, 0, h};
Point(2) = {1, 0, 0, h};
Point(3) = {1, 1, 0, h};
Point(4) = {0, 1, 0, h};
Line(1) = {1, 2};
Line(2) = {2, 3};
Line(3) = {3, 4};
Line(4) = {4, 1};
Line Loop(6) = {1, 2, 3, 4};
Plane Surface(6) = {6};
Physical Line(2) = {1, 2, 3, 4};
Physical Surface(7) = {6};

// Comment out for triangular mesh
//Recombine Surface {6};


/* Portions copyright (c) 2006-2009 Stanford University and Simbios.
 * Contributors: Pande Group
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <iostream> // This is only for testing purposes
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <cmath>
#include <cstdio>

#include "ReferenceForce.h"
#include "ReferenceObc.h"

//using namespace OpenMM;
using namespace std;

/**---------------------------------------------------------------------------------------

    ReferenceObc constructor

    obcParameters      obcParameters object
    
    --------------------------------------------------------------------------------------- */

ReferenceObc::ReferenceObc(ObcParameters* obcParameters) :
  _obcParameters(obcParameters),
  _includeAceApproximation(1)
{
    _obcChain.resize(_obcParameters->getNumberOfAtoms());
}

/**---------------------------------------------------------------------------------------

    ReferenceObc destructor

    --------------------------------------------------------------------------------------- */

ReferenceObc::~ReferenceObc() {
}

/**---------------------------------------------------------------------------------------

    Get ObcParameters reference

    @return ObcParameters reference

    --------------------------------------------------------------------------------------- */

ObcParameters* ReferenceObc::getObcParameters() const {
    return _obcParameters;
}

/**---------------------------------------------------------------------------------------

    Set ObcParameters reference

    @param ObcParameters reference

    --------------------------------------------------------------------------------------- */

void ReferenceObc::setObcParameters(ObcParameters* obcParameters) {
    _obcParameters = obcParameters;
}

/**---------------------------------------------------------------------------------------

   Return flag signalling whether AceApproximation for nonpolar term is to be included

   @return flag

   --------------------------------------------------------------------------------------- */

int ReferenceObc::includeAceApproximation() const {
    return _includeAceApproximation;
}

/**---------------------------------------------------------------------------------------

   Set flag indicating whether AceApproximation is to be included

   @param includeAceApproximation new includeAceApproximation value

   --------------------------------------------------------------------------------------- */

void ReferenceObc::setIncludeAceApproximation(int includeAceApproximation) {
    _includeAceApproximation = includeAceApproximation;
}

/**---------------------------------------------------------------------------------------

    Return OBC chain derivative: size = _obcParameters->getNumberOfAtoms()

    @return array

    --------------------------------------------------------------------------------------- */

vector<double>& ReferenceObc::getObcChain() {
    return _obcChain;
}

/**---------------------------------------------------------------------------------------

    Get Born radii based on papers:

       J. Phys. Chem. 1996 100, 19824-19839 (HCT paper)
       Proteins: Structure, Function, and Bioinformatcis 55:383-394 (2004) (OBC paper)

    @param atomCoordinates     atomic coordinates
    @param bornRadii           output array of Born radii

    --------------------------------------------------------------------------------------- */

//void ReferenceObc::computeBornRadii(const vector<RealVec>& atomCoordinates, vector<double>& bornRadii) {
void ReferenceObc::computeBornRadii(const ObcParameters* obcParameters,
  const vector3* atomCoordinates,
  const double* Igrid,
  vector<double>& bornRadii) {

    // ---------------------------------------------------------------------------------------

    static const double zero    = static_cast<double>(0.0);
    static const double one     = static_cast<double>(1.0);
    static const double two     = static_cast<double>(2.0);
    static const double three   = static_cast<double>(3.0);
    static const double half    = static_cast<double>(0.5);
    static const double fourth  = static_cast<double>(0.25);

    // ---------------------------------------------------------------------------------------

    int numberOfAtoms                           = obcParameters->getNumberOfAtoms();
    const vector<double>& atomicRadii         = obcParameters->getAtomicRadii();
    const vector<double>& scaledRadiusFactor  = obcParameters->getScaledRadiusFactors();
    vector<double>& obcChain                  = getObcChain();

    double dielectricOffset                 = obcParameters->getDielectricOffset();
    double alphaObc                         = obcParameters->getAlphaObc();
    double betaObc                          = obcParameters->getBetaObc();
    double gammaObc                         = obcParameters->getGammaObc();

    // ---------------------------------------------------------------------------------------

    // calculate Born radii

    for (int atomI = 0; atomI < numberOfAtoms; atomI++) {
      
       double radiusI         = atomicRadii[atomI];
       double offsetRadiusI   = radiusI - dielectricOffset;

       double radiusIInverse  = one/offsetRadiusI;
       double sum             = zero;

       // HCT code

       for (int atomJ = 0; atomJ < numberOfAtoms; atomJ++) {

          if (atomJ != atomI) {

             double deltaR[OpenMM::ReferenceForce::LastDeltaRIndex];
             OpenMM::ReferenceForce::getDeltaR(atomCoordinates[atomI], atomCoordinates[atomJ], deltaR);
             double r               = deltaR[OpenMM::ReferenceForce::RIndex];
             if (_obcParameters->getUseCutoff() && r > _obcParameters->getCutoffDistance())
                 continue;

             double offsetRadiusJ   = atomicRadii[atomJ] - dielectricOffset; 
             double scaledRadiusJ   = offsetRadiusJ*scaledRadiusFactor[atomJ];
             double rScaledRadiusJ  = r + scaledRadiusJ;

             if (offsetRadiusI < rScaledRadiusJ) {
                double rInverse = one/r;
                double l_ij     = offsetRadiusI > FABS(r - scaledRadiusJ) ? offsetRadiusI : FABS(r - scaledRadiusJ);
                l_ij     = one/l_ij; // the inverse of Eq. 10

                double u_ij     = one/rScaledRadiusJ; // the inverse of Eq. 11

                double l_ij2    = l_ij*l_ij;
                double u_ij2    = u_ij*u_ij;
 
                double ratio    = LN((u_ij/l_ij));
                // the summand in Eq. 9
                double term     = l_ij - u_ij + fourth*r*(u_ij2 - l_ij2)
                  + (half*rInverse*ratio)
                  + (fourth*scaledRadiusJ*scaledRadiusJ*rInverse)*(l_ij2 - u_ij2);

                // this case (atom i completely inside atom j) is not considered in the original paper
                // Jay Ponder and the authors of Tinker recognized this and
                // worked out the details

                if (offsetRadiusI < (scaledRadiusJ - r)) {
                   term += two*(radiusIInverse - l_ij);
                }
                sum += term;

             }
          }
       }
 
       // OBC-specific code (Eqs. 6-8 in OBC paper)

       sum              *= half; // Now sum becomes I in OBC paper
       if (Igrid!=NULL) {
         // printf("Atom %d, Born radius = %f, I_HCT = %f, I_grid = %f\n", atomI, radiusI, sum, Igrid[atomI]);
         sum += Igrid[atomI];
       }
      
       // TODO: Derivatives not correct after adding numerical integral.

       // OBC-specific code (Eqs. 6-8 in OBC paper)
       sum              *= offsetRadiusI; // Now sum becomes \Psi in OBC paper
       double sum2       = sum*sum;
       double sum3       = sum*sum2;
       double tanhSum    = TANH(alphaObc*sum - betaObc*sum2 + gammaObc*sum3);
       
       bornRadii[atomI]      = one/(one/offsetRadiusI - tanhSum/radiusI); 
 
       // This is the derivative of the Born radius with respect to Psi,
       // multiplied by offsetRadiusI and divided by BornRadii[atomI]**2
       obcChain[atomI]       = offsetRadiusI*(alphaObc - two*betaObc*sum + three*gammaObc*sum2);
       obcChain[atomI]       = (one - tanhSum*tanhSum)*obcChain[atomI]/radiusI;
    }
}

/**---------------------------------------------------------------------------------------

    Get nonpolar solvation force constribution via ACE approximation

    @param obcParameters parameters
    @param bornRadii                 Born radii
    @param energy                    energy (output): value is incremented from input value 
    @param forces                    forces: values are incremented from input values

    --------------------------------------------------------------------------------------- */

//void ReferenceObc::computeAceNonPolarForce(const ObcParameters* obcParameters,
//                                      const vector<double>& bornRadii,
//                                      double* energy,
//                                      vector<double>& forces) const {
void ReferenceObc::computeAceNonPolarForce(const ObcParameters* obcParameters,
                                      const vector<double>& bornRadii,
                                      double* energy,
                                      vector<double>& forces) const {

    // ---------------------------------------------------------------------------------------

    static const double zero     = static_cast<double>(0.0);
    static const double minusSix = -6.0;
    static const double six      = static_cast<double>(6.0);

    // ---------------------------------------------------------------------------------------

    // compute the nonpolar solvation via ACE approximation
    const double strength             = obcParameters->getStrength();
    const double probeRadius          = obcParameters->getProbeRadius();
    const double surfaceAreaFactor    = obcParameters->getPi4Asolv();

    const vector<double>& atomicRadii   = obcParameters->getAtomicRadii();
    int numberOfAtoms                     = obcParameters->getNumberOfAtoms();

    // the original ACE equation is based on Eq.2 of

    // M. Schaefer, C. Bartels and M. Karplus, "Solution Conformations
    // and Thermodynamics of Structured Peptides: Molecular Dynamics
    // Simulation with an Implicit Solvation Model", J. Mol. Biol.,
    // 284, 835-848 (1998)  (ACE Method)

    // The original equation includes the factor (atomicRadii[atomI]/bornRadii[atomI]) to the first power,
    // whereas here the ratio is raised to the sixth power: (atomicRadii[atomI]/bornRadii[atomI])**6

    // This modification was made by Jay Ponder who observed it gave better correlations w/
    // observed values. He did not think it was important enough to write up, so there is
    // no paper to cite.

    for (int atomI = 0; atomI < numberOfAtoms; atomI++) {
        if (bornRadii[atomI] > zero) {
            double r            = atomicRadii[atomI] + probeRadius;
            double ratio6       = POW(atomicRadii[atomI]/bornRadii[atomI], six);
            double saTerm       = surfaceAreaFactor*r*r*ratio6;
            *energy                += strength*saTerm;
            forces[atomI]          += strength*minusSix*saTerm/bornRadii[atomI];
        }
    }
}

/**---------------------------------------------------------------------------------------

    Get Obc Born energy (no forces)

    @param atomCoordinates     atomic coordinates
    @param partialCharges      partial charges

    The array bornRadii is also updated and the obcEnergy

    --------------------------------------------------------------------------------------- */

//double ReferenceObc::computeBornEnergyForces(const vector<RealVec>& atomCoordinates,
//                                           const vector<double>& partialCharges, vector<RealVec>& inputForces) {
double ReferenceObc::computeBornEnergy(const ObcParameters* obcParameters,
                                       const vector3* atomCoordinates,
                                       const vector<double>& partialCharges,
                                       const double* Igrid) {

    // ---------------------------------------------------------------------------------------

    static const double zero    = static_cast<double>(0.0);
    static const double one     = static_cast<double>(1.0);
    static const double two     = static_cast<double>(2.0);
//    static const double three   = static_cast<double>(3.0);
    static const double four    = static_cast<double>(4.0);
    static const double half    = static_cast<double>(0.5);
    static const double fourth  = static_cast<double>(0.25);
    static const double eighth  = static_cast<double>(0.125);

    // constants
    const int numberOfAtoms = obcParameters->getNumberOfAtoms();
    const double strength = obcParameters->getStrength();
    const double dielectricOffset = obcParameters->getDielectricOffset();
    const double cutoffDistance = obcParameters->getCutoffDistance();
    const double soluteDielectric = obcParameters->getSoluteDielectric();
    const double solventDielectric = obcParameters->getSolventDielectric();
    double preFactor;
    if (soluteDielectric != zero && solventDielectric != zero)
        preFactor = two*obcParameters->getElectricConstant()*((one/soluteDielectric) - (one/solventDielectric));
    else
        preFactor = zero;

    // ---------------------------------------------------------------------------------------

    // compute Born radii

    vector<double> bornRadii(numberOfAtoms);
    computeBornRadii(obcParameters, atomCoordinates, Igrid, bornRadii);

    // set energy/forces to zero

    double obcEnergy                 = zero;
    vector<double> bornForces(numberOfAtoms, 0.0);

    // ---------------------------------------------------------------------------------------

    // compute the nonpolar solvation via ACE approximation
     
    if (includeAceApproximation()) {
       computeAceNonPolarForce(obcParameters, bornRadii, &obcEnergy, bornForces);
    }
 
//    cout << "ObcEnergy, after Ace:" << obcEnergy << std::endl;
 
    // ---------------------------------------------------------------------------------------

    // first main loop

    for (int atomI = 0; atomI < numberOfAtoms; atomI++) {
 
       double partialChargeI = preFactor*partialCharges[atomI];
       for (int atomJ = atomI; atomJ < numberOfAtoms; atomJ++) {
       
          double deltaR[OpenMM::ReferenceForce::LastDeltaRIndex];
          OpenMM::ReferenceForce::getDeltaR(atomCoordinates[atomI], atomCoordinates[atomJ], deltaR);
          if (obcParameters->getUseCutoff() && deltaR[OpenMM::ReferenceForce::RIndex] > cutoffDistance)
              continue;

          double r2                 = deltaR[OpenMM::ReferenceForce::R2Index];
//          double deltaX             = deltaR[OpenMM::ReferenceForce::XIndex];
//          double deltaY             = deltaR[OpenMM::ReferenceForce::YIndex];
//          double deltaZ             = deltaR[OpenMM::ReferenceForce::ZIndex];

          double alpha2_ij          = bornRadii[atomI]*bornRadii[atomJ];
          double D_ij               = r2/(four*alpha2_ij);

          double expTerm            = EXP(-D_ij);
          double denominator2       = r2 + alpha2_ij*expTerm; 
          double denominator        = SQRT(denominator2); 
          
          double Gpol               = (partialChargeI*partialCharges[atomJ])/denominator; 
//          double dGpol_dr           = -Gpol*(one - fourth*expTerm)/denominator2;  
//
//          double dGpol_dalpha2_ij   = -half*Gpol*expTerm*(one + D_ij)/denominator2;
         
          double energy = Gpol;

          if (atomI != atomJ) {

              if (obcParameters->getUseCutoff())
                  energy -= partialChargeI*partialCharges[atomJ]/cutoffDistance;
              
//              bornForces[atomJ]        += dGpol_dalpha2_ij*bornRadii[atomI];

//              deltaX                   *= dGpol_dr;
//              deltaY                   *= dGpol_dr;
//              deltaZ                   *= dGpol_dr;

//              inputForces[atomI][0]    -= deltaX;
//              inputForces[atomI][1]    -= deltaY;
//              inputForces[atomI][2]    -= deltaZ;
//
//              inputForces[atomJ][0]    += deltaX;
//              inputForces[atomJ][1]    += deltaY;
//              inputForces[atomJ][2]    += deltaZ;

          } else {
             energy *= half;
          }

          obcEnergy         += strength*energy;
//          bornForces[atomI] += dGpol_dalpha2_ij*bornRadii[atomJ];

       }
    }
  
    return obcEnergy;
}


/**---------------------------------------------------------------------------------------

    Get Obc Born energy and forces

    @param atomCoordinates     atomic coordinates
    @param partialCharges      partial charges
    @param forces              forces

    The array bornRadii is also updated and the obcEnergy

    --------------------------------------------------------------------------------------- */

//double ReferenceObc::computeBornEnergyForces(const vector<RealVec>& atomCoordinates,
//                                           const vector<double>& partialCharges, vector<RealVec>& inputForces) {
double ReferenceObc::computeBornEnergyForces(const ObcParameters* obcParameters,
                                             const vector3* atomCoordinates,
                                             const vector<double>& partialCharges,
                                             const double* Igrid,
                                             vector3* inputForces) {

    // ---------------------------------------------------------------------------------------

    static const double zero    = static_cast<double>(0.0);
    static const double one     = static_cast<double>(1.0);
    static const double two     = static_cast<double>(2.0);
//    static const double three   = static_cast<double>(3.0);
    static const double four    = static_cast<double>(4.0);
    static const double half    = static_cast<double>(0.5);
    static const double fourth  = static_cast<double>(0.25);
    static const double eighth  = static_cast<double>(0.125);

    // constants
    const int numberOfAtoms = obcParameters->getNumberOfAtoms();
    const double strength = obcParameters->getStrength();
    const double dielectricOffset = obcParameters->getDielectricOffset();
    const double cutoffDistance = obcParameters->getCutoffDistance();
    const double soluteDielectric = obcParameters->getSoluteDielectric();
    const double solventDielectric = obcParameters->getSolventDielectric();
    double preFactor;
    if (soluteDielectric != zero && solventDielectric != zero)
        preFactor = two*obcParameters->getElectricConstant()*((one/soluteDielectric) - (one/solventDielectric));
    else
        preFactor = zero;
    preFactor *= strength; // Should adjust strength in first main loop

    // ---------------------------------------------------------------------------------------

//    cout << "Number of atoms: " << numberOfAtoms << endl;
//    cout << "Atomic coordinates: " << endl;
//    for (int i = 0; i < numberOfAtoms; ++i)
//      cout << atomCoordinates[i][0] << ' ' << atomCoordinates[i][1] << ' ' << atomCoordinates[i][2] << endl;
//    cout << "Partial charges: " << endl;
//    for (int i = 0; i < numberOfAtoms; ++i)
//      cout << partialCharges[i] << endl;
//    cout << "Input forces: " << endl;
//    for (int i = 0; i < numberOfAtoms; ++i)
//      cout << inputForces[i][0] << ' ' << inputForces[i][1] << ' ' << inputForces[i][2] << endl;

    // compute Born radii

    vector<double> bornRadii(numberOfAtoms);
    computeBornRadii(obcParameters, atomCoordinates, Igrid, bornRadii);

    // set energy/forces to zero

    double obcEnergy                 = zero;
    vector<double> bornForces(numberOfAtoms, 0.0);

    // ---------------------------------------------------------------------------------------

    // compute the nonpolar solvation via ACE approximation
     
    if (includeAceApproximation()) {
       computeAceNonPolarForce(obcParameters, bornRadii, &obcEnergy, bornForces);
    }
 
    // ---------------------------------------------------------------------------------------

    // first main loop

    for (int atomI = 0; atomI < numberOfAtoms; atomI++) {
 
       double partialChargeI = preFactor*partialCharges[atomI];
       for (int atomJ = atomI; atomJ < numberOfAtoms; atomJ++) {

          double deltaR[OpenMM::ReferenceForce::LastDeltaRIndex];
          OpenMM::ReferenceForce::getDeltaR(atomCoordinates[atomI], atomCoordinates[atomJ], deltaR);
          if (obcParameters->getUseCutoff() && deltaR[OpenMM::ReferenceForce::RIndex] > cutoffDistance)
              continue;

          double r2                 = deltaR[OpenMM::ReferenceForce::R2Index];
          double deltaX             = deltaR[OpenMM::ReferenceForce::XIndex];
          double deltaY             = deltaR[OpenMM::ReferenceForce::YIndex];
          double deltaZ             = deltaR[OpenMM::ReferenceForce::ZIndex];

          double alpha2_ij          = bornRadii[atomI]*bornRadii[atomJ];
          double D_ij               = r2/(four*alpha2_ij);

          double expTerm            = EXP(-D_ij);
          double denominator2       = r2 + alpha2_ij*expTerm; 
          double denominator        = SQRT(denominator2); 
          
          double Gpol               = (partialChargeI*partialCharges[atomJ])/denominator; 
          double dGpol_dr           = -Gpol*(one - fourth*expTerm)/denominator2;  

          double dGpol_dalpha2_ij   = -half*Gpol*expTerm*(one + D_ij)/denominator2;
          
          double energy = Gpol;

          if (atomI != atomJ) {

              if (obcParameters->getUseCutoff())
                  energy -= partialChargeI*partialCharges[atomJ]/cutoffDistance;
              
              bornForces[atomJ]        += dGpol_dalpha2_ij*bornRadii[atomI];

              deltaX                   *= dGpol_dr;
              deltaY                   *= dGpol_dr;
              deltaZ                   *= dGpol_dr;

              inputForces[atomI][0]    -= deltaX;
              inputForces[atomI][1]    -= deltaY;
              inputForces[atomI][2]    -= deltaZ;

              inputForces[atomJ][0]    += deltaX;
              inputForces[atomJ][1]    += deltaY;
              inputForces[atomJ][2]    += deltaZ;

          } else {
             energy *= half;
          }

          obcEnergy         += energy;
          bornForces[atomI] += dGpol_dalpha2_ij*bornRadii[atomJ];

       }
    }
  
    // ---------------------------------------------------------------------------------------

    // second main loop
  
    const vector<double>& obcChain            = getObcChain();
    const vector<double>& atomicRadii         = obcParameters->getAtomicRadii();

//    const double alphaObc                   = obcParameters->getAlphaObc();
//    const double betaObc                    = obcParameters->getBetaObc();
//    const double gammaObc                   = obcParameters->getGammaObc();
    const vector<double>& scaledRadiusFactor  = obcParameters->getScaledRadiusFactors();

    // compute factor that depends only on the outer loop index

    for (int atomI = 0; atomI < numberOfAtoms; atomI++) {
       bornForces[atomI] *= bornRadii[atomI]*bornRadii[atomI]*obcChain[atomI];      
    }

    for (int atomI = 0; atomI < numberOfAtoms; atomI++) {
 
       // radius w/ dielectric offset applied

       double radiusI        = atomicRadii[atomI];
       double offsetRadiusI  = radiusI - dielectricOffset;

       for (int atomJ = 0; atomJ < numberOfAtoms; atomJ++) {

          if (atomJ != atomI) {

             double deltaR[OpenMM::ReferenceForce::LastDeltaRIndex];
             OpenMM::ReferenceForce::getDeltaR(atomCoordinates[atomI], atomCoordinates[atomJ], deltaR);
             if (obcParameters->getUseCutoff() && deltaR[OpenMM::ReferenceForce::RIndex] > cutoffDistance)
                    continue;
    
             double deltaX             = deltaR[OpenMM::ReferenceForce::XIndex];
             double deltaY             = deltaR[OpenMM::ReferenceForce::YIndex];
             double deltaZ             = deltaR[OpenMM::ReferenceForce::ZIndex];
             double r                  = deltaR[OpenMM::ReferenceForce::RIndex];
 
             // radius w/ dielectric offset applied

             double offsetRadiusJ      = atomicRadii[atomJ] - dielectricOffset;

             double scaledRadiusJ      = offsetRadiusJ*scaledRadiusFactor[atomJ];
             double scaledRadiusJ2     = scaledRadiusJ*scaledRadiusJ;
             double rScaledRadiusJ     = r + scaledRadiusJ;

             // dL/dr & dU/dr are zero (this can be shown analytically)
             // removed from calculation

             if (offsetRadiusI < rScaledRadiusJ) {

                double l_ij          = offsetRadiusI > FABS(r - scaledRadiusJ) ? offsetRadiusI : FABS(r - scaledRadiusJ);
                     l_ij                = one/l_ij;

                double u_ij          = one/rScaledRadiusJ;

                double l_ij2         = l_ij*l_ij;

                double u_ij2         = u_ij*u_ij;
 
                double rInverse      = one/r;
                double r2Inverse     = rInverse*rInverse;

                double t3            = eighth*(one + scaledRadiusJ2*r2Inverse)*(l_ij2 - u_ij2) + fourth*LN(u_ij/l_ij)*r2Inverse;

                double de            = bornForces[atomI]*t3*rInverse;

                deltaX                  *= de;
                deltaY                  *= de;
                deltaZ                  *= de;
    
                inputForces[atomI][0]   += deltaX;
                inputForces[atomI][1]   += deltaY;
                inputForces[atomI][2]   += deltaZ;
  
                inputForces[atomJ][0]   -= deltaX;
                inputForces[atomJ][1]   -= deltaY;
                inputForces[atomJ][2]   -= deltaZ;
 
             }
          }
       }

    }

    return obcEnergy;
}

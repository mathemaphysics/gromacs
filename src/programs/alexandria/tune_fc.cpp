/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2016, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/*! \internal \brief
 * Implements part of the alexandria program.
 * \author David van der Spoel <david.vanderspoel@icm.uu.se>
 */
#include "gmxpre.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <random>

#include "gromacs/commandline/pargs.h"
#include "gromacs/commandline/viewit.h"
#include "gromacs/fileio/confio.h"
#include "gromacs/fileio/xvgr.h"
#include "gromacs/gmxlib/network.h"
#include "gromacs/gmxlib/nrnb.h"
#include "gromacs/gmxpreprocess/convparm.h"
#include "gromacs/gmxpreprocess/gpp_atomtype.h"
#include "gromacs/gmxpreprocess/grompp.h"
#include "gromacs/gmxpreprocess/pdb2top.h"
#include "gromacs/linearalgebra/matrix.h"
#include "gromacs/listed-forces/bonded.h"
#include "gromacs/math/units.h"
#include "gromacs/math/vec.h"
#include "gromacs/mdlib/force.h"
#include "gromacs/mdlib/mdatoms.h"
#include "gromacs/mdlib/shellfc.h"
#include "gromacs/mdlib/vsite.h"
#include "gromacs/mdtypes/md_enums.h"
#include "gromacs/mdtypes/state.h"
#include "gromacs/statistics/statistics.h"
#include "gromacs/timing/wallcycle.h"
#include "gromacs/topology/atomprop.h"
#include "gromacs/topology/mtop_util.h"
#include "gromacs/topology/topology.h"
#include "gromacs/utility/coolstuff.h"
#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/futil.h"
#include "gromacs/utility/init.h"
#include "gromacs/utility/stringutil.h"

#include "nmsimplex.h"

// alexandria stuff
#include "gentop_core.h"
#include "gmx_simple_comm.h"
#include "moldip.h"
#include "molprop.h"
#include "molprop_util.h"
#include "molprop_xml.h"
#include "molselect.h"
#include "mymol.h"
#include "optparam.h"
#include "poldata.h"
#include "poldata_xml.h"
#include "qgen_eem.h"
#include "regression.h"
#include "stringutil.h"

/*! \brief Write a csv file containing molecule names and bond energy
 *
 * Writes the whole bond energy matrix.
 */
static void dump_csv(const std::vector<std::string>        &ctest,
                     const std::vector<alexandria::MyMol>  &mm,
                     const std::vector<int>                &ntest,
                     const std::vector<double>             &Edissoc,
                     double                               **a,
                     double                                *x)
{
    FILE *csv = gmx_ffopen("tune_fc.csv", "w");
    fprintf(csv, ",");
    for (auto j : ctest)
    {
        fprintf(csv, "%s,", j.c_str());
    }
    fprintf(csv, "\n");
    int i = 0;
    for (auto &mymol : mm)
    {
        fprintf(csv, "%s,", mymol.molProp()->getMolname().c_str());
        for (size_t j = 0; (j < ctest.size()); j++)
        {
            fprintf(csv, "%g,", a[j][i]);
        }
        fprintf(csv, "%.3f\n", x[i]);
        i++;
    }
    fprintf(csv, "Total,");
    for (auto j : ntest)
    {
        fprintf(csv, "%d,", j);
    }
    fprintf(csv, "\n");
    fprintf(csv, "Edissoc,");
    for (auto j : Edissoc)
    {
        fprintf(csv, "%.3f,", j);
    }
    fprintf(csv, "\n");
    fclose(csv);
}

namespace alexandria
{

/*! \brief Helper class storing bond/angle/dihedral names
 *
 * For one bond/angle/dihedral here the name of the bondtypes
 * are stored as in e.g. c c h for an angle, along with the number
 * of occurrences in the force field.
 */
class BondNames
{
    public:
    
        BondNames(int                ncopies,
                  const std::string &name,
                  const std::string &params,
                  int                index,
                  double             bondorder = 0) 
                  
            :
                ncopies_(ncopies), 
                name_(name), 
                params_(params), 
                bondorder_(bondorder), 
                poldataIndex_(index)
            {
                extractParams();
            }

        void inc() { ncopies_++; }

        int nCopies() const { return ncopies_; }

        void setParamString(const std::string &params);

        const std::string &name() const { return name_; }

        double bondorder() const { return bondorder_; }

        int poldataIndex() const { return poldataIndex_; }

        const std::string &paramString() const { return params_; }

        const std::vector<double> &paramValues() const { return p_; }

        size_t nParams() const { return p_.size(); }
        
    private:
    
        //! Number of copies in the molecule data set
        int                 ncopies_;
        //! Name of this bond/angle/dihedral
        std::string         name_;
        //! String holding all the parameters
        std::string         params_;
        //! Vector containing all the parameters
        std::vector<double> p_;
        //! The bond order in case this is a bond
        double              bondorder_;
        //! Index in Poldata structure
        int                 poldataIndex_;
        //! Internal routine to extract the parameters
        void extractParams();
};

using BondNamesIterator = typename std::vector<BondNames>::iterator ;

void BondNames::setParamString(const std::string &params)
{
    params_ = params;
    extractParams();
}

void BondNames::extractParams()
{
    std::vector<std::string> p = gmx::splitString(params_);
    p_.clear();
    for (const auto &d : p)
    {
        p_.push_back(std::move(atof(d.c_str())));
    }
}

/*! \brief Class holding for one type of interactions all names
 *
 * Class holding the OptNames for one interaction type.
 */
class ForceConstants
{

    public:
    
        ForceConstants(int bt, int ftype, InteractionType itype, bool bOpt) 
            : 
                bt_(bt), 
                ftype_(ftype), 
                itype_(itype), 
                bOpt_(bOpt)
            {}

        void addForceConstant(BondNames bn) { bn_.push_back(std::move(bn)); }

        void analyzeIdef(std::vector<MyMol> &mm,
                         const Poldata      &pd);

        /*! \brief Make reverse index from Poldata to BondNames
         *
         * The BondNames structure stores the Poldata index for
         * all interactions. This routine makes an index to convert
         * the Poldata index to the index in BondNames.
         */
        void makeReverseIndex();

        int reverseIndex(int poldataIndex)
        {
            GMX_RELEASE_ASSERT(poldataIndex >= 0 && poldataIndex < static_cast<int>(reverseIndex_.size()), "Incorrect poldataIndex");
            GMX_RELEASE_ASSERT(reverseIndex_[poldataIndex] != -1, "The reverseIndex is incorrect");

            return reverseIndex_[poldataIndex];
        }

        int bt2() const { return bt_; }

        int ftype() const { return ftype_; }

        InteractionType interactionType() const { return itype_; }

        void dump(FILE *fp) const;

        BondNamesIterator beginBN() { return bn_.begin(); }

        BondNamesIterator endBN() { return bn_.end(); }

        size_t nbad() const { return bn_.size(); }
        
    private:
    
        int                    bt_;
        int                    ftype_;
        InteractionType        itype_;
        bool                   bOpt_;
        std::vector<BondNames> bn_;
        std::vector<int>       reverseIndex_;
        std::vector<double>    params_;
};

void ForceConstants::analyzeIdef(std::vector<MyMol> &mm,
                                 const Poldata      &pd)
{
    std::string  aai, aaj, aak, aal;

    if (!bOpt_)
    {
        return;
    }
    for (auto &mymol : mm)
    {
        for (int i = 0; (i < mymol.ltop_->idef.il[ftype_].nr);
             i += interaction_function[ftype_].nratoms+1)
        {
            std::vector<std::string> atoms;
            std::string              params;
            bool                     found     = false;
            int                      ai        = mymol.ltop_->idef.il[ftype_].iatoms[i+1];
            int                      aj        = mymol.ltop_->idef.il[ftype_].iatoms[i+2];
            if (pd.atypeToBtype( *mymol.topology_->atoms.atomtype[ai], aai) &&
                pd.atypeToBtype( *mymol.topology_->atoms.atomtype[aj], aaj))
            {
                int  index = 0;
                char buf[STRLEN];
                char buf_reverse[STRLEN];
                auto iType = static_cast<InteractionType>(bt_);
                switch (iType)
                {
                    case eitBONDS:
                    {
                        atoms   = {aai, aaj};
                        auto fs = pd.findForces(iType);
                        auto f  = fs->findForce(atoms);

                        if (fs->forceEnd() != f)
                        {
                            sprintf(buf, "%s %s", aai.c_str(), aaj.c_str());
                            sprintf(buf_reverse, "%s %s", aaj.c_str(), aai.c_str());
                            params    = f->params();
                            index     = f - fs->forceBegin();
                            found     = true;
                        }
                    }
                    break;
                    case eitANGLES:
                    case eitLINEAR_ANGLES:
                    {
                        int ak  = mymol.ltop_->idef.il[ftype_].iatoms[i+3];
                        if (pd.atypeToBtype( *mymol.topology_->atoms.atomtype[ak], aak))
                        {
                            atoms   = {aai, aaj, aak};
                            auto fs = pd.findForces(iType);
                            auto f  = fs->findForce(atoms);

                            if (fs->forceEnd() != f)
                            {
                                sprintf(buf, "%s %s %s", aai.c_str(),
                                        aaj.c_str(), aak.c_str());
                                sprintf(buf_reverse, "%s %s %s", aak.c_str(),
                                        aaj.c_str(), aai.c_str());
                                params = f->params();
                                index  = f - fs->forceBegin();
                                found  = true;
                            }
                        }
                    }
                    break;
                    case eitPROPER_DIHEDRALS:
                    case eitIMPROPER_DIHEDRALS:
                    {
                        int ak  = mymol.ltop_->idef.il[ftype_].iatoms[i+3];
                        int al  = mymol.ltop_->idef.il[ftype_].iatoms[i+4];
                        if (pd.atypeToBtype( *mymol.topology_->atoms.atomtype[ak], aak) &&
                            pd.atypeToBtype( *mymol.topology_->atoms.atomtype[al], aal))
                        {
                            atoms   = {aai, aaj, aak, aal};
                            auto fs = pd.findForces(iType);
                            auto f  = fs->findForce(atoms);

                            if (fs->forceEnd() != f)
                            {
                                sprintf(buf, "%s %s %s %s", aai.c_str(),
                                        aaj.c_str(), aak.c_str(), aal.c_str());
                                sprintf(buf_reverse, "%s %s %s %s", aal.c_str(),
                                        aaj.c_str(), aak.c_str(), aai.c_str());
                                params = f->params();
                                index  = f - fs->forceBegin();
                                found  = true;
                            }
                        }
                    }
                    break;
                    case eitPOLARIZATION:
                    case eitLJ14:
                    case eitVSITE2:
                    case eitCONSTR:
                    case eitNR:
                        break;
                }
                if (found)
                {
                    auto c = std::find_if(bn_.begin(), bn_.end(),
                                          [buf, buf_reverse](const BondNames &bn)
                        {
                            return (bn.name().compare(buf) == 0 ||
                                    bn.name().compare(buf_reverse) == 0);
                        });
                    if (c != bn_.end())
                    {
                        c->inc();
                    }
                    else
                    {
                        BondNames bn(1, buf, params, index, 0);
                        addForceConstant(bn);
                    }
                }
            }
        }
    }
}

void ForceConstants::makeReverseIndex()
{
    int N = 0;
    for (const auto &i : bn_)
    {
        N = std::max(N, i.poldataIndex());
    }
    reverseIndex_.resize(N+1, -1);
    int j = 0;
    for (const auto &i : bn_)
    {
        reverseIndex_[i.poldataIndex()] = j++;
    }
}

void ForceConstants::dump(FILE *fp) const
{
    const char  *btsnames[ebtsNR] =
    { "bond", "angle", "proper", "improper", nullptr, nullptr };

    if (bOpt_)
    {
        int ntot = 0;
        fprintf(fp, "Interaction  Bondtypes             Copies Poldata entry\n");
        for (const auto &i : bn_)
        {
            fprintf(fp, "%-10s  %-20s  %5d  %5d\n",
                    btsnames[bt_], i.name().c_str(), i.nCopies(), i.poldataIndex());
            ntot += i.nCopies();
        }
        fprintf(fp, "%-8s %d of %4d types\n", btsnames[bt_], ntot,
                static_cast<int>(bn_.size()));
    }
}

class Optimization : public MolDip
{
    using param_type = std::vector<double>;

    private:
    public:

        std::vector<ForceConstants> ForceConstants_;
        param_type                  param_, lower_, upper_, best_, orig_, psigma_, pmean_;
        const real                  forceWeight_;

        /*! \brief
         *
         * Constructor
         */
        Optimization(const real forceWeight) 
            :
                forceWeight_(forceWeight)
            {};

        /*! \brief
         *
         * Destructor
         */
        ~Optimization() {};

        /*! \brief
         *
         * Check whether molecules are supported by the force field.
         * Check whether all the atomtypes in the molecules are present
         * in the force field file. If not the molecules are removed.
         *
         * Routine also divides molecules over processors.
         */
        void checkSupport(FILE *fp, bool  bOpt[]);

        /*! \brief
         *
         * Fill parameter vector using ForceConstants which
         * is built based on Poldata.
         */
        void polData2TuneFc();

        /*! \brief
         *
         * Copy the optimized parameters back to Poldata
         */
        void tuneFc2PolData();

        /*! \brief
         *
         * Compute the dissociation energies for all the bonds.
         * Given all the bonds and the enthalpies of formation of all
         * molecules, we can approximate the dissociation enthalpy (D0 in the
         * Morse potential by least squares fitting the D0 to reproduce the
         * molecular energy (Delta H formation of molecule - Delta H formation of
         * the atoms). This is a crude approximation since all other energy
         * terms in the force field are ignored, however the dissociation
         * energy is the largest contribution to the molecular energy.
         */
        void getDissociationEnergy(FILE *fplog);
        
        void InitOpt(FILE *fplog, bool bOpt[eitNR], real factor);

        void optRun(FILE *fp, FILE *fplog, int maxiter,
                    int nrun, real stepsize, int seed,
                    const gmx_output_env_t *oenv,
                    int nprint, const char *xvgconv, 
                    const char *xvgepot,
                    real temperature, bool bBound);
                    
        void Print(FILE *fp);
        
        void printSpecs(FILE *fp, char *title,
                        const char *xvg, 
                        const gmx_output_env_t *oenv,
                        bool bCheckOutliers);

        double calcDeviation();
        
        double objFunction(const double v[]);
};

void Optimization::checkSupport(FILE *fp, bool  bOpt[])
{
    int ntotal  = _mymol.size();
    int nlocal  = 0;

    for (auto mymol = _mymol.begin(); mymol < _mymol.end(); )
    {
        if (mymol->eSupp != eSupportLocal)
        {
            continue;
        }

        bool bSupport = true;

        for (int bt = 0; bSupport && (bt <= eitNR); bt++)
        {
            int  ft;
            if (bOpt[bt])
            {
                auto iType = static_cast<InteractionType>(bt);
                ft         = pd_.findForces(iType)->fType();
                bSupport   = (mymol->ltop_ != nullptr);
                for (int i = 0; bSupport && (i < mymol->ltop_->idef.il[ft].nr);
                     i += interaction_function[ft].nratoms+1)
                {
                    int                      ai, aj, ak, al;
                    std::string              aai, aaj, aak, aal;
                    std::vector<std::string> atoms;

                    ai  = mymol->ltop_->idef.il[ft].iatoms[i+1];
                    aj  = mymol->ltop_->idef.il[ft].iatoms[i+2];
                    if (!(pd_.atypeToBtype(*mymol->topology_->atoms.atomtype[ai], aai) &&
                          pd_.atypeToBtype(*mymol->topology_->atoms.atomtype[aj], aaj)))
                    {
                        bSupport = false;
                        if (debug)
                        {
                            fprintf(debug, "Cannot find bond types %s and %s in %s.\n",
                                    aai.c_str(), aaj.c_str(),
                                    mymol->molProp()->getMolname().c_str());
                        }
                    }
                    switch (iType)
                    {
                        case eitBONDS:
                        {
                            atoms = {aai, aaj};
                            auto fs = pd_.findForces(iType);
                            auto f  = fs->findForce(atoms);
                            if (fs->forceEnd() == f)
                            {
                                bSupport = false;
                                if (debug)
                                {
                                    fprintf(debug, "Cannot find bond %s-%s in %s.\n",
                                            aai.c_str(), aaj.c_str(),
                                            mymol->molProp()->getMolname().c_str());
                                }
                            }
                            break;
                        }
                        case eitANGLES:
                        case eitLINEAR_ANGLES:
                        {
                            ak  = mymol->ltop_->idef.il[ft].iatoms[i+3];
                            if (!pd_.atypeToBtype( *mymol->topology_->atoms.atomtype[ak], aak))
                            {
                                bSupport = false;
                                if (debug)
                                {
                                    fprintf(debug, "Cannot find bond types %s, %s and %s in %s.\n",
                                            aai.c_str(), aaj.c_str(), aak.c_str(),
                                            mymol->molProp()->getMolname().c_str());
                                }
                            }
                            else
                            {
                                atoms   = {aai, aaj, aak};
                                auto fs = pd_.findForces(iType);
                                auto f  = fs->findForce(atoms);
                                if (fs->forceEnd() == f)
                                {
                                    bSupport = false;
                                    if (debug)
                                    {
                                        fprintf(debug, "Cannot find %s %s-%s-%s in %s.\n",
                                                eitANGLES ? "angles" : "linear_angles",
                                                aai.c_str(), aaj.c_str(), aak.c_str(),
                                                mymol->molProp()->getMolname().c_str());
                                    }
                                }
                            }
                        }
                        break;
                        case eitPROPER_DIHEDRALS:
                        case eitIMPROPER_DIHEDRALS:
                        {
                            ak  = mymol->ltop_->idef.il[ft].iatoms[i+3];
                            al  = mymol->ltop_->idef.il[ft].iatoms[i+4];
                            if (!(pd_.atypeToBtype( *mymol->topology_->atoms.atomtype[ak], aak) &&
                                  pd_.atypeToBtype( *mymol->topology_->atoms.atomtype[al], aal)))
                            {
                                bSupport = false;
                                if (debug)
                                {
                                    fprintf(debug, "Cannot find bond types %s, %s, %s, and %s in %s\n",
                                            aai.c_str(), aaj.c_str(), aak.c_str(), aal.c_str(),
                                            mymol->molProp()->getMolname().c_str());
                                }
                            }
                            else
                            {
                                atoms   = {aai, aaj, aak, aal};
                                auto fs = pd_.findForces(iType);
                                auto f  = fs->findForce(atoms);
                                if (fs->forceEnd() == f)
                                {
                                    bSupport = false;
                                    if (debug)
                                    {
                                        fprintf(debug, "Cannot find %s dihedral %s-%s-%s-%s in %s\n",
                                                eitPROPER_DIHEDRALS ? "proper" : "improper",
                                                aai.c_str(), aaj.c_str(), aak.c_str(), aal.c_str(),
                                                mymol->molProp()->getMolname().c_str());
                                    }
                                }
                            }
                            break;
                            case eitPOLARIZATION:
                            case eitLJ14:
                            case eitVSITE2:
                            case eitCONSTR:
                            case eitNR:
                                break;
                        }
                    }
                }
            }
        }
        if (!bSupport)
        {
            fprintf(stderr, "No force field support for %s\n",
                    mymol->molProp()->getMolname().c_str());
            mymol = _mymol.erase(mymol);
        }
        else
        {
            mymol++;
            nlocal++;
        }
    }
    if (PAR(_cr))
    {
        gmx_sumi(1, &nlocal, _cr);
    }
    if (nullptr != fp)
    {
        fprintf(fp, "%d out of %d molecules have support in the force field.\n",
                nlocal, ntotal);
    }
}

void Optimization::polData2TuneFc()
{
    param_.clear();
    for (auto &fc : ForceConstants_)
    {
        for (auto b = fc.beginBN(); b  < fc.endBN(); ++b)
        {
            for (const auto &p : b->paramValues())
            {
                param_.push_back(std::move(p));
            }
        }
    }
}

void Optimization::tuneFc2PolData()
{
    int n = 0;
    std::vector<std::string> atoms;
    for (auto &fc : ForceConstants_)
    {
        for (auto b = fc.beginBN(); b  < fc.endBN(); ++b)
        {
            char buf[STRLEN];
            buf[0] = '\0';

            for (size_t p = 0; p < b->nParams(); p++)
            {
                strncat(buf, " ", sizeof(buf)-1);
                strncat(buf, gmx_ftoa(param_[n++]).c_str(), sizeof(buf)-1);
            }
            b->setParamString(buf);
            const auto bondtypes = gmx::splitString(b->name());
            const auto iType     = fc.interactionType();
            switch (iType)
            {
                case eitBONDS:
                {
                    atoms    = {bondtypes[0], bondtypes[1]};
                    auto  fs = pd_.findForces(iType);
                    auto  f  = fs->findForce(atoms);
                    if (fs->forceEnd() != f)
                    {
                        f->setParams(buf);
                    }
                }
                break;
                case eitANGLES:
                case eitLINEAR_ANGLES:
                {
                    atoms    = {bondtypes[0], bondtypes[1], bondtypes[2]};
                    auto  fs = pd_.findForces(iType);
                    auto  f  = fs->findForce(atoms);
                    if (fs->forceEnd() != f)
                    {
                        f->setParams(buf);
                    }
                }
                break;
                case eitPROPER_DIHEDRALS:
                case eitIMPROPER_DIHEDRALS:
                {
                    atoms    = {bondtypes[0], bondtypes[1], bondtypes[2], bondtypes[3]};
                    auto  fs = pd_.findForces(iType);
                    auto  f  = fs->findForce(atoms);
                    if (fs->forceEnd() != f)
                    {
                        f->setParams(buf);
                    }
                }
                break;
                default:
                    gmx_fatal(FARGS, "Unsupported InteractionType %d",
                              static_cast<int>(fc.interactionType()));
            }
        }
    }
}

void Optimization::getDissociationEnergy(FILE *fplog)
{
    double                    **a;
    std::vector<double>         rhs;
    std::vector<int>            ntest;
    std::vector<std::string>    ctest;

    int nD   = ForceConstants_[eitBONDS].nbad();
    int nMol = _mymol.size();

    if ((0 == nD) || (0 == nMol))
    {
        gmx_fatal(FARGS, "Number of variables is %d and number of molecules is %d",
                  nD, nMol);
    }

    a = alloc_matrix(nD, nMol);
    ntest.resize(nD, 0);
    ctest.resize(nD);

    fprintf(fplog, "There are %d different bondtypes to optimize the heat of formation\n",
            nD);
    fprintf(fplog, "There are %d (experimental) reference heat of formation.\n", nMol);

    auto fs  = pd_.findForces(eitBONDS);
    int  ftb = fs->fType();
    int  j   = 0;

    for (auto mymol = _mymol.begin(); mymol < _mymol.end(); mymol++, j++)
    {
        for (int i = 0; (i < mymol->ltop_->idef.il[ftb].nr);
             i += interaction_function[ftb].nratoms+1)
        {
            int                      ai = mymol->ltop_->idef.il[ftb].iatoms[i+1];
            int                      aj = mymol->ltop_->idef.il[ftb].iatoms[i+2];
            std::string              aai, aaj;
            std::vector<std::string> atoms;
            if (pd_.atypeToBtype(*mymol->topology_->atoms.atomtype[ai], aai) &&
                pd_.atypeToBtype(*mymol->topology_->atoms.atomtype[aj], aaj))
            {
                atoms  = {aai, aaj};
                auto f = fs->findForce(atoms);
                if (fs->forceEnd() != f)
                {
                    int gt  = f - fs->forceBegin();
                    int gti = ForceConstants_[eitBONDS].reverseIndex(gt);

                    a[gti][j]++;
                    ntest[gti]++;
                    if (ctest[gti].empty())
                    {
                        char buf[STRLEN];
                        snprintf(buf, sizeof(buf), "%s-%s", aai.c_str(), aaj.c_str());
                        ctest[gti].assign(buf);
                    }
                }
            }
            else
            {
                gmx_fatal(FARGS, "No parameters for bond %s-%s in the force field, atoms %s-%s mol %s",
                          aai.c_str(), aaj.c_str(),
                          *mymol->topology_->atoms.atomtype[ai],
                          *mymol->topology_->atoms.atomtype[aj],
                          mymol->molProp()->getIupac().c_str());
            }
        }
        rhs.push_back(std::move(-mymol->Emol));
    }

    char buf[STRLEN];
    snprintf(buf, sizeof(buf), "Inconsistency in number of energies nMol %d != #rhs %d", nMol, static_cast<int>(rhs.size()));
    GMX_RELEASE_ASSERT(static_cast<int>(rhs.size()) == nMol, buf);

    int nzero = std::count_if(ntest.begin(), ntest.end(), [](const int n)
        {
            return n == 0;
        });

    GMX_RELEASE_ASSERT(nzero == 0, "Inconsistency in the number of bonds in poldata and ForceConstants_");

    std::vector<double> Edissoc(nD);

    multi_regression2(nMol, rhs.data(), nD, a, Edissoc.data());
    dump_csv(ctest, _mymol, ntest, Edissoc, a, rhs.data());

    for (size_t i = 0; (i < ctest.size()); i++)
    {
        if (fplog)
        {
            fprintf(fplog, "Optimized dissociation energy for %8s with %4d copies to %g\n",
                    ctest[i].c_str(), ntest[i], Edissoc[i]);
        }
    }

    free_matrix(a);

    int i = 0;
    for (auto b = ForceConstants_[eitBONDS].beginBN();
         b < ForceConstants_[eitBONDS].endBN(); ++b)
    {
        const auto       atoms = gmx::splitString(b->name());
        auto             fs    = pd_.findForces(eitBONDS);
        auto             f     = fs->findForce(atoms);
        GMX_RELEASE_ASSERT(fs->forceEnd() != f, "Cannot find my bonds");
        const auto       pp    = gmx::splitString(b->paramString());
        char             buf[256];

        /*De is assumed to be the first parameter. Not good!*/
        snprintf(buf, sizeof(buf), "%.2f  %s", Edissoc[i], pp[1].c_str());
        f->setParams(buf);
        b->setParamString(buf);
        i++;
    }
}

void Optimization::InitOpt(FILE *fplog, bool bOpt[eitNR], real  factor)
{
    std::vector<unsigned int> fts;

    for (auto fs = pd_.forcesBegin();
         fs != pd_.forcesEnd(); fs++)
    {
        fts.push_back(std::move(fs->fType()));
    }

    for (size_t bt = 0; (bt < fts.size()); bt++)
    {
        ForceConstants fc(bt, fts[bt], static_cast<InteractionType>(bt), bOpt[bt]);
        fc.analyzeIdef(_mymol, pd_);
        fc.makeReverseIndex();
        fc.dump(fplog);
        ForceConstants_.push_back(std::move(fc));
    }

    if (ForceConstants_[eitBONDS].nbad() <= _mymol.size())
    {
        getDissociationEnergy(fplog);
    }
    else
    {
        printf("\n"
               "WARNING: %zu molecule(s) is (are) not enough to calculate dissociation\n"
               "         energy for %zu bond type(s) using linear regression. Defualt\n"
               "         values from gentop.dat will be used as the initial guess.\n"
               "         Recomendation is to add more molecules having the same bond types.\n\n",
               _mymol.size(), ForceConstants_[eitBONDS].nbad());
    }

    polData2TuneFc();

    orig_.resize(param_.size(), 0);
    best_.resize(param_.size(), 0);
    lower_.resize(param_.size(), 0);
    upper_.resize(param_.size(), 0);
    psigma_.resize(param_.size(), 0);
    pmean_.resize(param_.size(), 0);

    if (factor < 1)
    {
        factor = 1/factor;
    }
    for (size_t i = 0; (i < param_.size()); i++)
    {
        best_[i]  = orig_[i] = param_[i];
        lower_[i] = orig_[i]/factor;
        upper_[i] = orig_[i]*factor;
    }
}

void Optimization::Print(FILE *fp)
{
    fprintf(fp, "Param        Orig        Best\n");
    for (size_t k = 0; (k < param_.size()); k++)
    {
        fprintf(fp, "%-5d  %10g  %10g\n", static_cast<int>(k),
                orig_[k], best_[k]);
    }
}

static void xvgr_symbolize(FILE *xvgf, int nsym, const char *leg[],
                           const gmx_output_env_t *oenv)
{
    int i;

    xvgr_legend(xvgf, nsym, leg, oenv);
    for (i = 0; (i < nsym); i++)
    {
        xvgr_line_props(xvgf, i, elNone, ecBlack+i, oenv);
        fprintf(xvgf, "@ s%d symbol %d\n", i, i+1);
    }
}

double Optimization::calcDeviation()
{
    int     j;
    rvec    mu_tot;
    FILE   *dbcopy;
    double  ener    = 0;
    double  optHF   = 0;
    double  spHF    = 0;
    double  deltaEn = 0;
    double  Emol    = 0;

    if (PAR(_cr))
    {
        gmx_bcast(sizeof(_bDone), &_bDone, _cr);
        gmx_bcast(sizeof(_bFinal), &_bFinal, _cr);
    }
    if (_bDone)
    {
        return _ener[ermsTOT];
    }
    if (nullptr != debug)
    {
        fprintf(debug, "Begin communicating force parameters\n");
        fflush(debug);
    }
    if (PAR(_cr))
    {
        pd_.broadcast(_cr);
    }
    if (nullptr != debug)
    {
        fprintf(debug, "Done communicating force parameters\n");
        fflush(debug);
    }

    for (j = 0; (j < ermsNR); j++)
    {
        _ener[j] = 0;
    }

    for (auto &mymol : _mymol)
    {
        if (mymol.molProp()->getOptHF(&optHF))
        {
            int natoms = mymol.molProp()->NAtom();
            int nOptSP = mymol.molProp()->NOptSP();

            if ((mymol.eSupp == eSupportLocal) ||
                (_bFinal && (mymol.eSupp == eSupportRemote)))
            {
                for (const auto fc : ForceConstants_)
                {
                    if (fc.nbad() > 0)
                    {
                        mymol.UpdateIdef(pd_, fc.interactionType());
                    }
                }

                for (auto ei = mymol.molProp()->BeginExperiment();
                     ei < mymol.molProp()->EndExperiment(); ++ei)
                {
                    auto jtype = ei->getJobtype();

                    if (jtype == JOB_OPT || jtype == JOB_SP)
                    {
                        ei->getHF(&spHF);

                        deltaEn = spHF - optHF;
                        Emol    = mymol.Emol + deltaEn;

                        for (j = 0; (j < natoms); j++)
                        {
                            clear_rvec(mymol.f_[j]);
                        }

                        dbcopy = debug;
                        debug  = nullptr;

                        mymol.changeCoordinate(ei);
                        mymol.computeForces(debug, _cr, mu_tot);

                        debug         = dbcopy;
                        mymol.Force2  = 0.0;

                        mymol.Ecalc  = mymol.enerd_->term[F_EPOT];
                        ener         = gmx::square(mymol.Ecalc-Emol);
                        
                        for (j = 0; (j < natoms); j++)
                        {
                            mymol.Force2 += iprod(mymol.f_[j], mymol.f_[j]);
                        }

                        mymol.Force2 /= natoms;

                        if (jtype == JOB_OPT)
                        {
                            mymol.OptForce2 = 0.0;
                            
                            for (j = 0; (j < natoms); j++)
                            {
                                mymol.OptForce2 += iprod(mymol.f_[j], mymol.f_[j]);
                                copy_rvec(mymol.f_[j], mymol.optf_[j]);
                            }

                            mymol.OptForce2 /= natoms;

                            _ener[ermsForce2] += _fc[ermsForce2]*mymol.OptForce2*forceWeight_;
                            mymol.OptEcalc     = mymol.enerd_->term[F_EPOT];
                            
                            ener *= gmx::square(nOptSP-1);
                        }

                        _ener[ermsEPOT]   += _fc[ermsEPOT]*ener;

                        if (nullptr != debug)
                        {
                            fprintf(debug, "spHF: %g  optHF: %g  DeltaEn: %g\n", spHF, optHF, deltaEn);

                            fprintf(debug, "%s Chi2 %g Hform %g Emol %g  Ecalc %g Morse %g  "
                                    "Hangle %g Langle %g PDIHS %g IDIHS %g Coul %g LJ %g BHAM %g Force2 %g\n",
                                    mymol.molProp()->getMolname().c_str(), ener, mymol.Hform, Emol, mymol.Ecalc,
                                    mymol.enerd_->term[F_MORSE], mymol.enerd_->term[F_UREY_BRADLEY],
                                    mymol.enerd_->term[F_LINEAR_ANGLES], mymol.enerd_->term[F_PDIHS],
                                    mymol.enerd_->term[F_IDIHS], mymol.enerd_->term[F_COUL_SR], mymol.enerd_->term[F_LJ],
                                    mymol.enerd_->term[F_BHAM], mymol.Force2);
                        }
                    }
                }
                _ener[ermsEPOT] /= nOptSP;
            }
        }
        else
        {
            gmx_fatal(FARGS, "There is no optimized structure for %s\n",
                      mymol.molProp()->getMolname().c_str());
        }
    }

    /* Compute E-bounds */
    for (size_t j = 0; (j < param_.size()); j++)
    {
        if (param_[j] < lower_[j])
        {
            _ener[ermsBOUNDS] += _fc[ermsBOUNDS]*gmx::square(param_[j]-lower_[j]);
        }
        else if (param_[j] > upper_[j])
        {
            _ener[ermsBOUNDS] += _fc[ermsBOUNDS]*gmx::square(param_[j]-upper_[j]);
        }
    }

    for (j = 0; (j < ermsTOT); j++)
    {
        _ener[ermsTOT] += (_ener[j]/_nmol_support);
    }

    if (debug)
    {
        fprintf(debug, "ENER:");
        for (j = 0; (j < ermsNR); j++)
        {
            fprintf(debug, "  %8.3f", _ener[j]);
        }
        fprintf(debug, "\n");
    }
    /* Global sum energies */
    if (PAR(_cr))
    {
#if GMX_DOUBLE
        gmx_sumd(ermsNR, _ener, _cr);
#else
        gmx_sumf(ermsNR, _ener, _cr);
#endif
    }
    return _ener[ermsTOT];
}

double Optimization::objFunction(const double v[])
{
    double rms = 0;
    for (size_t i = 0; (i < param_.size()); i++)
    {
        param_[i] = v[i];
    }
    tuneFc2PolData(); /* Copy parameters to topologies */
    rms = calcDeviation();

    return rms;
}

void Optimization::optRun(FILE *fp, FILE *fplog, int maxiter,
                          int nrun, real stepsize, int seed,
                          const gmx_output_env_t *oenv,
                          int nprint, const char *xvgconv, 
                          const char *xvgepot, real temperature, 
                          bool bBound)
{

    std::vector<double>              optx, opts, optm;
    double                           chi2, chi2_min;
    gmx_bool                         bMinimum = false;
    std::random_device               rd;
    std::mt19937                     gen;
    std::uniform_real_distribution<> dis;

    auto func = [&] (const double v[]) {
            return objFunction(v);
        };

    if (MASTER(_cr))
    {
        chi2 = chi2_min  = GMX_REAL_MAX;
        Bayes <double> TuneFc(func, param_, lower_, upper_, &chi2);
        TuneFc.Init(xvgconv, xvgepot, oenv, seed,
                    stepsize, maxiter, nprint,
                    temperature, bBound);
        for (int n = 0; (n < nrun); n++)
        {
            if ((nullptr != fp) && (0 == n))
            {
                fprintf(fp, "\nStarting run %d out of %d\n", n, nrun);
            }

            TuneFc.simulate();
            TuneFc.getBestParam(optx);
            TuneFc.getPsigma(opts);
            TuneFc.getPmean(optm);

            if (chi2 < chi2_min)
            {
                bMinimum = true;
                for (size_t k = 0; (k < param_.size()); k++)
                {
                    best_[k]   = optx[k];
                    psigma_[k] = opts[k];
                    pmean_[k]  = optm[k];
                }
                chi2_min = chi2;
            }

            if (nullptr != fp)
            {
                fprintf(fp, "Run: %3d  rmsd: %4.3f   ermsBOUNDS: %4.3f\n",
                        n, sqrt(chi2), _ener[ermsBOUNDS]);
            }
            if (nullptr != fplog)
            {
                fprintf(fplog, "Run: %3d  rmsd: %4.3f   ermsBOUNDS: %4.3f\n",
                        n, sqrt(chi2), _ener[ermsBOUNDS]);
                fflush(fplog);
            }

            TuneFc.setParam(best_);
        }

        if (bMinimum)
        {
            param_ = best_;
            double emin = objFunction(best_.data());
            if (fplog)
            {
                fprintf(fplog, "\nMinimum rmsd value during optimization: %.3f.\n",
                        sqrt(emin));
                fprintf(fplog, "Average and standard deviation of parameters\n");
                for (size_t k = 0; (k < param_.size()); k++)
                {
                    fprintf(fplog, "%5d  %10g  %10g\n",
                            static_cast<int>(k), pmean_[k], psigma_[k]);
                }
            }
        }
        calcDeviation();
        _bDone = true;
    }
    else
    {
        /* Slave calculators */
        do
        {
            calcDeviation();
        }
        while (!_bDone);
    }
    calcDeviation();
}

static void print_moldip_mols(FILE *fp, std::vector<alexandria::MyMol> mol,
                              gmx_bool bForce, gmx_bool bMtop)
{
    int j, k;

    for (auto mi = mol.begin(); (mi < mol.end()); mi++)
    {
        fprintf(fp, "%-30s  %d\n", mi->molProp()->getMolname().c_str(), mi->molProp()->NAtom());
        for (j = 0; (j < mi->molProp()->NAtom()); j++)
        {
            fprintf(fp, "  %-5s  %-5s  q = %10g", *(mi->topology_->atoms.atomname[j]),
                    *(mi->topology_->atoms.atomtype[j]), mi->topology_->atoms.atom[j].q);
            if (bForce)
            {
                fprintf(fp, "   f = %8.3f  %8.3f  %8.3f",
                        mi->optf_[j][XX],
                        mi->optf_[j][YY],
                        mi->optf_[j][ZZ]);
            }
            fprintf(fp, "\n");
        }
        if (bForce)
        {
            for (k = 0; (k < F_NRE); k++)
            {
                if ((mi->enerd_->term[k] != 0) ||
                    (mi->mtop_->moltype[0].ilist[k].nr > 0))
                {
                    fprintf(fp, "%s %d %g\n", interaction_function[k].name,
                            mi->mtop_->moltype[0].ilist[k].nr,
                            mi->enerd_->term[k]);
                }
            }
        }
        if (bMtop)
        {
            pr_mtop(fp, 0, mi->molProp()->getMolname().c_str(), mi->mtop_, true);
        }
    }
}


void Optimization::printSpecs(FILE *fp, char *title,
                              const char *xvg,
                              const gmx_output_env_t *oenv,
                              bool bCheckOutliers)
{
    int         N, i;
    real        a, b;
    real        da, db;
    real        chi2, Rfit;
    double      msd;
    FILE       *xfp;
    gmx_stats_t gst;

    gst = gmx_stats_init();
    
    if (nullptr != xvg)
    {
        xfp = xvgropen(xvg, "Entalpy of Formation", "Experiment (kJ/mol)", "Calculated (kJ/mol)",
                       oenv);
    }
    
    fprintf(fp, "%s\n", title);
    
    fprintf(fp, "Nr.   %-30s %10s %10s %10s %10s %10s\n",
            "Molecule", "DHf@298K", "Emol@0K", "Delta E", "rms F", "Outlier?");
    msd = 0;
    i   = 0;
    
    for (auto mi = _mymol.begin(); (mi < _mymol.end()); mi++, i++)
    {
        real DeltaE = mi->OptEcalc - mi->Emol;

        fprintf(fp, "%-5d %-30s %10g %10g %10g %10g %-10s\n",
                i, mi->molProp()->getMolname().c_str(),
                mi->Hform, mi->Emol, DeltaE,
                sqrt(mi->OptForce2), (bCheckOutliers && (fabs(DeltaE) > 1000)) ? "XXX" : "");

        msd += gmx::square(DeltaE);
        gmx_stats_add_point(gst, mi->Hform, mi->Hform + DeltaE, 0, 0);

        if (nullptr != xvg)
        {
            fprintf(xfp, "%10g  %10g\n", mi->Hform, mi->Hform + DeltaE);
        }
    }
    
    fprintf(fp, "\n");
    fprintf(fp, "RMSD is %g kJ/mol for %d molecules.\n\n",
            sqrt(msd/_mymol.size()), static_cast<int>(_mymol.size()));
    fflush(fp);
    if (nullptr != xvg)
    {
        xvgrclose(xfp);
        do_view(oenv, xvg, nullptr);
    }
    
    gmx_stats_get_ab(gst, 1, &a, &b, &da, &db, &chi2, &Rfit);
    gmx_stats_get_npoints(gst, &N);
    
    fprintf(fp, "Regression analysis fit to y = ax + b:\n");
    fprintf(fp, "a = %.3f  b = %3f  R2 = %.1f%%  chi2 = %.1f N = %d\n",
            a, b, Rfit*100, chi2, N);
            
    gmx_stats_free(gst);
    fflush(fp);
}
}

int alex_tune_fc(int argc, char *argv[])
{
    static const char    *desc[] = {
        "tune_fc read a series of molecules and corresponding experimental",
        "heats of formation from a file, and tunes parameters in an algorithm",
        "until the experimental energies are reproduced by the force field.[PAR]",
        "Minima and maxima for the parameters can be set, these are however",
        "not strictly enforced, but rather they are penalized with a harmonic",
        "function, for which the force constant can be set explicitly.[PAR]",
        "At every reinit step parameters are changed by a random amount within",
        "the fraction set by step size, and within the boundaries given",
        "by the minima and maxima. If the [TT]-random[tt] flag is",
        "given a completely random set of parameters is generated at the start",
        "of each run. At reinit steps however, the parameters are only changed",
        "slightly, in order to speed-up local search but not global search."
        "In other words, complete random starts are done only at the beginning of each",
        "run, and only when explicitly requested.[PAR]",
        "The absolut dipole moment of a molecule remains unchanged if all the",
        "atoms swap the sign of the charge. To prevent this kind of mirror",
        "effects a penalty is added to the square deviation ",
        "if hydrogen atoms have a negative charge. Similarly a penalty is",
        "added if atoms from row VI or VII in the periodic table have a positive",
        "charge. The penalty is equal to the force constant given on the command line",
        "time the square of the charge.[PAR]",
        "One of the electronegativities (chi) is redundant in the optimization,",
        "only the relative values are meaningful.",
        "Therefore by default we fix the value for hydrogen to what is written",
        "in the eemprops.dat file (or whatever is given with the [tt]-d[TT] flag).",
        "A suitable value would be 2.3, the original, value due to Pauling,",
        "this can by overridden by setting the [tt]-fixchi[TT] flag to something else (e.g. a non-existing atom).[PAR]",
        "A selection of molecules into a training set and a test set (or ignore set)",
        "can be made using option [TT]-sel[tt]. The format of this file is:[BR]",
        "iupac|Train[BR]",
        "iupac|Test[BR]",
        "iupac|Ignore[BR]",
        "and you should ideally have a line for each molecule in the molecule database",
        "([TT]-f[tt] option). Missing molecules will be ignored."
    };

    t_filenm              fnm[] = {
        { efDAT, "-f",     "allmols",    ffREAD  },
        { efDAT, "-d",     "gentop",     ffOPTRD },
        { efDAT, "-o",     "tune_fc",    ffWRITE },
        { efDAT, "-sel",   "molselect",  ffREAD  },
        { efXVG, "-table", "table",      ffOPTRD },
        { efLOG, "-g",     "tune_fc",    ffWRITE },
        { efXVG, "-x",     "hform-corr", ffWRITE },
        { efXVG, "-conv",  "param-conv", ffWRITE },
        { efXVG, "-epot",  "param-epot", ffWRITE }
    };
    
#define NFILE sizeof(fnm)/sizeof(fnm[0])

    static int            nrun          = 1;
    static int            maxiter       = 100; 
    static int            reinit        = 0;
    static int            seed          = 0;
    static int            minimum_data  = 3;
    static int            compress      = 0;
    static int            nprint        = 10;
    static real           tol           = 1e-3;
    static real           stol          = 1e-6;
    static real           watoms        = 0;   
    static real           J0_0          = 5;
    static real           Chi0_0        = 1;
    static real           w_0           = 5;
    static real           step          = 0.01;
    static real           hfac          = 0.0;
    static real           rDecrZeta     = -1;
    static real           J0_1          = 30;
    static real           Chi0_1        = 30;
    static real           w_1           = 50;
    static real           fc_mu         = 1;
    static real           fc_bound      = 1;
    static real           fc_quad       = 1;
    static real           fc_charge     = 0;
    static real           fc_esp        = 0; 
    static real           fc_epot       = 1;
    static real           fc_force      = 0.001;
    static real           fweight       = 0.001;
    static real           factor        = 0.8;
    static real           beta0         = 0;
    static real           D0            = 0; 
    static real           beta_min      = 10; 
    static real           D0_min        = 50; 
    static real           temperature   = 300;
    static char          *opt_elem      = nullptr;
    static char          *const_elem    = nullptr;
    static char          *fixchi        = (char *)"H";
    static char          *lot           = (char *)"B3LYP/aug-cc-pVTZ";    
    static gmx_bool       bBound        = false;
    static gmx_bool       bWeighted     = false;
    static gmx_bool       bOptHfac      = false;
    static gmx_bool       bQM           = false; 
    static gmx_bool       bPolar        = false;
    static gmx_bool       bFitZeta      = false;
    static gmx_bool       bZPE          = false;
    static gmx_bool       bGaussianBug  = true;
    static gmx_bool       bZero         = true;  
    static const char    *cqdist[]      = {nullptr, "AXp", "AXg", "AXs","Yang", "Bultinck", "Rappe", nullptr};
    static const char    *cqgen[]       = {nullptr, "None", "EEM", "ESP", "RESP", nullptr};
    static bool           bOpt[eitNR]   = { true, false, false, false, false, false, false, false, false};
      
    t_pargs               pa[]         = {
        { "-tol",     FALSE, etREAL, {&tol},
          "Tolerance for convergence in optimization" },
        { "-maxiter", FALSE, etINT, {&maxiter},
          "Max number of iterations for optimization" },
        { "-nprint",  FALSE, etINT, {&nprint},
          "How often to print the parameters during the simulation" },
        { "-temp",    FALSE, etREAL, {&temperature},
          "'Temperature' for the Monte Carlo simulation" },
        { "-reinit",  FALSE, etINT, {&reinit},
          "After this many iterations the search vectors are randomized again. A vlue of 0 means this is never done at all." },
        { "-stol",    FALSE, etREAL, {&stol},
          "If reinit is -1 then a reinit will be done as soon as the simplex size is below this treshold." },
        { "-nrun",    FALSE, etINT,  {&nrun},
          "This many runs will be done, before each run a complete randomization will be done" },
        { "-qdist",   FALSE, etENUM, {cqdist},
          "Model used for charge distribution" },
        { "-qgen",    FALSE, etENUM, {cqgen},
          "Algorithm used for charge generation" },
        { "-bonds",   FALSE, etBOOL, {&bOpt[eitBONDS]},
          "Optimize bond parameters" },
        { "-angles",  FALSE, etBOOL, {&bOpt[eitANGLES]},
          "Optimize angle parameters" },
        { "-langles", FALSE, etBOOL, {&bOpt[eitLINEAR_ANGLES]},
          "Optimize linear angle parameters" },
        { "-dihedrals", FALSE, etBOOL, {&bOpt[eitPROPER_DIHEDRALS]},
          "Optimize proper dihedral parameters" },
        { "-impropers", FALSE, etBOOL, {&bOpt[eitIMPROPER_DIHEDRALS]},
          "Optimize improper dihedral parameters" },
        { "-pairs",  FALSE, etBOOL, {&bOpt[eitLJ14]},
          "Optimize 1-4 interaction parameters" },
        { "-beta0", FALSE, etREAL, {&beta0},
          "Reset the initial beta for Morse potentials to this value, independent of gentop.dat. If value is <= 0 gentop.dat value is used." },
        { "-D0", FALSE, etREAL, {&D0},
          "Reset the initial D for Morse potentials to this value, independent of gentop.dat. If value is <= 0 gentop.dat value is used." },
        { "-beta_min", FALSE, etREAL, {&beta_min},
          "Minimum value for beta in Morse potential" },
        { "-DO_min", FALSE, etREAL, {&D0_min},
          "Minimum value for D0 in Morse potential" },
        { "-qm",     FALSE, etBOOL, {&bQM},
          "Use only quantum chemistry results (from the levels of theory below) in order to fit the parameters. If not set, experimental values will be used as reference with optional quantum chemistry results, in case no experimental results are available" },
        { "-zpe",     FALSE, etBOOL, {&bZPE},
          "Consider zero-point energy from thermochemistry calculations in order to calculate the reference enthalpy of the molecule" },
        { "-lot",    FALSE, etSTR,  {&lot},
          "Use this method and level of theory when selecting coordinates and charges. Multiple levels can be specified which will be used in the order given, e.g.  B3LYP/aug-cc-pVTZ:HF/6-311G**" },
        { "-fc_bound",    FALSE, etREAL, {&fc_bound},
          "Force constant in the penalty function for going outside the borders given with the above six options." },
        { "-fc_epot",    FALSE, etREAL, {&fc_epot},
          "Force constant in the penalty function for the energy term" },
        { "-fc_force",    FALSE, etREAL, {&fc_force},
          "Force constant in the penalty function for the force term" },         
        { "-fweight",    FALSE, etREAL, {&fweight},
          "Force weight in chi squared calculation" },         
        { "-step",  FALSE, etREAL, {&step},
          "Step size in parameter optimization. Is used as a fraction of the starting value, should be less than 10%. At each reinit step the step size is updated." },
        { "-min_data",  FALSE, etINT, {&minimum_data},
          "Minimum number of data points in order to be able to optimize the parameters for a given atomtype" },
        { "-opt_elem",  FALSE, etSTR, {&opt_elem},
          "Space-separated list of elements to optimize, e.g. \"H C Br\". The other available elements in gentop.dat are left unmodified. If this variable is not set, all elements will be optimized." },
        { "-const_elem",  FALSE, etSTR, {&const_elem},
          "Space-separated list of elements to include but keep constant, e.g. \"O N\". These elements from gentop.dat are left unmodified" },
        { "-seed", FALSE, etINT, {&seed},
          "Random number seed for reinit" },
        { "-factor", FALSE, etREAL, {&factor},
          "Factor for generating random parameters. Parameters will be taken within the limit factor*x - x/factor" },
        { "-bound", FALSE, etBOOL, {&bBound},
          "Impose box-constrains for the optimization. Box constraints give lower and upper bounds for each parameter seperately." },
        { "-weight", FALSE, etBOOL, {&bWeighted},
          "Perform a weighted fit, by using the errors in the dipoles presented in the input file. This may or may not improve convergence." },
        { "-compress", FALSE, etBOOL, {&compress},
          "Compress output XML file" }
    };
    
    FILE                 *fp;
    t_commrec            *cr;
    gmx_output_env_t     *oenv;
    time_t                my_t;

    cr = init_commrec();
    if (MASTER(cr))
    {
        printf("There are %d threads/processes.\n", cr->nnodes);
    }
    if (!parse_common_args(&argc, argv, PCA_CAN_VIEW,
                           NFILE, fnm,
                           sizeof(pa)/sizeof(pa[0]), pa,
                           sizeof(desc)/sizeof(desc[0]), desc,
                           0, nullptr, &oenv))
    {
        return 0;
    }

    if (MASTER(cr))
    {
        fp = gmx_ffopen(opt2fn("-g", NFILE, fnm), "w");

        time(&my_t);
        fprintf(fp, "# This file was created %s", ctime(&my_t));
        fprintf(fp, "# alexandria is part of G R O M A C S:\n#\n");
        fprintf(fp, "# %s\n#\n", gmx::bromacs().c_str());
    }
    else
    {
        fp = nullptr;
    }

    MolSelect gms;
    if (MASTER(cr))
    {
        gms.read(opt2fn_null("-sel", NFILE, fnm));
    }

    alexandria::Optimization       opt(fweight);
    ChargeDistributionModel        iChargeDistributionModel   = name2eemtype(cqdist[0]);
    ChargeGenerationAlgorithm      iChargeGenerationAlgorithm = (ChargeGenerationAlgorithm) get_option(cqgen);

    const char                    *tabfn = opt2fn_null("-table", NFILE, fnm);

    if (iChargeDistributionModel != eqdAXp && nullptr == tabfn)
    {
        gmx_fatal(FARGS, "Cannot generate charges with the %s charge model without a potential table. "
                  "Please supply a table file.", getEemtypeName(iChargeDistributionModel));
    }

    opt.Init(cr, bQM, bGaussianBug, iChargeDistributionModel,
             iChargeGenerationAlgorithm, rDecrZeta,
             J0_0, Chi0_0, w_0, J0_1, Chi0_1, w_1,
             fc_bound, fc_mu, fc_quad, fc_charge,
             fc_esp, fc_epot, fc_force, fixchi,
             bOptHfac, hfac, bPolar, bFitZeta);

    opt.Read(fp ? fp : (debug ? debug : nullptr),
             opt2fn("-f", NFILE, fnm),
             opt2fn_null("-d", NFILE, fnm),
             minimum_data, bZero,
             opt_elem, const_elem,
             lot, gms, watoms, false,
             bOpt[eitLJ14], bOpt[eitPROPER_DIHEDRALS],
             bPolar, bZPE, tabfn);

    opt.checkSupport(fp, bOpt);

    fprintf(fp, "In the total data set of %d molecules we have:\n",
            static_cast<int>(opt._mymol.size()));


    opt.InitOpt(fp, bOpt, factor);

    print_moldip_mols(fp, opt._mymol, false, false);

    opt.calcDeviation();

    if (MASTER(cr))
    {
        opt.printSpecs(fp, (char *)"Before optimization", nullptr, oenv, false);
    }

    opt.optRun(MASTER(cr) ? stderr : nullptr, fp,
               maxiter, nrun, step, seed,
               oenv, nprint,
               opt2fn("-conv", NFILE, fnm),
               opt2fn("-epot", NFILE, fnm),
               temperature, bBound);

    if (MASTER(cr))
    {
        print_moldip_mols(fp, opt._mymol, true, false);

        opt.printSpecs(fp, (char *)"After optimization", opt2fn("-x", NFILE, fnm), oenv, true);

        writePoldata(opt2fn("-o", NFILE, fnm), opt.pd_, compress);

        done_filenms(NFILE, fnm);

        gmx_ffclose(fp);
    }

    return 0;
}
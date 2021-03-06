/*
 * MicroHH
 * Copyright (c) 2011-2015 Chiel van Heerwaarden
 * Copyright (c) 2011-2015 Thijs Heus
 * Copyright (c) 2014-2015 Bart van Stratum
 *
 * This file is part of MicroHH
 *
 * MicroHH is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * MicroHH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with MicroHH.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <cmath>
#include <algorithm>
#include "grid.h"
#include "fields.h"
#include "advec_2.h"
#include "defines.h"
#include "constants.h"
#include "finite_difference.h"
#include "model.h"

using namespace Finite_difference::O2;

Advec_2::Advec_2(Model* modelin, Input* inputin) : Advec(modelin, inputin)
{
    swadvec = "2";
}

Advec_2::~Advec_2()
{
}

#ifndef USECUDA
double Advec_2::get_cfl(double dt)
{
    return calc_cfl(fields->u->data, fields->v->data, fields->w->data, grid->dzi, dt);
}

unsigned long Advec_2::get_time_limit(unsigned long idt, double dt)
{
    // Calculate cfl and prevent zero divisons.
    double cfl = calc_cfl(fields->u->data, fields->v->data, fields->w->data, grid->dzi, dt);
    cfl = std::max(cflmin, cfl);
    return idt * cflmax / cfl;
}

void Advec_2::exec()
{
    advec_u(fields->ut->data, fields->u->data, fields->v->data, fields->w->data, grid->dzi,
            fields->rhoref, fields->rhorefh);
    advec_v(fields->vt->data, fields->u->data, fields->v->data, fields->w->data, grid->dzi,
            fields->rhoref, fields->rhorefh);
    advec_w(fields->wt->data, fields->u->data, fields->v->data, fields->w->data, grid->dzhi,
            fields->rhoref, fields->rhorefh);

    for (FieldMap::const_iterator it = fields->st.begin(); it!=fields->st.end(); it++)
        advec_s(it->second->data, fields->sp[it->first]->data, fields->u->data, fields->v->data, fields->w->data,
                grid->dzi, fields->rhoref, fields->rhorefh);
}
#endif

double Advec_2::calc_cfl(double* restrict u, double* restrict v, double* restrict w, double* restrict dzi, double dt)
{
    const int ii = 1;
    const int jj = grid->icells;
    const int kk = grid->ijcells;

    const double dxi = 1./grid->dx;
    const double dyi = 1./grid->dy;

    double cfl = 0;

    for (int k=grid->kstart; k<grid->kend; ++k)
        for (int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
            for (int i=grid->istart; i<grid->iend; ++i)
            {
                const int ijk = i + j*jj + k*kk;
                cfl = std::max(cfl, std::abs(interp2(u[ijk], u[ijk+ii]))*dxi + std::abs(interp2(v[ijk], v[ijk+jj]))*dyi + std::abs(interp2(w[ijk], w[ijk+kk]))*dzi[k]);
            }

    grid->get_max(&cfl);

    cfl = cfl*dt;

    return cfl;
}

void Advec_2::advec_u(double* restrict ut, double* restrict u, double* restrict v, double* restrict w,
                      double* restrict dzi, double* restrict rhoref, double* restrict rhorefh)
{
    const int ii = 1;
    const int jj = grid->icells;
    const int kk = grid->ijcells;

    const double dxi = 1./grid->dx;
    const double dyi = 1./grid->dy;

    for (int k=grid->kstart; k<grid->kend; ++k)
        for (int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
            for (int i=grid->istart; i<grid->iend; ++i)
            {
                const int ijk = i + j*jj + k*kk;
                ut[ijk] +=
                         - ( interp2(u[ijk   ], u[ijk+ii]) * interp2(u[ijk   ], u[ijk+ii])
                           - interp2(u[ijk-ii], u[ijk   ]) * interp2(u[ijk-ii], u[ijk   ]) ) * dxi

                         - ( interp2(v[ijk-ii+jj], v[ijk+jj]) * interp2(u[ijk   ], u[ijk+jj])
                           - interp2(v[ijk-ii   ], v[ijk   ]) * interp2(u[ijk-jj], u[ijk   ]) ) * dyi

                         - ( rhorefh[k+1] * interp2(w[ijk-ii+kk], w[ijk+kk]) * interp2(u[ijk   ], u[ijk+kk])
                           - rhorefh[k  ] * interp2(w[ijk-ii   ], w[ijk   ]) * interp2(u[ijk-kk], u[ijk   ]) ) / rhoref[k] * dzi[k];
            }
}

void Advec_2::advec_v(double* restrict vt, double* restrict u, double* restrict v, double* restrict w,
                      double* restrict dzi, double* restrict rhoref, double* restrict rhorefh)
{
    const int ii = 1;
    const int jj = grid->icells;
    const int kk = grid->ijcells;

    const double dxi = 1./grid->dx;
    const double dyi = 1./grid->dy;

    for (int k=grid->kstart; k<grid->kend; ++k)
        for (int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
            for (int i=grid->istart; i<grid->iend; ++i)
            {
                const int ijk = i + j*jj + k*kk;
                vt[ijk] +=
                         - ( interp2(u[ijk+ii-jj], u[ijk+ii]) * interp2(v[ijk   ], v[ijk+ii])
                           - interp2(u[ijk   -jj], u[ijk   ]) * interp2(v[ijk-ii], v[ijk   ]) ) * dxi

                         - ( interp2(v[ijk   ], v[ijk+jj]) * interp2(v[ijk   ], v[ijk+jj])
                           - interp2(v[ijk-jj], v[ijk   ]) * interp2(v[ijk-jj], v[ijk   ]) ) * dyi

                         - ( rhorefh[k+1] * interp2(w[ijk-jj+kk], w[ijk+kk]) * interp2(v[ijk   ], v[ijk+kk])
                           - rhorefh[k  ] * interp2(w[ijk-jj   ], w[ijk   ]) * interp2(v[ijk-kk], v[ijk   ]) ) / rhoref[k] * dzi[k];
            }
}

void Advec_2::advec_w(double* restrict wt, double* restrict u, double* restrict v, double* restrict w,
                      double* restrict dzhi, double* restrict rhoref, double* restrict rhorefh)
{
    const int ii = 1;
    const int jj = grid->icells;
    const int kk = grid->ijcells;

    const double dxi = 1./grid->dx;
    const double dyi = 1./grid->dy;

    for (int k=grid->kstart+1; k<grid->kend; ++k)
        for (int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
            for (int i=grid->istart; i<grid->iend; ++i)
            {
                const int ijk = i + j*jj + k*kk;
                wt[ijk] +=
                         - ( interp2(u[ijk+ii-kk], u[ijk+ii]) * interp2(w[ijk   ], w[ijk+ii])
                           - interp2(u[ijk   -kk], u[ijk   ]) * interp2(w[ijk-ii], w[ijk   ]) ) * dxi

                         - ( interp2(v[ijk+jj-kk], v[ijk+jj]) * interp2(w[ijk   ], w[ijk+jj])
                           - interp2(v[ijk   -kk], v[ijk   ]) * interp2(w[ijk-jj], w[ijk   ]) ) * dyi

                         - ( rhoref[k  ] * interp2(w[ijk   ], w[ijk+kk]) * interp2(w[ijk   ], w[ijk+kk])
                           - rhoref[k-1] * interp2(w[ijk-kk], w[ijk   ]) * interp2(w[ijk-kk], w[ijk   ]) ) / rhorefh[k] * dzhi[k];
            }
}

void Advec_2::advec_s(double* restrict st, double* restrict s, double* restrict u, double* restrict v, double* restrict w,
                      double* restrict dzi, double* restrict rhoref, double* restrict rhorefh)
{
    const int ii = 1;
    const int jj = grid->icells;
    const int kk = grid->ijcells;

    const double dxi = 1./grid->dx;
    const double dyi = 1./grid->dy;

    for (int k=grid->kstart; k<grid->kend; ++k)
        for (int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
            for (int i=grid->istart; i<grid->iend; ++i)
            {
                const int ijk = i + j*jj + k*kk;
                st[ijk] +=
                         - ( u[ijk+ii] * interp2(s[ijk   ], s[ijk+ii])
                           - u[ijk   ] * interp2(s[ijk-ii], s[ijk   ]) ) * dxi

                         - ( v[ijk+jj] * interp2(s[ijk   ], s[ijk+jj])
                           - v[ijk   ] * interp2(s[ijk-jj], s[ijk   ]) ) * dyi

                         - ( rhorefh[k+1] * w[ijk+kk] * interp2(s[ijk   ], s[ijk+kk])
                           - rhorefh[k  ] * w[ijk   ] * interp2(s[ijk-kk], s[ijk   ]) ) / rhoref[k] * dzi[k];
            }
}

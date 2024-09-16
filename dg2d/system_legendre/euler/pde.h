//------------------------------------------------------------------------------
// Euler equations for compressible flows
//------------------------------------------------------------------------------
#ifndef __PDE_H__
#define __PDE_H__

#include <deal.II/numerics/data_postprocessor.h>

using namespace dealii;

const unsigned int nvar = 4;

// Numerical flux functions
enum class FluxType {rusanov, none};

std::map<std::string, FluxType> FluxTypeList{{"rusanov", FluxType::rusanov},
                                             {"none",    FluxType::none}};

//------------------------------------------------------------------------------
// This should be set by user in a problem.h file
//------------------------------------------------------------------------------
namespace ProblemData
{
   extern const double gamma;
}

//------------------------------------------------------------------------------
//                 | rho       |  0
// u = conserved = | rho * vel |  1,...,dim
//                 |    E      |  dim+1
//
//                 | rho |  0
// q = primitive = | vel |  1,...,dim
//                 | pre |  dim+1
//------------------------------------------------------------------------------
namespace PDE
{

   const std::string name = "2D Euler equations";
   const double gamma = ProblemData::gamma;

   //---------------------------------------------------------------------------
   template <int dim>
   inline void
   con2prim(const Vector<double>& u,
            double&               rho,
            Tensor<1,dim>&        vel,
            double&               pre)
   {
      rho = u[0];

      double v2 = 0.0;
      for(unsigned int d = 0; d < dim; ++d)
      {
         vel[d] = u[d + 1] / rho;
         v2 += pow(vel[d], 2);
      }

      const double E = u[dim + 1];
      pre = (gamma - 1.0) * (E - 0.5 * rho * v2);
   }

   //---------------------------------------------------------------------------
   template <int dim>
   inline void
   con2prim(const Vector<double>& u, Vector<double>& q)
   {
      // density
      q[0] = u[0];

      // velocity
      double v2 = 0.0;
      for(unsigned int d = 1; d <= dim; ++d)
      {
         q[d] = u[d] / u[0];
         v2 += pow(q[d], 2);
      }

      // pressure
      q[dim+1] = (gamma - 1.0) * (u[dim+1] - 0.5 * u[0] * v2);
   }

   //---------------------------------------------------------------------------
   template <int dim>
   inline void
   prim2prim(const Vector<double>& q,
             double&               rho,
             Tensor<1,dim>&        vel,
             double&               pre)
   {
      rho = q[0];
      pre = q[dim+1];

      for(unsigned int d=0; d<dim; ++d)
         vel[d] = q[d+1];
   }

   //---------------------------------------------------------------------------
   template <int dim>
   void
   physical_flux(const Vector<double>& q,
                 const Tensor<1, dim>& normal,
                 Vector<double>&       flux)
   {
      double vn = 0.0, v2 = 0.0;
      for(unsigned int d = 0; d < dim; ++d)
      {
         vn += q[d+1] * normal[d];
         v2 += pow(q[d+1], 2);
      }

      flux[0] = q[0] * vn;
      for(unsigned int d = 0; d < dim; ++d)
         flux[d+1] = q[dim+1] * normal[d] + q[0] * q[d+1] * vn;

      const double E = q[dim+1] / (gamma - 1.0) + 0.5 * q[0] * v2;
      flux[dim + 1] = (E + q[dim+1]) * vn;
   }

   //---------------------------------------------------------------------------
   template <int dim>
   inline double
   max_speed(const Vector<double>&  q,
             const Tensor<1, dim>&  normal)
   {
      double vn = 0.0;
      for(unsigned int d = 0; d < dim; ++d)
         vn += q[d + 1] * normal[d];

      return abs(vn) + sqrt(gamma * q[dim + 1] / q[0]);
   }

   //---------------------------------------------------------------------------
   template <int dim>
   void
   rusanov_flux(const Vector<double>&  ul,
                const Vector<double>&  ur,
                const Tensor<1, dim>&  normal,
                Vector<double>&        flux)
   {
      Vector<double> ql(nvar), qr(nvar);
      con2prim<dim>(ul, ql);
      con2prim<dim>(ur, qr);

      Vector<double> fl(nvar), fr(nvar);
      physical_flux(ql, normal, fl);
      physical_flux(qr, normal, fr);

      const double al = max_speed(ql, normal);
      const double ar = max_speed(qr, normal);
      const double lam = std::max(al, ar);

      for(unsigned int i = 0; i < nvar; ++i)
         flux[i] = 0.5 * (fl[i] + fr[i] - lam * (ur[i] - ul[i]));
   }

   //---------------------------------------------------------------------------
   // Following functions are directly called from DG solver
   //---------------------------------------------------------------------------
   template <int dim>
   void
   max_speed(const Vector<double>& u,
             const Point<dim>&     /*p*/,
             Tensor<1, dim>&       speed)
   {
      double rho, pre;
      Tensor<1,dim> vel;
      con2prim<dim>(u, rho, vel, pre);
      const double c = sqrt(gamma * pre / rho);

      for(unsigned int d = 0; d < dim; ++d)
         speed[d] = abs(vel[d]) + c;
   }

   //---------------------------------------------------------------------------
   // Flux of the PDE model: f(u,x)
   //---------------------------------------------------------------------------
   template <int dim>
   void
   physical_flux(const Vector<double>&       u,
                 const Point<dim>&           /*p*/,
                 ndarray<double, nvar, dim>& flux)
   {
      double rho, pre;
      Tensor<1,dim> vel;
      con2prim<dim>(u, rho, vel, pre);

      const double E = u[dim + 1];

      for(unsigned int d = 0; d < dim; ++d)
      {
         flux[0][d] = u[d+1]; // mass flux

         // momentum flux
         for(unsigned int e=1; e<=dim; ++e)
            flux[e][d] = u[e] * vel[d];

         flux[d+1][d] += pre;

         // energy flux
         flux[dim + 1][d] = (E + pre) * vel[d];
      }
   }

   //---------------------------------------------------------------------------
   // Compute flux across cell faces
   //---------------------------------------------------------------------------
   template <int dim>
   void
   numerical_flux(const FluxType        flux_type,
                  const Vector<double>& ul,
                  const Vector<double>& ur,
                  const Point<dim>&     /*p*/,
                  const Tensor<1, dim>& normal,
                  Vector<double>&       flux)
   {
      switch(flux_type)
      {
         case FluxType::rusanov:
            rusanov_flux(ul, ur, normal, flux);
            break;

         default:
            AssertThrow(false, ExcMessage("Unknown numerical flux"));
      }
   }

   //---------------------------------------------------------------------------
   template <int dim>
   void
   boundary_flux(const Vector<double>& /*ul*/,
                 const Vector<double>& /*ur*/,
                 const Point<dim>&     /*p*/,
                 const Tensor<1, dim>& /*normal*/,
                 Vector<double>&       /*flux*/)
   {
      AssertThrow(false, ExcNotImplemented());
   }

   //---------------------------------------------------------------------------
   void
   char_mat(const Vector<double>& sol,
            const Point<2>&       /*p*/,
            const Tensor<1, 2>&   ex,
            const Tensor<1, 2>&   ey,
            FullMatrix<double>&   Rx,
            FullMatrix<double>&   Lx,
            FullMatrix<double>&   Ry,
            FullMatrix<double>&   Ly)
   {
      double rho, pre;
      Tensor<1,2> vel;
      con2prim(sol, rho, vel, pre);

      const double u = vel * ex;
      const double v = vel * ey;

      const double g1 = gamma - 1.0;
      const double q2 = u * u + v * v;
      const double c2 = gamma * pre / rho;
      const double c = std::sqrt(c2);
      const double beta = 0.5 / c2;
      const double phi2 = 0.5 * g1 * q2;
      const double h = c2 / g1 + 0.5 * q2;

      Rx[0][0] = 1;
      Rx[1][0] = u;
      Rx[2][0] = v;
      Rx[3][0] = 0.5 * q2;

      Rx[0][1] = 0;
      Rx[1][1] = 0;
      Rx[2][1] = -1;
      Rx[3][1] = -v;

      Rx[0][2] = 1;
      Rx[1][2] = u + c;
      Rx[2][2] = v;
      Rx[3][2] = h + c * u;

      Rx[0][3] = 1;
      Rx[1][3] = u - c;
      Rx[2][3] = v;
      Rx[3][3] = h - c * u;

      Ry[0][0] = 1;
      Ry[1][0] = u;
      Ry[2][0] = v;
      Ry[3][0] = 0.5 * q2;

      Ry[0][1] = 0;
      Ry[1][1] = 1;
      Ry[2][1] = 0;
      Ry[3][1] = u;

      Ry[0][2] = 1;
      Ry[1][2] = u;
      Ry[2][2] = v + c;
      Ry[3][2] = h + c * v;

      Ry[0][3] = 1;
      Ry[1][3] = u;
      Ry[2][3] = v - c;
      Ry[3][3] = h - c * v;

      Lx[0][0] = 1 - phi2 / c2;
      Lx[1][0] = v;
      Lx[2][0] = beta * (phi2 - c * u);
      Lx[3][0] = beta * (phi2 + c * u);

      Lx[0][1] = g1 * u / c2;
      Lx[1][1] = 0;
      Lx[2][1] = beta * (c - g1 * u);
      Lx[3][1] = -beta * (c + g1 * u);

      Lx[0][2] = g1 * v / c2;
      Lx[1][2] = -1;
      Lx[2][2] = -beta * g1 * v;
      Lx[3][2] = -beta * g1 * v;

      Lx[0][3] = -g1 / c2;
      Lx[1][3] = 0;
      Lx[2][3] = beta * g1;
      Lx[3][3] = beta * g1;

      Ly[0][0] = 1 - phi2 / c2;
      Ly[1][0] = -u;
      Ly[2][0] = beta * (phi2 - c * v);
      Ly[3][0] = beta * (phi2 + c * v);

      Ly[0][1] = g1 * u / c2;
      Ly[1][1] = 1;
      Ly[2][1] = -beta * g1 * u;
      Ly[3][1] = -beta * g1 * u;

      Ly[0][2] = g1 * v / c2;
      Ly[1][2] = 0;
      Ly[2][2] = beta * (c - g1 * v);
      Ly[3][2] = -beta * (c + g1 * v);

      Ly[0][3] = -g1 / c2;
      Ly[1][3] = 0;
      Ly[2][3] = beta * g1;
      Ly[3][3] = beta * g1;
   }

   //---------------------------------------------------------------------------
   void print_info()
   {
      std::cout << "Ratio of specific heats, gamma = " << gamma << std::endl;
   }

   //---------------------------------------------------------------------------
   template <int dim>
   class Postprocessor : public DataPostprocessor<dim>
   {
   public:
      void
      evaluate_vector_field(const DataPostprocessorInputs::Vector<dim> &input_data,
                            std::vector<Vector<double>> &computed_quantities) const override
      {
         const std::vector<Vector<double>> &uh = input_data.solution_values;
         Assert(uh[0].size() == nvar && computed_quantities[0].size() == nvar,
                ExcInternalError());
         for (unsigned int q = 0; q < uh.size(); ++q)
         {
            con2prim<dim>(uh[q], computed_quantities[q]);
         }
      }
      std::vector<std::string> get_names() const override
      {
         if (dim == 2)
            return {"Density", "XVelocity", "YVelocity", "Pressure"};
         else
            return {"Density", "XVelocity", "YVelocity", "ZVelocity", "Pressure"};
      }
      UpdateFlags get_needed_update_flags() const override
      {
         return update_values;
      }
   };

}
#endif

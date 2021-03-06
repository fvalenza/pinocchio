//
// Copyright (c) 2016 CNRS
//
// This file is part of Pinocchio
// Pinocchio is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// Pinocchio is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Lesser Public License for more details. You should have
// received a copy of the GNU Lesser General Public License along with
// Pinocchio If not, see
// <http://www.gnu.org/licenses/>.

#ifndef __se3_aba_hpp__
#define __se3_aba_hpp__

#include "pinocchio/multibody/visitor.hpp"
#include "pinocchio/multibody/model.hpp"

namespace se3
{
  ///
  /// \brief The Articulated-Body algorithm. It computes the forward dynamics, aka the joint accelerations given the current state and actuation of the model.
  ///
  /// \param[in] model The model structure of the rigid body system.
  /// \param[in] data The data structure of the rigid body system.
  /// \param[in] q The joint configuration vector (dim model.nq).
  /// \param[in] v The joint velocity vector (dim model.nv).
  /// \param[in] tau The joint torque vector (dim model.nv).
  ///
  /// \return The current joint acceleration stored in data.ddq.
  ///
  inline const Eigen::VectorXd &
  aba(const Model & model,
      Data & data,
      const Eigen::VectorXd & q,
      const Eigen::VectorXd & v,
      const Eigen::VectorXd & tau);

} // namespace se3

/* --- Details -------------------------------------------------------------------- */
namespace se3
{
  struct AbaForwardStep1 : public fusion::JointVisitor<AbaForwardStep1>
  {
    typedef boost::fusion::vector<const se3::Model &,
                                  se3::Data &,
                                  const Model::Index,
                                  const Eigen::VectorXd &,
                                  const Eigen::VectorXd &
                                  > ArgsType;

    JOINT_VISITOR_INIT(AbaForwardStep1);

    template<typename JointModel>
    static void algo(const se3::JointModelBase<JointModel> & jmodel,
                    se3::JointDataBase<typename JointModel::JointData> & jdata,
                    const se3::Model & model,
                    se3::Data & data,
                    const Model::Index i,
                    const Eigen::VectorXd & q,
                    const Eigen::VectorXd & v)
    {
      using namespace Eigen;
      using namespace se3;

      jmodel.calc(jdata.derived(),q,v);

      const Model::Index & parent = model.parents[i];
      data.liMi[i] = model.jointPlacements[i] * jdata.M();

      data.v[i] = jdata.v();

      if (parent>0)
      {
        data.oMi[i] = data.oMi[parent] * data.liMi[i];
        data.v[i] += data.liMi[i].actInv(data.v[parent]);
      }
      else
        data.oMi[i] = data.liMi[i];

      data.a[i] = jdata.c() + (data.v[i] ^ jdata.v());

      data.Yaba[i] = model.inertias[i].matrix();
      data.f[i] = model.inertias[i].vxiv(data.v[i]); // -f_ext
    }

  };

  struct AbaBackwardStep : public fusion::JointVisitor<AbaBackwardStep>
  {
    typedef boost::fusion::vector<const Model &,
                                  Data &,
                                  const Model::Index> ArgsType;

    JOINT_VISITOR_INIT(AbaBackwardStep);

    template<typename JointModel>
    static void algo(const JointModelBase<JointModel> & jmodel,
                     JointDataBase<typename JointModel::JointData> & jdata,
                     const Model & model,
                     Data & data,
                     const Model::Index i)
    {
      const Model::Index & parent  = model.parents[i];
      Inertia::Matrix6 & Ia = data.Yaba[i];

      jmodel.jointVelocitySelector(data.u) -= jdata.S().transpose()*data.f[i];
      jmodel.calc_aba(jdata.derived(), Ia, parent > 0);
      jmodel.jointVelocitySelector(data.ddq) = jdata.Dinv() * jmodel.jointVelocitySelector(data.u);

      if (parent > 0)
      {
        Force & pa = data.f[i];
        pa.toVector() += Ia * data.a[i].toVector() + jdata.UDinv() * jmodel.jointVelocitySelector(data.u);
        data.Yaba[parent] += SE3actOn(data.liMi[i], Ia);
        data.f[parent] += data.liMi[i].act(pa);
      }

//      Data::Matrix6x & U = data.U_aba[i];
//      Eigen::MatrixXd & D_inv = data.D_inv_aba[i];
//
////      const ConstraintXd::DenseBase S = ((ConstraintXd)jdata.S()).matrix();
//
//      U = data.Yaba[i] * ((ConstraintXd)jdata.S()).matrix();
////      U = Data::Matrix6x::Zero(6, JointModelBase<JointModel>::NV);
//      D_inv = (jdata.S().transpose() * U.block(0,0,U.rows(), U.cols())).inverse();
////      D_inv = Eigen::MatrixXd::Zero(JointModelBase<JointModel>::NV, JointModelBase<JointModel>::NV);
//      jmodel.jointVelocitySelector(data.tau) -= jdata.S().transpose()*data.f[i];
//      if(parent>0)
//      {
//        Inertia::Matrix6 & Ia = data.Yaba[i];
//        Force & pa = data.f[i];
//
//        Ia -= U * D_inv * U.transpose();
//
//        pa.toVector() += Ia * data.a[i].toVector() + U * D_inv * jmodel.jointVelocitySelector(data.tau);
////        Inertia::Matrix6 tmp = data.liMi[i].inverse().toActionMatrix();
////        data.Yaba[parent].triangularView<Eigen::Upper>() += tmp.transpose() * Ia.selfadjointView<Eigen::Upper>() * tmp;
//        data.Yaba[parent] += SE3actOn(data.liMi[i], Ia);
//        data.f[parent] += data.liMi[i].act(pa);
//      }
    }

    inline static Inertia::Matrix6 SE3actOn(const SE3 & M, const Inertia::Matrix6 & I)
    {
      typedef Inertia::Matrix6 Matrix6;
      typedef SE3::Matrix3 Matrix3;
      typedef SE3::Vector3 Vector3;
      typedef Eigen::Block<const Matrix6,3,3> constBlock3;
      typedef Eigen::Block<Matrix6,3,3> Block3;

      const constBlock3 & Ai = I.block<3,3> (Inertia::LINEAR, Inertia::LINEAR);
      const constBlock3 & Bi = I.block<3,3> (Inertia::LINEAR, Inertia::ANGULAR);
      const constBlock3 & Di = I.block<3,3> (Inertia::ANGULAR, Inertia::ANGULAR);

      const Matrix3 & R = M.rotation();
      const Vector3 & t = M.translation();

      Matrix6 res;
      Block3 Ao = res.block<3,3> (Inertia::LINEAR, Inertia::LINEAR);
      Block3 Bo = res.block<3,3> (Inertia::LINEAR, Inertia::ANGULAR);
      Block3 Co = res.block<3,3> (Inertia::ANGULAR, Inertia::LINEAR);
      Block3 Do = res.block<3,3> (Inertia::ANGULAR, Inertia::ANGULAR);

      Ao = R*Ai*R.transpose();
      Bo = R*Bi*R.transpose();
      Do.row(0) = t.cross(Bo.col(0));
      Do.row(1) = t.cross(Bo.col(1));
      Do.row(2) = t.cross(Bo.col(2));

      Co.col(0) = t.cross(Ao.col(0));
      Co.col(1) = t.cross(Ao.col(1));
      Co.col(2) = t.cross(Ao.col(2));
      Co += Bo.transpose();

      Bo = Co.transpose();
      Do.col(0) += t.cross(Bo.col(0));
      Do.col(1) += t.cross(Bo.col(1));
      Do.col(2) += t.cross(Bo.col(2));
      Do += R*Di*R.transpose();
      return res;
    }
  };

  struct AbaForwardStep2 : public fusion::JointVisitor<AbaForwardStep2>
  {
    typedef boost::fusion::vector<const se3::Model &,
                                  se3::Data &,
                                  const Model::Index
                                  > ArgsType;

    JOINT_VISITOR_INIT(AbaForwardStep2);

    template<typename JointModel>
    static void algo(const se3::JointModelBase<JointModel> & jmodel,
                    se3::JointDataBase<typename JointModel::JointData> & jdata,
                    const se3::Model & model,
                    se3::Data & data,
                    const Model::Index i)
    {
      using namespace Eigen;
      using namespace se3;

      const Model::Index & parent = model.parents[i];

      data.a[i] += data.liMi[i].actInv(data.a[parent]);
      jmodel.jointVelocitySelector(data.ddq) -= jdata.UDinv().transpose() * data.a[i].toVector();

      data.a[i] += jdata.S() * jmodel.jointVelocitySelector(data.ddq);
    }

  };

  inline const Eigen::VectorXd &
  aba(const Model & model,
      Data & data,
      const Eigen::VectorXd & q,
      const Eigen::VectorXd & v,
      const Eigen::VectorXd & tau)
  {
    data.v[0].setZero();
    data.a[0] = -model.gravity;
    data.u = tau;

    for(Model::Index i=1;i<(Model::Index)model.nbody;++i)
    {
      AbaForwardStep1::run(model.joints[i],data.joints[i],
                           AbaForwardStep1::ArgsType(model,data,i,q,v));
    }

    for( Model::Index i=(Model::Index)model.nbody-1;i>0;--i )
    {
      AbaBackwardStep::run(model.joints[i],data.joints[i],
                           AbaBackwardStep::ArgsType(model,data,i));
    }

    for(Model::Index i=1;i<(Model::Index)model.nbody;++i)
    {
      AbaForwardStep2::run(model.joints[i],data.joints[i],
                           AbaForwardStep2::ArgsType(model,data,i));
    }

    return data.ddq;
  }
} // namespace se3

#endif // ifndef __se3_aba_hpp__
